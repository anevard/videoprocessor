# Shaders — NLS

![NLS screen](images/4c-nls.png)

NLS means nonlinear stretch. The NLS tab manages shipped/custom nonlinear-stretch modes plus the special **Off** option. One NLS mode can be active at a time; the first matching mode wins.

- **NLS modes** — Ordered list of modes. Reorder by dragging or with **Move up**/**Move down**. The first matching mode wins.
- **Shortcut key** — Selects the mode directly.
- **Cycle shortcut key** — Cycles through modes that share the key, in list order.
- **Select automatically with a rule** — Enables rule selection.
- **Rule** — Source condition used to select the mode.
- **Display name** — Friendly mode name.
- **Stage** — Applies the mode before resize or after resize.
- **DirectShow shader file** — DirectShow-side implementation, when available.
- **VP Renderer shader file** — VP Renderer-side implementation, when available.
- **Shader parameters** — Mode-specific parameters exposed by the shader.
- **Off** — Explicitly disables NLS.

NLS changes the shape of the picture intentionally. Keep it conceptually separate from the Screen and Zoom pages: Screen describes the display surface, Zoom describes crop/fill behavior, and NLS changes how the image stretches across that surface.

---

[← Shaders](4-shaders.md) · [Configuration guide index](README.md)
