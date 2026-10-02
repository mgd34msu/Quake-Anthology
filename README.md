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

After arranging your game data as described below, open the startup menu:

```sh
./build/quake-anthology --content-root /path/to/qfiles --user-content-root /path/to/anthology-user
```

`--content-root` points to installed game data. `--user-content-root` selects writable user content. Keep the same user directory between sessions to retain your files. Without an explicit content root, the application looks in `../qfiles` relative to the working directory.

For a system or user installation, choose an installation prefix when configuring, then run `cmake --install build`. Keep the installed native runtime files alongside the application’s installation; original native mods use them.

## Add your games

Copy each game's data directory contents into the matching location under your content root. Keep PAK and PK3 archives intact, and include loose files and additional archives from the installation.

| Game or campaign | Directory under your content root | Product ID |
| --- | --- | --- |
| Quake | `q1/id1/` | `q1-classic-id1` |
| Scourge of Armagon | `q1/hipnotic/` | `q1-classic-hipnotic` |
| Dissolution of Eternity | `q1/rogue/` | `q1-classic-rogue` |
| Quake rerelease | `q1/rerelease/id1/` | `q1-rerelease-id1` |
| Quake II | `q2/baseq2/` | `q2-classic-baseq2` |
| The Reckoning | `q2/xatrix/` | `q2-classic-xatrix` |
| Ground Zero | `q2/rogue/` | `q2-classic-rogue` |
| Quake II rerelease | `q2/rerelease/baseq2/` | `q2-rerelease-baseq2` |
| Quake III Arena | `q3a/baseq3/` | `q3-baseq3` |
| Team Arena | `q3a/missionpack/` | `q3-missionpack` |

For example, a classic Quake installation should contain `q1/id1/pak0.pak` and `q1/id1/pak1.pak`. Quake III's archives belong in `q3a/baseq3/`, including its installed updates. Expansions also need their base game's data.

List the products discovered in your installation:

```sh
./build/quake-anthology --content-root /path/to/qfiles --list-content
```

Use the product IDs from that output when selecting a game. Rerelease campaigns and discovered mods have their own entries.

## Start playing

Launch a game directly, or omit `--game` to choose through the startup menu:

```sh
./build/quake-anthology --content-root /path/to/qfiles --game q1-classic-id1 --map start
./build/quake-anthology --content-root /path/to/qfiles --game q2-classic-baseq2 --map base1
./build/quake-anthology --content-root /path/to/qfiles --game q3-baseq3 --map q3dm1
```

To use Quake III movement while playing Quake II:

```sh
./build/quake-anthology --content-root /path/to/qfiles --game q2-classic-baseq2 --movement q3
```

`--movement` accepts `q1`, `qw`, `q2`, `q3` or a discovered product ID. `--character` accepts `q1`, `q2`, `q3` or a product ID. `--map-game PRODUCT` selects map content independently. Add `--mod PRODUCT/COMPONENT` for an independent mod component; repeat the option to combine components.

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
./build/quake-anthology --content-root /path/to/qfiles --game q2-classic-baseq2 --map base1 --dedicated --host 0.0.0.0 --port 27910
```

Connect to an Anthology server with `--connect ADDRESS --port PORT`. For an original game server, explicitly choose its protocol, for example:

```sh
./build/quake-anthology --content-root /path/to/qfiles --game q2-classic-baseq2 --connect 192.0.2.10 --port 27910 --protocol q2-34
```

Protocol names include `nq15`, `fitz666`, `rmq999`, `qw28`, `qw29`, `q2-34`, `r1q2-35`, `q2pro-36`, `q2repro-1038`, `q2kex-2023`, `q3-68` and `unified-1`. Match the server's game, protocol and required content.

## Troubleshooting

- **Game missing from the menu:** check the directory layout and run `--list-content`. The root should contain `q1`, `q2` or `q3a`, rather than pointing directly at `id1` or `baseq2`.
- **Missing expansion assets:** install the base game and the expansion's complete data directory.
- **Display problems:** try `--renderer cpu` and a smaller window size.
- **Menu font missing:** pass `--font-directory /path/to/fonts --font Font.ttf`. The default font is `DejaVuSans.ttf` in `/usr/share/fonts/truetype/dejavu`.
- **Server connection problems:** check the address, port, protocol and required game or mod data.

When [reporting a problem](https://github.com/mgd34msu/Quake-Anthology/issues), include your operating system, game/product ID, launch command and error output.
