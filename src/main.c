/*
 * Port of src/main.ts (in progress). --version/--help match the
 * TypeScript output exactly; application execution lands with
 * src/app/bootstrap/options.ts and its dependencies (see
 * docs/manifest.tsv). Unported commands fail explicitly.
 * GPL-2.0-or-later.
 */
#include <stdio.h>
#include <string.h>

#include "qa/version.h"

/* applicationHelp from src/app/bootstrap/options.ts; only the Usage line
 * changes (the TypeScript names its Bun entry point). */
static const char application_help[] =
    "Quake\n"
    "\n"
    "Usage: quake-anthology [options]\n"
    "\n"
    "  +command [arguments]       Run source startup command (use '+bind x \"+attack\"' as one shell argument)\n"
    "  --menu                     Open the startup menu (default without launch selections)\n"
    "  --preset q2-q1-q3|q1-q2     Select an initial mixed-game profile\n"
    "  --content-root PATH        Game data root (default: search own folder, its parent, then Steam)\n"
    "  --steam                    Look only in the default Steam library locations\n"
    "  --user-content-root PATH   Writable user content root (default ~/.local/share/quake-typescript/content)\n"
    "  --game PRODUCT             Installed catalog product, e.g. q2-classic-baseq2\n"
    "  --map-game PRODUCT         Select map content independently from the game module\n"
    "  --map NAME                 Map name or maps/path.bsp\n"
    "  --progs MOUNTED_PATH       Validated mounted QuakeC .dat artifact\n"
    "  --q2-game MOUNTED_PATH     Explicit Quake II game DLL (classic i386 / rerelease x64)\n"
    "  --weapon-behavior PRODUCT/ID  Overlay a declared projectile trajectory\n"
    "  --mod PRODUCT/ID           Enable a declared mod component (repeat to combine)\n"
    "  --movement q1|q2|q3|qw|PRODUCT  Movement family or exact installed product\n"
    "  --character q1|q2|q3       Player character provider\n"
    "  --model NAME               Character model (e.g. sarge or male)\n"
    "  --renderer cpu|gl          Renderer (default gl)\n"
    "  --render-worker 0|1       Execute rendering on a worker (default 0)\n"
    "  --gamma N                 Display gamma, 0.5 through 3 (default 1; higher is brighter)\n"
    "  --width N --height N       Window dimensions (default 960 by 600)\n"
    "  --seats N                  Local seats, 1 through 4\n"
    "  --mode singleplayer|coop|deathmatch\n"
    "  --server-profile PATH      Load validated shared server settings from JSON\n"
    "  --rules standard|ctf|lmctf|tag|deathball|horde Source match rules\n"
    "  --skill 0|1|2|3            Quake I/II gameplay difficulty\n"
    "  --bot-skill 1|2|3|4|5      Quake III bot difficulty (default 2)\n"
    "  --dedicated                Run without a window or local seats\n"
    "  --listen-unified PORT      Host the selected mixed-game recipe\n"
    "  --connect-unified ADDRESS  Join a mixed-game server\n"
    "  --listen PORT              Host the selected game's native source protocol\n"
    "  --q1-protocol 15|666|999    NetQuake host protocol (default 15; RMQ flags 130)\n"
    "  --listen-q2 PORT           Host the native Quake II source protocol\n"
    "  --bind ADDRESS             Server IP (default 0.0.0.0)\n"
    "  --connect-q1 ADDRESS       Join a native Quake server (id1, protocols 15/666/999)\n"
    "  --connect-qw ADDRESS       Join a base QuakeWorld protocol 28 server\n"
    "  --connect-q3 ADDRESS       Join a baseq3 protocol 68 server (sv_pure 0)\n"
    "  --q2-protocol 34|35[:1904|1905]|36[:revision]|4038|1038|2023 Q2 client/server protocol (35 defaults to 1904; 36 to 1026)\n"
    "  --connect-q2 ADDRESS       Join a native Quake II server\n"
    "  --ipx-dosbox HOST[:PORT]   Use DOSBox IPXNET relay (default relay port 213)\n"
    "  --ipx-native               Require a host AF_IPX socket capability\n"
    "  --seed N                   Gameplay random seed\n"
    "  --frames N                 Close after N simulation steps\n"
    "  --hidden                   Start a hidden native window\n"
    "  --list-content             Show installed games and expansions\n"
    "  --version                  Show the application version\n"
    "  weapon-behavior --help     Inspect and author mounted source behavior declarations\n"
    "  --help                     Show these options\n";

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "--splash-child") == 0) {
    fprintf(stderr, "splash child is not yet ported (src/app/bootstrap/splash.ts)\n");
    return 1;
  }
  if (argc > 1 && strcmp(argv[1], "weapon-behavior") == 0) {
    fprintf(stderr, "weapon-behavior tool is not yet ported "
                    "(src/app/bootstrap/weapon-behavior-tool.ts)\n");
    return 1;
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      fputs(application_help, stdout);
      return 0;
    }
    if (strcmp(argv[i], "--version") == 0) {
      printf("Quake Anthology %s\n", qa_version);
      return 0;
    }
  }
  fprintf(stderr, "application execution is not yet ported "
                  "(src/app/bootstrap/options.ts and dependencies; "
                  "see docs/manifest.tsv)\n");
  return 1;
}
