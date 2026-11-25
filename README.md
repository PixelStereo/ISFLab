<p align="center"><img src="assets/ISFLab-256.png" alt="ISFLab logo" width="112"></p>

# ISFLab

**Browse, preview and edit ISF shaders.**
A desktop app by **Pixel Stereo** for macOS, Windows and Linux.

- **Browse** generators, filters and transitions by name, type or category, and make your own collections with tags (right-click a shader, Tags).
- **Preview** filters with an animated test card and transitions with looping **BEFORE / AFTER** cards. Use your own images, ISF generators or live feeds via Syphon, Spout or GStreamer shared memory.
- **Share** the preview with other software from the **Output** menu: Syphon (macOS), Spout (Windows), a virtual webcam through v4l2loopback (Linux) or OMT over the network (macOS, Windows, Linux, Raspberry Pi). See [Video outputs](#video-outputs).
- **Edit** GLSL alongside a visual editor for inputs and passes. Save your preview settings as shader defaults.
- **Export** filtered shaders to a folder or ZIP with their vertex shaders, images and relative paths intact.

## Download

<!-- release-downloads:start -->
**LATEST VERSION · [v0.4.1](https://github.com/PixelStereo/ISFLab/releases/tag/v0.4.1)**

[![macOS Apple Silicon](https://img.shields.io/badge/macOS-Apple_Silicon-6b7787?style=for-the-badge&logo=apple&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/ISFLab/releases/download/v0.4.1/ISFLab-macos-arm64-v0.4.1.zip)&nbsp;&nbsp;[![macOS Intel](https://img.shields.io/badge/macOS-Intel-515b69?style=for-the-badge&logo=apple&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/ISFLab/releases/download/v0.4.1/ISFLab-macos-x64-v0.4.1.zip)&nbsp;&nbsp;[![Windows Installer](https://img.shields.io/badge/Windows-Installer-2b6fb6?style=for-the-badge&logo=windows&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/ISFLab/releases/download/v0.4.1/ISFLab-windows-x64-v0.4.1-Setup.exe)&nbsp;&nbsp;[![Linux AppImage](https://img.shields.io/badge/Linux-Archive-b8821c?style=for-the-badge&logo=linux&logoColor=white&labelColor=1a1c20)](https://github.com/PixelStereo/ISFLab/releases/download/v0.4.1/ISFLab-linux-x64-v0.4.1.tar.gz)
<!-- release-downloads:end -->

Starting with the next release, Windows x64 is distributed as a single installer that bundles the required runtime libraries and adds ISFLab to the Start menu.

## Quick start

Clone with `git clone --recurse-submodules https://github.com/PixelStereo/ISFLab.git`.

Build with **Qt 6.5+** (Widgets, OpenGL, OpenGL Widgets and Multimedia), **CMake 3.21+** and a **C++17** compiler.

```sh
./build.sh
```

On Windows, run `build.cmd` from a Visual Studio developer shell with Qt on CMake’s search path. For live input on Linux, build with the GStreamer development libraries and install the shared-memory plugin.

## Video outputs

The rendered picture can be shared with other software. Every output is off until you enable it in the **Output** menu, and costs nothing while it is off. The picture is shared at its render size: choose a fixed resolution in the preview for a stable size. Nothing is sent while the preview is paused or hidden.

| Output | Platform | How it works |
| --- | --- | --- |
| **Syphon** | macOS | The GPU texture is shared (a GPU-side copy, no read-back). |
| **Spout** | Windows | The GPU texture is shared (a GPU-side copy, no read-back). |
| **Virtual webcam** | Linux, Raspberry Pi | The picture is read back, converted to YUYV and written to a v4l2loopback device. Needs the module: `sudo modprobe v4l2loopback exclusive_caps=1 card_label=ISFLab` (`exclusive_caps=1` makes it visible to Chrome). The size cannot change while another application has the camera open. |
| **OMT** | macOS, Windows, Linux, Raspberry Pi | The picture is read back and sent over the network ([Open Media Transport](https://github.com/openmediatransport), MIT license; the VMX codec runs on the CPU). `libomt` and `libvmx` are included in the downloads (built from source by the release workflow). When you build ISFLab yourself, run `python3 scripts/build_omt.py out` (needs the .NET 8 SDK and clang) and put them next to the application, in `OMT_LIBRARY_DIR` or in the system's library folder. On Linux it also needs the Avahi service. |

The read-back is asynchronous (three pixel buffers, mapped two frames later), allocated only while a read-back output is on and only again when the picture's size changes. Conversion and sending run in a thread of their own; a late output drops pictures instead of slowing the preview.

Each output can be left out of the build with `-DISFLAB_OUTPUT_SYPHON=OFF`, `-DISFLAB_OUTPUT_SPOUT=OFF`, `-DISFLAB_OUTPUT_V4L2=OFF` or `-DISFLAB_OUTPUT_OMT=OFF`. Preferences holds the output name, the virtual webcam device and the OMT quality.

Open **File → Load examples** to try all three shader types.

Push a version tag such as `v0.0.1` to publish macOS, Windows and Linux archives through GitHub Actions.

[ISF documentation](https://docs.isf.video/)

## License

AGPL-3.0, see [LICENSE](LICENSE).
