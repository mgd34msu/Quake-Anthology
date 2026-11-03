/* Port of src/content/q1/addons/corpses.ts (quakec_mg1/misc_corpses.qc and
 * MG3 inhibition). GPL-2.0-or-later. */
#include "qa/content/q1/addons/corpses.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/base/map_entities.h"
#include "qa/foundation/support.h"

/* Final corpse-frame macros from the corresponding source model frame declarations. */
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

static void spawn_corpse(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  if (context->program == QA_Q1AD_MG3 &&
      (qa_q1ad_removed_outside_coop(context, entity, true) || qa_q1ad_removed_for_runes(context, entity))) return;
  /* poses[entity.number("style")]: only an integer index in range names a pose. */
  double style = qa_q1_actor_number(entity, "style");
  if (!(style >= 0 && style < (double)POSE_COUNT && style == (double)(size_t)style))
    qa_range_error("misc_corpse with invalid style");
  qa_q1ad_set_text(&entity->model, POSES[(size_t)style].model);
  entity->frame = POSES[(size_t)style].frame;
  entity->solid = "none";
  entity->movement = "none";
}

typedef struct ambient_sound { const char *classname, *sound; double volume; } ambient_sound;
static const ambient_sound AMBIENT[] = {
  {"ambient_suck_wind", "suck1", 1}, {"ambient_drone", "drone6", 0.5}, {"ambient_flouro_buzz", "buzz1", 1},
  {"ambient_drip", "drip1", 0.5}, {"ambient_comp_hum", "comp1", 1}, {"ambient_thunder", "thunder1", 0.5},
  {"ambient_light_buzz", "fl_hum1", 0.5}, {"ambient_swamp1", "swamp1", 0.5}, {"ambient_swamp2", "swamp2", 0.5},
};

static void emit_ambient(qa_q1_services *game, qa_q1_actor *entity, const char *path, double volume, double attenuation) {
  qa_q1_event event = {.kind = "ambient", .has_origin = true, .origin = qa_q1_get_body(game, entity).origin,
                       .path = path, .volume = volume, .attenuation = attenuation};
  qa_q1_emit(game, event);
}

/* ctx: the captured ambient row (TS closure over [classname, sound, volume]). */
static void spawn_ambient(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const ambient_sound *row = ctx;
  char wav[64];
  (void)snprintf(wav, sizeof wav, "ambience/%s.wav", row->sound);
  const char *path = qa_q1_services_precache_sound(game, wav);
  emit_ambient(game, entity, path, row->volume, 3);
  qa_q1_make_static(game, entity);
}

static void spawn_ambient_generic(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  const char *path = qa_q1_actor_text(entity, "noise");
  if (path[0] == '\0') { qa_q1_remove(game, entity); return; }
  (void)qa_q1_services_precache_sound(game, path);
  double volume = qa_q1_actor_number(entity, "volume");
  emit_ambient(game, entity, path, volume != 0 ? volume : 0.5, entity->delay == 0 || entity->delay != entity->delay ? 3 : entity->delay);
  qa_q1_make_static(game, entity);
}

void qa_q1ad_register_corpses(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_register_spawn(game, "misc_corpse", spawn_corpse, context);
  for (size_t i = 0; i < sizeof AMBIENT / sizeof AMBIENT[0]; i++) {
    void *row = (void *)&AMBIENT[i];
    if (strcmp(AMBIENT[i].classname, "ambient_drone") == 0 || strcmp(AMBIENT[i].classname, "ambient_comp_hum") == 0)
      qa_q1_register_spawn(game, AMBIENT[i].classname, spawn_ambient, row);
    else
      qa_q1_replace_spawn(game, AMBIENT[i].classname, spawn_ambient, row);
  }
  qa_q1_register_spawn(game, "ambient_generic", spawn_ambient_generic, context);
}
