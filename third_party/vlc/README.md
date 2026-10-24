# Terminal VLC backend

This directory contains the application's Qt Quick adapter and CMake integration,
not a platform-specific VLC binary or a fork of VLC. Link the **VLC 3.x** SDK for
the target architecture. VLC 4 is deliberately rejected (incompatible public API).
Upstream API: https://code.videolan.org/videolan/vlc/-/tree/3.0.x/include/vlc

## SDK discovery

- Debian/Ubuntu/Raspberry Pi OS: install `libvlc-dev vlc-plugin-base vlc-plugin-video-output`.
- Windows: extract the matching VLC 3.x SDK to a prefix containing `include/vlc`
  and `lib/libvlc.lib` (MSVC) or a compatible import library (MinGW). Set
  `-DVLC_ROOT=...`; do not mix SDK architecture/compiler formats. The official VLC
  distribution may require using its `sdk/include` as the include prefix.
- macOS: use the VLC 3.x SDK prefix with `include/vlc` and `lib/libvlc.dylib`;
  set `-DVLC_ROOT=...`. For VLC.app, supply `LIBVLC_INCLUDE_DIR` and
  `LIBVLC_LIBRARY` explicitly if the SDK layout differs.
- Any layout: `-DLIBVLC_INCLUDE_DIR=... -DLIBVLC_LIBRARY=...` takes precedence.

At runtime VLC needs **libvlc, libvlccore, plugins and their dependent libraries**.
For a private deployment set `VLC_PLUGIN_PATH` to that distribution's plugins
directory before launch, and use the platform's normal loader search paths.
Do not copy Linux binaries to Windows/macOS, or x86 binaries to ARM. Distribute
the applicable VLC/plugin licenses and corresponding source as required by the
specific VLC build; SDK/binaries are not silently fetched during configuration.

## Raspberry Pi cross compilation

Use the project's target Qt 6.4+ toolchain and a sysroot containing ARM VLC 3.x.
Set `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY` and
`CMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY`. For pkg-config set
`PKG_CONFIG_SYSROOT_DIR` and `PKG_CONFIG_LIBDIR` to target paths, not host paths.
Use `VLC_ROOT` or explicit target SDK paths if no target pkg-config is available.

## Renderer and limits

The memory-callback renderer is independent of HWND/X11/NSView. This keeps QML
controls, clipping and fullscreen overlays composited correctly on each platform.
It is **not a zero-copy hardware-decoding path**. The video page offers 480p
(default, lower load), 720p and 1080p output caps, selectable in the playback
controls or fullscreen controls. Choosing a cap restarts the
current file and attempts to restore its position and pause/play state. 1080p
requires an explicit Pi 3B performance warning confirmation. The backend still
limits UI presentation to about 30 Hz and keeps one aligned decode buffer plus
one latest snapshot. Input files retain their original aspect ratio. Output
resolution is not an input-stream bitrate selector, and reducing it does not
guarantee that an upstream decoder avoids all high-resolution work. A Pi hardware
performance run is required before claiming any cap is suitable for a particular source.

The SDK/backend can also be built/tested independently of the terminal's Linux
hardware integrations: `cmake -S third_party/vlc -B build/vlc -DBUILD_TESTING=ON`.
The terminal itself still requires the existing Qt Bluetooth, WebEngine and other
platform integrations; SDK portability does not certify the entire app on every OS.
