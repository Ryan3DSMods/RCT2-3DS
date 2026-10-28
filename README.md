# RCT2-3DS

**RollerCoaster Tycoon 2 running natively on the New Nintendo 3DS.**
Built on [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2). No streaming and no PC needed.

## What you need

- A **New** 3DS, New 3DS XL or New 2DS XL with custom firmware (Luma3DS): see [3ds.hacks.guide](https://3ds.hacks.guide)
- Your own copy of **RollerCoaster Tycoon 2** (GOG or Steam)

> An original (non-New) 3DS doesn't have enough memory.
> No game files are included. You need your own copy.

## Install (5 minutes)

1. Download the latest zip from the [Releases](../../releases) page.
2. Unzip it onto your SD card. It creates `3ds/openrct2/` with the game and its data.
3. Copy your RCT2 game folder into `3ds/openrct2/rct2/`.
   It should contain `Data`, `ObjData`, `Scenarios`, `Landscapes` and `Tracks`.
4. Open the Homebrew Launcher and start **OpenRCT2**.

Have RollerCoaster Tycoon 1 too? Copy it to `3ds/openrct2/rct1/` to play the classic RCT1 scenarios.

### Where is my RCT2 folder?

- **GOG (Windows):** `C:\GOG Games\RollerCoaster Tycoon 2 Triple Thrill Pack\`
- **Steam:** right-click the game → *Manage* → *Browse local files*
- **GOG on Mac:** right-click the app → *Show Package Contents* → `Contents/Resources/drive_c/GOG Games/...`

## Controls

| Input | Action |
| --- | --- |
| Touch screen | Mouse: tap to click |
| Circle pad / D-pad | Scroll around the park |
| Text fields | Open the 3DS keyboard |

## Known limits

- Work in progress. The source here is an early version of the port; newer builds will follow.

- New 3DS only.
- The game runs on the bottom screen (320×240). The top screen shows a log for now.
- No multiplayer.

## Building it yourself

Most people can skip this and use the Releases download.

This repo holds the 3DS port as a patch on top of OpenRCT2 v0.5.6 (in `patches/`). One command downloads OpenRCT2 and applies it:

```sh
./setup-source.sh
cd openrct2
```

To build, you need [devkitPro](https://devkitpro.org/) with devkitARM, libctru and 3ds-pkg-config, the 3DS portlibs (zlib, libpng, zstd, libiconv, fribidi), plus SDL2, libzip and the nlohmann-json headers.

```sh
cmake -S . -B build-3ds -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake \
  -DCMAKE_BUILD_TYPE=Release
ninja -C build-3ds
```

The data files (`g2.dat` and friends) come from a desktop build of `openrct2-cli`.

## Credits

- 3DS port by **Ryan**, [Ryan3DSMods on YouTube](https://www.youtube.com/@Ryan3DSMods)
- The [OpenRCT2](https://github.com/OpenRCT2/OpenRCT2) team and contributors, who made the engine
- RollerCoaster Tycoon 2 by Chris Sawyer
- [devkitPro](https://devkitpro.org/) and the SDL2 3DS port

### How AI was used

The 3DS port code was largely written with Claude, Anthropic's AI assistant. Ryan set the goal, supplied his own game files, built every version and tested it on his New 3DS.

## License

GPL-3.0, the same as OpenRCT2. See [LICENSE](LICENSE).

Unofficial fan project. Not affiliated with OpenRCT2, Chris Sawyer, Atari or Nintendo.
