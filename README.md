# Ridgeline

Ridgeline is a modular game patching framework. One launcher (`Ridgeline.exe`) plus one DLL per supported game; the launcher injects the appropriate DLL into the target game process to apply runtime patches.

Currently supported games:

| Game            | Developer        | Publisher        | Module ID        |
| --------------- | ---------------- | ---------------- | ---------------- |
| Alcatraz (2009) | Silden | City Interactive | `alcatraz` |
| US Most Wanted (2002) | Fun Labs | Activision Value | `us_mostwanted` |

Ridgeline is a fork of Alpine Faction (https://github.com/gooberRF/alpinefaction).

<img src="https://raw.githubusercontent.com/GooberRF/ridgeline/refs/heads/master/docs/ridgeline.png">

## Using Ridgeline

1. Run `Ridgeline.exe`.
2. Pick a game from the list on the left.
3. Set the `Game executable` path to your installed copy of that game and adjust per-game settings.
4. Click **Launch**.

Settings are stored in `ridgeline.ini` next to `Ridgeline.exe`. Each game has its own `[module.<id>]` section; per-launcher settings live under `[ridgeline]`.

## Documentation

- [docs/BUILDING.md](docs/BUILDING.md) — how to build Ridgeline from source.
- [docs/ADDING-A-MODULE.md](docs/ADDING-A-MODULE.md) — the three-step recipe for adding a new game.
- [docs/LAYOUT.md](docs/LAYOUT.md) — what each top-level directory contains.

## License

Ridgeline's source code is licensed under Mozilla Public License 2.0. See [LICENSE.txt](LICENSE.txt). For third-party dependency licensing, see [licensing-info.txt](licensing-info.txt).

## Acknowledgments

- The foundation on which Ridgeline relies is derived from [Dash Faction](https://github.com/rafalh/dashfaction) by [Rafalh (Rafał Harabień)](https://github.com/rafalh).
- Ridgeline also contains code derived from [Alpine Faction](https://github.com/GooberRF/alpinefaction) by [Chris "Goober" Parsons](https://github.com/GooberRF), itself a fork of Dash Faction.
