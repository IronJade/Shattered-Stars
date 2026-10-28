# Building from Source

This page covers compiling Shattered Stars yourself, running its tests, making the release
packages and building the Archipelago `.apworld`. The commands are for Linux; the Windows
release is cross-compiled from Linux. A macOS build (Apple Silicon, Xcode's clang) is in
progress: the same `cmake` commands work there, with CMake and Ninja from Homebrew or
`pip3 install --user cmake ninja`; headless runs there use `SDL_VIDEO_DRIVER=dummy`, since SDL's
offscreen driver can't open a window on macOS.

## Getting the source

```sh
git clone https://github.com/IronJade/Shattered-Stars.git
cd Shattered-Stars
```

The repository contains no game data. To run the game or the disc tests you need your own
disc images, as for playing ([Installing and Setup](Installing-and-Setup#your-disc-images)).

## Dependencies

| Need | Notes |
|---|---|
| CMake 3.24 or newer | |
| A C++20 compiler | GCC or Clang |
| Ninja | Optional, but the examples use it |
| SDL3 development files | Recommended: `sudo dnf install SDL3-devel` (Fedora) or `sudo apt install libsdl3-dev` (Debian / Ubuntu). Without them the build downloads SDL 3.4.18 (pinned by SHA-256) and builds it, but that copy only has sound if your system has the PipeWire, PulseAudio or ALSA development headers |
| OpenSSL development files | Optional. Without them the Archipelago client can only use `ws://` servers, not `wss://` |
| zlib development files | Optional. Lets the Archipelago client use compression |
| Python 3 | For the tools in `tools/` (patch writer, smoke test, packaging helpers) |

CMake also downloads, pinned by SHA-256 or version:

- **Dear ImGui**, for the setup program;
- **apclientpp** with **wswrap**, **websocketpp**, **asio** and **nlohmann/json**, for the
  Archipelago network client.

### Build options

| Option | Default | Effect |
|---|---|---|
| `SS_BUILD_SETUP` | `ON` | Build the setup program (fetches Dear ImGui) |
| `SS_BUILD_AP_NET` | `ON` | Build the Archipelago network client (fetches apclientpp and its dependencies) |

For example, `-DSS_BUILD_SETUP=OFF` leaves the setup program out.

## Configure and build

```sh
cmake -S port -B port/build -G Ninja
cmake --build port/build
```

This produces, in `port/build/`:

| Program | What it is |
|---|---|
| `shattered-stars` | The game |
| `shattered-stars-setup` | The setup program |
| `ss_tests` | Unit and disc tests of the hardware layers |
| `ss_game_tests` | Tests of the game code |

Run the game with `port/build/shattered-stars`, or set it up first with
`port/build/shattered-stars-setup`. Disc paths given on the command line are remembered:

```sh
port/build/shattered-stars --disc1 ".../Star Ocean - The Second Story (Disc 1).cue" \
                             --disc2 ".../Star Ocean - The Second Story (Disc 2).cue"
```

The project version (0.03.00 today) is set once, in `port/CMakeLists.txt`'s `project(...)`
line. The programs print it with `--version`.

## Tests

| Test | Command | Needs |
|---|---|---|
| `ss_tests` | `SS_DISC1=".../Disc 1.cue" port/build/ss_tests` | `SS_DISC1` for the disc tests. Without it, only the unit tests run |
| `ss_game_tests` | `SS_DISC1=".../Disc 1.cue" port/build/ss_game_tests` | `SS_DISC1` for the disc tests |
| Smoke test | `SS_DISC1=".../Disc 1.cue" port/tools/smoke_field_events.sh port/build/shattered-stars` | `SS_DISC1`; Disc 2 is found next to it. Python 3 |

- **`ss_tests`** covers the disc reader, the archive and decompression, the GPU, GTE, MDEC,
  sound and memory cards. Its disc tests compare against reference values taken from the
  original.
- **`ss_game_tests`** covers the game code: the event-script interpreter, field characters,
  menus, battles, Archipelago and more.
- **The smoke test** plays real stretches of the game headless: it starts on chosen maps, plays
  scripted input, and checks doors, chests, shops, talking, mods, the debug console and
  Archipelago seeds.

The tests never open a window or play sound, and the smoke test uses a throwaway memory card.
None of them write to your own settings.

### Testing tips

- **Headless runs:** `--headless` runs the game without a window; it uses only memory cards
  named for the run (`--memcard1`) and never writes `config.ini`. Use `SDL_AUDIO_DRIVER=dummy`
  to keep a run silent. With an Archipelago seed, a headless run uses the seed's own memory card
  only with `SS_AP_SEED_CARD=1`, and `SS_AP_ANY_SAVE=1` lets it load an ordinary save.
- **AddressSanitizer:** configure a separate build directory with `-fsanitize=address` to catch
  memory errors.
- **Reference regression suite:** `port/tools/ref_regress.py` replays recorded comparisons with the
  original game. It needs recordings made with PCSX-Redux, which are not part of the repository;
  see [port/tools/refs/README.md](https://github.com/IronJade/Shattered-Stars/blob/main/port/tools/refs/README.md).

## Release packages

### Linux

```sh
port/tools/build_linux.sh --container
# -> dist/ShatteredStars-<version>-linux-x86_64.tar.gz
```

This builds inside a Debian 12 container with podman (`port/tools/linux-build/Containerfile`,
pinned by digest), so the result runs on any distribution with glibc 2.36 or newer. SDL 3 and
Dear ImGui are built from source there and linked in, along with the C++ runtime, so players
need only glibc and OpenGL 3.3. The repository is mounted read-only.

Without `--container`, the script builds on your own machine (it needs SDL3 development files
and the static C++ library, `libstdc++-static` on Fedora). That package then needs your
machine's glibc version or newer.

### Windows

The Windows release is cross-compiled from Linux with MinGW-w64. On Fedora:

```sh
sudo dnf install mingw64-gcc-c++ mingw64-SDL3 mingw64-openssl-static mingw64-zlib-static
port/tools/build_windows.sh
# -> dist/ShatteredStars-<version>-win64.zip
```

OpenSSL (for `wss://` Archipelago servers) and zlib are linked in, as are the C and C++
runtimes, so the zip holds everything the game needs. Without root, unpack the OpenSSL and zlib
packages' RPMs yourself and set `MINGW_SSL_SYSROOT` to the unpacked
`usr/x86_64-w64-mingw32/sys-root/mingw` folder.

The Windows build renders the same frames as the Linux build, byte for byte, and its test
suites pass under Wine.

Both scripts read the version from `port/CMakeLists.txt`, and both packages include
`LICENSE`, the third-party licences, the README and an empty `discs` folder.

### Checking the setup program's layout

`port/tools/setup_screenshots.sh [PROGRAM] [OUT_DIR]` renders every page and state of the setup
program offscreen as PNG files, with scratch settings, without opening a window.

## The .apworld

The Archipelago world's source is in `apworld/so2`. To package it:

```sh
AP_INSTALL=path/to/Archipelago AP_VENV=path/to/venv apworld/tools/build_apworld.sh [OUT_DIR]
# -> dist/so2.apworld and dist/"Star Ocean The Second Story.yaml"
```

- `AP_INSTALL` is an Archipelago source checkout (0.6.8 or newer), and `AP_VENV` a Python
  virtual environment with its requirements installed.
- The script uses Archipelago's own **Build APWorlds** launcher component, which adds the
  manifest fields a packaged world needs. Don't zip the folder by hand: a hand-made zip loads
  with a manifest error.
- It also writes the world's options template YAML with Archipelago's own template generator.
- If the world isn't already installed in that checkout, the script links it in for the build
  and removes the link afterwards.

The world's own tests run with Archipelago's test runner, from the Archipelago checkout, with
the world linked in as `worlds/so2`:

```sh
python -m pytest worlds/so2/test
```

## The PopTracker pack

The [PopTracker pack](Trackers#poptracker) is generated from the world's own tables and the map
layout (`docs/trackers/tracker_maps.json`). It needs the scratch Archipelago that
`apworld/tools/ap_test_server.sh` sets up (`scratchpad/ap`, the world linked in, Pillow and lupa in its
venv):

```sh
trackers/tools/build_poptracker.sh all
# -> dist/poptracker/so2_shattered_stars_<version>.zip, its .sha256 and versions.json,
#    then PopTracker's pack checker and the pack's tests
```

Its images are drawn by the generator, original stand-ins at the layout's sizes; the real maps
come from each player's own disc (`shattered-stars --render-tracker-maps`). The details, the
tests and a headless run in the real PopTracker are in
[docs/trackers/poptracker.md](https://github.com/IronJade/Shattered-Stars/blob/main/docs/trackers/poptracker.md).

## Where to go next

- [How the Port Was Made](How-the-Port-Was-Made): the method and the verification.
- [Contributing](Contributing): coding conventions, and how to propose a change.
