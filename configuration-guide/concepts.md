# Rendering concepts in plain language

Several VP Renderer settings use the same ideas described in libplacebo's user-facing options guide. The short explanations below are intended to make the controls understandable without requiring graphics or rendering knowledge:

- **Tone mapping** adapts brightness between the source and display. For example, it can fit bright HDR highlights into a display with a lower peak brightness while trying to preserve visible detail. Different curves make different trade-offs in highlight detail, shadow detail, and average picture brightness.
- **Gamut mapping** handles colors that the destination display cannot reproduce. Instead of simply cutting those colors off, a mapping method can compress saturation, shift hues, or preserve brightness according to its policy. Tone mapping can create additional out-of-gamut colors, so the two operations are related.
- **Peak detection** estimates how bright the current HDR content actually is. That estimate lets the renderer adapt to scene or frame changes instead of treating every source as if it had the same brightness.
- **Contrast recovery** restores some high-frequency detail after tone mapping. It can make the result look crisper, but too much can create ringing or halos.
- **Debanding** reduces visible steps in smooth gradients. It is most useful when the source already contains quantization or compression banding, and it costs additional processing time.
- **Dithering** adds carefully controlled variation before a picture is reduced to a lower output precision. This hides quantization patterns and usually looks smoother than rounding every pixel the same way.
- **Scaling** reconstructs the image when its size changes. An upscaler enlarges an image, a downscaler reduces it, and anti-ringing limits bright or dark halos around sharp edges.

These concepts explain what the controls are trying to achieve; the VP editor's labels, available choices, defaults, and profile rules determine what is actually applied on this installation. VP's bundled renderer is based on libplacebo 7.360.1 and also includes VP-specific behavior such as analysis-crop and D3D11 timing controls. Do not assume that a similarly named control behaves exactly like a standalone libplacebo application.

For technical background, see the [libplacebo options guide](https://github.com/haasn/libplacebo/blob/master/docs/options.md) and [project overview](https://github.com/haasn/libplacebo). These are background references, not configuration steps for VP.

---

[← Configuration guide index](README.md)
