![VideoProcessor banner](images/vp%20banner.png)

:film_projector: VideoProcessor turns a computer into a 4k HDR capable live video processor by connecting a video
capture card to a renderer and taking care of details such as conversion, timing and HDR metadata.

This allows advanced renderers to do things like 3D LUT, HDR tone mapping, scaling, deinterlacing and much more
which can significantly improve image quality on most displays and beamers.

_Capture cards cannot capture HDCP protected data, VideoProcessor can only process what can be captured._

# Install

Download the x64 Setup executable from [Releases](https://github.com/billslack2/videoprocessor/releases) and run it
normally, **not as administrator** — the [install guide](configuration-guide/install.md) covers destinations,
updating, moving and uninstalling.

# Documentation

- [Configuration guide](configuration-guide/README.md) — the configuration editor screen by screen, with screenshots.
  Start here if you want to understand what a setting does before you change it.
- [Configuration reference](CONFIGURATION.html) — the `VideoProcessor.cfg` keys, section by section. Use this
  when you are reading or editing the configuration file directly.
- [Developers.md](DEVELOPERS.md) — building from source.

# License & legal

This application is released under the GNU GPL 3.0 for non-commercial usage; commercial usage to build and sell
video processor systems is not allowed. See [LICENSE.txt](LICENSE.txt).

Parts of this code are made and owned by others, for example SDKs; in all such cases there are LICENSE.txt and
README.txt files present to point to sources, attributions and licenses.

------

VideoProcessor was created by [Dennis Fleurbaaij](mailto:mail@dennisfleurbaaij.com), copyright 2021. This
repository is an independent fork and is not maintained by the original author. The original project's site and
wiki — [videoprocessor.org](http://videoprocessor.org) and the
[defl/videoprocessor wiki](https://github.com/defl/videoprocessor/wiki) — remain useful background, but they
describe the original release and not the features or configuration of this fork.
