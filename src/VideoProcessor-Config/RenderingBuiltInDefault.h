#pragma once
#include <HdrTargetLuminance.h>
#include <QString>

// What an unset key in a Rendering profile ([vprenderer] and
// [vprenderer.<profile>]) means: built-in values only, never another profile's.
// Header-only, so any tool that reads a profile shares this one table.
inline QString RenderingBuiltInDefault(const QString& key)
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
