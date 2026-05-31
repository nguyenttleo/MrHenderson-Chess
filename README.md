# MrHenderson

MrHenderson is a C++ UCI chess engine. He likes naps and tuna fish.

## Features

- Legal move generation with castling, promotion, and en passant
- FEN and UCI `position` parsing
- Iterative deepening alpha-beta search
- Quiescence search for tactical stability
- Transposition table with configurable hash size
- Capture, killer, history, and promotion move ordering
- Simple material, piece activity, pawn-structure, and king-safety evaluation

## Build your own Mr. Henderson

With CMake:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

With MinGW Make:

```powershell
mingw32-make
```

Or directly with MinGW:

```powershell
g++ -std=c++17 -O3 -DNDEBUG src/main.cpp -o MrHenderson.exe
```

## Quick UCI Test

```text
uci
isready
position startpos moves e2e4 e7e5
go depth 5
quit
```

The engine should answer with `uciok`, `readyok`, search `info` lines, and a
final `bestmove`.

## Lichess Henderson

MrHenderson is a UCI engine. You can connect him with
[`lichess-bot`](https://github.com/lichess-bot-devs/lichess-bot). In your
`lichess-bot` config, point the engine path at the compiled executable.

For the verified MinGW Make or direct `g++` build:

```yaml
engine:
  dir: C:\path\to\Chess-Engine
  name: MrHenderson.exe
  protocol: uci
```

If you build with CMake, use the CMake output directory instead, usually
`C:\path\to\Chess-Engine\build`.

Example `lichess-bot` setup:

Create a Lichess OAuth token with the `bot:play` scope. Keep it out of source
control by setting it as an environment variable, then run:

```powershell
cd C:\path\to\lichess-bot
$env:LICHESS_BOT_TOKEN = "your_token_here"
python lichess-bot.py --config config.yml -u
python lichess-bot.py --config config.yml -v
```

The `-u` command upgrades the account to a bot account. This is irreversible
and only works for an account that has not already played games.

Use it only with a bot account or in contexts where engine assistance is
allowed by the site rules.
