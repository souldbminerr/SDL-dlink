# sdl-dlink FORK

an UNNOFFICAL, fork of devkitPro SDL3 that adds support for dynamic linking

using libnx-dlink and a neko3d draw backend (instead of EGL)
requires neko3d.dnro is sdmc:/lib/

And one other thing: you have to manually do VI layer initialization and NWindow initialization instead of using SDL for this (this is to allow low level/nonstandard applications)

The unmodified SDL3 readme follows

Simple DirectMedia Layer (SDL for short) is a cross-platform library
designed to make it easy to write multi-media software, such as games
and emulators.

You can find the latest release and additional information at:
https://www.libsdl.org/

Installation instructions and a quick introduction is available in
[INSTALL.md](INSTALL.md)

This library is distributed under the terms of the zlib license,
available in [LICENSE.txt](LICENSE.txt).

Enjoy!

Sam Lantinga (slouken@libsdl.org)
