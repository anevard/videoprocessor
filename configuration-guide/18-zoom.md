# 18. VP Renderer — Zoom

![Zoom configuration screen](images/18-zoom.png)

Zoom profiles are independent from Screen profiles. A Zoom shortcut changes crop/fill behavior without changing the selected Screen geometry.

## Geometry and fill

- **Screen aspect ratio** — Optional profile-level screen ratio override.
- **Vertical picture alignment** — Top, center, or bottom alignment.
- **Enable anamorphic lens compensation** — Enables lens compensation for this Zoom profile.
- **Lens expansion ratio** — Expansion ratio used by anamorphic compensation.
- **Automatically crop black bars** — Detects and removes black bars.
- **Crop narrower content to fill screen** — Crops narrower content to fill the screen.
- **Aspect ratio limit** — Upper/lower guard for narrower-content cropping, for example `2.20:1`.
- **Crop wider content to fill screen** — Crops wider content to fill the screen.
- **Aspect ratio limit** — Guard for wider-content cropping, for example `2.76:1`.
- **Fixed crop aspect** — Forces a fixed crop aspect, or leave it Off.

The two **Aspect ratio limit** fields belong to different crop directions. Keep them distinct when documenting or editing the raw configuration.

## Subtitles

- **Keep subtitles inside screen bounds** — Prevents crop/fill choices from pushing subtitles outside the target screen.
- **Subtitle hold** — Subtitle stability hold time.
- **Subtitle engage drift** — Drift threshold before subtitle handling engages.
- **Subtitle release drift** — Drift threshold before it releases.
- **Subtitle padding** — Padding in pixels.
- **Subtitle target buffer** — Target buffer in pixels.

## HDR analysis protection

Zoom profiles can also carry the HDR analysis-protection controls:

- **Limit HDR analysis to picture center**
- **Protect HDR analysis during subtitle movement**
- **HDR analysis protection** — Off, Smart (Experimental), or Percentage (Beta)
- **HDR analysis height** — Percentage used for fixed protection
- **HDR analysis position** — Top, center, or bottom

Use the Zoom page when the desired change is crop/fill or subtitle-safe placement. Use the Screen page when the desired change is the physical presentation geometry.

---

[← Configuration guide index](README.md)
