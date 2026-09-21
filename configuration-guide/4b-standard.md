# Shaders — Standard

![Standard shaders screen](images/4b-standard.png)

Standard shaders are optional ordinary shader effects. They are separate from NLS modes and other manual shader sections.

## Shader list

- **+ Add profile** — Creates a named standard-shader profile.
- **Remove selected** — Removes the configuration; it does not delete the shader files it referenced.
- Multiple matching standard-shader profiles may be active at once and compose in list order. A shortcut or rule selects an effect; list order is meaningful for the composed shader sequence.

## Shader details

- **Shortcut key** — Selects the shader directly. Blank leaves it inactive unless a rule selects it.
- **Select automatically with a rule** — Enables rule-based selection.
- **Rule** — Source condition such as `${eotf} == "HDR"`.
- **Display name** — Friendly name shown in the editor/profile UI.
- **Stage** — Runs the shader **Before resize** or **After resize**.
- **DirectShow shader file** — Shader file used when the effect runs through DirectShow. Leave blank unless you have a compatible file for that renderer.
- **VP Renderer shader file** — Shader file used when the effect runs through VP Renderer. Leave blank unless you have a compatible file for that renderer.
- **Shader parameters** — Parameter controls defined by the selected shader, when available.

The DirectShow and VP Renderer file fields are separate because a shader file made for one renderer may not work in the other.

---

[← Shaders](4-shaders.md) · [Configuration guide index](README.md)
