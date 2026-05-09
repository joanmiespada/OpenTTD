# CLAUDE.md - OpenTTD Codebase Guide

## Project Overview

OpenTTD is an open-source transport simulation game based on Transport Tycoon Deluxe. Written in C++20, it supports Windows, macOS, Linux, and Emscripten (WebAssembly). The project is licensed under GPL v2.

**Version**: 16.0
**C++ Standard**: C++20 (required, no extensions)
**Build System**: CMake 3.17+

## Build Instructions

```bash
mkdir build && cd build
cmake ..
cmake --build . -j $(nproc)
```

### Common build variants

```bash
# Release build
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Dedicated server (no GUI)
cmake .. -DOPTION_DEDICATED=ON

# Tools only (for cross-compilation)
cmake .. -DOPTION_TOOLS_ONLY=ON
```

### Running tests

```bash
cd build
ctest -j $(nproc) --timeout 120
```

Regression tests (AI/GameScript):
```bash
cmake --build . --target regression
```

## Source Code Layout

```
src/                    Main source code
  ai/                   AI subsystem (Squirrel-based)
  blitter/              Graphics rendering backends (8bpp, 32bpp, SSE variants)
  core/                 Core utilities (pools, math, strings, data structures)
  fontcache/            Font caching and rendering
  game/                 Game script API
  lang/                 Translation files
  linkgraph/            Cargo routing algorithm
  misc/                 Miscellaneous utilities
  music/                Music drivers
  network/              Multiplayer networking (client/server, coordinator, crypto)
  newgrf/               NewGRF content format support
  os/                   OS-specific code (windows/, macosx/, unix/, emscripten/)
  pathfinder/           Pathfinding (YAPF)
  saveload/             Save/load system with version compatibility
    compat/             Backward compatibility for old savegames
  script/               Squirrel script engine
    api/                Script API definitions
  sound/                Sound drivers
  spriteloader/         Sprite loading
  strgen/               String generation tool for translations
  settingsgen/          Settings generator tool
  table/                Data tables and constants
    settings/           Game settings definitions
  tests/                Unit tests (Catch2)
  timer/                Timer subsystem (calendar, economy, realtime)
  video/                Video drivers (SDL2, OpenGL, Win32, Cocoa)
  widgets/              GUI widget definitions
```

### File naming conventions per feature

Each feature typically uses this file pattern:
- `*_type.h` - Type definitions, enums, constants
- `*_base.h` - Base data structures (pool items)
- `*_func.h` - Function declarations
- `*_cmd.h` - Command declarations with traits
- `*_cmd.cpp` - Command implementations (game logic)
- `*_gui.cpp` - GUI/window code
- `*.hpp` - Template implementations

### Key entry points

- `src/openttd.cpp` - Main entry point (`openttd_main()`), game loop (`GameLoop()`, `StateGameLoop()`)
- `src/os/unix/unix_main.cpp` - Unix platform entry
- `src/os/macosx/osx_main.cpp` - macOS platform entry

## Coding Style

Defined in detail in `CODINGSTYLE.md`. Key rules:

### Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Functions | CamelCase | `ThisIsAFunction()` |
| Variables | lowercase_with_underscores | `my_variable` |
| Global variables | Prefixed with `_` | `_global_variable` |
| Class members | Access via `this->` | `this->member` |
| Unscoped enumerators | ALL_CAPS | `DIAGDIR_NE` |
| Scoped enumerators | CamelCase | `VehState::Hidden` |
| Classes/Structs | CamelCase | `Vehicle`, `Station` |

### Formatting

- **Indentation**: Tabs only (from line start)
- **Function braces**: Opening brace on new line (Allman style)
- **Control flow braces**: Same line as keyword
- **Pointers**: Symbol next to name: `Vehicle *v`, not `Vehicle* v`
- **Includes end with**: `#include "safeguards.h"` at end of .cpp files

### Comments

- Multi-line: `/* comment */`
- End-of-line: `// comment`
- Doxygen: `/** ... */` with `@file`, `@param`, `@return` tags
- Every file must have `@file` doxygen tag

### Commit message format

```
<keyword>( #<issue>|<commit>)?: [<component>] <details>
```

Keywords (player-facing): `Feature`, `Add`, `Change`, `Fix`, `Remove`, `Revert`, `Doc`, `Update`
Keywords (developer-facing): `Codechange`, `Cleanup`, `Codefix`
Components: `[YAPF]`, `[Network]`, `[NewGRF]`, `[Script]`, etc.

Example: `Fix #5926: [YAPF] Infinite loop in pathfinder`

## Architecture Patterns

### Command pattern

All gameplay actions use a replicated command system for network safety:

```cpp
// Declaration in *_cmd.h
CommandCost CmdBuildDepot(DoCommandFlags flags, TileIndex tile, ...);
DEF_CMD_TRAIT(Commands::BuildDepot, CmdBuildDepot, CommandFlags({...}), CommandType::Construction)

// Execution
Command<Commands::BuildDepot>::Do(flags, tile, args...);    // Direct
Command<Commands::BuildDepot>::Post(err_msg, tile, args...); // From UI
```

Logic goes in `*_cmd.cpp`, GUI in `*_gui.cpp`. Commands are validated server-side and replicated across the network.

### Pool-based entity management

Game objects (vehicles, stations, depots, etc.) use pool allocation:

```cpp
struct Depot : DepotPool::PoolItem<&_depot_pool> {
    TileIndex xy;
};
```

### Type-safe ID wrappers

Strong typing for entity IDs:
```cpp
using CompanyID = PoolID<uint8_t, struct CompanyIDTag, 0xF, 0xFF>;
```

### Error handling

- **No exceptions** - uses return codes
- `CommandCost` with `Failed()`/`Succeeded()` for command results
- `UserError()` / `FatalError()` for unrecoverable errors (both `[[noreturn]]`)
- `NOT_REACHED()` for impossible default cases

### Driver plugin system

Video, sound, and music drivers are pluggable via factory pattern. Blitters provide different rendering implementations (8bpp, 32bpp with SSE optimizations).

## Testing

### Unit tests (Catch2)

Located in `src/tests/`. Test files cover:
- `bitmath_func.cpp`, `math_func.cpp` - Math utilities
- `string_func.cpp`, `string_builder.cpp`, `utf8.cpp` - String handling
- `test_network_crypto.cpp` - Network authentication
- `test_window_desc.cpp` - Window descriptor validation
- `flatset_type.cpp`, `tilearea.cpp` - Data structures

Mock objects available: `mock_environment.h`, `mock_fontcache.h`, `mock_spritecache.h`

### Regression tests

Located in `regression/`. Squirrel scripts that run OpenTTD headless for 30000 ticks and compare output against expected `result.txt` files.

## Dependencies

**Required**: Threads
**Encouraged**: zlib, liblzma, libpng, libcurl (Linux), SDL2 (Linux), Breakpad
**Optional**: liblzo2, freetype, fontconfig, harfbuzz, libicu, opusfile
**Bundled (3rdparty)**: Squirrel, fmt, nlohmann JSON, Catch2, monocypher

Dependencies are managed via vcpkg on Windows (`vcpkg.json`) and system packages on Linux.

## Key Documentation

- `CODINGSTYLE.md` - Comprehensive coding standards
- `CONTRIBUTING.md` - Contribution guidelines and project goals
- `COMPILING.md` - Platform-specific build instructions
- `docs/savegame_format.md` - Save game binary format
- `docs/linkgraph.md` - Cargo routing algorithm
- `docs/multiplayer.md` - Multiplayer architecture
- `docs/game_coordinator.md` - Network game coordinator

## Important Notes

- In-source builds are forbidden; always use a separate build directory.
- The project prefers backward compatibility for savegames (loads all prior versions).
- NewGRF is the primary extensibility mechanism for content.
- The Squirrel scripting engine powers both AI and GameScript systems.
- `safeguards.h` must be the last include in every `.cpp` file.
