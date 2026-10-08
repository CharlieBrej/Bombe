# Bombe architecture

This is the living map of the Bombe codebase: what each major part owns, how
state moves through the program, and where a change normally belongs. Keep it
focused on durable structure and invariants; implementation details should stay
near the code.

## System map

```text
main.cpp
  |
  v
GameState -----------------------------------------------------+
  |        |             |             |                       |
  |        |             |             +--> SDL UI/audio/input |
  |        |             +--> SaveState + Compress             |
  |        +--> LevelSet + levels.data                         |
  v                                                          network
Grid + GridRule + GridRegion <--> Z3                            |
  |                                                             v
  +--> SquareGrid / TriangleGrid / HexagonGrid             BombeServer

Optional development path:
BombeControl --> Unix-domain socket --> ControlSocket --> GameState
```

The game client is deliberately fairly direct: `main.cpp` owns the application
lifetime, `GameState` owns the running game and interface, and `Grid` owns the
puzzle domain. There is no separate UI framework or dependency-injection layer.

## Source boundaries

| Area | Files | Responsibility |
| --- | --- | --- |
| Application lifetime | `main.cpp` | SDL startup/shutdown, Steam integration, save-file selection, the main loop, and periodic saves. |
| Runtime and interface | `GameState.cpp`, `GameState.h` | Input, rendering, audio, themes, translations, progression, active rules, hints, robot workers, and server communication. |
| Puzzle domain | `Grid.cpp`, `Grid.h` | Grid geometry, clues, cells, regions, rule matching and application, rule statistics, logical validation, and deterministic-solution checks. |
| Structured data | `SaveState.cpp`, `SaveState.h` | The small JSON-compatible `SaveObject` parser and serializer used throughout the project. |
| Compression | `Compress.cpp`, `Compress.h` | Zstandard compression using the project's embedded dictionary. |
| Level bundles | `LevelSet.cpp`, `LevelSet.h`, `levels.data` | Loading and saving the bundled compressed level collections. |
| Shared primitives | `Misc.cpp`, `Misc.h` | Coordinates, rectangles, colours, random numbers, transforms, and checksums. |
| Clipboard | `ImgClipBoard.cpp`, `ImgClipBoard.h`, `clip/` | Rule and level clipboard data plus image export; `clip/` is a Git submodule. |
| Offline generation | `grid_generator.cpp` | Generates level collections and verifies their solvability. |
| Online service | `BombeServer.cpp` | Weekly levels, scores, persistence, and Steam-ticket authentication. |
| Local automation | `ControlSocket.cpp`, `ControlSocket.h`, `BombeControl.cpp` | Opt-in, local Unix control protocol for inspecting state, hints, and rules. |
| Regression tests | `BombeTest.cpp` | Shared core regressions, currently focused on rule and region behaviour. |

## Core puzzle model

`Grid` is the aggregate root for a board. Its concrete subclasses implement the
square, triangular, and hexagonal neighbourhood and rendering geometry while
the base class implements the shared solving behaviour.

- `GridPlace` is a cell: its hidden bomb value, reveal state, optional negation,
  and clue.
- `RegionType` is a predicate over a bomb count, such as equality, inequality,
  parity, XOR, or a variable-based expression.
- `RegionIfType` represents either an ordinary predicate or an IF/THEN pair.
- `GridRegion` applies a predicate to sets of board cells. It also records its
  visibility, stale/deleted state, colour, priority, and the rule or clue which
  generated it.
- `GridRule` describes input-region predicates, constraints on Venn areas, and
  an action. Actions can reveal clear cells, reveal bombs, create regions, or
  alter region visibility.
- `XYSet` is the compact cell-set representation. It is a 32 by 32 bitset, so a
  new board representation must not silently exceed that coordinate range.

For ordinary negative regions, `GridRegion::elements_neg` identifies the
negative cells. For implication regions, the same field stores the IF-side
partition while `elements` stores the THEN side. Code which displays, filters,
or transforms regions must distinguish those two meanings with `is_if_then()`.

### Rule slots and areas

A rule's Venn model has at most four dimensions. An ordinary input uses one. A
negative input adds a negative-membership dimension, while an implication input
occupies adjacent IF and THEN slots but refers to one physical `GridRegion`.
`GridRule::has_valid_structure()` enforces the permitted combinations. Use
`GridRule::input_index_for_slot()` and `GridRule::make_cause()` when translating
between logical slots and physical inputs; treating the two implication halves
as two region pointers loses or swaps provenance.

Rule action areas are membership bitmasks over the logical slots. For ordinary
inputs, bit 0 is R1, bit 1 is R2, and so on; area 3 therefore means the R1/R2
intersection. Implication halves contribute separate adjacent dimensions. Keep
area transforms, rendering, serialization, and matching in agreement whenever
the slot model changes.

## Runtime flow

1. `main.cpp` loads and decompresses the user's save, then constructs one
   `GameState`.
2. `GameState` loads translations, assets, level progress, per-mode rule lists,
   and level data, then selects the active grid.
3. The main loop polls optional control requests, handles SDL events, renders,
   and calls `GameState::advance()`.
4. Advancement adds base regions from clues and incrementally processes regions
   through the active rules. A match may reveal a cell, change visibility, or
   queue another derived region.
5. Generated regions retain their causes, allowing the inspector, statistics,
   filtering, and visibility logic to trace deductions.
6. Background robot workers load independent grids and run the same rule set
   over other levels to update coverage and performance statistics.
7. The main loop serializes `GameState`, compresses it, and writes the main save
   plus rolling backups periodically and on shutdown.

The incremental rule loop intentionally yields between deductions so rendering
and input remain responsive. Avoid replacing it with an unbounded solve on the
main thread.

## Rule validation and hints

`GridRule::is_legal()` encodes the input and output relationship in Z3 and
classifies it as valid, impossible, useless, unbounded, or potentially
information-losing. `GameState::rule_is_permitted()` accepts valid and
information-losing classifications, then enforces mode-specific limits. Keep
that distinction intact when changing either validation path.

Hints also use the grid's determinability checks. They find provable cells and
temporarily hide candidate regions until a supporting subset remains. Hint-owned
visibility is distinct from visibility explicitly chosen by the player and must
be restored without overwriting the player's choice.

## Ownership and threading

- `GameState` owns the active `Grid` and the rule lists for all six game modes.
- `Grid` owns regions in `std::list` containers; cause and inspector structures
  hold pointers into those containers. When deleting or replacing a region,
  update the existing bookkeeping rather than leaving a dangling cause.
- `SaveObjectMap` and `SaveObjectList` take ownership of objects passed to
  `add_item()` and delete their children.
- SDL event handling and rendering belong on the main thread. Level generation,
  server requests, and robot evaluation use background threads and the existing
  SDL mutexes for shared progress.
- `ControlSocket::poll()` is non-blocking and runs in the main loop. It is built
  only when `--enable-control-socket` defines `ENABLE_CONTROL_SOCKET`.

## Persistence and compatibility

The Steam build stores `bombe.save`; a `--disable-steam` development build uses
`test_bombe.save`. Both live beneath the directory returned by
`SDL_GetPrefPath("CharlieBrej", "Bombe")`. Saves are `SaveObject` text compressed
with Zstandard, and the game keeps ten rotating backup files beside the primary
save.

`GameState::game_version` controls save compatibility, while
`rule_check_version` decides whether saved rules must be revalidated. When a save
field is added, provide a default for older saves and guard optional reads with
`has_key()`. When rule semantics change, consider whether `rule_check_version`
must also change.

Boards use the compact strings parsed by `Grid::Load()` and emitted by the
geometry-specific `to_string()` methods. Prefer those APIs over hand-editing the
format. `levels.data` is a compressed generated artefact; running
`GridGenerator` can be expensive and rewrites it, so do so deliberately and
review the resulting data change separately.

## Runtime assets and text

The client expects to run with the repository root as its working directory.
Important relative-path inputs are `levels.data`, `lang.json`, `texture.png`,
`icon.png`, the font files, `tutorial/`, `snd/`, and the separately supplied
`music.ogg`.

Local marker files also affect startup: `FULL` selects the full game,
`PLAYTEST` selects the playtest build, and no marker selects the demo.

Interface text is translated through `GameState::translate()` and keys in
`lang.json`. Player-written rule comments are user content and must not be sent
through translation. A UI change which adds text should update every language
entry or deliberately use an existing key.

## Where to make a change

- Board topology, clues, region semantics, rule matching, or legality: `Grid.*`.
- Rendering, input, rule construction UI, themes, hints, or progression:
  `GameState.*`.
- Save schema: `GameState::save()`, its constructor/load path, and possibly the
  version constants.
- Built-in level generation: `grid_generator.cpp`, followed by a deliberate
  `levels.data` regeneration.
- Translation: `lang.json` and the corresponding call site in `GameState.cpp`.
- Local development protocol: both `ControlSocket.cpp` and `BombeControl.cpp`,
  plus the command documentation in `README.md`.
- Online protocol: both the client networking in `GameState.cpp` and
  `BombeServer.cpp`.

Add core regression coverage to the existing `BombeTest.cpp` executable and run
`make check`. The project intentionally keeps these tests together instead of
creating a separate test program for every issue.

## Build-system convention

`configure` and `Makefile.in` are generated and ignored. Edit `configure.ac` or
`Makefile.am`, then run:

```sh
autoreconf --install --force
./configure --disable-steam
make
make check
```

Steam support is enabled by default and needs the Steamworks headers and
`libsteam_api`. The local control socket is disabled by default and is available
only on Unix hosts.
