# OpenRCT2 for Nintendo 3DS (homebrew, work in progress)

An experimental port of OpenRCT2 to the **New Nintendo 3DS** (devkitARM / libctru).
An original 3DS does not have enough RAM; a New 3DS / New 2DS XL running
custom firmware (Luma3DS) is required.

## SD card layout

```
sdmc:/3ds/openrct2/openrct2.3dsx     the game (launch from the Homebrew Launcher)
sdmc:/3ds/openrct2/data/             OpenRCT2's own data (see below)
sdmc:/3ds/openrct2/rct2/             your RollerCoaster Tycoon 2 installation
sdmc:/3ds/openrct2/rct1/             (optional) RollerCoaster Tycoon 1
sdmc:/3ds/openrct2/config.ini        written on first start; saves go to save/
```

`data/` must contain `g2.dat`, `fonts.dat`, `palettes.dat`, `tracks.dat`,
`language/`, `scenario_patches/`, `object/`, `sequence/` and `assetpack/`
(the same files a desktop install ships in `share/openrct2`).

`rct2/` is a copy of the RCT2 game folder with its `Data`, `ObjData`,
`Scenarios`, `Landscapes` and `Tracks` sub-folders. Only the files are read;
nothing is written there.

## Screens and controls

* Bottom screen: the game, 320x240. The touch screen is the mouse.
* Top screen: log output (useful while the port is young).
* Circle pad / D-pad: scroll the view. Text input opens the system keyboard.

## Building

Needs devkitPro with `devkitARM`, `libctru`, `3ds-pkg-config` and the 3DS
portlibs `3ds-zlib`, `3ds-libpng`, `3ds-libzstd`, `3ds-libiconv`,
`3ds-libfribidi`, plus SDL2 and libzip cross-compiled into
`$DEVKITPRO/portlibs/3ds` and the nlohmann-json headers in its `include/`.

```
cmake -S . -B build-3ds -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake \
    -DCMAKE_BUILD_TYPE=Release
ninja -C build-3ds
```

The graphics data files (`g2.dat` etc.) are produced by a native build of
`openrct2-cli` (`openrct2-cli sprite build ...`), as for any cross-compile.
