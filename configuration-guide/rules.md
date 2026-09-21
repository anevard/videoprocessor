# Rules

Rules let VP choose a profile automatically or decide whether an action should run. A rule reads the current state; it does not edit the configuration and it is not evaluated for every video frame.

## How profile rules work

The **Rule** field appears after **Use rule** is enabled on profile screens. It applies only to the profile group that contains it.

- Each profile group is independent. Selecting a Queue profile does not select a Color / Output, Screen, Zoom, or shader profile.
- For single-selection groups, the first profile is the default. VP checks later profiles in list order and uses the first one whose rule matches. If none matches, the default remains selected. NLS follows this first-match behavior.
- Standard shaders are the exception: every matching shader profile can be active, and matching shaders compose in list order.
- A profile can inherit settings that it does not define from the group's default profile. The rule selects the profile; it does not merge unrelated groups.
- A shortcut and a rule are alternatives for the same profile. Either one can select it. A manual shortcut selection takes precedence for that group until another profile, the default/reset selection, or normal automatic selection replaces it.
- Source-based rules are reevaluated when the relevant source, renderer, profile, or display state changes. They are not a per-frame scene detector.

## Operators and value types

Use the documented variable form `${name}`. Text values go in quotes; numbers and Boolean values do not.

| Value type | Operators and examples | Meaning |
| --- | --- | --- |
| Text | `${eotf} == "HDR"` or `${format} != "P010"` | Text comparisons use `==` or `!=`. Text matching is case-insensitive, except for keyboard shortcuts. |
| Number | `${width} >= 1920`, `${cadence} < 30`, `${cadence} == 23.976-24` | Numeric comparisons support `==`, `!=`, `<`, `<=`, `>`, and `>=`. A numeric range with `==` or `!=` is inclusive. Fractions such as `24000/1001` are valid numeric values. |
| Boolean | `${hdr_metadata}`, `!${interlaced}`, or `${hdr_metadata} == false` | A Boolean can stand alone, be negated with `!`, or be compared with `true` or `false`. |
| Keyboard shortcut | `${key} == "F5"` | `${key}` is for manual profile selection only. It must compare one non-empty, quoted shortcut. Shortcut spelling is case-sensitive. |

Combine complete comparisons with:

- `&&` — both conditions must be true.
- `||` — either condition may be true.
- `!` — reverses the following condition.
- Parentheses — make grouping explicit. `!` binds most tightly, then `&&`, then `||`; parentheses are recommended for anything more complex than a short condition.

Use `==`, not a single `=`. Use `||`, not a single `|`, when combining alternatives.

## Variables available in profile rules

These are the variables accepted by the Rule field on profile-based screens, including Queue, Rendering, Color / Output, Scaling, Screen, Zoom, LLDV, and shader profiles.

| Type | Variables | What the value describes |
| --- | --- | --- |
| Text | `eotf`, `transfer` | The source transfer/brightness encoding. One of `sdr`, `hdr`, `pq`, `hlg`, `unknown`. |
| Text | `colorspace`, `primaries` | The source color system or primary-color set. One of `rec709`, `bt2020`, `p3_d65`, `p3_dci`, `p3_d60`, `rec601_525`, `rec601_625`, `unknown`. |
| Text | `format` | The source pixel format, such as `P010` or another format reported by the capture path. |
| Text | `scan` | Whether the source is `progressive` or `interlaced`. |
| Text | `resolution` | The source dimensions in `widthxheight` form, such as `1920x1080`. |
| Text | `renderer` | The active renderer name as shown in the renderer selector. |
| Boolean | `hdr_metadata` | Whether usable HDR metadata is present for the source. |
| Boolean | `interlaced` | Whether the source is interlaced. This is the Boolean counterpart to `scan`. |
| Number | `source_rate` | The source rate rounded down to a whole number, such as `23`, `29`, or `59`. |
| Number | `cadence` | The more precise source/display cadence, such as `23.976` or `59.94`. |
| Number | `width`, `height` | Source width and height in pixels. |
| Number | `actual_refresh` | The active Windows display refresh rate in Hz. |
| Manual selector | `key` | A registered keyboard shortcut used to select or reset a profile manually. |

The value lists above are complete, not examples. Comparisons are case-insensitive, so `rec709` and `Rec709` both
match — but a value that is not in the list matches nothing. In particular there is no bare `P3`: the primaries
values are `p3_d65`, `p3_dci` and `p3_d60`. Note that these are the values a **rule** compares against, which are
spelled differently from the values a **setting** takes; `target_primaries: P3_D65` on the Color / Output page is
a setting, not a rule value.

Use the value VP reports for the current source. For example:

```text
${eotf} == "PQ" && ${colorspace} == "BT2020"
${width} >= 1920 && ${height} >= 1080
${scan} == "interlaced" || ${interlaced}
${cadence} == 24000/1001
```

## Variables available in Actions

The **Only run when** field on an Action is a different kind of rule. The Action's **On** event determines when VP takes a snapshot; the condition then filters that event. It is evaluated at the event boundary, not continuously.

The following variables are available for Action conditions:

| Context | Variables | Use |
| --- | --- | --- |
| Event identity | `event`, `event_reason` | Identify the event being handled and why it occurred. For example, `${event_reason} == "source"`. `event_reason` can be `manual`, `source`, `renderer_ready`, or `refresh`. |
| Refresh events | `actual_refresh`, `requested_refresh`, `previous_refresh` | Compare the refresh rate that is active, requested, or being left. These are available for `refresh.applied`, `refresh.confirmed`, and `refresh.restored`. |
| Current source state | `eotf`, `transfer`, `colorspace`, `primaries`, `format`, `scan`, `resolution`, `hdr_metadata`, `interlaced`, `source_rate`, `cadence`, `width`, `height` | Test the source state at a source, profile, renderer, or committed-state event. |
| Current profile selection | `profile.input`, `profile.scaling`, `profile.display`, `profile.color`, `profile.output`, `profile.viewport`, `profile.zoom`, `profile.queue`, `profile.lldv`, `profile.nls`, `profile.standard_shaders` | Test which profile is active in a group. The value is the profile's stable name; the UI label is also accepted for profile comparisons. |
| Current profile labels | `profile.viewport_name`, `profile.zoom_name`, `screen_config`, `zoom_config` | Test the visible label for the selected Screen or Zoom profile. |
| Current presentation state | `vertical_alignment`, `screen_aspect`, `anamorphic_scale`, `automatic_crop`, `subtitle_fit` | Test the active screen geometry and crop/subtitle behavior. |
| Current HDR/subtitle policy | `hdr_peak_analysis_height_percent`, `hdr_peak_analysis_picture_only`, `hdr_peak_analysis_motion_compensation`, `subtitle_hold_seconds`, `subtitle_engage_drift_ms`, `subtitle_release_drift_ms`, `subtitle_padding_pixels`, `subtitle_target_buffer_pixels` | Test the active HDR-analysis or subtitle-placement settings. |
| Viewport state | `viewport_generation` | Identify the current viewport-generation number when diagnosing transitions. |
| Previous state | `previous.<variable>` and `previous_profile.<group>` | Compare the state being left with the new state. For example, `${previous_profile.viewport} == "scope" && ${profile.viewport} == "base"`. |

An Action with more than one event must use variables that are available for every event named in **On**. If a condition mixes refresh-only variables with source/profile events, VP rejects the Action; split it into separate Actions instead.

`renderer` and `key` are useful in profile-selection rules but are not Action snapshot variables. `previous_profile.<group>` uses the same group names as `profile.<group>`; it does not mean the previous display label.

## Useful rule examples

```text
${eotf} == "HDR" && ${width} >= 1920

${interlaced} && ${height} >= 720

${event} == "profile.color.changed" && ${profile.color} == "bt2020"

${previous_profile.viewport} == "scope" && ${profile.viewport} == "base"
```

If a rule does not match, first check the variable spelling, the value shown by the active profile/source, and the profile order. For Actions, also check that the variables are valid for every event in **On**.

A rule that asks about something VP cannot currently supply is treated as not matching rather than as an error. It
parses, it saves, and the editor reports no problem — the profile it guards simply never activates. So a profile
that never comes on is a reason to re-read its rule, not evidence that the rule is correct.


---

[← Configuration guide index](README.md)
