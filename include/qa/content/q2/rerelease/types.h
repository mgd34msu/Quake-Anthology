/*
 * Port of src/content/q2/rerelease/types.ts. Quake II rerelease game
 * DLL extensions, GPL-2.0-or-later.
 * C accommodations: Q2RereleaseEvent union is one struct on kind
 * (exact strings); Q2RereleaseHooks is fn+ctx; Q2RereleasePlayerState
 * owns dogtag/socialId/helpImage strings (fixed buffers; values are
 * short source strings) and a malloc'd helpPoints array. Plain TS
 * Errors abort via qa_q2rr_error ("Error: <msg>"); RangeErrors use
 * qa_range_error. Getters that would share a type name get _get_.
 */
#ifndef QA_CONTENT_Q2_RERELEASE_TYPES_H
#define QA_CONTENT_Q2_RERELEASE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>

#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/debug/shapes.h"
#include "qa/text/world.h"

noreturn void qa_q2rr_error(const char *format, ...);
/* strdup; NULL only on OOM. NULL in -> NULL. */
char *qa_q2rr_dup(const char *s);
/* Case-insensitive ASCII compare (TS toLowerCase equality). */
bool qa_q2rr_ieq(const char *a, const char *b);
bool qa_q2rr_contains(const char *s, const char *needle);
bool qa_q2rr_starts_with(const char *s, const char *prefix);

typedef struct qa_q2_fog { double density; qa_vec3 color; double sky_factor; } qa_q2_fog;
typedef struct qa_q2rr_height_fog {
  qa_vec3 start_color; double start_distance;
  qa_vec3 end_color; double end_distance;
  double falloff, density;
  /* TS key order only: q2RereleaseFogFields builds { ..., density, falloff },
   * createQ2Fog, interpolateQ2Fog and the save reader { ..., falloff,
   * density }; the checkpoint encoder writes keys in that order. */
  bool density_first;
} qa_q2rr_height_fog;
typedef struct qa_q2_fog_state { qa_q2_fog fog; qa_q2rr_height_fog height_fog; } qa_q2_fog_state;
qa_q2_fog_state qa_q2_create_fog(void);

/* TS Q2CoopRespawnState exact strings. */
typedef enum qa_q2_coop_respawn_state {
  QA_Q2_COOP_NONE, QA_Q2_COOP_IN_COMBAT, QA_Q2_COOP_BAD_AREA,
  QA_Q2_COOP_BLOCKED, QA_Q2_COOP_WAITING, QA_Q2_COOP_NO_LIVES
} qa_q2_coop_respawn_state;
const char *qa_q2_coop_respawn_state_name(qa_q2_coop_respawn_state s);

/** TS Omit<Q2LandmarkCarry,"player">. name borrowed. */
typedef struct qa_q2rr_pending_landmark {
  bool has;
  const char *name;
  qa_vec3 relative_origin, relative_velocity, relative_view_angles;
} qa_q2rr_pending_landmark;

/** TS Q2RereleasePlayerState (class). */
typedef struct qa_q2rr_player_extra_state {
  double seat;
  char social_id[128];
  bool spawned;
  double game_help1_changed, game_help2_changed, help_changed, help_time;
  double invisibility_until, invisibility_fade_until, slime_debounce;
  double animation_time, flash_time, flashes;
  double last_damage_until, last_firing_until, lives;
  qa_q2_coop_respawn_state coop_respawn_state;
  bool flashlight;
  qa_q2_fog_state fog, wanted_fog;
  double fog_transition;
  bool bob_skip;
  double auto_switch; /* 0|1|2|3 */
  double auto_shield;
  char dogtag[128];
  double impact_delta;
  bool on_ladder;
  double grapple_released_until;
  bool grapple_attached;
  qa_vec3 slow_view_angles;
  double quake_time, wind_sound_time;
  bool awaiting_respawn;
  double respawn_timeout;
  qa_q2rr_pending_landmark pending_landmark;
  qa_vec3 help_location;
  char help_image[128];
  qa_vec3 *help_points; size_t help_point_count, help_point_cap;
  double help_index, help_draw_time, help_marker_until;
} qa_q2rr_player_extra_state;
void qa_q2rr_player_extra_state_init(qa_q2rr_player_extra_state *s, double seat, const char *social_id);
void qa_q2rr_player_extra_state_destroy(qa_q2rr_player_extra_state *s);
/* Deep copy (structuredClone); false only on OOM. dst destroyed first if reused. */
bool qa_q2rr_player_extra_state_clone(qa_q2rr_player_extra_state *dst, const qa_q2rr_player_extra_state *src);

typedef struct qa_q2_rerelease_options {
  bool coop_squad_respawn, coop_instanced_items, coop_lives;
  double coop_num_lives;
  bool deathmatch_force_respawn, deathmatch_no_fall_damage, deathmatch_spawn_farthest;
  double deathmatch_force_respawn_time;
  bool deathmatch_allow_exit, coop_player_collision;
  double auto_save_minimum_time;
} qa_q2_rerelease_options;
/* TS Partial<Q2RereleaseOptions>: has_* false = absent. */
typedef struct qa_q2_rerelease_options_changes {
  qa_q2_rerelease_options v;
  bool has_coop_squad_respawn, has_coop_instanced_items, has_coop_lives, has_coop_num_lives;
  bool has_deathmatch_force_respawn, has_deathmatch_no_fall_damage, has_deathmatch_spawn_farthest;
  bool has_deathmatch_force_respawn_time, has_deathmatch_allow_exit, has_coop_player_collision;
  bool has_auto_save_minimum_time;
} qa_q2_rerelease_options_changes;
qa_q2_rerelease_options qa_q2_create_rerelease_options(const qa_q2_rerelease_options_changes *changes);

/** TS Q2RereleaseEvent union on kind (exact strings). Unused zero. */
typedef struct qa_q2_rerelease_event {
  const char *kind;
  const qa_debug_line *lines; size_t line_count; double lifetime_milliseconds; /* debug-shapes */
  qa_world_text text_value; double lifetime;                                  /* world-text */
  bool has_actor; qa_actor_id actor;
  const char *level;                       /* localized-print */
  const char *text;                        /* localized-print/mission-objective/story */
  const char *const *args; size_t arg_count;
  bool talk_sound;                         /* mission-objective */
  bool icon_visible;                       /* mission-status */
  qa_vec4 blend;                           /* screen-blend */
  bool visible;                            /* help-computer/healthbar/dynamic-light/item-visibility */
  const char *primary, *secondary; bool slow_time; /* help-computer */
  qa_q2_fog_state fog_value; double transition_milliseconds; /* fog */
  bool enabled; const char *hand;          /* flashlight: Q2PlayerState hand */
  double key; qa_vec3 position; const char *image; double duration, color, flags; /* keyed-poi/poi */
  qa_vec3 direction; double damage; bool health, armor, shield; /* directional-damage */
  bool first;                              /* help-path (position/direction shared) */
  qa_q2_coop_respawn_state coop_state; double lives; /* coop-respawn */
  double alpha;                            /* alpha */
  const struct qa_q2_rerelease_level_entry *levels; size_t level_count; double button_time; /* end-of-unit */
  const char *value;                       /* player-dogtag */
  qa_vec3 origin; double radius; qa_vec3 light_color; /* dynamic-light */
  const char *map;                         /* restart-level */
  const char *id;                          /* achievement */
  const char *sky_name; double rotation; bool auto_rotate; qa_vec3 axis; /* sky */
  double slot; bool has_target; qa_actor_id target; const char *name; double fraction; /* healthbar */
  bool has_item; qa_actor_id item;         /* item-visibility */
} qa_q2_rerelease_event;

/** TS navigation result: kind "path"|"no-navigation"|"unreachable". */
typedef struct qa_q2rr_navigation {
  const char *kind;
  double distance_squared;
  const qa_vec3 *points; size_t point_count;
} qa_q2rr_navigation;
typedef struct qa_q2rr_player_identity { double seat; const char *social_id; } qa_q2rr_player_identity;
typedef struct qa_q2rr_expansion_powerups { double double_until, quad_fire_until, ir_until; } qa_q2rr_expansion_powerups;
typedef struct qa_q2rr_powerups_result { bool has; qa_q2rr_expansion_powerups v; } qa_q2rr_powerups_result;

/** TS Q2RereleaseHooks. Optional TS members may be NULL. */
typedef struct qa_q2_rerelease_hooks {
  void *ctx;
  void (*emit)(void *ctx, qa_q2_rerelease_event event);
  const char *(*light_style)(void *ctx, double style);
  qa_q2rr_player_identity (*player_identity)(void *ctx, qa_actor_id actor);
  bool (*clip_trigger)(void *ctx, qa_q2_entity *trigger, qa_actor_id player, qa_q2_game_services *game);
  qa_q2rr_navigation (*navigation)(void *ctx, qa_vec3 start, qa_vec3 goal, const qa_actor_id *actor);
  bool (*monsters_searching)(void *ctx, const qa_actor_id *player);
  bool (*monster_holds_health_bar)(void *ctx, qa_actor_id monster); /* optional */
  qa_q2rr_powerups_result (*expansion_powerups)(void *ctx, qa_actor_id player); /* optional */
  void (*clear_expansion_powerups)(void *ctx, qa_actor_id player); /* optional */
  void (*player_collision)(void *ctx, qa_actor_id player, bool collide); /* optional */
  bool (*grounded_on_world)(void *ctx, qa_actor_id player);
  void (*push_player)(void *ctx, qa_actor_id player, qa_vec3 velocity);
  void (*set_actor_gravity)(void *ctx, qa_actor_id actor, double gravity);
  void (*set_world_gravity)(void *ctx, double gravity);
} qa_q2_rerelease_hooks;

bool qa_q2_is_n64(const qa_q2_game_options *options);
bool qa_q2_uses_instanced_items(qa_q2_rerelease_options options);

#endif
