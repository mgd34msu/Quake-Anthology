/* Port of src/content/q1/addons/corpses.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/corpses.h"

#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

static const struct { const char *model; double frame; } POSES[] = {
  {"progs/demon.mdl", 53}, {"progs/dog.mdl", 16}, {"progs/dog.mdl", 25}, {"progs/enforcer.mdl", 54}, {"progs/enforcer.mdl", 65}, {"progs/fish.mdl", 38},
  {"progs/hknight.mdl", 53}, {"progs/hknight.mdl", 62}, {"progs/knight.mdl", 85}, {"progs/knight.mdl", 96}, {"progs/ogre.mdl", 116}, {"progs/ogre.mdl", 126},
  {"progs/shalrath.mdl", 22}, {"progs/shambler.mdl", 87}, {"progs/soldier.mdl", 17}, {"progs/soldier.mdl", 28}, {"progs/wizard.mdl", 53},
  {"progs/player.mdl", 49}, {"progs/player.mdl", 60}, {"progs/player.mdl", 69}, {"progs/player.mdl", 84}, {"progs/player.mdl", 93}, {"progs/player.mdl", 102},
  {"progs/h_demon.mdl", 0}, {"progs/h_dog.mdl", 0}, {"progs/h_guard.mdl", 0}, {"progs/h_hellkn.mdl", 0}, {"progs/h_knight.mdl", 0}, {"progs/h_mega.mdl", 0},
  {"progs/h_ogre.mdl", 0}, {"progs/h_player.mdl", 0}, {"progs/h_shal.mdl", 0}, {"progs/h_shams.mdl", 0}, {"progs/h_wizard.mdl", 0}, {"progs/h_zombie.mdl", 0},
  {"progs/gib1.mdl", 0}, {"progs/gib2.mdl", 0}, {"progs/gib3.mdl", 0},
};
#define POSE_COUNT (sizeof POSES / sizeof POSES[0])

static void spawn_corpse(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->program == QA_Q1AD_MG3 &&
      (qa_q1ad_removed_outside_coop(context, entity, true) ||
       qa_q1ad_removed_for_runes(context, entity))) return;
  double style = qa_q1ad_actor_number(entity, "style");
  if (style < 0 || style >= (double)POSE_COUNT || style != (double)(int64_t)style)
    qa_range_error("misc_corpse with invalid style");
  entity->model = POSES[(size_t)style].model;
  entity->frame = POSES[(size_t)style].frame;
  entity->solid = "none";
  entity->movement = "none";
}

static const struct { const char *classname, *sound; double volume; } AMBIENT[] = {
  {"ambient_suck_wind", "suck1", 1}, {"ambient_drone", "drone6", 0.5},
  {"ambient_flouro_buzz", "buzz1", 1}, {"ambient_drip", "drip1", 0.5},
  {"ambient_comp_hum", "comp1", 1}, {"ambient_thunder", "thunder1", 0.5},
  {"ambient_light_buzz", "fl_hum1", 0.5}, {"ambient_swamp1", "swamp1", 0.5},
  {"ambient_swamp2", "swamp2", 0.5},
};
static void ambient_spawn(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *sound, double volume) {
  char path[64];
  (void)snprintf(path, sizeof path, "ambience/%s.wav", sound);
  const char *cached = context->game.precache_sound != NULL
      ? context->game.precache_sound(context->game.ctx, path) : path;
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_q1ad_host_event event = {.kind = "ambient", .origin = body.origin, .path = cached,
                              .volume = volume, .attenuation = 3};
  qa_q1ad_host_emit(context, &event);
  if (context->game.make_static != NULL) context->game.make_static(context->game.ctx, entity);
}
#define AMBIENT_SPAWN(i) \
  static void spawn_ambient_##i(qa_q1ad_context *c, qa_q1ad_actor *e) { \
    ambient_spawn(c, e, AMBIENT[i].sound, AMBIENT[i].volume); }
AMBIENT_SPAWN(0) AMBIENT_SPAWN(1) AMBIENT_SPAWN(2) AMBIENT_SPAWN(3) AMBIENT_SPAWN(4)
AMBIENT_SPAWN(5) AMBIENT_SPAWN(6) AMBIENT_SPAWN(7) AMBIENT_SPAWN(8)
#undef AMBIENT_SPAWN
static const qa_q1ad_spawn_fn AMBIENT_FNS[] = {
  spawn_ambient_0, spawn_ambient_1, spawn_ambient_2, spawn_ambient_3, spawn_ambient_4,
  spawn_ambient_5, spawn_ambient_6, spawn_ambient_7, spawn_ambient_8,
};

static void spawn_ambient_generic(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  const char *path = qa_q1ad_actor_text(entity, "noise");
  if (path[0] == '\0') { qa_q1ad_remove(context, entity); return; }
  if (context->game.precache_sound != NULL) (void)context->game.precache_sound(context->game.ctx, path);
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  double volume = qa_q1ad_actor_number(entity, "volume");
  qa_q1ad_host_event event = {.kind = "ambient", .origin = body.origin, .path = path,
      .volume = volume != 0 ? volume : 0.5, .attenuation = entity->delay != 0 ? entity->delay : 3};
  qa_q1ad_host_emit(context, &event);
  if (context->game.make_static != NULL) context->game.make_static(context->game.ctx, entity);
}

void qa_q1ad_register_corpses(qa_q1ad_context *context) {
  qa_q1ad_register_spawn(context, "misc_corpse", false, spawn_corpse);
  for (size_t i = 0; i < sizeof AMBIENT / sizeof AMBIENT[0]; i++) {
    bool plain = strcmp(AMBIENT[i].classname, "ambient_drone") == 0 ||
                 strcmp(AMBIENT[i].classname, "ambient_comp_hum") == 0;
    qa_q1ad_register_spawn(context, AMBIENT[i].classname, !plain, AMBIENT_FNS[i]);
  }
  qa_q1ad_register_spawn(context, "ambient_generic", false, spawn_ambient_generic);
}
