# Third-party notices

Pixmo Games is built with the following software. Thanks to their authors.

## In the app (`pixmogames.wuhb`)

| Component | License | Source |
|---|---|---|
| SDL2 (Wii U port) | zlib | https://github.com/libsdl-org/SDL , https://github.com/devkitPro/SDL |
| wut (Wii U toolchain and runtime) | zlib | https://github.com/devkitPro/wut |
| stb_image, stb_image_write, stb_truetype (Sean Barrett) | Public domain or MIT (your choice) | https://github.com/nothings/stb |

## In the autostart plugin (`pixmo_inicio.wps`)

| Component | License | Source |
|---|---|---|
| WiiUPluginSystem (libwups) | LGPL-3.0 | https://github.com/wiiu-env/WiiUPluginSystem |
| librpxloader | LGPL-3.0 | https://github.com/wiiu-env/librpxloader |
| wut | zlib | https://github.com/devkitPro/wut |

The plugin links these libraries statically. As the LGPL-3.0 requires, the
complete source code of the plugin and its build files are in `plugin/` of the
Pixmo Games source code, so you can rebuild it with a modified version of the
libraries (`./build-plugin.ps1`, or `make` inside the build image described in
`plugin/Dockerfile`). The texts of the LGPL-3.0 and of the GPL-3.0 it refers to
are in `licenses/`.

## Used at runtime, not included

- **Game covers**: downloaded by the app from [GameTDB](https://www.gametdb.com)
  (art.gametdb.com) when the "Download covers" setting is on.
- **Font**: the app draws text with the Wii U's own system font, read from the
  console at runtime.
- **Aroma** (https://aroma.foryour.cafe): the homebrew environment the app and
  the plugin run in.

## zlib license (SDL2, wut)

```
This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## stb license

stb is available under your choice of the MIT license or public domain
(Unlicense); see the end of each `third_party/stb_*.h` file.

Pixmo Games is not affiliated with or endorsed by Nintendo. Wii U, GamePad and
the names of consoles and games are trademarks of their respective owners.
