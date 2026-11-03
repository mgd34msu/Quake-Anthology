/* Port of src/content/q3/presentation/media.ts. GPL-2.0-or-later.
 * The straight-line registration sequences in registerClientSounds /
 * registerClientGraphics are generated from media.ts by
 * /tmp/gen_media.py; conditions and call order are the TS source's. */
#include "qa/content/q3/presentation/media.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/presentation/character_resources.h"
#include "qa/content/q3/presentation/model_access.h"
#include "qa/core/math.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/foundation/support.h"

typedef qa_q3_pres_client_media media_t;
typedef qa_q3_pres_client_media_host host_t;
#define STR(text) ((text) != NULL ? (text) : "")

bool qa_q3_pres_client_media_init(media_t *media, qa_q3_base_product product,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_renderer_resources resources,
    qa_q3_pres_client_sound_bank sound_bank, qa_q3_pres_client_weapon_media_registry_view weapon_registry) {
  if (product != static_state->product) qa_range_error("Client media product differs from cgs");
  memset(media, 0, sizeof *media);
  media->product = product;
  media->static_state = static_state;
  media->resources = resources;
  media->sound_bank = sound_bank;
  media->weapon_registry = weapon_registry;
  qa_q3_pres_client_media_graphics *graphics = &media->graphics;
  graphics->blue_prox_mine = qa_q3_pres_default_model;
  graphics->red_cube_model = qa_q3_pres_default_model;
  graphics->blue_cube_model = qa_q3_pres_default_model;
  graphics->red_flag_model = qa_q3_pres_default_model;
  graphics->blue_flag_model = qa_q3_pres_default_model;
  graphics->flag_pole_model = qa_q3_pres_default_model;
  graphics->flag_flap_model = qa_q3_pres_default_model;
  graphics->red_flag_base_model = qa_q3_pres_default_model;
  graphics->blue_flag_base_model = qa_q3_pres_default_model;
  graphics->neutral_flag_base_model = qa_q3_pres_default_model;
  graphics->neutral_flag_model = qa_q3_pres_default_model;
  graphics->overload_base_model = qa_q3_pres_default_model;
  graphics->overload_target_model = qa_q3_pres_default_model;
  graphics->overload_lights_model = qa_q3_pres_default_model;
  graphics->overload_energy_model = qa_q3_pres_default_model;
  graphics->harvester_model = qa_q3_pres_default_model;
  graphics->harvester_neutral_model = qa_q3_pres_default_model;
  graphics->armor_model = qa_q3_pres_default_model;
  graphics->machinegun_brass_model = qa_q3_pres_default_model;
  graphics->shotgun_brass_model = qa_q3_pres_default_model;
  graphics->gib_abdomen = qa_q3_pres_default_model;
  graphics->gib_arm = qa_q3_pres_default_model;
  graphics->gib_chest = qa_q3_pres_default_model;
  graphics->gib_fist = qa_q3_pres_default_model;
  graphics->gib_foot = qa_q3_pres_default_model;
  graphics->gib_forearm = qa_q3_pres_default_model;
  graphics->gib_intestine = qa_q3_pres_default_model;
  graphics->gib_leg = qa_q3_pres_default_model;
  graphics->gib_skull = qa_q3_pres_default_model;
  graphics->gib_brain = qa_q3_pres_default_model;
  graphics->smoke2 = qa_q3_pres_default_model;
  graphics->bullet_flash_model = qa_q3_pres_default_model;
  graphics->ring_flash_model = qa_q3_pres_default_model;
  graphics->dish_flash_model = qa_q3_pres_default_model;
  graphics->teleport_effect_model = qa_q3_pres_default_model;
  graphics->kamikaze_effect_model = qa_q3_pres_default_model;
  graphics->kamikaze_shock_wave = qa_q3_pres_default_model;
  graphics->kamikaze_head_model = qa_q3_pres_default_model;
  graphics->kamikaze_head_trail = qa_q3_pres_default_model;
  graphics->guard_powerup_model = qa_q3_pres_default_model;
  graphics->scout_powerup_model = qa_q3_pres_default_model;
  graphics->doubler_powerup_model = qa_q3_pres_default_model;
  graphics->ammo_regen_powerup_model = qa_q3_pres_default_model;
  graphics->invulnerability_impact_model = qa_q3_pres_default_model;
  graphics->invulnerability_juiced_model = qa_q3_pres_default_model;
  graphics->medkit_usage_model = qa_q3_pres_default_model;
  graphics->invulnerability_powerup_model = qa_q3_pres_default_model;
  media->inline_models = malloc(8 * sizeof *media->inline_models);
  if (media->inline_models == NULL) return false;
  media->inline_model_capacity = 8;
  media->inline_models[0].model = qa_q3_pres_default_model;
  media->inline_models[0].midpoint = qa_math_vec3(0, 0, 0);
  media->inline_model_count = 1;
  return true;
}
void qa_q3_pres_client_media_destroy(media_t *media) {
  free(media->inline_models);
  memset(media, 0, sizeof *media);
}

qa_q3_pres_client_event_media qa_q3_pres_client_media_events(media_t *media) {
  qa_q3_pres_client_event_media events;
  memset(&events, 0, sizeof events);
  events.sounds[QA_Q3_PRES_USE_NOTHING_SOUND] = media->sounds.use_nothing_sound;
  events.sounds[QA_Q3_PRES_MEDKIT_SOUND] = media->sounds.medkit_sound;
  events.sounds[QA_Q3_PRES_LAND_SOUND] = media->sounds.land_sound;
  events.sounds[QA_Q3_PRES_JUMP_PAD_SOUND] = media->sounds.jump_pad_sound;
  events.sounds[QA_Q3_PRES_WATR_IN_SOUND] = media->sounds.watr_in_sound;
  events.sounds[QA_Q3_PRES_WATR_OUT_SOUND] = media->sounds.watr_out_sound;
  events.sounds[QA_Q3_PRES_WATR_UN_SOUND] = media->sounds.watr_un_sound;
  events.sounds[QA_Q3_PRES_N_HEALTH_SOUND] = media->sounds.n_health_sound;
  events.sounds[QA_Q3_PRES_SELECT_SOUND] = media->sounds.select_sound;
  events.sounds[QA_Q3_PRES_TELE_IN_SOUND] = media->sounds.tele_in_sound;
  events.sounds[QA_Q3_PRES_TELE_OUT_SOUND] = media->sounds.tele_out_sound;
  events.sounds[QA_Q3_PRES_RESPAWN_SOUND] = media->sounds.respawn_sound;
  events.sounds[QA_Q3_PRES_HGRENB1A_SOUND] = media->sounds.hgrenb1a_sound;
  events.sounds[QA_Q3_PRES_HGRENB2A_SOUND] = media->sounds.hgrenb2a_sound;
  events.sounds[QA_Q3_PRES_CAPTURE_YOUR_TEAM_SOUND] = media->sounds.capture_your_team_sound;
  events.sounds[QA_Q3_PRES_CAPTURE_OPPONENT_SOUND] = media->sounds.capture_opponent_sound;
  events.sounds[QA_Q3_PRES_RETURN_YOUR_TEAM_SOUND] = media->sounds.return_your_team_sound;
  events.sounds[QA_Q3_PRES_RETURN_OPPONENT_SOUND] = media->sounds.return_opponent_sound;
  events.sounds[QA_Q3_PRES_BLUE_FLAG_RETURNED_SOUND] = media->sounds.blue_flag_returned_sound;
  events.sounds[QA_Q3_PRES_RED_FLAG_RETURNED_SOUND] = media->sounds.red_flag_returned_sound;
  events.sounds[QA_Q3_PRES_ENEMY_TOOK_YOUR_FLAG_SOUND] = media->sounds.enemy_took_your_flag_sound;
  events.sounds[QA_Q3_PRES_YOUR_TEAM_TOOK_ENEMY_FLAG_SOUND] = media->sounds.your_team_took_enemy_flag_sound;
  events.sounds[QA_Q3_PRES_YOUR_BASE_IS_UNDER_ATTACK_SOUND] = media->sounds.your_base_is_under_attack_sound;
  events.sounds[QA_Q3_PRES_RED_SCORED_SOUND] = media->sounds.red_scored_sound;
  events.sounds[QA_Q3_PRES_BLUE_SCORED_SOUND] = media->sounds.blue_scored_sound;
  events.sounds[QA_Q3_PRES_RED_LEADS_SOUND] = media->sounds.red_leads_sound;
  events.sounds[QA_Q3_PRES_BLUE_LEADS_SOUND] = media->sounds.blue_leads_sound;
  events.sounds[QA_Q3_PRES_TEAMS_TIED_SOUND] = media->sounds.teams_tied_sound;
  events.sounds[QA_Q3_PRES_QUAD_SOUND] = media->sounds.quad_sound;
  events.sounds[QA_Q3_PRES_PROTECT_SOUND] = media->sounds.protect_sound;
  events.sounds[QA_Q3_PRES_REGEN_SOUND] = media->sounds.regen_sound;
  events.sounds[QA_Q3_PRES_GIB_SOUND] = media->sounds.gib_sound;
  memcpy(events.footsteps, media->footsteps, sizeof events.footsteps);
  events.game_sounds = media->static_state->game_sounds;
  events.game_sound_count = QA_Q3_PRES_MAX_GAME_SOUNDS;
  events.smoke_puff_shader = media->graphics.smoke_puff_shader;
  return events;
}
qa_q3_pres_player_media qa_q3_pres_client_media_players(media_t *media) {
  const qa_q3_pres_client_media_graphics *g = &media->graphics;
  qa_q3_pres_player_media players;
  memset(&players, 0, sizeof players);
  players.connection_shader = g->connection_shader;
  players.balloon_shader = g->balloon_shader;
  players.medal_impressive = g->medal_impressive;
  players.medal_excellent = g->medal_excellent;
  players.medal_gauntlet = g->medal_gauntlet;
  players.medal_defend = g->medal_defend;
  players.medal_assist = g->medal_assist;
  players.medal_capture = g->medal_capture;
  players.friend_shader = g->friend_shader;
  players.shadow_mark_shader = g->shadow_mark_shader;
  players.wake_mark_shader = g->wake_mark_shader;
  players.invis_shader = g->invis_shader;
  players.quad_shader = g->quad_shader;
  players.red_quad_shader = g->red_quad_shader;
  players.regen_shader = g->regen_shader;
  players.battle_suit_shader = g->battle_suit_shader;
  players.haste_puff_shader = g->haste_puff_shader;
  players.flight_sound = media->sounds.flight_sound;
  players.red_flag_model = g->red_flag_model;
  players.blue_flag_model = g->blue_flag_model;
  players.neutral_flag_model = g->neutral_flag_model;
  players.flag_pole_model = g->flag_pole_model;
  players.flag_flap_model = g->flag_flap_model;
  players.red_flag_flap_skin = g->red_flag_flap_skin;
  players.blue_flag_flap_skin = g->blue_flag_flap_skin;
  players.neutral_flag_flap_skin = g->neutral_flag_flap_skin;
  return players;
}
qa_q3_pres_mission_player_media qa_q3_pres_client_media_mission_players(media_t *media) {
  if (media->product != QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    qa_range_error("Mission player media requested in baseq3");
  }
  const qa_q3_pres_client_media_graphics *g = &media->graphics;
  qa_q3_pres_mission_player_media mission;
  memset(&mission, 0, sizeof mission);
  mission.red_cube_model = g->red_cube_model;
  mission.blue_cube_model = g->blue_cube_model;
  mission.kamikaze_head_model = g->kamikaze_head_model;
  mission.kamikaze_head_trail = g->kamikaze_head_trail;
  mission.guard_powerup_model = g->guard_powerup_model;
  mission.scout_powerup_model = g->scout_powerup_model;
  mission.doubler_powerup_model = g->doubler_powerup_model;
  mission.ammo_regen_powerup_model = g->ammo_regen_powerup_model;
  mission.invulnerability_powerup_model = g->invulnerability_powerup_model;
  mission.medkit_usage_model = g->medkit_usage_model;
  mission.shotgun_smoke_puff_shader = g->shotgun_smoke_puff_shader;
  mission.dust_puff_shader = g->dust_puff_shader;
  return mission;
}
qa_q3_pres_packet_entity_media qa_q3_pres_client_media_packet(media_t *media) {
  const qa_q3_pres_client_media_graphics *g = &media->graphics;
  qa_q3_pres_packet_entity_media packet;
  memset(&packet, 0, sizeof packet);
  packet.game_models = media->static_state->game_models;
  packet.game_model_count = QA_Q3_PRES_MAX_GAME_MODELS;
  packet.game_sounds = media->static_state->game_sounds;
  packet.game_sound_count = QA_Q3_PRES_MAX_GAME_SOUNDS;
  packet.inline_models = media->inline_models;
  packet.inline_model_count = media->inline_model_count;
  packet.items = media->weapon_registry.items;
  packet.item_count = media->weapon_registry.item_count;
  packet.weapons = media->weapon_registry.weapons;
  packet.weapon_count = media->weapon_registry.weapon_count;
  packet.plasma_ball_shader = g->plasma_ball_shader;
  packet.red_flag_base_model = g->red_flag_base_model;
  packet.blue_flag_base_model = g->blue_flag_base_model;
  packet.neutral_flag_base_model = g->neutral_flag_base_model;
  packet.product = media->product;
  if (media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    packet.mission.weapon_hover_sound = media->sounds.weapon_hover_sound;
    packet.mission.obelisk_respawn_sound = media->sounds.obelisk_respawn_sound;
    packet.mission.blue_prox_mine = g->blue_prox_mine;
    packet.mission.overload_base_model = g->overload_base_model;
    packet.mission.overload_energy_model = g->overload_energy_model;
    packet.mission.overload_lights_model = g->overload_lights_model;
    packet.mission.overload_target_model = g->overload_target_model;
    packet.mission.harvester_model = g->harvester_model;
    packet.mission.harvester_neutral_model = g->harvester_neutral_model;
    packet.mission.harvester_red_skin = g->harvester_red_skin;
    packet.mission.harvester_blue_skin = g->harvester_blue_skin;
  }
  return packet;
}
qa_q3_pres_effect_media qa_q3_pres_client_media_effects(media_t *media) {
  const qa_q3_pres_client_media_graphics *g = &media->graphics;
  qa_q3_pres_effect_media effects;
  memset(&effects, 0, sizeof effects);
  effects.water_bubble_shader = g->water_bubble_shader;
  effects.smoke_puff_rage_pro_shader = g->smoke_puff_rage_pro_shader;
  effects.blood_explosion_shader = g->blood_explosion_shader;
  effects.teleport_effect_model = g->teleport_effect_model;
  effects.gib_skull = g->gib_skull;
  effects.gib_brain = g->gib_brain;
  effects.gib_abdomen = g->gib_abdomen;
  effects.gib_arm = g->gib_arm;
  effects.gib_chest = g->gib_chest;
  effects.gib_fist = g->gib_fist;
  effects.gib_foot = g->gib_foot;
  effects.gib_forearm = g->gib_forearm;
  effects.gib_intestine = g->gib_intestine;
  effects.gib_leg = g->gib_leg;
  effects.smoke2 = g->smoke2;
  effects.product = media->product;
  if (media->product == QA_Q3_BASE_PRODUCT_BASEQ3) {
    effects.teleport_effect_shader = g->teleport_effect_shader;
  } else {
    effects.mission.lightning_shader =
        media->weapon_registry.effects.lightning_shader(media->weapon_registry.effects.ctx);
    effects.mission.rocket_explosion_shader =
        media->weapon_registry.effects.rocket_explosion_shader(media->weapon_registry.effects.ctx);
    effects.mission.kamikaze_effect_model = g->kamikaze_effect_model;
    effects.mission.dish_flash_model = g->dish_flash_model;
    effects.mission.obelisk_hit_sounds[0] = media->sounds.obelisk_hit_sound1;
    effects.mission.obelisk_hit_sounds[1] = media->sounds.obelisk_hit_sound2;
    effects.mission.obelisk_hit_sounds[2] = media->sounds.obelisk_hit_sound3;
    effects.mission.invulnerability_impact_model = g->invulnerability_impact_model;
    effects.mission.invulnerability_impact_sounds[0] = media->sounds.invulnerability_impact_sound1;
    effects.mission.invulnerability_impact_sounds[1] = media->sounds.invulnerability_impact_sound2;
    effects.mission.invulnerability_impact_sounds[2] = media->sounds.invulnerability_impact_sound3;
    effects.mission.invulnerability_juiced_model = g->invulnerability_juiced_model;
    effects.mission.invulnerability_juiced_sound = media->sounds.invulnerability_juiced_sound;
  }
  return effects;
}
qa_particle_media qa_q3_pres_client_media_particles(media_t *media) {
  qa_particle_media particles;
  particles.tracer_shader = (const qa_particle_shader *)media->graphics.tracer_shader;
  particles.smoke_puff_shader = (const qa_particle_shader *)media->graphics.smoke_puff_shader;
  particles.water_bubble_shader = (const qa_particle_shader *)media->graphics.water_bubble_shader;
  return particles;
}
qa_q3_base_product qa_q3_pres_client_media_local_entities(media_t *media,
    qa_q3_pres_mission_local_entity_media *out) {
  const qa_q3_pres_client_media_graphics *g = &media->graphics;
  memset(out, 0, sizeof *out);
  out->base.blood_trail_shader = g->blood_trail_shader;
  out->base.blood_mark_shader = g->blood_mark_shader;
  out->base.burn_mark_shader = g->burn_mark_shader;
  memcpy(out->base.number_shaders, g->number_shaders, sizeof out->base.number_shaders);
  out->base.gib_bounce_sounds[0] = media->sounds.gib_bounce1_sound;
  out->base.gib_bounce_sounds[1] = media->sounds.gib_bounce2_sound;
  out->base.gib_bounce_sounds[2] = media->sounds.gib_bounce3_sound;
  if (media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
    out->kamikaze_shock_wave = g->kamikaze_shock_wave;
    out->kamikaze_explode_sound = media->sounds.kamikaze_explode_sound;
    out->kamikaze_implode_sound = media->sounds.kamikaze_implode_sound;
  }
  return media->product;
}
qa_q3_pres_weapon_presentation_media qa_q3_pres_client_media_weapons(media_t *media) {
  const qa_q3_pres_client_media_graphics *g = &media->graphics;
  const qa_q3_pres_client_media_sounds *s = &media->sounds;
  qa_q3_pres_weapon_presentation_media weapons;
  memset(&weapons, 0, sizeof weapons);
  weapons.models.machinegun_brass = g->machinegun_brass_model;
  weapons.models.shotgun_brass = g->shotgun_brass_model;
  weapons.models.dish_flash = g->dish_flash_model;
  weapons.models.ring_flash = g->ring_flash_model;
  weapons.models.bullet_flash = g->bullet_flash_model;
  weapons.shaders.smoke_puff = g->smoke_puff_shader;
  weapons.shaders.nail_puff = g->nail_puff_shader;
  weapons.shaders.shotgun_smoke_puff = g->shotgun_smoke_puff_shader;
  weapons.shaders.invis = g->invis_shader;
  weapons.shaders.battle_weapon = g->battle_weapon_shader;
  weapons.shaders.quad_weapon = g->quad_weapon_shader;
  weapons.shaders.select = g->select_shader;
  weapons.shaders.noammo = g->noammo_shader;
  weapons.shaders.hole_mark = g->hole_mark_shader;
  weapons.shaders.burn_mark = g->burn_mark_shader;
  weapons.shaders.energy_mark = g->energy_mark_shader;
  weapons.shaders.bullet_mark = g->bullet_mark_shader;
  weapons.shaders.tracer = g->tracer_shader;
  weapons.sounds.quad = s->quad_sound;
  weapons.sounds.nail_hit_flesh = s->sfx_nghitflesh;
  weapons.sounds.nail_hit_metal = s->sfx_nghitmetal;
  weapons.sounds.nail_hit = s->sfx_nghit;
  weapons.sounds.prox_explosion = s->sfx_proxexp;
  weapons.sounds.rocket_explosion = s->sfx_rockexp;
  weapons.sounds.plasma_explosion = s->sfx_plasmaexp;
  weapons.sounds.chaingun_hit_flesh = s->sfx_chghitflesh;
  weapons.sounds.chaingun_hit_metal = s->sfx_chghitmetal;
  weapons.sounds.chaingun_hit = s->sfx_chghit;
  weapons.sounds.ricochet1 = s->sfx_ric1;
  weapons.sounds.ricochet2 = s->sfx_ric2;
  weapons.sounds.ricochet3 = s->sfx_ric3;
  weapons.sounds.tracer = s->tracer_sound;
  return weapons;
}

static void validate(media_t *media, host_t *host) {
  if (media->static_state != host->static_state || media->product != host->state->product) {
    qa_range_error("Client media registration requires its canonical cgame state");
  }
}
static bool build_script(host_t *host) { return host->settings(host->ctx).build_script; }
static const char *item_bits(host_t *host) {
  const char *bits = STR(host->config_string(host->ctx, 27));
  if (strlen(bits) > 256) qa_range_error("CS_ITEMS exceeds source MAX_ITEMS precache buffer");
  return bits;
}

bool qa_q3_pres_register_item_sounds(media_t *media, double number) {
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(media->product, number);
  if (item->pickup_sound != NULL) {
    qa_pcm_sound *ignored = NULL;
    if (!media->sound_bank.register_sound(media->sound_bank.ctx, item->pickup_sound, false, &ignored)) {
      return false;
    }
  }
  const char *sounds = STR(item->sounds);
  size_t length = strlen(sounds);
  size_t offset = 0;
  while (offset < length) {
    size_t start = offset;
    while (offset < length && sounds[offset] != ' ') offset++;
    size_t len = offset - start;
    if (len >= 64 || len < 5) {
      qa_range_error("PrecacheItem: %s has bad precache string", STR(item->class_name));
    }
    char name[64];
    memcpy(name, sounds + start, len);
    name[len] = '\0';
    if (offset < length) offset++;
    if (len >= 3 && strcmp(name + len - 3, "wav") == 0) {
      qa_pcm_sound *ignored = NULL;
      if (!media->sound_bank.register_sound(media->sound_bank.ctx, name, false, &ignored)) return false;
    }
  }
  return true;
}

#define SHADER(field, name)                                                                            \
  do {                                                                                                 \
    if (!media->resources.register_shader(media->resources.ctx, (name),                               \
            (const qa_q3_pres_scene_shader **)&(field))) return false;                                 \
  } while (0)
#define SHADER_NOMIP(field, name)                                                                      \
  do {                                                                                                 \
    if (!media->resources.register_shader_no_mip(media->resources.ctx, (name),                        \
            (const qa_q3_pres_scene_shader **)&(field))) return false;                                 \
  } while (0)

bool qa_q3_pres_register_client_loading_graphics(media_t *media) {
  qa_q3_pres_client_media_graphics *graphics = &media->graphics;
  SHADER(graphics->charset_shader, "gfx/2d/bigchars");
  SHADER(graphics->white_shader, "white");
  SHADER_NOMIP(graphics->charset_prop, "menu/art/font1_prop.tga");
  SHADER_NOMIP(graphics->charset_prop_glow, "menu/art/font1_prop_glo.tga");
  SHADER_NOMIP(graphics->charset_prop_b, "menu/art/font2_prop.tga");
  return true;
}

bool qa_q3_pres_register_client_sounds(media_t *media, host_t *host) {
  validate(media, host);
  const bool mission = media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  const qa_q3_base_game_type game_type = media->static_state->game_type;
  if (mission && !host->load_voice_chats(host->commands_ctx)) return false;
  qa_q3_pres_client_media_sounds *sounds = &media->sounds;
  #define SOUND(field, path, compressed)                                                               \
    do {                                                                                               \
      if (!media->sound_bank.register_sound(media->sound_bank.ctx, (path), (compressed), &(field)))   \
        return false;                                                                                  \
    } while (0)
  #define DISCARD_SOUND(path, compressed)                                                              \
    do {                                                                                               \
      qa_pcm_sound *ignored = NULL;                                                                    \
      if (!media->sound_bank.register_sound(media->sound_bank.ctx, (path), (compressed), &ignored))   \
        return false;                                                                                  \
    } while (0)
if (mission) {
  }
  SOUND(sounds->one_minute_sound, "sound/feedback/1_minute.wav", true);
  SOUND(sounds->five_minute_sound, "sound/feedback/5_minute.wav", true);
  SOUND(sounds->sudden_death_sound, "sound/feedback/sudden_death.wav", true);
  SOUND(sounds->one_frag_sound, "sound/feedback/1_frag.wav", true);
  SOUND(sounds->two_frag_sound, "sound/feedback/2_frags.wav", true);
  SOUND(sounds->three_frag_sound, "sound/feedback/3_frags.wav", true);
  SOUND(sounds->count3_sound, "sound/feedback/three.wav", true);
  SOUND(sounds->count2_sound, "sound/feedback/two.wav", true);
  SOUND(sounds->count1_sound, "sound/feedback/one.wav", true);
  SOUND(sounds->count_fight_sound, "sound/feedback/fight.wav", true);
  SOUND(sounds->count_prepare_sound, "sound/feedback/prepare.wav", true);
  if (mission) {
  SOUND(sounds->count_prepare_team_sound, "sound/feedback/prepare_team.wav", true);
  }
  if (game_type >= QA_Q3_BASE_GT_TEAM || build_script(host)) {
  SOUND(sounds->capture_award_sound, "sound/teamplay/flagcapture_yourteam.wav", true);
  SOUND(sounds->red_leads_sound, "sound/feedback/redleads.wav", true);
  SOUND(sounds->blue_leads_sound, "sound/feedback/blueleads.wav", true);
  SOUND(sounds->teams_tied_sound, "sound/feedback/teamstied.wav", true);
  SOUND(sounds->hit_team_sound, "sound/feedback/hit_teammate.wav", true);
  SOUND(sounds->red_scored_sound, "sound/teamplay/voc_red_scores.wav", true);
  SOUND(sounds->blue_scored_sound, "sound/teamplay/voc_blue_scores.wav", true);
  SOUND(sounds->capture_your_team_sound, "sound/teamplay/flagcapture_yourteam.wav", true);
  SOUND(sounds->capture_opponent_sound, "sound/teamplay/flagcapture_opponent.wav", true);
  SOUND(sounds->return_your_team_sound, "sound/teamplay/flagreturn_yourteam.wav", true);
  SOUND(sounds->return_opponent_sound, "sound/teamplay/flagreturn_opponent.wav", true);
  SOUND(sounds->taken_your_team_sound, "sound/teamplay/flagtaken_yourteam.wav", true);
  SOUND(sounds->taken_opponent_sound, "sound/teamplay/flagtaken_opponent.wav", true);
  if (game_type == QA_Q3_BASE_GT_CTF || build_script(host)) {
  SOUND(sounds->red_flag_returned_sound, "sound/teamplay/voc_red_returned.wav", true);
  SOUND(sounds->blue_flag_returned_sound, "sound/teamplay/voc_blue_returned.wav", true);
  SOUND(sounds->enemy_took_your_flag_sound, "sound/teamplay/voc_enemy_flag.wav", true);
  SOUND(sounds->your_team_took_enemy_flag_sound, "sound/teamplay/voc_team_flag.wav", true);
  }
  if (mission) {
  if (game_type == QA_Q3_BASE_GT_1FCTF || build_script(host)) {
  SOUND(sounds->neutral_flag_returned_sound, "sound/teamplay/flagreturn_opponent.wav", true);
  SOUND(sounds->your_team_took_the_flag_sound, "sound/teamplay/voc_team_1flag.wav", true);
  SOUND(sounds->enemy_took_the_flag_sound, "sound/teamplay/voc_enemy_1flag.wav", true);
  }
  if (game_type == QA_Q3_BASE_GT_1FCTF || game_type == QA_Q3_BASE_GT_CTF || build_script(host)) {
  SOUND(sounds->you_have_flag_sound, "sound/teamplay/voc_you_flag.wav", true);
  SOUND(sounds->holy_shit_sound, "sound/feedback/voc_holyshit.wav", true);
  }
  if (game_type == QA_Q3_BASE_GT_OBELISK || build_script(host)) {
  SOUND(sounds->your_base_is_under_attack_sound, "sound/teamplay/voc_base_attack.wav", true);
  }
  } else {
  SOUND(sounds->you_have_flag_sound, "sound/teamplay/voc_you_flag.wav", true);
  SOUND(sounds->holy_shit_sound, "sound/feedback/voc_holyshit.wav", true);
  SOUND(sounds->neutral_flag_returned_sound, "sound/teamplay/flagreturn_opponent.wav", true);
  SOUND(sounds->your_team_took_the_flag_sound, "sound/teamplay/voc_team_1flag.wav", true);
  SOUND(sounds->enemy_took_the_flag_sound, "sound/teamplay/voc_enemy_1flag.wav", true);
  }
  }
  SOUND(sounds->tracer_sound, "sound/weapons/machinegun/buletby1.wav", false);
  SOUND(sounds->select_sound, qa_q3_character_sounds.select_sound, false);
  SOUND(sounds->wear_off_sound, "sound/items/wearoff.wav", false);
  SOUND(sounds->use_nothing_sound, "sound/items/use_nothing.wav", false);
  SOUND(sounds->gib_sound, qa_q3_character_sounds.gib_sound, false);
  SOUND(sounds->gib_bounce1_sound, "sound/player/gibimp1.wav", false);
  SOUND(sounds->gib_bounce2_sound, "sound/player/gibimp2.wav", false);
  SOUND(sounds->gib_bounce3_sound, "sound/player/gibimp3.wav", false);
  if (mission) {
  SOUND(sounds->use_invulnerability_sound, "sound/items/invul_activate.wav", false);
  SOUND(sounds->invulnerability_impact_sound1, "sound/items/invul_impact_01.wav", false);
  SOUND(sounds->invulnerability_impact_sound2, "sound/items/invul_impact_02.wav", false);
  SOUND(sounds->invulnerability_impact_sound3, "sound/items/invul_impact_03.wav", false);
  SOUND(sounds->invulnerability_juiced_sound, "sound/items/invul_juiced.wav", false);
  SOUND(sounds->obelisk_hit_sound1, "sound/items/obelisk_hit_01.wav", false);
  SOUND(sounds->obelisk_hit_sound2, "sound/items/obelisk_hit_02.wav", false);
  SOUND(sounds->obelisk_hit_sound3, "sound/items/obelisk_hit_03.wav", false);
  SOUND(sounds->obelisk_respawn_sound, "sound/items/obelisk_respawn.wav", false);
  SOUND(sounds->ammoregen_sound, "sound/items/cl_ammoregen.wav", false);
  SOUND(sounds->doubler_sound, "sound/items/cl_doubler.wav", false);
  SOUND(sounds->guard_sound, "sound/items/cl_guard.wav", false);
  SOUND(sounds->scout_sound, "sound/items/cl_scout.wav", false);
  }
  SOUND(sounds->tele_in_sound, qa_q3_character_sounds.tele_in_sound, false);
  SOUND(sounds->tele_out_sound, qa_q3_character_sounds.tele_out_sound, false);
  SOUND(sounds->respawn_sound, qa_q3_character_sounds.respawn_sound, false);
  SOUND(sounds->no_ammo_sound, "sound/weapons/noammo.wav", false);
  SOUND(sounds->talk_sound, "sound/player/talk.wav", false);
  SOUND(sounds->land_sound, qa_q3_character_sounds.land_sound, false);
  SOUND(sounds->hit_sound, "sound/feedback/hit.wav", false);
  if (mission) {
  SOUND(sounds->hit_sound_high_armor, "sound/feedback/hithi.wav", false);
  SOUND(sounds->hit_sound_low_armor, "sound/feedback/hitlo.wav", false);
  }
  SOUND(sounds->impressive_sound, "sound/feedback/impressive.wav", true);
  SOUND(sounds->excellent_sound, "sound/feedback/excellent.wav", true);
  SOUND(sounds->denied_sound, "sound/feedback/denied.wav", true);
  SOUND(sounds->humiliation_sound, "sound/feedback/humiliation.wav", true);
  SOUND(sounds->assist_sound, "sound/feedback/assist.wav", true);
  SOUND(sounds->defend_sound, "sound/feedback/defense.wav", true);
  if (mission) {
  SOUND(sounds->first_impressive_sound, "sound/feedback/first_impressive.wav", true);
  SOUND(sounds->first_excellent_sound, "sound/feedback/first_excellent.wav", true);
  SOUND(sounds->first_humiliation_sound, "sound/feedback/first_gauntlet.wav", true);
  }
  SOUND(sounds->taken_lead_sound, "sound/feedback/takenlead.wav", true);
  SOUND(sounds->tied_lead_sound, "sound/feedback/tiedlead.wav", true);
  SOUND(sounds->lost_lead_sound, "sound/feedback/lostlead.wav", true);
  if (mission) {
  SOUND(sounds->vote_now, "sound/feedback/vote_now.wav", true);
  SOUND(sounds->vote_passed, "sound/feedback/vote_passed.wav", true);
  SOUND(sounds->vote_failed, "sound/feedback/vote_failed.wav", true);
  }
  SOUND(sounds->watr_in_sound, qa_q3_character_sounds.watr_in_sound, false);
  SOUND(sounds->watr_out_sound, qa_q3_character_sounds.watr_out_sound, false);
  SOUND(sounds->watr_un_sound, qa_q3_character_sounds.watr_un_sound, false);
  SOUND(sounds->jump_pad_sound, qa_q3_character_sounds.jump_pad_sound, false);
  for (int i = 0; i < 4; i++) {
    for (size_t footstep = 0; footstep < qa_q3_footstep_path_count; footstep++) {
      char path[96];
      snprintf(path, sizeof path, "sound/player/footsteps/%s%d.wav", qa_q3_footstep_paths[footstep].path, i + 1);
      if (!media->sound_bank.register_sound(media->sound_bank.ctx, path, false,
              &media->footsteps[footstep][i])) return false;
    }
  }
  /* Source copies CS_ITEMS, but its sound filtering condition is commented out. */
  (void)item_bits(host);
  {
    size_t item_count = 0;
    (void)qa_q3_base_item_list(media->product, &item_count);
    for (size_t i = 1; i < item_count; i++) {
      if (!qa_q3_pres_register_item_sounds(media, (double)i)) return false;
    }
  }
  for (int i = 1; i < 256; i++) {
    const char *sound_name = STR(host->config_string(host->ctx, 288 + i));
    if (sound_name[0] == '\0') break;
    if (sound_name[0] == '*') continue;
    if (!media->sound_bank.register_sound(media->sound_bank.ctx, sound_name, false,
            &media->static_state->game_sounds[i])) return false;
  }
  SOUND(sounds->flight_sound, "sound/items/flight.wav", false);
  SOUND(sounds->medkit_sound, "sound/items/use_medkit.wav", false);
  SOUND(sounds->quad_sound, "sound/items/damage3.wav", false);
  SOUND(sounds->sfx_ric1, "sound/weapons/machinegun/ric1.wav", false);
  SOUND(sounds->sfx_ric2, "sound/weapons/machinegun/ric2.wav", false);
  SOUND(sounds->sfx_ric3, "sound/weapons/machinegun/ric3.wav", false);
  SOUND(sounds->sfx_railg, "sound/weapons/railgun/railgf1a.wav", false);
  SOUND(sounds->sfx_rockexp, "sound/weapons/rocket/rocklx1a.wav", false);
  SOUND(sounds->sfx_plasmaexp, "sound/weapons/plasma/plasmx1a.wav", false);
  if (mission) {
  SOUND(sounds->sfx_proxexp, "sound/weapons/proxmine/wstbexpl.wav", false);
  SOUND(sounds->sfx_nghit, "sound/weapons/nailgun/wnalimpd.wav", false);
  SOUND(sounds->sfx_nghitflesh, "sound/weapons/nailgun/wnalimpl.wav", false);
  SOUND(sounds->sfx_nghitmetal, "sound/weapons/nailgun/wnalimpm.wav", false);
  SOUND(sounds->sfx_chghit, "sound/weapons/vulcan/wvulimpd.wav", false);
  SOUND(sounds->sfx_chghitflesh, "sound/weapons/vulcan/wvulimpl.wav", false);
  SOUND(sounds->sfx_chghitmetal, "sound/weapons/vulcan/wvulimpm.wav", false);
  SOUND(sounds->weapon_hover_sound, "sound/weapons/weapon_hover.wav", false);
  SOUND(sounds->kamikaze_explode_sound, "sound/items/kam_explode.wav", false);
  SOUND(sounds->kamikaze_implode_sound, "sound/items/kam_implode.wav", false);
  SOUND(sounds->kamikaze_far_sound, "sound/items/kam_explode_far.wav", false);
  SOUND(sounds->winner_sound, "sound/feedback/voc_youwin.wav", false);
  SOUND(sounds->loser_sound, "sound/feedback/voc_youlose.wav", false);
  SOUND(sounds->you_suck_sound, "sound/misc/yousuck.wav", false);
  SOUND(sounds->wstbimpl_sound, "sound/weapons/proxmine/wstbimpl.wav", false);
  SOUND(sounds->wstbimpm_sound, "sound/weapons/proxmine/wstbimpm.wav", false);
  SOUND(sounds->wstbimpd_sound, "sound/weapons/proxmine/wstbimpd.wav", false);
  SOUND(sounds->wstbactv_sound, "sound/weapons/proxmine/wstbactv.wav", false);
  }
  SOUND(sounds->regen_sound, "sound/items/regen.wav", false);
  SOUND(sounds->protect_sound, "sound/items/protect3.wav", false);
  SOUND(sounds->n_health_sound, "sound/items/n_health.wav", false);
  SOUND(sounds->hgrenb1a_sound, "sound/weapons/grenade/hgrenb1a.wav", false);
  SOUND(sounds->hgrenb2a_sound, "sound/weapons/grenade/hgrenb2a.wav", false);
  if (mission) {
  DISCARD_SOUND("sound/player/james/death1.wav", false);
  DISCARD_SOUND("sound/player/james/death2.wav", false);
  DISCARD_SOUND("sound/player/james/death3.wav", false);
  DISCARD_SOUND("sound/player/james/jump1.wav", false);
  DISCARD_SOUND("sound/player/james/pain25_1.wav", false);
  DISCARD_SOUND("sound/player/james/pain75_1.wav", false);
  DISCARD_SOUND("sound/player/james/pain100_1.wav", false);
  DISCARD_SOUND("sound/player/james/falling1.wav", false);
  DISCARD_SOUND("sound/player/james/gasp.wav", false);
  DISCARD_SOUND("sound/player/james/drown.wav", false);
  DISCARD_SOUND("sound/player/james/fall1.wav", false);
  DISCARD_SOUND("sound/player/james/taunt.wav", false);
  DISCARD_SOUND("sound/player/janet/death1.wav", false);
  DISCARD_SOUND("sound/player/janet/death2.wav", false);
  DISCARD_SOUND("sound/player/janet/death3.wav", false);
  DISCARD_SOUND("sound/player/janet/jump1.wav", false);
  DISCARD_SOUND("sound/player/janet/pain25_1.wav", false);
  DISCARD_SOUND("sound/player/janet/pain75_1.wav", false);
  DISCARD_SOUND("sound/player/janet/pain100_1.wav", false);
  DISCARD_SOUND("sound/player/janet/falling1.wav", false);
  DISCARD_SOUND("sound/player/janet/gasp.wav", false);
  DISCARD_SOUND("sound/player/janet/drown.wav", false);
  DISCARD_SOUND("sound/player/janet/fall1.wav", false);
  DISCARD_SOUND("sound/player/janet/taunt.wav", false);
  }
  #undef SOUND
  #undef DISCARD_SOUND
  return true;
}

static const qa_particle_shader *particle_register_shader(void *ctx, const char *name) {
  media_t *media = ctx;
  const qa_q3_pres_scene_shader *shader = NULL;
  if (!media->resources.register_shader(media->resources.ctx, name, &shader)) return NULL;
  return (const qa_particle_shader *)shader;
}

bool qa_q3_pres_register_client_graphics(media_t *media, host_t *host,
    qa_q3_pres_registered_client_graphics *out) {
  validate(media, host);
  const bool mission = media->product == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  const qa_q3_base_game_type game_type = media->static_state->game_type;
  (void)game_type;
  (void)mission;
  host->state->refdef = qa_q3_pres_create_refdef();
  host->clear_scene(host->ctx);
  host->loading_string(host->ctx, STR(media->static_state->mapname));
  qa_q3_pres_world_scene world;
  if (!media->resources.load_world(media->resources.ctx, STR(media->static_state->mapname), &world)) {
    return false;
  }
  host->loading_string(host->ctx, "game media");
  qa_q3_pres_client_media_graphics *graphics = &media->graphics;
  #define MODEL(field, path)                                                                           \
    do {                                                                                               \
      if (!media->resources.register_model(media->resources.ctx, (path), &(field))) return false;      \
    } while (0)
  #define SKIN(field, path)                                                                            \
    do {                                                                                               \
      if (!media->resources.register_skin(media->resources.ctx, (path),                               \
              (const qa_q3_pres_scene_skin **)&(field))) return false;                                 \
    } while (0)
  #define DISCARD_MODEL(path)                                                                          \
    do {                                                                                               \
      qa_q3_pres_scene_model ignored;                                                                  \
      if (!media->resources.register_model(media->resources.ctx, (path), &ignored)) return false;      \
    } while (0)
host->state->refdef = qa_q3_pres_create_refdef();
  {
    static const char *const number_names[] = {"zero", "one", "two", "three", "four", "five",
                                               "six", "seven", "eight", "nine", "minus"};
    for (size_t i = 0; i < 11; i++) {
      char path[64];
      snprintf(path, sizeof path, "gfx/2d/numbers/%s_32b", number_names[i]);
      SHADER(graphics->number_shaders[i], path);
    }
  }
  SHADER(graphics->bot_skill_shaders[0], "menu/art/skill1.tga");
  SHADER(graphics->bot_skill_shaders[1], "menu/art/skill2.tga");
  SHADER(graphics->bot_skill_shaders[2], "menu/art/skill3.tga");
  SHADER(graphics->bot_skill_shaders[3], "menu/art/skill4.tga");
  SHADER(graphics->bot_skill_shaders[4], "menu/art/skill5.tga");
  SHADER(graphics->view_blood_shader, "viewBloodBlend");
  SHADER_NOMIP(graphics->defer_shader, "gfx/2d/defer.tga");
  SHADER_NOMIP(graphics->scoreboard_name, "menu/tab/name.tga");
  SHADER_NOMIP(graphics->scoreboard_ping, "menu/tab/ping.tga");
  SHADER_NOMIP(graphics->scoreboard_score, "menu/tab/score.tga");
  SHADER_NOMIP(graphics->scoreboard_time, "menu/tab/time.tga");
  SHADER(graphics->smoke_puff_shader, "smokePuff");
  SHADER(graphics->smoke_puff_rage_pro_shader, "smokePuffRagePro");
  SHADER(graphics->shotgun_smoke_puff_shader, "shotgunSmokePuff");
  if (mission) {
  SHADER(graphics->nail_puff_shader, "nailtrail");
  MODEL(graphics->blue_prox_mine, "models/weaphits/proxmineb.md3");
  }
  SHADER(graphics->plasma_ball_shader, "sprites/plasma1");
  SHADER(graphics->blood_trail_shader, "bloodTrail");
  SHADER(graphics->lagometer_shader, "lagometer");
  SHADER(graphics->connection_shader, "disconnected");
  SHADER(graphics->water_bubble_shader, "waterBubble");
  SHADER(graphics->tracer_shader, "gfx/misc/tracer");
  SHADER(graphics->select_shader, "gfx/2d/select");
  for (int i = 0; i < 10; i++) {
    char path[32];
    snprintf(path, sizeof path, "gfx/2d/crosshair%c", 'a' + i);
    SHADER(graphics->crosshair_shader[i], path);
  }
  SHADER(graphics->back_tile_shader, "gfx/2d/backtile");
  SHADER(graphics->noammo_shader, "icons/noammo");
  SHADER(graphics->quad_shader, "powerups/quad");
  SHADER(graphics->quad_weapon_shader, "powerups/quadWeapon");
  SHADER(graphics->battle_suit_shader, "powerups/battleSuit");
  SHADER(graphics->battle_weapon_shader, "powerups/battleWeapon");
  SHADER(graphics->invis_shader, "powerups/invisibility");
  SHADER(graphics->regen_shader, "powerups/regen");
  SHADER(graphics->haste_puff_shader, "hasteSmokePuff");
  if (game_type == QA_Q3_BASE_GT_CTF || (mission && (game_type == QA_Q3_BASE_GT_1FCTF || game_type == QA_Q3_BASE_GT_HARVESTER)) || build_script(host)) {
  MODEL(graphics->red_cube_model, "models/powerups/orb/r_orb.md3");
  MODEL(graphics->blue_cube_model, "models/powerups/orb/b_orb.md3");
  SHADER(graphics->red_cube_icon, "icons/skull_red");
  SHADER(graphics->blue_cube_icon, "icons/skull_blue");
  }
  if (game_type == QA_Q3_BASE_GT_CTF || (mission && (game_type == QA_Q3_BASE_GT_1FCTF || game_type == QA_Q3_BASE_GT_HARVESTER)) || build_script(host)) {
  MODEL(graphics->red_flag_model, "models/flags/r_flag.md3");
  MODEL(graphics->blue_flag_model, "models/flags/b_flag.md3");
  SHADER_NOMIP(graphics->red_flag_shader[0], "icons/iconf_red1");
  SHADER_NOMIP(graphics->red_flag_shader[1], "icons/iconf_red2");
  SHADER_NOMIP(graphics->red_flag_shader[2], "icons/iconf_red3");
  SHADER_NOMIP(graphics->blue_flag_shader[0], "icons/iconf_blu1");
  SHADER_NOMIP(graphics->blue_flag_shader[1], "icons/iconf_blu2");
  SHADER_NOMIP(graphics->blue_flag_shader[2], "icons/iconf_blu3");
  if (mission) {
  MODEL(graphics->flag_pole_model, "models/flag2/flagpole.md3");
  MODEL(graphics->flag_flap_model, "models/flag2/flagflap3.md3");
  SKIN(graphics->red_flag_flap_skin, "models/flag2/red.skin");
  SKIN(graphics->blue_flag_flap_skin, "models/flag2/blue.skin");
  SKIN(graphics->neutral_flag_flap_skin, "models/flag2/white.skin");
  MODEL(graphics->red_flag_base_model, "models/mapobjects/flagbase/red_base.md3");
  MODEL(graphics->blue_flag_base_model, "models/mapobjects/flagbase/blue_base.md3");
  MODEL(graphics->neutral_flag_base_model, "models/mapobjects/flagbase/ntrl_base.md3");
  }
  }
  if (mission) {
  if (game_type == QA_Q3_BASE_GT_1FCTF || build_script(host)) {
  MODEL(graphics->neutral_flag_model, "models/flags/n_flag.md3");
  SHADER_NOMIP(graphics->flag_shader[0], "icons/iconf_neutral1");
  SHADER_NOMIP(graphics->flag_shader[1], "icons/iconf_red2");
  SHADER_NOMIP(graphics->flag_shader[2], "icons/iconf_blu2");
  SHADER_NOMIP(graphics->flag_shader[3], "icons/iconf_neutral3");
  }
  if (game_type == QA_Q3_BASE_GT_OBELISK || build_script(host)) {
  MODEL(graphics->overload_base_model, "models/powerups/overload_base.md3");
  MODEL(graphics->overload_target_model, "models/powerups/overload_target.md3");
  MODEL(graphics->overload_lights_model, "models/powerups/overload_lights.md3");
  MODEL(graphics->overload_energy_model, "models/powerups/overload_energy.md3");
  }
  if (game_type == QA_Q3_BASE_GT_HARVESTER || build_script(host)) {
  MODEL(graphics->harvester_model, "models/powerups/harvester/harvester.md3");
  SKIN(graphics->harvester_red_skin, "models/powerups/harvester/red.skin");
  SKIN(graphics->harvester_blue_skin, "models/powerups/harvester/blue.skin");
  MODEL(graphics->harvester_neutral_model, "models/powerups/obelisk/obelisk.md3");
  }
  SHADER(graphics->red_kamikaze_shader, "models/weaphits/kamikred");
  SHADER(graphics->dust_puff_shader, "hasteSmokePuff");
  }
  if (game_type >= QA_Q3_BASE_GT_TEAM || build_script(host)) {
  SHADER(graphics->friend_shader, "sprites/foe");
  SHADER(graphics->red_quad_shader, "powerups/blueflag");
  SHADER(graphics->team_status_bar, "gfx/2d/colorbar.tga");
  if (mission) {
  SHADER(graphics->blue_kamikaze_shader, "models/weaphits/kamikblu");
  }
  }
  MODEL(graphics->armor_model, "models/powerups/armor/armor_yel.md3");
  SHADER_NOMIP(graphics->armor_icon, "icons/iconr_yellow");
  MODEL(graphics->machinegun_brass_model, "models/weapons2/shells/m_shell.md3");
  MODEL(graphics->shotgun_brass_model, "models/weapons2/shells/s_shell.md3");
  MODEL(graphics->gib_abdomen, "models/gibs/abdomen.md3");
  MODEL(graphics->gib_arm, "models/gibs/arm.md3");
  MODEL(graphics->gib_chest, "models/gibs/chest.md3");
  MODEL(graphics->gib_fist, "models/gibs/fist.md3");
  MODEL(graphics->gib_foot, "models/gibs/foot.md3");
  MODEL(graphics->gib_forearm, "models/gibs/forearm.md3");
  MODEL(graphics->gib_intestine, "models/gibs/intestine.md3");
  MODEL(graphics->gib_leg, "models/gibs/leg.md3");
  MODEL(graphics->gib_skull, "models/gibs/skull.md3");
  MODEL(graphics->gib_brain, "models/gibs/brain.md3");
  MODEL(graphics->smoke2, "models/weapons2/shells/s_shell.md3");
  SHADER(graphics->balloon_shader, "sprites/balloon3");
  SHADER(graphics->blood_explosion_shader, "bloodExplosion");
  MODEL(graphics->bullet_flash_model, "models/weaphits/bullet.md3");
  MODEL(graphics->ring_flash_model, "models/weaphits/ring02.md3");
  MODEL(graphics->dish_flash_model, "models/weaphits/boom01.md3");
  if (mission) {
  MODEL(graphics->teleport_effect_model, "models/powerups/pop.md3");
  } else {
  MODEL(graphics->teleport_effect_model, "models/misc/telep.md3");
  SHADER(graphics->teleport_effect_shader, "teleportEffect");
  }
  if (mission) {
  MODEL(graphics->kamikaze_effect_model, "models/weaphits/kamboom2.md3");
  MODEL(graphics->kamikaze_shock_wave, "models/weaphits/kamwave.md3");
  MODEL(graphics->kamikaze_head_model, "models/powerups/kamikazi.md3");
  MODEL(graphics->kamikaze_head_trail, "models/powerups/trailtest.md3");
  MODEL(graphics->guard_powerup_model, "models/powerups/guard_player.md3");
  MODEL(graphics->scout_powerup_model, "models/powerups/scout_player.md3");
  MODEL(graphics->doubler_powerup_model, "models/powerups/doubler_player.md3");
  MODEL(graphics->ammo_regen_powerup_model, "models/powerups/ammo_player.md3");
  MODEL(graphics->invulnerability_impact_model, "models/powerups/shield/impact.md3");
  MODEL(graphics->invulnerability_juiced_model, "models/powerups/shield/juicer.md3");
  MODEL(graphics->medkit_usage_model, "models/powerups/regen.md3");
  SHADER_NOMIP(graphics->heart_shader, "ui/assets/statusbar/selectedhealth.tga");
  }
  MODEL(graphics->invulnerability_powerup_model, "models/powerups/shield/shield.md3");
  SHADER_NOMIP(graphics->medal_impressive, "medal_impressive");
  SHADER_NOMIP(graphics->medal_excellent, "medal_excellent");
  SHADER_NOMIP(graphics->medal_gauntlet, "medal_gauntlet");
  SHADER_NOMIP(graphics->medal_defend, "medal_defend");
  SHADER_NOMIP(graphics->medal_assist, "medal_assist");
  SHADER_NOMIP(graphics->medal_capture, "medal_capture");
  {
    const char *items = item_bits(host);
    size_t item_length = strlen(items);
    size_t item_count = 0;
    (void)qa_q3_base_item_list(media->product, &item_count);
    for (size_t i = 1; i < item_count; i++) {
      if ((i < item_length && items[i] == '1') || build_script(host)) {
        host->loading_item(host->ctx, (double)i);
        if (!media->weapon_registry.register_item_visuals(media->weapon_registry.ctx, (double)i)) {
          return false;
        }
      }
    }
  }
  SHADER(graphics->bullet_mark_shader, "gfx/damage/bullet_mrk");
  SHADER(graphics->burn_mark_shader, "gfx/damage/burn_med_mrk");
  SHADER(graphics->hole_mark_shader, "gfx/damage/hole_lg_mrk");
  SHADER(graphics->energy_mark_shader, "gfx/damage/plasma_mrk");
  SHADER(graphics->shadow_mark_shader, "markShadow");
  SHADER(graphics->wake_mark_shader, "wake");
  SHADER(graphics->blood_mark_shader, "bloodMark");
  for (size_t i = 1; i < world.model_count; i++) {
    char path[32];
    snprintf(path, sizeof path, "*%zu", i);
    qa_q3_pres_scene_model model;
    if (!media->resources.register_model(media->resources.ctx, path, &model)) return false;
    qa_bounds bounds = qa_q3_pres_model_bounds(&model);
    if (media->inline_model_count == media->inline_model_capacity) {
      size_t capacity = media->inline_model_capacity * 2;
      qa_q3_pres_inline_model_entry *grown =
          realloc(media->inline_models, capacity * sizeof *grown);
      if (grown == NULL) return false;
      media->inline_models = grown;
      media->inline_model_capacity = capacity;
    }
    qa_q3_pres_inline_model_entry *entry = &media->inline_models[media->inline_model_count++];
    entry->model = model;
    entry->midpoint = qa_math_vec3(
        (double)(float)(bounds.min.x + (double)(float)((double)(float)0.5 * (double)(float)(bounds.max.x - bounds.min.x))),
        (double)(float)(bounds.min.y + (double)(float)((double)(float)0.5 * (double)(float)(bounds.max.y - bounds.min.y))),
        (double)(float)(bounds.min.z + (double)(float)((double)(float)0.5 * (double)(float)(bounds.max.z - bounds.min.z))));
  }
  for (int i = 1; i < 256; i++) {
    const char *model_name = STR(host->config_string(host->ctx, 32 + i));
    if (model_name[0] == '\0') break;
    if (!media->resources.register_model(media->resources.ctx, model_name,
            &media->static_state->game_models[i])) return false;
  }
  if (mission) {
    SHADER_NOMIP(graphics->patrol_shader, "ui/assets/statusbar/patrol.tga");
    SHADER_NOMIP(graphics->assault_shader, "ui/assets/statusbar/assault.tga");
    SHADER_NOMIP(graphics->camp_shader, "ui/assets/statusbar/camp.tga");
    SHADER_NOMIP(graphics->follow_shader, "ui/assets/statusbar/follow.tga");
    SHADER_NOMIP(graphics->defend_shader, "ui/assets/statusbar/defend.tga");
    SHADER_NOMIP(graphics->team_leader_shader, "ui/assets/statusbar/team_leader.tga");
    SHADER_NOMIP(graphics->retrieve_shader, "ui/assets/statusbar/retrieve.tga");
    SHADER_NOMIP(graphics->escort_shader, "ui/assets/statusbar/escort.tga");
    SHADER_NOMIP(graphics->cursor, "menu/art/3_cursor2");
    SHADER_NOMIP(graphics->size_cursor, "ui/assets/sizecursor.tga");
    SHADER_NOMIP(graphics->select_cursor, "ui/assets/selectcursor.tga");
    SHADER_NOMIP(graphics->flag_shaders[0], "ui/assets/statusbar/flag_in_base.tga");
    SHADER_NOMIP(graphics->flag_shaders[1], "ui/assets/statusbar/flag_capture.tga");
    SHADER_NOMIP(graphics->flag_shaders[2], "ui/assets/statusbar/flag_missing.tga");
    DISCARD_MODEL("models/players/james/lower.md3");
    DISCARD_MODEL("models/players/james/upper.md3");
    DISCARD_MODEL("models/players/heads/james/james.md3");
    DISCARD_MODEL("models/players/janet/lower.md3");
    DISCARD_MODEL("models/players/janet/upper.md3");
    DISCARD_MODEL("models/players/heads/janet/janet.md3");
  }
  #undef MODEL
  #undef SKIN
  #undef DISCARD_MODEL
  out->world = world;
  qa_particle_resources particle_resources = {media, particle_register_shader};
  return qa_load_particle_animations(&particle_resources, NULL, &out->particle_animations);
}

bool qa_q3_pres_register_clients(media_t *media, host_t *host) {
  validate(media, host);
  host->loading_client(host->ctx, host->state->client_num);
  if (!host->new_client_info(host->clients_ctx, host->state->client_num,
          STR(host->config_string(host->ctx, 544 + host->state->client_num)))) {
    return false;
  }
  for (int i = 0; i < 64; i++) {
    if (i == host->state->client_num) continue;
    const char *info = STR(host->config_string(host->ctx, 544 + i));
    if (info[0] == '\0') continue;
    host->loading_client(host->ctx, i);
    if (!host->new_client_info(host->clients_ctx, i, info)) return false;
  }
  return host->build_spectator_string(host->commands_ctx);
}
