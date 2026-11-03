/* Port of src/app/bootstrap/options.ts. C accommodations:
 *  - ApplicationOptions owns its strings/arrays; free with
 *    qa_app_options_destroy. Optional TS fields use has_* flags.
 *  - Opaque cross-slice values (networkTransport, q3MapLaunch is this
 *    slice's q3_map_command type, teamArenaSkirmish, serverProfile)
 *    are borrowed void pointers set by their owning slices; the CLI
 *    parser only fills networkTransport kind/relay (its own fields).
 *  - parseWeaponBehaviorTool (weapon-behavior-tool-options.ts, other
 *    slice) is a host callback returning an owned opaque command.
 *  - resolve() (node:path) is approximated by the host resolve_path
 *    callback; NULL keeps the argument text.
 *  - RangeError and plain Error both abort via qa_range_error with
 *    the identical message, as in landed ports.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_OPTIONS_H
#define QA_APP_BOOTSTRAP_OPTIONS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/protocol.h"
#include "qa/core/q3_product_policy.h"
#include "qa/content/catalog/index.h" /* qa_remote_content_selection */
#include "qa/content/roots.h"
typedef struct qa_app_options {
  /* networkTransport: kind "" = absent */
  const char *network_transport_kind; /* "ipx-native"|"ipx-dosbox"|"" */
  char *network_transport_relay;      /* owned, ipx-dosbox only */
  const void *q3_map_launch;          /* borrowed qa_app_q3_map_launch* */
  bool has_q3_product;
  qa_q3_application_product q3_product;
  bool authored_campaign_start;
  char **startup_commands; size_t startup_command_count; /* owned */
  bool explicit_skill, explicit_mode, explicit_capacity;
  const void *team_arena_skirmish;    /* borrowed, other slice */
  bool has_remote_content;
  qa_remote_content_selection remote_content;
  bool has_q1_protocol; qa_q1_protocol_identity q1_protocol;
  bool has_q2_protocol; qa_q2_protocol_identity q2_protocol;
  const void *server_profile;         /* borrowed qa_server_profile* */
  char *server_profile_path;          /* owned, NULL = absent */
  char *corpus_root;                  /* owned */
  char *user_content_root;            /* owned, NULL = absent */
  qa_nightdive_addon_root *addon_content_roots; size_t addon_content_root_count;
  char *product;                      /* owned */
  char *map_product;                  /* owned, NULL = absent */
  char *map;                          /* owned */
  char *quakec_program;               /* owned, NULL = absent */
  char *q2_game_library;              /* owned, NULL = absent */
  bool has_weapon_behavior;
  char *weapon_behavior_product;      /* owned */
  char *weapon_behavior_id;           /* owned */
  qa_mod_selection *mods; size_t mod_count; /* owned array, borrowed strings in argv lifetime */
  qa_game_family movement;
  char *movement_product;             /* owned, NULL = absent */
  qa_game_family character;
  char *character_model;              /* owned */
  char *character_skin;               /* owned, NULL = absent */
  bool has_character_top_color; double character_top_color;
  bool has_character_bottom_color; double character_bottom_color;
  const char *renderer;               /* "cpu"|"gl" */
  bool has_render_worker; bool render_worker;
  const char *renderer_selection;     /* "default"|"explicit", NULL = absent */
  double gamma;
  bool has_display_width; double display_width;
  bool has_display_height; double display_height;
  bool has_display_gamma; double display_gamma;
  bool trusted_networks_only; /* TS trustedNetworksOnly (--lan-only) */
  bool dedicated;
  double width, height, seats;
  double skill;                       /* 0..3 */
  bool has_bot_skill; double bot_skill;
  const char *mode;                   /* "singleplayer"|"coop"|"deathmatch" */
  const char *rules;                  /* NULL = absent */
  double seed;
  bool has_frame_limit; double frame_limit;
  bool hidden;
  bool allow_load_paths;              /* TS allowLoadPaths (--allow-load-paths) */
  /* TS vmGame?: 0 | 1 | 2 (--vm-game). 0 runs a mod's native qagame module when it ships one. */
  bool has_vm_game; int vm_game;
  const char *network_kind;           /* "offline"|*-server|*-client */
  char *network_host;                 /* owned, server kinds */
  double network_port;
  char *network_remote;               /* owned, client kinds */
} qa_app_options;
void qa_app_options_destroy(qa_app_options *options);
/* TS liveQ2Protocol. */
qa_q2_protocol_identity qa_app_live_q2_protocol(const qa_app_options *options, bool rerelease);
/* TS ApplicationCommand union on kind. */
typedef struct qa_app_application_command {
  const char *kind; /* "help"|"version"|"weapon-behavior"|"list-content"|"run"|"menu" */
  void *weapon_behavior_command; /* owned opaque, weapon-behavior kind */
  void (*weapon_behavior_destroy)(void *command);
  char *corpus_root;             /* owned, list-content kind */
  qa_app_options options;        /* run/menu kinds */
} qa_app_application_command;
void qa_app_application_command_destroy(qa_app_application_command *command);
extern const char *const qa_app_application_help;
typedef struct qa_app_options_host {
  void *ctx;
  /* TS parseWeaponBehaviorTool; returns owned opaque or aborts. */
  void *(*parse_weapon_behavior_tool)(void *ctx, const char *const *argv, size_t argc,
                                      void (**destroy)(void *command));
  /* node:path resolve(); malloc'd result or NULL to keep the text. */
  char *(*resolve_path)(void *ctx, const char *path);
} qa_app_options_host;
/* Malloc'd normalized map path; aborts (RangeError) when invalid. */
char *qa_app_map_resource_path(const char *name);
qa_app_application_command qa_app_parse_application_command(const char *const *argv, size_t argc,
    qa_content_root_search search, const qa_app_options_host *host);
#endif
