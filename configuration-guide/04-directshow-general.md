# 4. DirectShow — General

![DirectShow General screen](images/04-directshow-general.png)

This screen affects DirectShow renderers only. It contains Timing and Renderer overrides cards.

## Timing

- **Start/stop method** — Selects the clock/theoretical timing pair used when a DirectShow renderer starts and stops presentation. The choices include Clock Smart, Clock Smart 2, Rational / Rational, Clock / Rational, Clock / Theoretical, Clock / Clock, Theoretical / Theoretical, Clock / None, Theoretical / None, and None.
- **Frame offset (ms)** — Applies a timing offset in milliseconds. **Auto** inherits the shared/default frame-offset behavior; clearing Auto enables an explicit value.

These are synchronization controls. Change them only when measurements or a renderer-specific compatibility requirement show that the default timing method is wrong.

## Renderer overrides

- **Nominal range** — Declares the expected code range: Auto, Full, Limited, or Small.
- **Transfer function** — Declares the source transfer function, including PQ, Rec.709, BT.2020 constant-luminance variants, common gamma values, linear RGB, log encodings, and HLG.
- **Transfer matrix** — Declares the YUV/RGB conversion matrix, such as BT.2020, BT.709, BT.601, 240M, FCC, or YCgCo.
- **Primaries** — Declares the source primaries, including BT.2020, DCI-P3, BT.709, NTSC variants, CIE 1931, or ACES.

**Auto** is the safest choice when the source metadata is trustworthy and the renderer handles it correctly. An explicit override is for known-bad or incomplete source metadata.

---

[← Configuration guide index](README.md)
