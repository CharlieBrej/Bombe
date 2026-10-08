# Bombe

Bombe is a Minesweeper-derived deduction and automation game. Number clues
constrain neighbouring bombs, and those constraints become coloured regions
showing how many undiscovered bombs remain. Instead of solving every board by
hand, the player constructs reusable graphical rules which run automatically.

A rule can match one or more overlapping regions, mark cells as clear or as
bombs, create a derived region, or change a region's visibility. The same rules
are then tested against progressively harder boards. The larger puzzle is to
build a small, efficient, general-purpose ruleset which can solve as many levels
as possible.

The game and its tools are written in C++20 using SDL 2, Z3, and Zstandard.

## Features

- Square, triangular, and hexagonal boards, including wrapped and merged layouts.
- Six game modes, ranging from the regular ruleset to negative-bomb and
  implication-region puzzles.
- Exact, inequality, parity, XOR, variable, negative, and IF/THEN constraints.
- Rule usage, clearing, and CPU statistics, plus background robots which test a
  ruleset across the level catalogue.
- Hints, region filtering, rule priorities and groups, clipboard sharing,
  selectable themes, and multiple interface languages.
- Bundled level sets and an optional service for scores and weekly levels.

## Repository contents

- `Bombe` — the SDL game client, built from `main.cpp`, `GameState.*`, and the
  shared core.
- `GridGenerator` — the offline puzzle generator and verifier.
- `BombeServer` — the score and weekly-level service.
- `BombeTest` — the core rule and region regression test program.
- `BombeControl` — an optional Unix-domain-socket development client.
- `Grid.*` — board geometries, cells, regions, rules, Z3 validation, and rule
  application.
- `SaveState.*`, `Compress.*`, and `LevelSet.*` — structured data, compression,
  saves, and bundled levels.
- `lang.json`, `texture.png`, `font-*`, `snd/`, and `tutorial/` — runtime data
  and assets.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the subsystem map, important data
flows, and guidance on where different changes belong.

## Building

The Autotools configuration currently supports Linux and macOS. The dependency
installation examples below are for Linux.

Clone the repository with its clipboard helper submodule:

```sh
git clone --recurse-submodules https://github.com/CharlieBrej/Bombe.git
cd Bombe
```

If the repository was cloned without submodules, initialize them with:

```sh
git submodule update --init --recursive
```

### Dependencies

Debian or Ubuntu:

```sh
sudo apt install \
  autoconf automake g++ make pkg-config \
  libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-net-dev libsdl2-ttf-dev \
  libz3-dev libzstd-dev libcurl4-openssl-dev \
  libxcb1-dev libx11-dev libpng-dev
```

Fedora:

```sh
sudo dnf install \
  autoconf automake gcc-c++ make pkgconf-pkg-config \
  SDL2-devel SDL2_image-devel SDL2_mixer-devel SDL2_net-devel SDL2_ttf-devel \
  z3-devel libzstd-devel libcurl-devel \
  libxcb-devel libX11-devel libpng-devel
```

### Configure and compile

Generate `configure`, `Makefile.in`, and the other required Autotools helper files using `--install`:

```sh
autoreconf --install --force
./configure --disable-steam
make -j"$(nproc)"
```

The examples use Linux's `nproc`. On macOS, omit `-j` or use
`make -j"$(sysctl -n hw.logicalcpu)"`.

This builds the game, grid generator, and server in the repository root. To
build only the game:

```sh
make -j"$(nproc)" Bombe
```

The default Steam-enabled configuration requires the Steamworks SDK headers and `libsteam_api`. Use `--disable-steam` unless those are available locally.

### Faster development builds

When generated-code performance and debugging symbols are unimportant, disable optimization and debug information:

```sh
make -j"$(nproc)" CXXFLAGS="-O0 -g0 -pipe"
```

After changing `configure.ac` or `Makefile.am`, regenerate the build system before configuring again:

```sh
autoreconf --install --force
./configure --disable-steam
```

## Testing

Build and run the core regression suite with:

```sh
make check
```

After it has been built, the test executable can also be run directly:

```sh
./BombeTest
```

## Running

Run the game from the repository root so it can find its fonts, textures, sounds, translations, tutorials, and level data:

```sh
./Bombe
```

The client loads assets using paths relative to the working directory. A local
development checkout also needs `music.ogg` in the repository root when SDL
audio is available; that asset is currently supplied separately rather than
tracked in Git.

The presence of an empty `FULL` file selects the full build; without it, the
client runs as the demo. An empty `PLAYTEST` file selects the playtest build.
These are local packaging markers and are not tracked.

### Optional local control socket

The Unix-domain control socket is excluded from normal builds. Enable it explicitly
when configuring on Linux or macOS:

```sh
./configure --disable-steam --enable-control-socket
make -j"$(nproc)" Bombe BombeControl
```

While the game is running, query it with:

```sh
./BombeControl state
./BombeControl rules
./BombeControl hint
./BombeControl hint-status
./BombeControl hint-clear
./BombeControl ping
```

`hint` starts the same asynchronous reduction as the in-game Hint button. It
reports cells that are logically determined by the shown regions, then hides
irrelevant regions until a supporting set remains. Poll `hint-status` until it
reports `complete`; the output lists target cells, supporting regions, regions
fixed visible by the user, regions hidden by the hint, and regions not yet
classified. `hint-clear` stops an active hint, restores only hint-owned
visibility changes, and clears its targets. The control-socket `hint` command
waits until the board, regions, and rules have all finished processing; retry
shortly if it reports that they are still busy.

Add a rule using a single-line JSON description. The success response and the
`rules` command expand each chosen area into a readable Venn expression:

```sh
./BombeControl add-rule '{"inputs":[{"type":"equal","value":0}],"action":{"type":"clear","areas":[1]},"comment":"Clear a zero-bomb region"}'
```

Each input is a region type such as `equal`, `less`, `more`, `xor1`, `xor2`,
`xor3`, `not-equal`, or `parity`. An input may have `"negated":true`; an
implication input instead has `"if":{...}` and `"then":{...}`. Optional
cell-count constraints have the form
`{"area":3,"type":{"type":"equal","value":1}}`.

Actions use one of these forms:

```json
{"type":"clear","areas":[1]}
{"type":"bomb","areas":[1]}
{"type":"hide","regions":[1]}
{"type":"equal","value":1,"areas":[1,3]}
```

Visibility actions may be `show`, `hide`, or `trash`. A region-creation action
may also specify `negative_areas` and an `if_type`. Top-level optional fields are
`priority` (-2 through 2), `group` (0 through 7), `paused`, and `comment`.
The game validates legality, mode restrictions, and duplicates before adding a rule.

For ordinary inputs, an area number is a membership bitmask: R1 is bit 1, R2
is bit 2, R3 is bit 4, and R4 is bit 8. For example, area 1 means R1 but not
the other inputs, while area 3 means the R1/R2 intersection but not R3 or R4.
Conditional inputs use adjacent `R1.if`/`R1.then` dimensions; negated inputs
add a corresponding `.negated` dimension. The normalized command response
shows the exact interpretation, such as `area 3 [R1 & R2]`.

The socket is created as `$XDG_RUNTIME_DIR/bombe-control.sock`, or as
`/tmp/bombe-control-UID.sock` when `XDG_RUNTIME_DIR` is unavailable. Its permissions
allow access only by the user running the game. Set `BOMBE_CONTROL_SOCKET` for both
programs to select a non-default path; the client also accepts `--socket PATH`.

## Server configuration

`BombeServer` is not required for local play. It listens on TCP port 42071 for
score and level traffic and uses libcurl for Steam ticket authentication. Review
its deployment and authentication settings before exposing an instance to a
network.

Set the Steam Web API key in the environment before running a production server:

```sh
export BOMBE_STEAM_WEB_API_KEY="your-api-key"
./BombeServer
```

Do not commit API keys to the repository.

## License

This project is licensed under the
[Creative Commons Attribution 4.0 International license](LICENSE). The `clip`
submodule carries its own license.
