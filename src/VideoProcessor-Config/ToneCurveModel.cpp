#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "ToneCurveProvider.h"
#include "ToneCurveRuntime.h"
#include <vprenderer/LibplaceboRenderParameters.h>
#include <libplacebo/tone_mapping.h>
#include <algorithm>
#include <cmath>
#include <cstring>

// Samples the tone curve as VP Renderer (Beta) builds it, on a grey scene.
// Nothing links libplacebo: its functions and data are found by name in VP
// Renderer (Beta)'s copy, libplacebo-377.dll. Every curve value comes from
// pl_tone_map_sample.
//
// ColorMap() copies the tone part of LibplaceboRenderParameters::Build()
// (src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.cpp): the
// quality preset's colour-map parameters, the chosen curve, its
// VP_LIBPLACEBO_BETA block, and the tone-key loop with its knee_default clamp.
// That file cannot be compiled here, because it imports libplacebo data
// directly. If its mapping changes, change this copy.
//
// Map() then repeats what pl_shader_color_map_ex() in his fork's
// src/shaders/colorspace.c (codex/vp-master-beta 8bccd657) computes and does
// not export, in its order, citing its lines: infer, round near-equal peaks,
// the signal black, the input peak, the dynamic target and the mastering soft
// clip. On grey, film strength changes nothing (both ends of its blend are the
// same, lines 2341-2379) and gamut overflow correction acts on colour only.
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
        error = L"libplacebo-377.dll is missing a tone-mapping function";
        return false;
    }

    template<typename T>
    const T* Data(const char* exportName, std::wstring& error)
    {
        const T* value = static_cast<const T*>(ToneCurveRuntime::Find(exportName));
        if (!value)
            error = L"libplacebo-377.dll is missing " + std::wstring(exportName, exportName + strlen(exportName));
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
        // Its VP_LIBPLACEBO_BETA block; vp_gamut_compression acts on colour only.
        map.dynamic_target = request.dynamicTarget;
        if (request.toneTuning[ToneMappingTuning::FilmStrength])
            map.film_strength = static_cast<float>(*request.toneTuning[ToneMappingTuning::FilmStrength]);
        if (request.toneTuning[ToneMappingTuning::MasteringClip])
            map.mastering_clip = static_cast<float>(*request.toneTuning[ToneMappingTuning::MasteringClip]);
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

    // The curve's parameters and what the shader does around it.
    struct Mapping
    {
        pl_tone_map_params tone{};
        float exposure = 1.0f;     // dt_exposure
        float virtualTargetNits = 0.0f;
        float clip = 0.0f;         // mclip
        float masterNits = 0.0f;
    };

    Mapping Map(const Library& library, const pl_color_map_params& map,
        const ToneCurveProvider::Request& request)
    {
        const auto pq = [&](double nits) {
            return library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(nits));
        };
        const auto nits = [&](float value) { return library.rescale(PL_HDR_PQ, PL_HDR_NITS, value); };
        // Peak detection measures the scene; without it, or on a scene with
        // only metadata, there is only metadata.
        const bool measured = request.sceneAverageKnown && request.sceneAverageNits > 0;
        Mapping mapping;
        pl_tone_map_params& tone = mapping.tone;
        // A null function becomes pl_tone_map_clip inside infer, as in the
        // shader. The deprecated `param` stays 0: VP never sets it.
        tone.function = map.tone_mapping_function;
        tone.constants = map.tone_constants;
        tone.input_scaling = PL_HDR_PQ;
        tone.output_scaling = PL_HDR_PQ;
        tone.lut_size = map.lut_size;
        tone.input_min = pq(kSourceMinimumNits);
        tone.input_max = pq(request.scenePeakNits);
        tone.input_avg = measured ? pq(request.sceneAverageNits) : 0.0f;
        tone.output_min = pq(LibplaceboRenderParameters::ResolveTargetBlack(
            static_cast<float>(request.blackNits)));
        tone.output_max = pq(request.targetNits);

        // colorspace.c 1964-1975.
        library.infer(&tone);
        if (std::fabs(tone.input_max - tone.output_max) < 1e-6)
            tone.output_max = tone.input_max;
        if (std::fabs(tone.input_min - tone.output_min) < 1e-6)
            tone.output_min = tone.input_min;
        const float signalBlack = pq(PL_COLOR_HDR_BLACK); // pl_signal_black, src/colorspace.h 26
        if (tone.input_min <= signalBlack) tone.input_min = 0.0f;
        if (tone.output_min <= signalBlack) tone.output_min = 0.0f;

        // 1977-1994. On grey every channel's scene maximum is the scene peak.
        if (!map.inverse_tone_mapping)
        {
            tone.input_max = (std::max)(tone.input_max, tone.output_max);
            if (measured && request.scenePeakNits > 0)
                tone.input_max = (std::max)(tone.input_max, pq(request.scenePeakNits));
        }

        // 1999-2050, the dynamic target.
        const float outMinPhys = tone.output_min;
        const float sceneAverage = measured ? static_cast<float>(request.sceneAverageNits) : 0.0f;
        if (map.dynamic_target && sceneAverage > 0 && !map.inverse_tone_mapping &&
            tone.input_max > tone.output_max + 1e-4f)
        {
            const float dtDim = 0.09f, dtSoft = 0.04f;
            const float pPq = tone.output_max;
            const float sPq = tone.input_max;
            const float pNits = nits(pPq);
            const float fPq = pq(sceneAverage);
            const float dtU = pPq;
            const float dtV = fPq - dtDim;
            const float tPq = (std::clamp)(0.5f * (dtU + dtV) +
                std::hypot(0.5f * (dtU - dtV), dtSoft), pPq, sPq);
            if (tPq > pPq + 1e-4f && tPq < sPq - 1e-4f)
            {
                const float tNits = nits(tPq);
                mapping.exposure = pNits / tNits;
                mapping.virtualTargetNits = tNits;
                tone.output_max = tPq;
                tone.output_min = pq(nits(outMinPhys) / mapping.exposure);
            }
        }

        // 2182-2198, the mastering soft clip's strength; it needs the measured peak.
        mapping.masterNits = static_cast<float>(request.masteringPeakNits);
        float clip = (std::clamp)(map.mastering_clip, 0.0f, 1.0f);
        if (clip > 0.0f && mapping.masterNits > 0.0f && mapping.masterNits < 9999.0f &&
            measured && request.scenePeakNits > 0)
        {
            // pl_smoothstep, src/common.h 140.
            float x = (static_cast<float>(request.scenePeakNits) / mapping.masterNits - 1.1f) / (1.4f - 1.1f);
            x = (std::clamp)(x, 0.0f, 1.0f);
            clip *= 1.0f - x * x * (3.0f - 2.0f * x);
        }
        else
            clip = 0.0f;
        mapping.clip = clip;
        return mapping;
    }

    // One grey source level through the shader's order: mastering soft clip
    // (2199-2212), the curve (pl_tone_map_sample, as its LUT), then the dynamic
    // target's exposure (2377).
    float Apply(const Library& library, const Mapping& mapping, float sourcePq)
    {
        float value = sourcePq;
        if (mapping.clip > 1e-3f)
        {
            const float knee = 0.95f;
            const float source = library.rescale(PL_HDR_PQ, PL_HDR_NITS, value);
            float mcn = source / mapping.masterNits;
            const float mct = (std::max)(mcn - knee, 0.0f) * (1.0f / (1.0f - knee));
            mcn = (std::min)(mcn, knee) + (1.0f - knee) * mct / (1.0f + mct);
            const float clipped = source + (mapping.masterNits * mcn - source) * mapping.clip;
            value = library.rescale(PL_HDR_NITS, PL_HDR_PQ, clipped);
        }
        value = library.sample(value, &mapping.tone);
        if (mapping.exposure != 1.0f)
            value = library.rescale(PL_HDR_NITS, PL_HDR_PQ,
                mapping.exposure * library.rescale(PL_HDR_PQ, PL_HDR_NITS, value));
        return value;
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
        // the profile's display (Target nits and black) and the chosen scene.
        ToneCurveProvider::Request fixed = request;
        fixed.quality = "high";
        fixed.toneMapping = "auto";
        fixed.peakDetection = "auto";
        fixed.toneTuning = {};
        fixed.sceneAverageKnown = true;
        pl_color_map_params profile{}, standard{};
        if (!ColorMap(request, true, profile, error) || !ColorMap(fixed, false, standard, error))
            return false;
        const Mapping profileMap = Map(library, profile, request);
        const Mapping standardMap = Map(library, standard, fixed);
        result.profilePq.reserve(request.sourcePq.size());
        result.defaultPq.reserve(request.sourcePq.size());
        for (double x : request.sourcePq)
        {
            result.profilePq.push_back(Apply(library, profileMap, static_cast<float>(x)));
            result.defaultPq.push_back(Apply(library, standardMap, static_cast<float>(x)));
        }
        const pl_tone_map_params& tone = profileMap.tone;
        result.inputMaxPq = tone.input_max;
        result.scenePeakPq = library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(request.scenePeakNits));
        result.outputMaxPq = library.rescale(PL_HDR_NITS, PL_HDR_PQ, static_cast<float>(request.targetNits));
        result.outputMinPq = tone.output_min;
        result.curve = tone.function && tone.function->name ? tone.function->name : "clip";
        result.virtualTargetNits = profileMap.virtualTargetNits;
        result.exposure = profileMap.exposure;
        result.masteringClip = profileMap.clip;
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
