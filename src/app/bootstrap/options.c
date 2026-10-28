/* Port of src/app/bootstrap/options.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/options.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts/paths.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/profile.h"
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
static void set_owned(char **slot, const char *text) { free(*slot); *slot = dup_text(text); }
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
const char *const qa_app_application_help =
    "Quake\n\nUsage: bun run src/main.ts [options]\n\n"
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
void qa_app_options_destroy(qa_app_options *o) {
  if (o == NULL) return;
  free(o->network_transport_relay);
  for (size_t i = 0; i < o->startup_command_count; i++) free(o->startup_commands[i]);
  free(o->startup_commands);
  free(o->remote_content.directory);
  free(o->server_profile_path); free(o->corpus_root); free(o->user_content_root);
  qa_nightdive_addon_roots_free(o->addon_content_roots, o->addon_content_root_count);
  free(o->product); free(o->map_product); free(o->map); free(o->quakec_program);
  free(o->q2_game_library); free(o->weapon_behavior_product); free(o->weapon_behavior_id);
  for (size_t i = 0; i < o->mod_count; i++) qa_mod_selection_free(&o->mods[i]);
  free(o->mods); free(o->movement_product); free(o->character_model); free(o->character_skin);
  free(o->network_host); free(o->network_remote);
  memset(o, 0, sizeof *o);
}
void qa_app_application_command_destroy(qa_app_application_command *command) {
  if (command == NULL) return;
  if (command->weapon_behavior_destroy != NULL && command->weapon_behavior_command != NULL)
    command->weapon_behavior_destroy(command->weapon_behavior_command);
  free(command->corpus_root);
  qa_app_options_destroy(&command->options);
  memset(command, 0, sizeof *command);
}
qa_q2_protocol_identity qa_app_live_q2_protocol(const qa_app_options *options, bool rerelease) {
  if (options->has_q2_protocol) return options->q2_protocol;
  qa_q2_protocol_identity out = {rerelease ? "q2-kex" : "q2-classic", rerelease ? 2023 : 34, 0, 0};
  return out;
}
static double integer_value(const char *value, const char *label, double minimum, double maximum) {
  if (value[0] == '\0') qa_range_error("%s must be an integer", label);
  for (const char *p = value; *p != '\0'; p++)
    if (!isdigit((unsigned char)*p)) qa_range_error("%s must be an integer", label);
  double number = strtod(value, NULL);
  if (!(number >= minimum && number <= maximum) || number > 9007199254740991.0)
    qa_range_error("%s must be between %lld and %lld", label, (long long)minimum, (long long)maximum);
  return number;
}
static qa_game_family family_value(const char *value) {
  qa_game_family family;
  if (!qa_game_family_parse(value, &family)) qa_range_error("Unknown game family: %s", value);
  return family;
}
char *qa_app_map_resource_path(const char *name) {
  size_t len = strlen(name);
  char *normalized = malloc(len + 1);
  if (normalized == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < len; i++) normalized[i] = name[i] == '\\' ? '/' : name[i];
  normalized[len] = '\0';
  bool ok = len > 0;
  const char *body = normalized;
  if (strncmp(body, "maps/", 5) == 0) body += 5;
  size_t body_len = strlen(body);
  bool bsp = body_len >= 4 && strcmp(body + body_len - 4, ".bsp") == 0;
  size_t stem = bsp ? body_len - 4 : body_len;
  if (stem == 0) ok = false;
  for (size_t i = 0; ok && i < stem; i++) {
    char ch = body[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' || ch == '-')) ok = false;
  }
  /* ".." path segment check (TS split("/").includes("..")) */
  for (const char *p = normalized;;) {
    const char *slash = strchr(p, '/');
    size_t seg = slash != NULL ? (size_t)(slash - p) : strlen(p);
    if (seg == 2 && p[0] == '.' && p[1] == '.') ok = false;
    if (slash == NULL) break;
    p = slash + 1;
  }
  if (!ok) { free(normalized); qa_range_error("Invalid map name: %s", name); }
  bool has_prefix = strncmp(normalized, "maps/", 5) == 0;
  bool has_bsp = len >= 4 && strcmp(normalized + len - 4, ".bsp") == 0;
  char *out = malloc(len + 10);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, len + 10, "%s%s%s", has_prefix ? "" : "maps/", normalized, has_bsp ? "" : ".bsp");
  free(normalized);
  return out;
}
static qa_q2_protocol_identity parse_q2_protocol(const char *value) {
  qa_q2_protocol_identity out = {NULL, 0, 0, 0};
  if (is(value, "34")) { out.kind = "q2-classic"; out.version = 34; return out; }
  if (is(value, "35") || is(value, "35:1904")) { out.kind = "q2-r1q2"; out.version = 35; out.revision = 1904; return out; }
  if (is(value, "35:1905")) { out.kind = "q2-r1q2"; out.version = 35; out.revision = 1905; return out; }
  if (is(value, "4038")) { out.kind = "q2-private-classic"; out.version = 4038; return out; }
  if (is(value, "2023")) { out.kind = "q2-kex"; out.version = 2023; return out; }
  if (is(value, "1038")) { out.kind = "q2-rerelease"; out.version = 1038; return out; }
  long revision = is(value, "36") ? 1026 : strncmp(value, "36:", 3) == 0 ? strtol(value + 3, NULL, 10) : 0;
  switch (revision) {
    case 1015: case 1017: case 1018: case 1019: case 1020: case 1021: case 1022: case 1023: case 1024: case 1025: case 1026:
      out.kind = "q2-q2pro"; out.version = 36; out.revision = revision; return out;
    default:
      qa_range_error("--q2-protocol requires 34, 35:1904/1905, 36:1015/1017..1026, 4038, 1038 or 2023");
  }
}
/* Internal mirror of startup-commands.ts readStartupCommand /
 * startupRequestsWorld (other slice) on landed text helpers. */
static char *read_startup_command(const char *const *argv, size_t argc, size_t index, size_t *out_end) {
  const char *first = argv[index];
  if (first[0] != '+' || first[1] == '\0') qa_range_error("Expected +command");
  size_t capacity = strlen(first) + 1, end = index;
  for (size_t i = index + 1; i < argc; i++) {
    if (argv[i][0] == '+' || strncmp(argv[i], "--", 2) == 0) break;
    capacity += strlen(argv[i]) + 4; end = i;
  }
  char *text = malloc(capacity + 1);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, capacity + 1, "%s", first + 1);
  for (size_t i = index + 1; i <= end; i++) {
    const char *value = argv[i];
    char *checked = qa_source_command_text(value); free(checked);
    if (strchr(value, '"') != NULL || strchr(value, '\r') != NULL || strchr(value, '\n') != NULL) {
      free(text);
      qa_range_error("Startup argument cannot contain quotes or line breaks; use a quoted +command batch or exec a cfg file.");
    }
    strncat(text, " \"", capacity - strlen(text));
    strncat(text, value, capacity - strlen(text));
    strncat(text, "\"", capacity - strlen(text));
  }
  char *checked = qa_source_command_text(text); free(checked);
  if (strchr(text, '\r') != NULL || strchr(text, '\n') != NULL
      || qa_command_separator_offset(text, QA_COMMAND_DIALECT_Q3) < strlen(text)) {
    free(text);
    qa_range_error("Use a separate +command for each startup command.");
  }
  qa_command_tokens tokens = {0};
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens) || tokens.argc == 0) {
    qa_command_tokens_destroy(&tokens); free(text);
    qa_range_error("Expected +command");
  }
  char *head = qa_ascii_fold(tokens.argv[0]);
  if (is(head, "connect")) {
    free(head); qa_command_tokens_destroy(&tokens); free(text);
    qa_range_error("Ordered +connect startup is unsupported; use --connect-q1, --connect-qw, --connect-q2 or --connect-q3.");
  }
  if (is(head, "set") && tokens.argc > 1) {
    char *second = qa_ascii_fold(tokens.argv[1]);
    bool fs = is(second, "game") || is(second, "fs_game") || is(second, "basedir") || is(second, "cddir")
        || is(second, "fs_basepath") || is(second, "fs_homepath") || is(second, "fs_cdpath");
    free(second);
    if (fs) {
      free(head); qa_command_tokens_destroy(&tokens); free(text);
      qa_range_error("Filesystem +set selection is unsupported; use --game with an installed catalog product and --content-root or --user-content-root.");
    }
  }
  free(head);
  qa_command_tokens_destroy(&tokens);
  *out_end = end;
  return text;
}
static bool startup_requests_world(char *const *lines, size_t count) {
  for (size_t i = 0; i < count; i++) {
    qa_command_tokens tokens = {0};
    if (!qa_tokenize_command(lines[i], QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens) || tokens.argc == 0) {
      qa_command_tokens_destroy(&tokens); continue;
    }
    char *head = qa_ascii_fold(tokens.argv[0]);
    bool world = is(head, "map") || is(head, "devmap") || is(head, "spmap") || is(head, "spdevmap");
    free(head);
    qa_command_tokens_destroy(&tokens);
    if (world) return true;
  }
  return false;
}
static char *resolve_arg(const qa_app_options_host *host, const char *path) {
  if (host != NULL && host->resolve_path != NULL) {
    char *resolved = host->resolve_path(host->ctx, path);
    if (resolved != NULL) return resolved;
  }
  return dup_text(path);
}
static void push_startup(qa_app_options *o, char *text) {
  char **next = realloc(o->startup_commands, (o->startup_command_count + 1) * sizeof *next);
  if (next == NULL) qa_range_error("Out of memory");
  o->startup_commands = next;
  o->startup_commands[o->startup_command_count++] = text;
}
static void push_mod(qa_app_options *o, const char *value) {
  qa_mod_selection *next = realloc(o->mods, (o->mod_count + 1) * sizeof *next);
  if (next == NULL) qa_range_error("Out of memory");
  o->mods = next;
  if (!qa_read_mod_selection(value, &o->mods[o->mod_count])) qa_range_error("Out of memory");
  o->mod_count++;
}
qa_app_application_command qa_app_parse_application_command(const char *const *argv, size_t argc,
    qa_content_root_search search, const qa_app_options_host *host) {
  qa_app_application_command command = {0};
  if (argc > 0 && is(argv[0], "weapon-behavior")) {
    if (host == NULL || host->parse_weapon_behavior_tool == NULL)
      qa_range_error("weapon-behavior tool is unavailable");
    command.kind = "weapon-behavior";
    command.weapon_behavior_command = host->parse_weapon_behavior_tool(
        host->ctx, argv + 1, argc - 1, &command.weapon_behavior_destroy);
    return command;
  }
  /* contentRoot: 0 auto, 1 steam, 2 explicit */
  int content_root_kind = 0;
  char *content_root_path = NULL;
  qa_app_options o = {0};
  o.network_transport_kind = "";
  o.product = dup_text("q2-classic-baseq2");
  o.map = dup_text("maps/base1.bsp");
  o.corpus_root = dup_text("");
  o.movement = QA_GAME_FAMILY_Q1;
  o.character = QA_GAME_FAMILY_Q3;
  o.character_model = dup_text("sarge");
  o.renderer = "gl"; o.renderer_selection = "default";
  o.gamma = 1; o.width = 960; o.height = 600; o.seats = 1; o.skill = 1;
  o.mode = "singleplayer"; o.seed = 1; o.network_kind = "offline";
  bool list = false, menu = false, explicit_launch = false;
  const char *listen_kind = "q2-server";
  const char *remote_kind = "q2-client";
  char *bind = dup_text("0.0.0.0");
  bool has_listen = false; double listen_port = 0;
  char *remote = NULL;
  static const char *const non_launch[] = {"--content-root", "--steam", "--user-content-root",
      "--renderer", "--render-worker", "--gamma", "--width", "--height", "--hidden", "--list-content"};
  for (size_t index = 0; index < argc; index++) {
    const char *flag = argv[index];
    if (flag[0] == '+') {
      size_t end = index;
      push_startup(&o, read_startup_command(argv, argc, index, &end));
      index = end;
      continue;
    }
    if (is(flag, "--menu")) { menu = true; continue; }
    if (is(flag, "--steam")) { content_root_kind = 1; continue; }
    bool launches = true;
    for (size_t i = 0; i < sizeof non_launch / sizeof non_launch[0]; i++)
      if (is(flag, non_launch[i])) launches = false;
    if (launches) explicit_launch = true;
    if (is(flag, "--help") || is(flag, "-h")) {
      free(bind); free(remote); free(content_root_path); qa_app_options_destroy(&o);
      command.kind = "help"; return command;
    }
    if (is(flag, "--version")) {
      free(bind); free(remote); free(content_root_path); qa_app_options_destroy(&o);
      command.kind = "version"; return command;
    }
    if (is(flag, "--dedicated")) { o.dedicated = true; continue; }
    if (is(flag, "--hidden")) { o.hidden = true; continue; }
    if (is(flag, "--list-content")) { list = true; continue; }
    if (is(flag, "--ipx-native")) {
      if (o.network_transport_kind[0] != '\0') qa_range_error("Choose one IPX transport");
      o.network_transport_kind = "ipx-native"; continue;
    }
    if (++index >= argc) qa_range_error("Missing value for %s", flag);
    const char *value = argv[index];
    if (is(flag, "--ipx-dosbox")) {
      if (o.network_transport_kind[0] != '\0') qa_range_error("Choose one IPX transport");
      bool blank = true;
      for (const char *p = value; *p != '\0'; p++) if (!isspace((unsigned char)*p)) blank = false;
      if (blank || strncmp(value, "--", 2) == 0)
        qa_range_error("--ipx-dosbox requires a relay hostname or IPv4 address");
      o.network_transport_kind = "ipx-dosbox"; set_owned(&o.network_transport_relay, value);
    } else if (is(flag, "--preset")) {
      free(o.movement_product); o.movement_product = NULL;
      if (is(value, "q2-q1-q3")) {
        set_owned(&o.product, "q2-classic-baseq2"); set_owned(&o.map, "maps/base1.bsp");
        o.movement = QA_GAME_FAMILY_Q1; o.character = QA_GAME_FAMILY_Q3; set_owned(&o.character_model, "sarge");
      } else if (is(value, "q1-q2")) {
        set_owned(&o.product, "q1-rerelease-id1"); set_owned(&o.map, "maps/e1m1.bsp");
        o.movement = QA_GAME_FAMILY_Q2; o.character = QA_GAME_FAMILY_Q2; set_owned(&o.character_model, "male");
      } else qa_range_error("Unknown launch preset: %s", value);
    } else if (is(flag, "--user-content-root")) { free(o.user_content_root); o.user_content_root = resolve_arg(host, value);
    } else if (is(flag, "--content-root")) { content_root_kind = 2; free(content_root_path); content_root_path = resolve_arg(host, value);
    } else if (is(flag, "--map-game")) set_owned(&o.map_product, value);
    else if (is(flag, "--game")) set_owned(&o.product, value);
    else if (is(flag, "--mod")) push_mod(&o, value);
    else if (is(flag, "--map")) { free(o.map); o.map = qa_app_map_resource_path(value); }
    else if (is(flag, "--weapon-behavior")) {
      const char *slash = strchr(value, '/');
      bool space = false;
      for (const char *p = value; *p != '\0'; p++) if (isspace((unsigned char)*p)) space = true;
      if (slash == NULL || slash == value || slash[1] == '\0' || space || strchr(slash + 1, ':') == NULL)
        qa_range_error("Weapon behavior must be PRODUCT/DECLARED_ID");
      o.has_weapon_behavior = true;
      free(o.weapon_behavior_product);
      o.weapon_behavior_product = malloc((size_t)(slash - value) + 1);
      if (o.weapon_behavior_product == NULL) qa_range_error("Out of memory");
      memcpy(o.weapon_behavior_product, value, (size_t)(slash - value));
      o.weapon_behavior_product[slash - value] = '\0';
      set_owned(&o.weapon_behavior_id, slash + 1);
    } else if (is(flag, "--q2-game")) {
      char *path = qa_normalize_resource_path(value);
      if (path == NULL) qa_range_error("Out of memory");
      size_t plen = strlen(path);
      if (!(plen >= 4 && strcmp(path + plen - 4, ".dll") == 0)) { free(path); qa_range_error("--q2-game requires a mounted .dll artifact"); }
      free(o.q2_game_library); o.q2_game_library = path;
    } else if (is(flag, "--progs")) {
      char *path = qa_normalize_resource_path(value);
      if (path == NULL) qa_range_error("Out of memory");
      size_t plen = strlen(path);
      if (!(plen >= 4 && strcmp(path + plen - 4, ".dat") == 0)) { free(path); qa_range_error("--progs requires a mounted .dat artifact"); }
      free(o.quakec_program); o.quakec_program = path;
    } else if (is(flag, "--movement")) {
      free(o.movement_product); o.movement_product = NULL;
      qa_game_family fam;
      if (qa_game_family_parse(value, &fam)) o.movement = fam;
      else {
        bool ok = value[0] != '\0' && isalnum((unsigned char)value[0]);
        for (const char *p = value; ok && *p != '\0'; p++)
          if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '_' || *p == '+' || *p == '-')) ok = false;
        if (!ok) qa_range_error("Invalid movement product: %s", value);
        o.movement_product = dup_text(is(value, "qw") ? "q1-quakeworld" : value);
      }
    } else if (is(flag, "--character")) {
      o.character = family_value(value);
      set_owned(&o.character_model, o.character == QA_GAME_FAMILY_Q3 ? "sarge" : o.character == QA_GAME_FAMILY_Q2 ? "male" : "player");
    } else if (is(flag, "--model")) {
      bool ok = value[0] != '\0';
      for (const char *p = value; ok && *p != '\0'; p++)
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-')) ok = false;
      if (!ok) qa_range_error("Invalid character model: %s", value);
      set_owned(&o.character_model, value);
    } else if (is(flag, "--render-worker")) {
      if (!is(value, "0") && !is(value, "1")) qa_range_error("Render worker must be 0 or 1");
      o.has_render_worker = true; o.render_worker = is(value, "1");
    } else if (is(flag, "--renderer")) {
      if (!is(value, "cpu") && !is(value, "gl")) qa_range_error("Unknown renderer: %s", value);
      o.renderer = is(value, "cpu") ? "cpu" : "gl"; o.renderer_selection = "explicit";
    } else if (is(flag, "--width")) {
      o.width = integer_value(value, flag, 64, 16384);
      o.has_display_width = true; o.display_width = o.width;
    } else if (is(flag, "--gamma")) {
      double gamma = strtod(value, NULL);
      if (!isfinite(gamma) || gamma < 0.5 || gamma > 3) qa_range_error("Display gamma must be between 0.5 and 3");
      o.gamma = gamma; o.has_display_gamma = true; o.display_gamma = gamma;
    } else if (is(flag, "--height")) {
      o.height = integer_value(value, flag, 64, 16384);
      o.has_display_height = true; o.display_height = o.height;
    } else if (is(flag, "--seats")) {
      o.seats = integer_value(value, flag, 1, 4); o.explicit_capacity = true;
    } else if (is(flag, "--seed")) o.seed = integer_value(value, flag, 0, 4294967295.0);
    else if (is(flag, "--frames")) { o.has_frame_limit = true; o.frame_limit = integer_value(value, flag, 1, 9007199254740991.0); }
    else if (is(flag, "--listen") || is(flag, "--listen-q2") || is(flag, "--listen-unified")) {
      const char *kind = is(flag, "--listen-unified") ? "unified-server" : is(flag, "--listen") ? "native-server" : "q2-server";
      if (has_listen && !is(listen_kind, kind)) qa_range_error("Choose one server listener");
      listen_kind = kind; has_listen = true; listen_port = integer_value(value, flag, 0, 65535);
    } else if (is(flag, "--q2-protocol")) { o.has_q2_protocol = true; o.q2_protocol = parse_q2_protocol(value); }
    else if (is(flag, "--q1-protocol")) { o.has_q1_protocol = true; o.q1_protocol = qa_default_netquake_profile(integer_value(value, flag, 15, 999)); }
    else if (is(flag, "--connect-qw") || is(flag, "--connect-q1") || is(flag, "--connect-q2") || is(flag, "--connect-q3") || is(flag, "--connect-unified")) {
      if (remote != NULL) qa_range_error("Choose one remote connection");
      remote_kind = is(flag, "--connect-unified") ? "unified-client" : is(flag, "--connect-qw") ? "qw-client"
          : is(flag, "--connect-q1") ? "q1-client" : is(flag, "--connect-q3") ? "q3-client" : "q2-client";
      remote = dup_text(value);
    } else if (is(flag, "--bind")) set_owned(&bind, value);
    else if (is(flag, "--skill")) {
      o.skill = integer_value(value, flag, 0, 3); o.explicit_skill = true;
    } else if (is(flag, "--bot-skill")) {
      o.has_bot_skill = true; o.bot_skill = integer_value(value, flag, 1, 5);
    } else if (is(flag, "--server-profile")) { free(o.server_profile_path); o.server_profile_path = resolve_arg(host, value); }
    else if (is(flag, "--rules")) {
      if (!is(value, "standard") && !is(value, "ctf") && !is(value, "lmctf") && !is(value, "tag") && !is(value, "deathball") && !is(value, "horde"))
        qa_range_error("Unknown match rules: %s", value);
      o.rules = is(value, "ctf") ? "ctf" : is(value, "lmctf") ? "lmctf" : is(value, "tag") ? "tag"
          : is(value, "deathball") ? "deathball" : is(value, "horde") ? "horde" : "standard";
    } else if (is(flag, "--mode")) {
      if (!is(value, "singleplayer") && !is(value, "coop") && !is(value, "deathmatch"))
        qa_range_error("Unknown game mode: %s", value);
      o.mode = is(value, "coop") ? "coop" : is(value, "deathmatch") ? "deathmatch" : "singleplayer";
      o.explicit_mode = true;
    } else qa_range_error("Unknown option: %s", flag);
  }
  if (is(o.rules, "ctf") || is(o.rules, "lmctf")
      || (o.rules == NULL && (is(o.product, "q2-classic-ctf") || is(o.product, "q2-classic-lmctf"))))
    o.mode = "deathmatch";
  free(o.corpus_root);
  o.corpus_root = content_root_kind == 2 ? content_root_path
      : qa_resolve_default_content_root(search, content_root_kind == 1);
  if (o.corpus_root == NULL) qa_range_error("Out of memory");
  content_root_path = NULL;
  explicit_launch = explicit_launch || startup_requests_world(o.startup_commands, o.startup_command_count);
  if (list) {
    command.kind = "list-content";
    command.corpus_root = o.corpus_root; o.corpus_root = NULL;
    free(bind); free(remote); free(content_root_path); qa_app_options_destroy(&o);
    return command;
  }
  if (has_listen && remote != NULL) qa_range_error("Choose a server listener or a remote connection");
  if (has_listen) {
    o.network_kind = listen_kind; o.network_host = bind; bind = NULL; o.network_port = listen_port;
  } else if (!is(bind, "0.0.0.0")) qa_range_error("--bind requires --listen, --listen-q2 or --listen-unified");
  if (remote != NULL) {
    if (o.has_bot_skill) qa_range_error("--bot-skill is not a native Quake II client setting");
    o.network_kind = remote_kind; o.network_remote = remote; remote = NULL;
  }
  if (o.network_transport_kind[0] != '\0' && (is(o.network_kind, "unified-server") || is(o.network_kind, "unified-client")))
    qa_range_error("Unified networking uses UDP; IPX applies to native game protocols");
  if (o.network_transport_kind[0] != '\0' && is(o.network_kind, "offline"))
    qa_range_error("IPX selection requires --listen, --listen-q2 or a native client connection");
  if (is(o.network_transport_kind, "ipx-native") && bind != NULL && !is(bind, "0.0.0.0"))
    qa_range_error("--bind selects an IP interface and cannot bind native AF_IPX");
  if (o.network_transport_kind[0] != '\0' && is(o.network_kind, "qw-client"))
    qa_range_error("QuakeWorld uses UDP; IPX is not a QuakeWorld transport");
  if (o.has_q2_protocol && !is(o.network_kind, "q2-client") && !is(o.network_kind, "q2-server") && !is(o.network_kind, "native-server"))
    qa_range_error("--q2-protocol requires --connect-q2, --listen-q2 or --listen");
  if (o.has_q1_protocol && !is(o.network_kind, "native-server"))
    qa_range_error("--q1-protocol requires --listen for a Quake I host");
  if (o.has_q1_protocol && is(o.product, "q1-quakeworld"))
    qa_range_error("--q1-protocol selects NetQuake; QuakeWorld uses native protocol 28");
  if (!is(o.network_kind, "offline") && is(o.mode, "singleplayer"))
    o.mode = (is(o.network_kind, "native-server") || is(o.network_kind, "unified-server"))
            && (strncmp(o.product, "q3-", 3) == 0 || is(o.product, "q1-quakeworld")) ? "deathmatch" : "coop";
  if (o.seats > 1 && is(o.mode, "singleplayer")) o.mode = "coop";
  if (is(o.rules, "ctf") || is(o.rules, "lmctf") || is(o.rules, "tag") || is(o.rules, "deathball")) o.mode = "deathmatch";
  if (menu && (o.dedicated || !is(o.network_kind, "offline")))
    qa_range_error("--menu requires a local, non-dedicated application");
  free(bind); free(remote); free(content_root_path);
  command.kind = menu || !explicit_launch ? "menu" : "run";
  command.options = o;
  return command;
}
