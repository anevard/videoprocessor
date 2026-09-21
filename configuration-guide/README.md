# VideoProcessor Configuration Guide (first draft)

> Draft status: first-pass documentation for review. The screenshots are real captures from the deployed VideoProcessor configuration editor. They show the UI as it was available during capture, including local device, monitor, renderer, and profile names.

This guide explains the configuration editor screen by screen and tab by tab. It is written for an operator who wants to understand what a setting does before changing it, rather than for someone editing the `.cfg` file by hand. For the configuration file itself, see [CONFIGURATION.html](../CONFIGURATION.html).

## Contents

**Reference**

- [Rules](rules.md) — the expression language: operators, variables, and worked examples.
- [Rendering concepts in plain language](concepts.md) — tone mapping, gamut mapping, peak detection, debanding, dithering, scaling.

**Screens**

| | Screen | What it covers |
| --- | --- | --- |
| 1 | [General](01-general.md) | Startup, capture hardware, display selection, shared input metadata defaults |
| 2 | [Queue](02-queue.md) | Frame-queue profiles and buffering policy |
| 3 | [VP Renderer — Rendering](03-rendering.md) | Quality preset, tone mapping, gamut mapping, peak detection, processing |
| 4 | [DirectShow — General](04-directshow-general.md) | DirectShow timing and renderer colour overrides |
| 5 | [VP Renderer — Screen](05-screen.md) | Presentation geometry, subtitle placement, HDR analysis protection |
| 6 | [LLDV](06-lldv.md) | Low-Latency Dolby Vision metadata profiles |
| 7 | [Shortcuts — Shortcuts](07-shortcuts.md) | Application, capture and renderer key chords |
| 8 | [Actions](08-actions.md) | External commands triggered by VideoProcessor events |
| 9 | [VP Renderer — Standard shaders](09-standard-shaders.md) | Optional shader effects and their ordering |
| 10 | [VP Renderer — NLS](10-nls.md) | Nonlinear-stretch modes |
| 11 | [Logs](11-logs.md) | Log enablement, enhanced diagnostics, retention |
| 12 | [VP Renderer — Processing](12-vp-renderer-processing.md) | VP Renderer input-policy overrides |
| 13 | [DirectShow — Input Processing](13-directshow-processing.md) | DirectShow input-policy overrides |
| 14 | [VP Renderer — Color / Output](14-color-output.md) | Display calibration, LUT slots, transfer, output transport |
| 15 | [Shaders — Setup](15-shaders-setup.md) | Shader cache maintenance |
| 16 | [Shortcuts — Setup](16-shortcuts-setup.md) | Shortcut focus behaviour |
| 17 | [VP Renderer — Scaling](17-scaling.md) | Upscaler, downscaler, anti-ringing |
| 18 | [VP Renderer — Zoom](18-zoom.md) | Crop and fill behaviour, subtitle bounds |

## Before you begin

### Save and apply

The editor changes the configuration document in memory as controls are edited. **OK** saves and closes, **Cancel** discards the in-memory edits, and **Apply** saves without closing. Apply may restart or reinitialize capture/renderer state for settings that cannot be changed live; each screen calls that out where it matters. The footer reports whether changes are pending.

### Profiles, order, inheritance, and rules

Several screens are profile editors. They share the same model:

- The first profile is the default and profile order is significant.
- **+ Add profile** creates another profile. **Remove** deletes the selected profile. **Move up** and **Move down** change precedence.
- **Name** is the human-readable profile name.
- **Shortcut key** selects one profile directly.
- **Cycle shortcut key** cycles through profiles that share that key, in list order.
- **Use rule** enables the rule field. A rule selects a profile from source metadata such as width, height, or transfer function.
- A blank or **Inherited / not set** value means “use the parent/default value” where inheritance is supported. An explicit value creates an override.
- A green marker and an **Active** title identify the profile currently selected by the running configuration snapshot; **Not active** means the profile is configured but not currently selected.

Rules use the expression language described in the [Rules](rules.md) section. For example, `${width} >= 1920 && ${eotf} == "HDR"` selects a profile for large HDR sources. Shortcut and rule are independent selectors: either can select a profile, and both may be configured.

## How the configuration editor is organized

The editor groups related settings under a small number of navigation headings. Use this as a quick orientation; the sections that follow explain each screen in detail and show the corresponding UI capture in context.

- **General** — Startup behavior, capture hardware, display selection, and shared input metadata defaults.
- **Queue** — Ordered frame-queue profiles and buffering policy.
- **LLDV** — Ordered Low-Latency Dolby Vision metadata profiles shared by the renderer paths.
- **Shaders** — Shader cache maintenance, standard shader profiles, and nonlinear-stretch (NLS) profiles.
- **Actions** — External commands triggered by VideoProcessor events.
- **Shortcuts** — Global/application shortcuts and shortcut-focus behavior.
- **Logs** — Log enablement, enhanced diagnostics, retention, and access to log files.
- **VP Renderer** — Rendering quality and HDR mapping; Color / Output calibration and transport; scaling; screen geometry; zoom/crop behavior; and renderer-specific input processing.
- **DirectShow** — DirectShow timing/color overrides and DirectShow-specific input processing.

The **VP Renderer** and **DirectShow** headings contain tabs. The **Shaders** and **Shortcuts** headings do as well. A screen may contain collapsible groups or controls below the initial view, so the screenshot at the start of each section is an orientation point rather than a complete inventory of everything on that page.

For the current origin beta tip, [`v1.3.005-beta`](https://github.com/billslack2/videoprocessor/commit/38477f13560a3c4f0085c8840127ae064d0a5b86), the VP Renderer tabs are **Rendering**, **Color / Output**, **Scaling**, **Screen**, **Zoom**, and **Processing**. The former Output page index redirects to **Color / Output**. This guide follows that current beta navigation and the real screenshot so readers can match the shipped UI.

---
## Practical troubleshooting order

When the picture is wrong, change one conceptual layer at a time:

1. **[General](01-general.md)** — Confirm capture device, input connection, monitor, renderer, and shared input metadata.
2. **[Input Processing](12-vp-renderer-processing.md)** — Confirm the active renderer is not overriding the General metadata unexpectedly.
3. **[Rendering](03-rendering.md)** — Check quality, target luminance, tone mapping, gamut mapping, peak detection, dithering, and LUT selection.
4. **[Color / Output](14-color-output.md)** — Check calibrated gamut/gamma, LUT slots and input gamma, output range, and presentation path.
5. **[Scaling](17-scaling.md) / [Screen](05-screen.md) / [Zoom](18-zoom.md)** — Separate filter quality from geometry and crop decisions.
6. **[Logs](11-logs.md)** — Enable enhanced logging only for the diagnostic capture, then return to normal logging when finished.

When comparing screenshots or support reports, record the active profile names and rules. Two machines can show the same controls while resolving different effective values because their profile order, hardware, display, or discovered renderer list differs.

## Screenshot publishing note

The screenshots in this draft are authentic UI captures. Before publishing them to a public GitHub repository, decide whether local device names, monitor names, renderer names, and profile names are acceptable. If they are not, recapture the same screens with neutral example values; do not replace the UI screenshots with mockups if the goal is to document the actual editor.
