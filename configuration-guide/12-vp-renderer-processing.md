# 12. VP Renderer — Processing

![VP Renderer Processing screen](images/12-vp-renderer-processing.png)

The tab is labelled **Processing**; the page inside it is headed "Input processing".

This tab is the VP Renderer-specific override of the General Input processing card. Each field can inherit the General value or replace it for VP Renderer profiles. The editor notes that changes here apply after a restart.

- **Video conversion** — Inherited/default, Disabled, or V210 to P010.
- **Container color space** — Inherited/default or an explicit container color-space description such as BT.2020, P3, or Rec.709.
- **HDR color space** — Inherited/default, follow input, follow input (LLDV), follow container, BT.2020, P3, or Rec.709.
- **HDR luminance** — Inherited/default, follow input, follow input (LLDV), or user values.

Use these overrides when one renderer path needs different interpretation than the shared General default. An inherited value is preferable when the same input policy should apply across renderers.

---

[← Configuration guide index](README.md)
