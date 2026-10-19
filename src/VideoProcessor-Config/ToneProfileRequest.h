#pragma once
#include "ToneCurveProvider.h"
#include <HdrTargetLuminance.h>
#include <RendererProfileConfig.h>
#include <ToneMappingTuning.h>
#include <QString>
#include <functional>

// One Rendering / DTM profile's values as a tone-curve request: the Config
// page's charts and the Levels Scope read a profile through this one mapping,
// so the two cannot disagree on what a profile means. Header-only, so his
// ConfigTests build of ConfigEditorWindow.cpp needs no new file.
namespace ToneProfileRequest
{
    // What an unset key in a Rendering profile ([vprenderer] and
    // [vprenderer.<profile>]) means: built-in values only, never another profile's.
    inline QString BuiltInDefault(const QString& key)
    {
        if (key == QStringLiteral("dynamic_target")) return QStringLiteral("true");
        if (key == QStringLiteral("custom_tone_mapping")) return QStringLiteral("false");
        if (key == QStringLiteral("output_path_profile")) return QStringLiteral("legacy");
        if (key == QStringLiteral("target_primaries")) return QStringLiteral("REC709");
        if (key == QStringLiteral("sdr_target_nits")) return QStringLiteral("203");
        if (key == QStringLiteral("sdr_black_nits")) return QString::number(HdrTargetLuminance::DefaultBlack);
        if (key == QStringLiteral("contrast_recovery")) return QStringLiteral("Auto");
        if (key == QStringLiteral("deband_strength")) return QStringLiteral("AUTO");
        if (key == QStringLiteral("report_bt2020_to_display") ||
            key == QStringLiteral("output_diagnostics") ||
            key == QStringLiteral("diagnostic_disable_shader_cache") ||
            key == QStringLiteral("diagnostic_disable_compute") ||
            key == QStringLiteral("diagnostic_force_8bit_sdr_swapchain") ||
            key == QStringLiteral("diagnostic_allow_limited_g22") ||
            key == QStringLiteral("diagnostic_allow_full_g22") ||
            key == QStringLiteral("diagnostic_vp_owned_dxgi_presenter")) return QStringLiteral("false");
        return QStringLiteral("AUTO");
    }

    // A chart request from any source of one profile's values: the edited
    // profile, another profile, an exported profile file or a saved cfg.
    inline ToneCurveProvider::Request From(const std::function<QString(const char*)>& get)
    {
        ToneCurveProvider::Request request;
        const auto choice = [&](const char* name, const char* fallback) {
            const QString chosen = get(name).toLower();
            return (chosen.isEmpty() ? QString::fromLatin1(fallback) : chosen).toStdString();
        };
        request.toneMapping = choice("tone_mapping", "auto");
        request.peakDetection = choice("peak_detection", "auto");
        const std::string peak = request.peakDetection != "auto" ? request.peakDetection : "high_quality";
        request.sceneAverageKnown = peak != "off";
        ToneMappingTuning::Read([&](const char* name, std::string& raw) {
            raw = get(name).toStdString();
            return !raw.empty();
        }, request.toneTuning);
        const QString dynamicTarget = get("dynamic_target");
        RendererProfileConfig::ParseBoolean((dynamicTarget.isEmpty() ?
            BuiltInDefault(QStringLiteral("dynamic_target")) : dynamicTarget).toStdString(),
            request.dynamicTarget);
        request.gamutMapping = choice("gamut_mapping", "auto");
        const QString overflow = get("custom_tone_mapping");
        RendererProfileConfig::ParseBoolean((overflow.isEmpty() ?
            BuiltInDefault(QStringLiteral("custom_tone_mapping")) : overflow).toStdString(),
            request.gamutOverflowCorrection);
        double white = 0.0, black = HdrTargetLuminance::DefaultBlack;
        request.targetNits = HdrTargetLuminance::ParseWhite(get("sdr_target_nits").toStdString(), white) ?
            white : 203.0;
        const QString blackText = get("sdr_black_nits").toLower();
        if (!blackText.isEmpty()) HdrTargetLuminance::ParseBlack(blackText.toStdString(), request.targetNits, black);
        request.blackNits = black;
        return request;
    }
}
