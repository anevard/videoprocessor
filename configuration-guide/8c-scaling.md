# VP Renderer — Scaling

![Scaling configuration screen](images/8c-scaling.png)

Scaling profiles choose the reconstruction filters used when the source and target sizes differ.

- **Upscaler** — Filter for enlarging the image. Choices include EWA Lanczos variants, Lanczos, Catmull-Rom, Bicubic, Gaussian, Oversample, Bilinear, Nearest, and **Use GPU**. Auto resolves from the quality preset/hardware.
- **Downscaler** — Filter for reducing the image. Choices include Lanczos, Mitchell, Catmull-Rom, Bicubic, Gaussian, Hermite, Bilinear, Box, and **Use GPU**.
- **Anti-ringing** — Enables or disables the UI's anti-ringing choice. Anti-ringing controls filter overshoot; sigmoidization is a separate scaling transform that reshapes values before scaling. VP exposes a compact UI here, but the two mechanisms should not be treated as identical libplacebo options.

Scaling changes spatial reconstruction, not crop geometry. Use Screen and Zoom for geometry/crop decisions and Scaling for the filter used after those geometry decisions.

The current editor labels automatic choices as **Auto** and shows the resolved value below the control. The default profile does not require a separate “Use default” choice.

---

[← VP Renderer](8-vp-renderer.md) · [Configuration guide index](README.md)
