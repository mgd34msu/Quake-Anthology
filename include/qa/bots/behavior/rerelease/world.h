/* Port of src/bots/behavior/rerelease/world.ts. BotWorldT becomes fn+ctx;
 * ammo Record becomes name/value list. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_WORLD_H
#define QA_BOTS_BEHAVIOR_RERELEASE_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/behavior/rerelease/math.h"
struct qa_bot_navigation_iface;
enum { QA_BOT_ENTITY_PLAYER = 0, QA_BOT_ENTITY_MONSTER = 1, QA_BOT_ENTITY_ITEM = 2, QA_BOT_ENTITY_INTERACTABLE = 3 };
enum { QA_BOT_CONTENTS_EMPTY = 0, QA_BOT_CONTENTS_SOLID = 1, QA_BOT_CONTENTS_WATER = 2, QA_BOT_CONTENTS_SLIME = 3, QA_BOT_CONTENTS_LAVA = 4, QA_BOT_CONTENTS_SKY = 5 };
#define QA_BOT_BUTTON_ATTACK 1
#define QA_BOT_BUTTON_JUMP 2
#define QA_BOT_BUTTON_USE 4
typedef struct qa_bot_ammo_entry { const char *name; double value; } qa_bot_ammo_entry;
double qa_bot_ammo_get(const qa_bot_ammo_entry *ammo, size_t count, const char *name); /* TS ?? 0 */
typedef struct qa_bot_entity {
  int64_t id, kind; const char *classname; qa_bot_vec3 origin, center, head, feet, velocity;
  double health; int64_t team; bool has_carrying_objective, carrying_objective, dead, invisible;
  int64_t water_level; bool is_bot; int64_t spawnflags; bool has_health, has_targetname;
} qa_bot_entity;
typedef struct qa_bot_sound { qa_bot_vec3 origin; int64_t source_id; double time, loudness; } qa_bot_sound;
typedef struct qa_bot_self {
  int64_t id; qa_bot_vec3 origin, velocity, view_angles, eye; double health, armor; int64_t items;
  const qa_bot_ammo_entry *ammo; size_t ammo_count; int64_t current_weapon; bool on_ground; int64_t water_level;
  bool has_air_seconds; double air_seconds; bool has_on_lift, on_lift; int64_t team; bool dead, has_protection;
  bool has_max_armor; double max_armor; bool has_carrying_objective, carrying_objective;
} qa_bot_self;
typedef struct qa_bot_trace { double fraction; qa_bot_vec3 endpos; bool startsolid; int64_t hit_id; } qa_bot_trace;
typedef struct qa_bot_usercmd { double forwardmove, sidemove, upmove; int64_t buttons, impulse; qa_bot_vec3 view_angles; } qa_bot_usercmd;
qa_bot_usercmd qa_empty_usercmd(void);
typedef struct qa_bot_world {
  void *ctx;
  double (*time)(void *ctx); double (*frame_time)(void *ctx); qa_bot_self (*get_self)(void *ctx);
  qa_bot_trace (*trace_line)(void *ctx, qa_bot_vec3 start, qa_bot_vec3 end);
  qa_bot_trace (*trace_box)(void *ctx, qa_bot_vec3 start, qa_bot_vec3 mins, qa_bot_vec3 maxs, qa_bot_vec3 end);
  int64_t (*point_contents)(void *ctx, qa_bot_vec3 p);
  const qa_bot_entity *(*entities)(void *ctx, size_t *out_count);
  const qa_bot_sound *(*hearing)(void *ctx, size_t *out_count);
  struct qa_bot_navigation_iface *(*nav)(void *ctx); /* NULL = TS null */
} qa_bot_world;
#endif
