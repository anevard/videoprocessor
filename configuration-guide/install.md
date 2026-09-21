# Installing VideoProcessor

VideoProcessor is installed by an x64 Setup executable. Setup is self-contained, works offline, and installs one copy per Windows user. Nothing is installed system-wide and no elevation is needed.

This page covers installing, updating, moving and uninstalling. For the complete contract — including how setup verifies and recovers files — see [VP-0192_INSTALLER.md](../docs/VP-0192_INSTALLER.md).

## Install

1. Download the x64 Setup executable from [Releases](https://github.com/billslack2/videoprocessor/releases).
2. Close VideoProcessor, then save and close Config. Config keeps a tray icon near the clock; closing its window only hides it, so choose **Exit** from the tray icon. Setup never force-terminates either program.
3. Run setup **normally, not as administrator**.
4. Choose the destination.

Fresh installs default to your local `Programs\VideoProcessor`. To adopt an existing ZIP installation, select its actual root — for example `C:\Videoprocessor\vp` — rather than its parent. Your existing settings and data in that folder are kept.

The destination must be writable without elevation. Setup tests write access and does not grant itself broader permissions, so a protected location such as `C:\Program Files` has to be moved to a user-writable folder instead.

Setup remembers the location for your Windows user, and later installers reuse it automatically wherever the downloaded installer happens to sit. Other Windows accounts have their own separate installation.

## What gets installed, and where

Everything lives in the application folder: the binaries, the default shaders, the bundled user guides, and the licenses. VideoProcessor keeps its configuration and state beside the executable, its logs under `logs`, and the renderer cache under `vprenderer`.

Microsoft-signed x64 Visual C++ runtime DLLs are installed beside the application and in `config` and `vprenderer` as their dependencies require. There is no system-wide redistributable to run, no runtime elevation prompt, and no desktop icon. Windows 10 and 11 supply the Windows and UCRT components themselves.

The only things outside the application folder are Start menu shortcuts and one per-user uninstall registration.

## Updating

Run the newer setup the same way. It replaces the application binaries and the bundled user guides, and retains everything you own:

- `VideoProcessor.cfg` and `VideoProcessor.state`
- Profiles, custom shaders and LUTs
- Logs and caches

`VideoProcessor.cfg.example` is a reference copy, not a replacement for your settings. No configuration migration happens, so setup does not edit or back up your configuration — keep your own backup before you hand-edit it.

Keep `VideoProcessor.exe` and `vprenderer\VideoProcessorVPRenderer.dll` from the same release. They are one versioned pair, and mixing them across builds is not supported.

Setup accepts the same build again, a different commit at the same version, and older builds. Rolling back means running the older installer you want; permanent copies of previous builds are not kept. An older application may not understand settings written by a newer one, and setup never rewrites them for it.

If you keep custom binary plugins in the application folder, check `recovered-files` after an update. Unknown DLLs found in the application load paths are moved there rather than deleted.

## Moving the installation

Uninstall first — your data is retained — then move the preserved folder and select it when you reinstall. Setup refuses a destination that conflicts with the one already registered for your user.

## Uninstalling

Use **Uninstall VideoProcessor** in the application folder or on the Start menu, or Windows Settings → Apps. Uninstall removes the tracked application files, the shortcuts, the private runtime DLLs and the single registration. It keeps your configuration, state, profiles, shaders and LUTs, so reinstalling into the same folder picks your settings back up. No system runtime is changed.

If Config is still running, uninstall will say so; exit it from the tray icon and choose **Retry**, or **Cancel** to leave the installation as it was.

Deleting the application folder by hand is not the same as uninstalling. It removes the application, its private runtimes and any of your data inside that folder, while leaving the Windows uninstall entry and the Start menu shortcuts behind. Rerunning setup recreates the folder at its remembered location, but it cannot bring back data you deleted.

## If setup is interrupted

Do not launch VideoProcessor. Close VideoProcessor and Config, then run an intact installer again: it restores the pending application-file transaction before it updates anything. Recovery covers application files only. Never restore your configuration or state from an application-file backup.

---

[← Configuration guide index](README.md)
