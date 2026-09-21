# For developers

> **Out of date — pending a maintainer update.** This page has not kept pace with the solution.
> `VideoProcessor-Config`, its tests and `OutputProbe` build with the v143 toolset, so Visual
> Studio 2019 alone cannot build the solution; the config editor is also a Qt 6 application, whose
> prerequisites are documented in
> [src/VideoProcessor-Config/README.md](src/VideoProcessor-Config/README.md). The release
> packaging section below describes only the legacy packager and predates the VP-0192 installer in
> [docs/VP-0192_INSTALLER.md](docs/VP-0192_INSTALLER.md). Check anything here against the project
> files before relying on it.

Get the source from https://github.com/billslack2/videoprocessor

 * MSVC 2019 community edition
    * Install MFC libraries
    * Debug builds require the [Visual Leak Detector](https://kinddragon.github.io/vld/) Visual C++ plugin

**Debugging**

 * Directshow call debugging
    * Open regedit
    * Go to Computer\HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectShow\Debug
    * Make key (=folder) VideoProcessor.exe
    * Set folder rights to user who will run it (or all)
    * Close regedit
    * Start application
    * Open regedit
    * Computer\HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\DirectShow\Debug\VideoProcessor.exe\ should have a bunch of entries like TRACE and LogToFile
    * Set log types to 5 or up

## Release packaging

After a successful x64 Release solution build, use `tools/package_release.ps1`
with the official Microsoft x64 runtime installer supplied through `-VcRedistPath`
or `VP_VC_REDIST_X64`. The build emits hashed toolset records used to derive the
runtime prerequisite; retain them with build outputs. Follow
[the release layout and prerequisite instructions](docs/VP-0107_RELEASE_LAYOUT.md).
Every ZIP must include the setup command, first-run instructions, installer, and
generated runtime requirement. Directly zipping build outputs is unsupported.
