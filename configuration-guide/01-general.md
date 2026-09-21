# 1. General

![General configuration screen](images/01-general.png)

The General screen establishes the machine-wide starting point. It has four cards: Hardware, General behavior, Display, and Input processing.

## Hardware

- **Capture device** — Selects the capture device VideoProcessor opens. The list is populated from devices discovered on this PC. If a saved device is no longer present, choose a replacement rather than leaving an unavailable entry.
- **Input connection** — Selects the physical input exposed by the chosen capture device. If the device exposes only one input, the editor may show a device default or explanatory message.

## General behavior

- **Start fullscreen** — Starts the presentation window in fullscreen mode.
- **Windowed fullscreen** — Uses a borderless window that covers the monitor instead of an exclusive fullscreen mode. This is often friendlier to desktop composition and multi-monitor workflows.
- **Start as Video Only** — Starts without the normal control UI. This is the `noui` behavior; the configuration editor and keyboard shortcuts remain the way to control the session.
- **Start minimized** — Starts VP minimized.
- **Scene detection** — Enables the scene-detection policy used by VP for source changes. The current editor exposes this as a supported on/off choice rather than a menu of algorithms.

## Display

- **Monitor** — Chooses the display used for fullscreen presentation. **Default monitor** lets Windows/VP choose the normal default.
- **Renderer** — Selects the active renderer. The list is discovered on the current PC and may include VP Renderer, DirectShow renderers, and installed legacy renderers.
- **Hide legacy renderers** — Removes older renderer entries from the selection list. It does not uninstall them; it only keeps the editor and runtime profile choices focused on current renderers.
- **Switch refresh rate** — Controls when VP asks Windows to switch the display refresh rate: **Never**, **Full Screen Only**, or **Always**.
- **Profile display** — Sets how long a profile-change notification remains visible. **Off** disables the display; otherwise the value is in seconds.

## Input processing

- **Video conversion** — Selects a source conversion. **Disabled** leaves the source format alone; **V210 to P010** converts V210 input to P010 for a renderer path that expects it.
- **Container color space** — Supplies a container-level color-space hint when one is available. **Follow input** preserves the source/device description.
- **HDR color space** — Chooses whether HDR color information follows the input, follows LLDV metadata, follows the container, or is forced to BT.2020, P3, or Rec.709.
- **HDR luminance** — Chooses whether HDR luminance follows the input, follows LLDV metadata, or uses user-supplied values. The user-value controls are part of the relevant HDR/profile workflow rather than this compact General card.

The General input choices are defaults. The VP Renderer and DirectShow **Input Processing** tabs can override them independently.

---

[← Configuration guide index](README.md)
