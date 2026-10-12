#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "ToneCurveProvider.h"
#include "ToneCurveRuntime.h"
#include <vprenderer/LibplaceboRenderParameters.h>
#include <libplacebo/tone_mapping.h>
#include <algorithm>
#include <cmath>
#include <cstring>

// Samples the tone curve as VP Renderer builds it. Nothing links libplacebo:
// its functions and data are found by name in VP Renderer's copy.
//
// ColorMap() copies the tone part of LibplaceboRenderParameters::Build()
// (src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.cpp): the
// quality preset's colour-map parameters, the chosen curve, and the tone-key
// loop with its knee_default clamp. That file cannot be compiled here, because
// it imports libplacebo data directly. If its mapping changes, change this copy.
//
// ToneParameters() then repeats pl_shader_color_map_ex() in libplacebo's
// src/shaders/colorspace.c: infer, round near-equal peaks together, and cap the
// output at the input peak.
namespace
{
    // The source minimum the explorer assumes.
    constexpr float kSourceMinimumNits = 1e-6f;

    struct Library
    {
        void (*infer)(pl_tone_map_params*) = nullptr;
        float (*sample)(float, const pl_tone_map_params*) = nullptr;
        float (*rescale)(pl_hdr_scaling, pl_hdr_scaling, float) = nullptr;
    };

    bool Resolve(Library& library, std::wstring& error)
    {
        if (!ToneCurveRuntime::Load(error))
            return false;
        library.infer = reinterpret_cast<decltype(library.infer)>(ToneCurveRuntime::Find("pl_tone_map_params_infer"));
        library.sample = reinterpret_cast<decltype(library.sample)>(ToneCurveRuntime::Find("pl_tone_map_sample"));
        library.rescale = reinterpret_cast<decltype(library.rescale)>(ToneCurveRuntime::Find("pl_hdr_rescale"));
        if (library.infer && library.sample && library.rescale)
            return true;
        error = L"libplacebo-360.dll is missing a tone-mapping function";
        return false;
    }

    template<typename T>
    const T* Data(const char* exportName, std::wstring& error)
    {
        const T* value = static_cast<const T*>(ToneCurveRuntime::Find(exportName));
        if (!value)
            error = L"libplacebo-360.dll is missing " + std::wstring(exportName, exportName + strlen(exportName));
        return value;
    }

    // As QualityPreset() and ToneMapping() in LibplaceboRenderParameters.cpp.
    const char* QualityPreset(const std::string& quality)
    {
        if (quality == "fast") return "pl_render_fast_params";
        if (quality == "balanced") return "pl_render_default_params";
        return "pl_render_high_quality_params";
    }

    const char* ToneMapping(const std::string& toneMapping)
    {
        if (toneMapping == "bt2390") return "pl_tone_map_bt2390";
        if (toneMapping == "st2094-40") return "pl_tone_map_st2094_40";
        if (toneMapping == "reinhard") return "pl_tone_map_reinhard";
        return "pl_tone_map_spline";
    }

    bool ColorMap(const ToneCurveProvider::Request& request, bool useTuning,
        pl_color_map_params& map, std::wstring& error)
    {
        const pl_render_params* preset = Data<pl_render_params>(QualityPreset(request.quality), error);
        if (!preset) return false;
        if (preset->color_map_params)
            map = *preset->color_map_params;
        else
        {
            const pl_color_map_params* defaults = Data<pl_color_map_params>("pl_color_map_default_params", error);
            if (!defaults) return false;
            map = *defaults;
        }
        if (request.toneMapping != "auto")
        {
            const pl_tone_map_function* function = Data<pl_tone_map_function>(ToneMapping(request.toneMapping), error);
            if (!function) return false;
            map.tone_mapping_function = function;
        }
        if (!useTuning) return true;
        auto& constants = map.tone_constants;
        float* toneFields[] = { &constants.knee_adaptation, &constants.knee_minimum,
            &constants.knee_maximum, &constants.knee_default, &constants.slope_tuning,
            &constants.slope_offset, &constants.spline_contrast, &constants.knee_offset,
            &constants.reinhard_contrast };
        for (int i = 0; i < ToneMappingTuning::Percentile; ++i)
            if (request.toneTuning[i]) *toneFields[i] = static_cast<float>(*request.toneTuning[i]);
        constants.knee_default = (std::max)(constants.knee_minimum,
            (std::min)(constants.knee_maximum, constants.knee_default));
        return true;
    }

    pl_tone_map_params ToneParameters(const Library& library, const pl_color_map_params& map,
        const ToneCurveProvider::Request& request)
    {
        const auto pq = [&](double nits) {
            return library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(nits));
        };
        pl_tone_map_params tone{};
        // A null function becomes pl_tone_map_clip inside infer, as in the
        // shader. The deprecated `param` stays 0: VP never sets it.
        tone.function = map.tone_mapping_function;
        tone.constants = map.tone_constants;
        tone.input_scaling = PL_HDR_PQ;
        tone.output_scaling = PL_HDR_PQ;
        tone.lut_size = map.lut_size;
        tone.input_min = pq(kSourceMinimumNits);
        tone.input_max = pq(request.scenePeakNits);
        tone.input_avg = request.sceneAverageKnown ? pq(request.sceneAverageNits) : 0.0f;
        tone.output_min = pq(LibplaceboRenderParameters::ResolveTargetBlack(
            static_cast<float>(request.blackNits)));
        tone.output_max = pq(request.targetNits);

        library.infer(&tone);
        if (std::fabs(tone.input_max - tone.output_max) < 1e-6)
            tone.output_max = tone.input_max;
        if (std::fabs(tone.input_min - tone.output_min) < 1e-6)
            tone.output_min = tone.input_min;
        if (!map.inverse_tone_mapping)
            tone.output_max = (std::min)(tone.output_max, tone.input_max);
        return tone;
    }

    bool Sample(const ToneCurveProvider::Request& request,
        ToneCurveProvider::Result& result, std::wstring& error)
    {
        result = ToneCurveProvider::Result{};
        Library library;
        if (!Resolve(library, error))
            return false;
        // libplacebo's default, the same whatever the profile chooses: its own
        // curve and constants, peak detection on, High quality. It is drawn on
        // the profile's display (Target nits and black) and the sliders' scene.
        ToneCurveProvider::Request fixed = request;
        fixed.quality = "high";
        fixed.toneMapping = "auto";
        fixed.peakDetection = "auto";
        fixed.toneTuning = {};
        fixed.sceneAverageKnown = true;
        pl_color_map_params profile{}, standard{};
        if (!ColorMap(request, true, profile, error) || !ColorMap(fixed, false, standard, error))
            return false;
        const pl_tone_map_params profileTone = ToneParameters(library, profile, request);
        const pl_tone_map_params standardTone = ToneParameters(library, standard, fixed);
        result.profilePq.reserve(request.sourcePq.size());
        result.defaultPq.reserve(request.sourcePq.size());
        for (double x : request.sourcePq)
        {
            result.profilePq.push_back(library.sample(static_cast<float>(x), &profileTone));
            result.defaultPq.push_back(library.sample(static_cast<float>(x), &standardTone));
        }
        result.inputMaxPq = profileTone.input_max;
        result.outputMaxPq = profileTone.output_max;
        result.outputMinPq = profileTone.output_min;
        result.curve = profileTone.function && profileTone.function->name ?
            profileTone.function->name : "clip";
        return true;
    }

    double Rescale(pl_hdr_scaling from, pl_hdr_scaling to, double value)
    {
        Library library;
        std::wstring error;
        if (!Resolve(library, error)) return std::nan("");
        return library.rescale(from, to, static_cast<float>(value));
    }

    double NitsToPq(double nits) { return Rescale(PL_HDR_NITS, PL_HDR_PQ, nits); }
    double PqToNits(double pq) { return Rescale(PL_HDR_PQ, PL_HDR_NITS, pq); }

    // Registered before main(); the page itself never touches libplacebo.
    const bool registered = [] {
        auto& functions = ToneCurveProvider::Registered();
        functions.sample = &Sample;
        functions.nitsToPq = &NitsToPq;
        functions.pqToNits = &PqToNits;
        return true;
    }();
}
