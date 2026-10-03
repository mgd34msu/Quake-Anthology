# Quake Anthology

Quake Anthology brings Quake, QuakeWorld, Quake II and Quake III Arena into one application. Choose a game and campaign, or combine map content, movement, characters and independent mods from different games.

This native C version is not ready to play yet. Game data is not included; use the files from your own game installations.

## Build and launch

You need a C17 compiler, CMake 3.20 or newer, Git and pkg-config. Install the development libraries for SDL2 2.0.18+, libcurl 7.85+, zlib, PNG, JPEG, GIF, ICU, FreeType, libffi, Vorbis, Ogg and Theora. The build downloads its pinned native runtime dependencies, so the first build needs internet access.

```sh
git clone https://github.com/mgd34msu/Quake-Anthology.git
cd Quake-Anthology
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target quake-anthology --parallel
```

Open the startup menu:

```sh
./build/quake-anthology
```

The application searches for installed games beside the executable, in surrounding directories, and in Steam libraries. Discovery uses the executable's location, so launching from another working directory does not change where it looks.

`--user-content-root PATH` selects a different directory for writable settings and user content. Keep the same user directory between sessions to retain your files.

For a system or user installation, choose an installation prefix when configuring, then run `cmake --install build`. Keep the installed native runtime files alongside the application’s installation; original native mods use them.

## Add your games

Keep your games in their existing installation directories. You can place the executable, or the folder containing it, alongside your games. For example:

```text
SteamLibrary/steamapps/common/
    Quake/
        id1/
    Quake II/
        baseq2/
    Quake III Arena/
        baseq3/
    Quake Anthology/
        quake-anthology
```

The outer installation folder names can differ. Quake Anthology recognizes the game data inside them. Expansions need their base game's data too.

Check which games were found:

```sh
./build/quake-anthology --list-content
```

If a game is elsewhere, add its installation directory for the current launch:

```sh
./build/quake-anthology --game-path "/path/to/Quake II" --list-content
```

To remember that directory for later launches:

```sh
./build/quake-anthology --save-game-path "/path/to/Quake II"
```

Repeat either option for multiple installations. `--save-game-path` updates `install-locations.json` in your user settings directory and exits without starting a game. The command prints the configuration file's location. You can also edit that file directly, using absolute paths:

```json
{
    "schema": "quake-anthology/install-locations",
    "version": 1,
    "paths": [
        "/path/to/Quake",
        "/another/drive/Quake II",
        "/path/to/Quake III Arena"
    ]
}
```

Use the product IDs from `--list-content` when selecting a game:

| Game or campaign | Product ID |
| --- | --- |
| Quake | `q1-classic-id1` |
| Scourge of Armagon | `q1-classic-hipnotic` |
| Dissolution of Eternity | `q1-classic-rogue` |
| Quake rerelease | `q1-rerelease-id1` |
| Quake II | `q2-classic-baseq2` |
| The Reckoning | `q2-classic-xatrix` |
| Ground Zero | `q2-classic-rogue` |
| Quake II rerelease | `q2-rerelease-baseq2` |
| Quake III Arena | `q3-baseq3` |
| Team Arena | `q3-missionpack` |

Rerelease campaigns and discovered mods have their own entries. `--content-root PATH` is also available to select a directory to search.

## Start playing

Launch a game directly, or omit `--game` to choose through the startup menu:

```sh
./build/quake-anthology --game q1-classic-id1 --map start
./build/quake-anthology --game q2-classic-baseq2 --map base1
./build/quake-anthology --game q3-baseq3 --map q3dm1
```

To use Quake III movement while playing Quake II:

```sh
./build/quake-anthology --game q2-classic-baseq2 --movement q3
```

`--movement` accepts `q1`, `qw`, `q2`, `q3` or a discovered product ID. `--character` accepts `q1`, `q2`, `q3` or a product ID. `--map-game PRODUCT` selects map content independently. Add `--mod PRODUCT/COMPONENT` for an independent mod component; repeat the option to combine components.

The game library's **Map gameplay** choice offers **Anthology**, **Original**, and available installed game types under their authored titles. Anthology is the default. Original uses the gameplay module from that game's installation; the module must be present and supported. You can also request it with `--original`:

```sh
./build/quake-anthology --game q1-classic-id1 --map start --original
```

For an authored game type, use `--game-type PRODUCT/COMPONENT` instead of `--original`. Its key combines the discovered product ID with the component's `id` in that installation's `gameplay-mods.json`, declared with `purpose: "game-type"`. This selects the map's entity program and retains the preset's native player roles. Replace the placeholders with your installed declaration's key:

```sh
./build/quake-anthology --game PRODUCT --game-type PRODUCT/COMPONENT
```

## Display, controls and configuration

Use the in-game options for input bindings and other preferences. Startup options include:

| Option | Purpose |
| --- | --- |
| `--renderer gl` | OpenGL output |
| `--renderer cpu` | Software output |
| `--width 1920 --height 1080` | Window size |
| `--seats 2` | Two local player seats; the range is 1–4 |
| `--gamma 1.2` | Output brightness; the range is 0.5–3 |
| `--no-audio` | Disable audio delivery |

Quake-style startup commands follow the options, for example `+set name Player`. Run `./build/quake-anthology --help` for the complete option list.

## Multiplayer

Anthology sessions use `unified-1` by default. To run a dedicated server with a stdin console:

```sh
./build/quake-anthology --game q2-classic-baseq2 --map base1 --dedicated --host 0.0.0.0 --port 27910
```

Connect to an Anthology server with `--connect ADDRESS --port PORT`. For an original game server, explicitly choose its protocol, for example:

```sh
./build/quake-anthology --game q2-classic-baseq2 --connect 192.0.2.10 --port 27910 --protocol q2-34
```

Protocol names include `nq15`, `fitz666`, `rmq999`, `qw28`, `qw29`, `q2-34`, `r1q2-35`, `q2pro-36`, `q2repro-1038`, `q2kex-2023`, `q3-68` and `unified-1`. Match the server's game, protocol and required content.

## Troubleshooting

- **Game missing from the menu:** run `--game-path "/path/to/game installation" --list-content`. If the game is found, remember its location with `--save-game-path`. Check that the installation contains the complete game archives.
- **Missing expansion assets:** install the base game and the expansion's complete data directory.
- **Display problems:** try `--renderer cpu` and a smaller window size.
- **Menu font missing:** pass `--font-directory /path/to/fonts --font Font.ttf`. The default font is `DejaVuSans.ttf` in `/usr/share/fonts/truetype/dejavu`.
- **Server connection problems:** check the address, port, protocol and required game or mod data.

When [reporting a problem](https://github.com/mgd34msu/Quake-Anthology/issues), include your operating system, game/product ID, launch command and error output.
