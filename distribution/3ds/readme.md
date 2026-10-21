# OpenRCT2 for Nintendo 3DS (homebrew, work in progress)

An experimental port of OpenRCT2 to the **New Nintendo 3DS** (devkitARM / libctru).
An original 3DS does not have enough RAM; a New 3DS / New 2DS XL running
custom firmware (Luma3DS) is required.

## Two ways to run it

* **`openrct2.cia`** (recommended): install with FBI, start it from the HOME
  menu. The title asks the kernel for the 178 MB "extended" memory mode, which
  OpenRCT2 needs for comfortable headroom (a title park + a scenario).
* **`openrct2.3dsx`**: copy to `sdmc:/3ds/openrct2/` and start it from the
  Homebrew Launcher. The Homebrew Launcher only grants ~124 MB, so large parks
  can run out of memory; the game tells you on the bottom screen if that happens.

## SD card layout

```
sdmc:/3ds/openrct2/openrct2.3dsx     (only for the Homebrew Launcher route)
sdmc:/3ds/openrct2/data/             OpenRCT2's own data (see below)
sdmc:/3ds/openrct2/rct2/             your RollerCoaster Tycoon 2 installation
sdmc:/3ds/openrct2/rct1/             (optional) RollerCoaster Tycoon 1
sdmc:/3ds/openrct2/OpenRCT2/         written by the game: config.ini, save/, object index
sdmc:/3ds/openrct2/openrct2.log      copy of everything printed on the bottom screen
```

`data/` must contain `g2.dat`, `fonts.dat`, `palettes.dat`, `tracks.dat`,
`language/`, `scenario_patches/`, `object/`, `sequence/` and `assetpack/`
(the same files a desktop install ships in `share/openrct2`).

`rct2/` is a copy of the RCT2 game folder with its `Data`, `ObjData`,
`Scenarios`, `Landscapes` and `Tracks` sub-folders. Only the files are read;
nothing is written there. The SD card is case sensitive as far as the game is
concerned, so keep the original file names (`ObjData/*.DAT` etc.).

For sound, the DSP firmware must have been dumped once with the DSP1 homebrew
(`sdmc:/3ds/dspfirm.cdc`); without it the game runs silently.

## Screens and controls

* Top screen: the game at 400x240. The toolbar uses two rows.
* Bottom screen: the touch screen is the mouse (tap = click, two quick taps =
  right click) and shows the log while the port is young.
* Circle pad / D-pad: scroll the view. L / R: zoom. A / Y: rotate the view.
* X: map. B: close the top-most window. START: pause. SELECT: options.
  (All of these are ordinary shortcuts and can be changed in the options.)

Keep the stylus still while tapping: a tap registers where the stylus was
last seen.

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

`cia/build-cia.sh` packages the ELF as a CIA (needs `makerom` and
`bannertool`).

## Known limitations

* The first start builds the object index (a few minutes); later starts
  validate it, which still takes a while on the SD card.
* Loading a park takes around a minute (object loading is I/O bound).
* Windows larger than the screen are cut off at the right/bottom edge; B
  closes them.
* No network, no scripting, no OpenGL, no TrueType fonts, no RCT1 graphics
  (csg1.dat is skipped to save memory, so RCT1 scenarios are not shown).
* The title sequence loads parks in the background; input is not processed
  while a park is loading.
