/* Port of src/content/q3/presentation/media.ts. GPL-2.0-or-later.
 * Registration from id Software's code/cgame/cg_main.c. TS async
 * registrations are synchronous; register_* return false only on OOM.
 * ClientWeaponMediaRegistry + RegisteredWeaponEffects +
 * WeaponPresentationMedia (weapons.ts) and EffectMedia/MissionEffectMedia
 * (effects.ts) are the effects.h types; the weapon registry is supplied by the caller. ParticleMedia/ParticleAnimations are the landed
 * render/scene/particles types; SceneShader and qa_particle_shader are
 * layout-identical single-pointer records, so particle views cast. */
#ifndef QA_CONTENT_Q3_PRESENTATION_MEDIA_H
#define QA_CONTENT_Q3_PRESENTATION_MEDIA_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/entities.h"
#include "qa/content/q3/presentation/events.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/players.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/render/scene/particles/q3_system.h"
typedef struct qa_q3_pres_client_media_sounds {
  qa_pcm_sound *one_minute_sound, *five_minute_sound, *sudden_death_sound, *one_frag_sound; /* borrowed, nullable */
  qa_pcm_sound *two_frag_sound, *three_frag_sound, *count3_sound, *count2_sound; /* borrowed, nullable */
  qa_pcm_sound *count1_sound, *count_fight_sound, *count_prepare_sound, *count_prepare_team_sound; /* borrowed, nullable */
  qa_pcm_sound *capture_award_sound, *red_leads_sound, *blue_leads_sound, *teams_tied_sound; /* borrowed, nullable */
  qa_pcm_sound *hit_team_sound, *red_scored_sound, *blue_scored_sound, *capture_your_team_sound; /* borrowed, nullable */
  qa_pcm_sound *capture_opponent_sound, *return_your_team_sound, *return_opponent_sound, *taken_your_team_sound; /* borrowed, nullable */
  qa_pcm_sound *taken_opponent_sound, *red_flag_returned_sound, *blue_flag_returned_sound, *enemy_took_your_flag_sound; /* borrowed, nullable */
  qa_pcm_sound *your_team_took_enemy_flag_sound, *neutral_flag_returned_sound, *your_team_took_the_flag_sound, *enemy_took_the_flag_sound; /* borrowed, nullable */
  qa_pcm_sound *you_have_flag_sound, *holy_shit_sound, *your_base_is_under_attack_sound, *tracer_sound; /* borrowed, nullable */
  qa_pcm_sound *select_sound, *wear_off_sound, *use_nothing_sound, *gib_sound; /* borrowed, nullable */
  qa_pcm_sound *gib_bounce1_sound, *gib_bounce2_sound, *gib_bounce3_sound, *use_invulnerability_sound; /* borrowed, nullable */
  qa_pcm_sound *invulnerability_impact_sound1, *invulnerability_impact_sound2, *invulnerability_impact_sound3, *invulnerability_juiced_sound; /* borrowed, nullable */
  qa_pcm_sound *obelisk_hit_sound1, *obelisk_hit_sound2, *obelisk_hit_sound3, *obelisk_respawn_sound; /* borrowed, nullable */
  qa_pcm_sound *ammoregen_sound, *doubler_sound, *guard_sound, *scout_sound; /* borrowed, nullable */
  qa_pcm_sound *tele_in_sound, *tele_out_sound, *respawn_sound, *no_ammo_sound; /* borrowed, nullable */
  qa_pcm_sound *talk_sound, *land_sound, *hit_sound, *hit_sound_high_armor; /* borrowed, nullable */
  qa_pcm_sound *hit_sound_low_armor, *impressive_sound, *excellent_sound, *denied_sound; /* borrowed, nullable */
  qa_pcm_sound *humiliation_sound, *assist_sound, *defend_sound, *first_impressive_sound; /* borrowed, nullable */
  qa_pcm_sound *first_excellent_sound, *first_humiliation_sound, *taken_lead_sound, *tied_lead_sound; /* borrowed, nullable */
  qa_pcm_sound *lost_lead_sound, *vote_now, *vote_passed, *vote_failed; /* borrowed, nullable */
  qa_pcm_sound *watr_in_sound, *watr_out_sound, *watr_un_sound, *jump_pad_sound; /* borrowed, nullable */
  qa_pcm_sound *flight_sound, *medkit_sound, *quad_sound, *sfx_ric1; /* borrowed, nullable */
  qa_pcm_sound *sfx_ric2, *sfx_ric3, *sfx_railg, *sfx_rockexp; /* borrowed, nullable */
  qa_pcm_sound *sfx_plasmaexp, *sfx_proxexp, *sfx_nghit, *sfx_nghitflesh; /* borrowed, nullable */
  qa_pcm_sound *sfx_nghitmetal, *sfx_chghit, *sfx_chghitflesh, *sfx_chghitmetal; /* borrowed, nullable */
  qa_pcm_sound *weapon_hover_sound, *kamikaze_explode_sound, *kamikaze_implode_sound, *kamikaze_far_sound; /* borrowed, nullable */
  qa_pcm_sound *winner_sound, *loser_sound, *you_suck_sound, *wstbimpl_sound; /* borrowed, nullable */
  qa_pcm_sound *wstbimpm_sound, *wstbimpd_sound, *wstbactv_sound, *regen_sound; /* borrowed, nullable */
  qa_pcm_sound *protect_sound, *n_health_sound, *hgrenb1a_sound, *hgrenb2a_sound; /* borrowed, nullable */
} qa_q3_pres_client_media_sounds;
typedef struct qa_q3_pres_client_media_graphics {
  const qa_q3_pres_scene_shader *charset_shader; /* nullable */
  const qa_q3_pres_scene_shader *white_shader; /* nullable */
  const qa_q3_pres_scene_shader *charset_prop; /* nullable */
  const qa_q3_pres_scene_shader *charset_prop_glow; /* nullable */
  const qa_q3_pres_scene_shader *charset_prop_b; /* nullable */
  const qa_q3_pres_scene_shader *number_shaders[11]; /* nullable */
  const qa_q3_pres_scene_shader *bot_skill_shaders[5]; /* nullable */
  const qa_q3_pres_scene_shader *view_blood_shader; /* nullable */
  const qa_q3_pres_scene_shader *defer_shader; /* nullable */
  const qa_q3_pres_scene_shader *scoreboard_name; /* nullable */
  const qa_q3_pres_scene_shader *scoreboard_ping; /* nullable */
  const qa_q3_pres_scene_shader *scoreboard_score; /* nullable */
  const qa_q3_pres_scene_shader *scoreboard_time; /* nullable */
  const qa_q3_pres_scene_shader *smoke_puff_shader; /* nullable */
  const qa_q3_pres_scene_shader *smoke_puff_rage_pro_shader; /* nullable */
  const qa_q3_pres_scene_shader *shotgun_smoke_puff_shader; /* nullable */
  const qa_q3_pres_scene_shader *nail_puff_shader; /* nullable */
  qa_q3_pres_scene_model blue_prox_mine;
  const qa_q3_pres_scene_shader *plasma_ball_shader; /* nullable */
  const qa_q3_pres_scene_shader *blood_trail_shader; /* nullable */
  const qa_q3_pres_scene_shader *lagometer_shader; /* nullable */
  const qa_q3_pres_scene_shader *connection_shader; /* nullable */
  const qa_q3_pres_scene_shader *water_bubble_shader; /* nullable */
  const qa_q3_pres_scene_shader *tracer_shader; /* nullable */
  const qa_q3_pres_scene_shader *select_shader; /* nullable */
  const qa_q3_pres_scene_shader *crosshair_shader[10]; /* nullable */
  const qa_q3_pres_scene_shader *back_tile_shader; /* nullable */
  const qa_q3_pres_scene_shader *noammo_shader; /* nullable */
  const qa_q3_pres_scene_shader *quad_shader; /* nullable */
  const qa_q3_pres_scene_shader *quad_weapon_shader; /* nullable */
  const qa_q3_pres_scene_shader *battle_suit_shader; /* nullable */
  const qa_q3_pres_scene_shader *battle_weapon_shader; /* nullable */
  const qa_q3_pres_scene_shader *invis_shader; /* nullable */
  const qa_q3_pres_scene_shader *regen_shader; /* nullable */
  const qa_q3_pres_scene_shader *haste_puff_shader; /* nullable */
  qa_q3_pres_scene_model red_cube_model;
  qa_q3_pres_scene_model blue_cube_model;
  const qa_q3_pres_scene_shader *red_cube_icon; /* nullable */
  const qa_q3_pres_scene_shader *blue_cube_icon; /* nullable */
  qa_q3_pres_scene_model red_flag_model;
  qa_q3_pres_scene_model blue_flag_model;
  const qa_q3_pres_scene_shader *red_flag_shader[3]; /* nullable */
  const qa_q3_pres_scene_shader *blue_flag_shader[3]; /* nullable */
  qa_q3_pres_scene_model flag_pole_model;
  qa_q3_pres_scene_model flag_flap_model;
  const qa_q3_pres_scene_skin *red_flag_flap_skin; /* nullable */
  const qa_q3_pres_scene_skin *blue_flag_flap_skin; /* nullable */
  const qa_q3_pres_scene_skin *neutral_flag_flap_skin; /* nullable */
  qa_q3_pres_scene_model red_flag_base_model;
  qa_q3_pres_scene_model blue_flag_base_model;
  qa_q3_pres_scene_model neutral_flag_base_model;
  qa_q3_pres_scene_model neutral_flag_model;
  const qa_q3_pres_scene_shader *flag_shader[4]; /* nullable */
  qa_q3_pres_scene_model overload_base_model;
  qa_q3_pres_scene_model overload_target_model;
  qa_q3_pres_scene_model overload_lights_model;
  qa_q3_pres_scene_model overload_energy_model;
  qa_q3_pres_scene_model harvester_model;
  const qa_q3_pres_scene_skin *harvester_red_skin; /* nullable */
  const qa_q3_pres_scene_skin *harvester_blue_skin; /* nullable */
  qa_q3_pres_scene_model harvester_neutral_model;
  const qa_q3_pres_scene_shader *red_kamikaze_shader; /* nullable */
  const qa_q3_pres_scene_shader *dust_puff_shader; /* nullable */
  const qa_q3_pres_scene_shader *friend_shader; /* nullable */
  const qa_q3_pres_scene_shader *red_quad_shader; /* nullable */
  const qa_q3_pres_scene_shader *team_status_bar; /* nullable */
  const qa_q3_pres_scene_shader *blue_kamikaze_shader; /* nullable */
  qa_q3_pres_scene_model armor_model;
  const qa_q3_pres_scene_shader *armor_icon; /* nullable */
  qa_q3_pres_scene_model machinegun_brass_model;
  qa_q3_pres_scene_model shotgun_brass_model;
  qa_q3_pres_scene_model gib_abdomen;
  qa_q3_pres_scene_model gib_arm;
  qa_q3_pres_scene_model gib_chest;
  qa_q3_pres_scene_model gib_fist;
  qa_q3_pres_scene_model gib_foot;
  qa_q3_pres_scene_model gib_forearm;
  qa_q3_pres_scene_model gib_intestine;
  qa_q3_pres_scene_model gib_leg;
  qa_q3_pres_scene_model gib_skull;
  qa_q3_pres_scene_model gib_brain;
  qa_q3_pres_scene_model smoke2;
  const qa_q3_pres_scene_shader *balloon_shader; /* nullable */
  const qa_q3_pres_scene_shader *blood_explosion_shader; /* nullable */
  qa_q3_pres_scene_model bullet_flash_model;
  qa_q3_pres_scene_model ring_flash_model;
  qa_q3_pres_scene_model dish_flash_model;
  qa_q3_pres_scene_model teleport_effect_model;
  const qa_q3_pres_scene_shader *teleport_effect_shader; /* nullable */
  qa_q3_pres_scene_model kamikaze_effect_model;
  qa_q3_pres_scene_model kamikaze_shock_wave;
  qa_q3_pres_scene_model kamikaze_head_model;
  qa_q3_pres_scene_model kamikaze_head_trail;
  qa_q3_pres_scene_model guard_powerup_model;
  qa_q3_pres_scene_model scout_powerup_model;
  qa_q3_pres_scene_model doubler_powerup_model;
  qa_q3_pres_scene_model ammo_regen_powerup_model;
  qa_q3_pres_scene_model invulnerability_impact_model;
  qa_q3_pres_scene_model invulnerability_juiced_model;
  qa_q3_pres_scene_model medkit_usage_model;
  const qa_q3_pres_scene_shader *heart_shader; /* nullable */
  qa_q3_pres_scene_model invulnerability_powerup_model;
  const qa_q3_pres_scene_shader *medal_impressive; /* nullable */
  const qa_q3_pres_scene_shader *medal_excellent; /* nullable */
  const qa_q3_pres_scene_shader *medal_gauntlet; /* nullable */
  const qa_q3_pres_scene_shader *medal_defend; /* nullable */
  const qa_q3_pres_scene_shader *medal_assist; /* nullable */
  const qa_q3_pres_scene_shader *medal_capture; /* nullable */
  const qa_q3_pres_scene_shader *bullet_mark_shader; /* nullable */
  const qa_q3_pres_scene_shader *burn_mark_shader; /* nullable */
  const qa_q3_pres_scene_shader *hole_mark_shader; /* nullable */
  const qa_q3_pres_scene_shader *energy_mark_shader; /* nullable */
  const qa_q3_pres_scene_shader *shadow_mark_shader; /* nullable */
  const qa_q3_pres_scene_shader *wake_mark_shader; /* nullable */
  const qa_q3_pres_scene_shader *blood_mark_shader; /* nullable */
  const qa_q3_pres_scene_shader *patrol_shader; /* nullable */
  const qa_q3_pres_scene_shader *assault_shader; /* nullable */
  const qa_q3_pres_scene_shader *camp_shader; /* nullable */
  const qa_q3_pres_scene_shader *follow_shader; /* nullable */
  const qa_q3_pres_scene_shader *defend_shader; /* nullable */
  const qa_q3_pres_scene_shader *team_leader_shader; /* nullable */
  const qa_q3_pres_scene_shader *retrieve_shader; /* nullable */
  const qa_q3_pres_scene_shader *escort_shader; /* nullable */
  const qa_q3_pres_scene_shader *cursor; /* nullable */
  const qa_q3_pres_scene_shader *size_cursor; /* nullable */
  const qa_q3_pres_scene_shader *select_cursor; /* nullable */
  const qa_q3_pres_scene_shader *flag_shaders[3]; /* nullable */
} qa_q3_pres_client_media_graphics;
typedef struct qa_q3_pres_client_media_settings { bool build_script; } qa_q3_pres_client_media_settings;
/* TS ClientMediaHost. clients is Pick<ClientInfoStore,
 * "newClientInfo"|"clientInfo">; commands is Pick<
 * ClientServerCommandRuntime, "loadVoiceChats"|"buildSpectatorString">.
 * loading_* are the (async in TS) progress notifications. */
typedef struct qa_q3_pres_client_media_host {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_client_game_static_state *static_state;
  void *clients_ctx;
  bool (*new_client_info)(void *ctx, double client_num, const char *config);
  qa_q3_pres_client_info *(*client_info)(void *ctx, double index);
  void *commands_ctx;
  bool (*load_voice_chats)(void *ctx);
  bool (*build_spectator_string)(void *ctx);
  void *ctx;
  const char *(*config_string)(void *ctx, double index);
  qa_q3_pres_client_media_settings (*settings)(void *ctx);
  void (*loading_string)(void *ctx, const char *text);
  void (*loading_item)(void *ctx, double index);
  void (*loading_client)(void *ctx, double index);
  void (*clear_scene)(void *ctx);
} qa_q3_pres_client_media_host;
typedef struct qa_q3_pres_registered_client_graphics {
  qa_q3_pres_world_scene world;
  qa_particle_animations particle_animations;
} qa_q3_pres_registered_client_graphics;
/* TS RegisteredWeaponEffects / ClientWeaponMediaRegistry /
 * EffectMedia / WeaponPresentationMedia (weapons.ts / effects.ts):
 * the landed weapons.h/effects.h types and views. */
/* TS ClientMedia. inline_models is an owned growing array seeded with
 * the default-model world entry. */
typedef struct qa_q3_pres_client_media {
  qa_q3_base_product product;
  qa_q3_pres_client_game_static_state *static_state;
  qa_q3_pres_renderer_resources resources;
  qa_q3_pres_client_sound_bank sound_bank;
  qa_q3_pres_client_weapon_media_registry_view weapon_registry;
  qa_q3_pres_client_media_sounds sounds;
  qa_q3_pres_client_media_graphics graphics;
  qa_pcm_sound *footsteps[QA_Q3_PRES_FOOTSTEP_KIND_COUNT][4]; /* borrowed, nullable */
  qa_q3_pres_inline_model_entry *inline_models; size_t inline_model_count, inline_model_capacity;
} qa_q3_pres_client_media;
/* Product mismatch aborts with the TS Error text. False only on OOM. */
bool qa_q3_pres_client_media_init(qa_q3_pres_client_media *media, qa_q3_base_product product,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_renderer_resources resources,
    qa_q3_pres_client_sound_bank sound_bank, qa_q3_pres_client_weapon_media_registry_view weapon_registry);
void qa_q3_pres_client_media_destroy(qa_q3_pres_client_media *media);
qa_q3_pres_client_event_media qa_q3_pres_client_media_events(qa_q3_pres_client_media *media);
qa_q3_pres_player_media qa_q3_pres_client_media_players(qa_q3_pres_client_media *media);
/* Aborts with the TS Error text in baseq3. */
qa_q3_pres_mission_player_media qa_q3_pres_client_media_mission_players(qa_q3_pres_client_media *media);
qa_q3_pres_packet_entity_media qa_q3_pres_client_media_packet(qa_q3_pres_client_media *media);
qa_q3_pres_effect_media qa_q3_pres_client_media_effects(qa_q3_pres_client_media *media);
qa_particle_media qa_q3_pres_client_media_particles(qa_q3_pres_client_media *media);
/* Fills the mission local-entity media (base fields always valid);
 * returns the product (base callers use out->base). */
qa_q3_base_product qa_q3_pres_client_media_local_entities(qa_q3_pres_client_media *media,
    qa_q3_pres_mission_local_entity_media *out);
qa_q3_pres_weapon_presentation_media qa_q3_pres_client_media_weapons(qa_q3_pres_client_media *media);
/* False only on OOM. */
bool qa_q3_pres_register_item_sounds(qa_q3_pres_client_media *media, double number);
bool qa_q3_pres_register_client_loading_graphics(qa_q3_pres_client_media *media);
bool qa_q3_pres_register_client_sounds(qa_q3_pres_client_media *media, qa_q3_pres_client_media_host *host);
bool qa_q3_pres_register_client_graphics(qa_q3_pres_client_media *media, qa_q3_pres_client_media_host *host,
    qa_q3_pres_registered_client_graphics *out);
bool qa_q3_pres_register_clients(qa_q3_pres_client_media *media, qa_q3_pres_client_media_host *host);
#endif
