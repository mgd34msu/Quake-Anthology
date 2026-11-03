/* Port of src/content/q3/base/game/save-values.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_values.h"
#include <stdlib.h>
#include <string.h>
static char *dupstr(const char *v) { if (!v) return NULL; size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
static void set_owned(char **dst, const char *v) { free(*dst); *dst = dupstr(v); }
qa_q3_entity_values qa_q3_capture_entity_values(qa_q3_game_entity *s) {
  qa_q3_entity_values v; v.spawnflags = s->spawnflags; v.never_free = s->never_free; v.flags = s->flags;
  v.model = dupstr(s->model); v.model2 = dupstr(s->model2); v.freetime = s->freetime; v.event_time = s->event_time;
  v.free_after_event = s->free_after_event; v.unlink_after_event = s->unlink_after_event; v.physics_object = s->physics_object;
  v.physics_bounce = s->physics_bounce; v.clipmask = s->clipmask; v.mover_state = s->mover_state;
  v.sound_pos1 = s->sound_pos1; v.sound_1to2 = s->sound_1to2; v.sound_2to1 = s->sound_2to1; v.sound_pos2 = s->sound_pos2; v.sound_loop = s->sound_loop;
  v.pos1 = s->pos1; v.pos2 = s->pos2; v.message = dupstr(s->message); v.timestamp = s->timestamp; v.angle = s->angle;
  v.target = dupstr(s->target); v.targetname = dupstr(s->targetname); v.team = dupstr(s->team);
  v.target_shader_name = dupstr(s->target_shader_name); v.target_shader_new_name = dupstr(s->target_shader_new_name);
  v.speed = s->speed; v.movedir = s->movedir; v.pain_debounce_time = s->pain_debounce_time;
  v.fly_sound_debounce_time = s->fly_sound_debounce_time; v.last_move_time = s->last_move_time;
  v.damage = s->damage; v.splash_damage = s->splash_damage; v.splash_radius = s->splash_radius;
  v.method_of_death = s->method_of_death; v.splash_method_of_death = s->splash_method_of_death; v.count = s->count;
  v.kamikaze_time = s->kamikaze_time; v.kamikaze_shock_time = s->kamikaze_shock_time; v.watertype = s->watertype;
  v.waterlevel = s->waterlevel; v.noise_index = s->noise_index; v.wait = s->wait; v.random = s->random; return v;
}
void qa_q3_entity_values_destroy(qa_q3_entity_values *v) {
  free(v->model); free(v->model2); free(v->message); free(v->target); free(v->targetname); free(v->team);
  free(v->target_shader_name); free(v->target_shader_new_name);
}
void qa_q3_restore_entity_values(qa_q3_game_entity *t, qa_q3_entity_values *s) {
  t->spawnflags = s->spawnflags; t->never_free = s->never_free; t->flags = s->flags;
  set_owned(&t->model, s->model); set_owned(&t->model2, s->model2); t->freetime = s->freetime; t->event_time = s->event_time;
  t->free_after_event = s->free_after_event; t->unlink_after_event = s->unlink_after_event; t->physics_object = s->physics_object;
  t->physics_bounce = s->physics_bounce; t->clipmask = s->clipmask; t->mover_state = s->mover_state;
  t->sound_pos1 = s->sound_pos1; t->sound_1to2 = s->sound_1to2; t->sound_2to1 = s->sound_2to1; t->sound_pos2 = s->sound_pos2; t->sound_loop = s->sound_loop;
  t->pos1 = s->pos1; t->pos2 = s->pos2; set_owned(&t->message, s->message); t->timestamp = s->timestamp; t->angle = s->angle;
  set_owned(&t->target, s->target); set_owned(&t->targetname, s->targetname); set_owned(&t->team, s->team);
  set_owned(&t->target_shader_name, s->target_shader_name); set_owned(&t->target_shader_new_name, s->target_shader_new_name);
  t->speed = s->speed; t->movedir = s->movedir; t->pain_debounce_time = s->pain_debounce_time;
  t->fly_sound_debounce_time = s->fly_sound_debounce_time; t->last_move_time = s->last_move_time;
  t->damage = s->damage; t->splash_damage = s->splash_damage; t->splash_radius = s->splash_radius;
  t->method_of_death = s->method_of_death; t->splash_method_of_death = s->splash_method_of_death; t->count = s->count;
  t->kamikaze_time = s->kamikaze_time; t->kamikaze_shock_time = s->kamikaze_shock_time; t->watertype = s->watertype;
  t->waterlevel = s->waterlevel; t->noise_index = s->noise_index; t->wait = s->wait; t->random = s->random;
}
qa_q3_client_values qa_q3_capture_client_values(qa_q3_game_client *s) {
  qa_q3_client_values v = {s->ready_to_exit, s->noclip, s->last_cmd_time, s->buttons, s->old_buttons, s->latched_buttons, s->old_origin,
    s->damage_armor, s->damage_blood, s->damage_knockback, s->damage_from, s->damage_from_world, s->accurate_count,
    s->accuracy_shots, s->accuracy_hits, s->last_killed_client, s->last_hurt_client, s->last_hurt_mod, s->respawn_time,
    s->inactivity_time, s->inactivity_warning, s->reward_time, s->air_out_time, s->last_kill_time, s->fire_held,
    s->switch_team_time, s->time_residual, s->portal_id, s->invulnerability_time}; return v;
}
void qa_q3_restore_client_values(qa_q3_game_client *t, qa_q3_client_values s) {
  t->ready_to_exit = s.ready_to_exit; t->noclip = s.noclip; t->last_cmd_time = s.last_cmd_time; t->buttons = s.buttons;
  t->old_buttons = s.old_buttons; t->latched_buttons = s.latched_buttons; t->old_origin = s.old_origin;
  t->damage_armor = s.damage_armor; t->damage_blood = s.damage_blood; t->damage_knockback = s.damage_knockback;
  t->damage_from = s.damage_from; t->damage_from_world = s.damage_from_world; t->accurate_count = s.accurate_count;
  t->accuracy_shots = s.accuracy_shots; t->accuracy_hits = s.accuracy_hits; t->last_killed_client = s.last_killed_client;
  t->last_hurt_client = s.last_hurt_client; t->last_hurt_mod = s.last_hurt_mod; t->respawn_time = s.respawn_time;
  t->inactivity_time = s.inactivity_time; t->inactivity_warning = s.inactivity_warning; t->reward_time = s.reward_time;
  t->air_out_time = s.air_out_time; t->last_kill_time = s.last_kill_time; t->fire_held = s.fire_held;
  t->switch_team_time = s.switch_team_time; t->time_residual = s.time_residual; t->portal_id = s.portal_id; t->invulnerability_time = s.invulnerability_time;
}
qa_q3_player_values qa_q3_capture_player_values(qa_q3_base_player_state *s) {
  qa_q3_player_values v; memset(&v, 0, sizeof v);
  v.command_time = s->command_time; v.pm_type = s->pm_type; v.bob_cycle = s->bob_cycle; v.pm_flags = s->pm_flags; v.pm_time = s->pm_time; v.weapon_time = s->weapon_time;
  v.gravity = s->gravity; v.speed = s->speed; v.delta_angles = s->delta_angles; v.ground_entity_num = s->ground_entity_num; v.legs_timer = s->legs_timer; v.legs_anim = s->legs_anim;
  v.torso_timer = s->torso_timer; v.torso_anim = s->torso_anim; v.movement_dir = s->movement_dir; v.grapple_point = s->grapple_point; v.e_flags = s->e_flags; v.event_sequence = s->event_sequence;
  v.external_event = s->external_event; v.external_event_parm = s->external_event_parm; v.external_event_time = s->external_event_time; v.client_num = s->client_num; v.weapon = s->weapon; v.weapon_state = s->weapon_state;
  v.viewangles = s->viewangles; v.viewheight = s->viewheight; v.damage_event = s->damage_event; v.damage_yaw = s->damage_yaw; v.damage_pitch = s->damage_pitch; v.damage_count = s->damage_count;
  v.generic1 = s->generic1; v.loop_sound = s->loop_sound; v.jumppad_ent = s->jumppad_ent; v.ping = s->ping; v.pmove_framecount = s->pmove_framecount; v.jumppad_frame = s->jumppad_frame;
  v.entity_event_sequence = s->entity_event_sequence;
  return v;
}
void qa_q3_restore_player_values(qa_q3_base_player_state *t, qa_q3_player_values s) {
  t->command_time = s.command_time; t->pm_type = s.pm_type; t->bob_cycle = s.bob_cycle; t->pm_flags = s.pm_flags; t->pm_time = s.pm_time; t->weapon_time = s.weapon_time;
  t->gravity = s.gravity; t->speed = s.speed; t->delta_angles = s.delta_angles; t->ground_entity_num = s.ground_entity_num; t->legs_timer = s.legs_timer; t->legs_anim = s.legs_anim;
  t->torso_timer = s.torso_timer; t->torso_anim = s.torso_anim; t->movement_dir = s.movement_dir; t->grapple_point = s.grapple_point; t->e_flags = s.e_flags; t->event_sequence = s.event_sequence;
  t->external_event = s.external_event; t->external_event_parm = s.external_event_parm; t->external_event_time = s.external_event_time; t->client_num = s.client_num; t->weapon = s.weapon; t->weapon_state = s.weapon_state;
  t->viewangles = s.viewangles; t->viewheight = s.viewheight; t->damage_event = s.damage_event; t->damage_yaw = s.damage_yaw; t->damage_pitch = s.damage_pitch; t->damage_count = s.damage_count;
  t->generic1 = s.generic1; t->loop_sound = s.loop_sound; t->jumppad_ent = s.jumppad_ent; t->ping = s.ping; t->pmove_framecount = s.pmove_framecount; t->jumppad_frame = s.jumppad_frame;
  t->entity_event_sequence = s.entity_event_sequence;
}
qa_q3_persistant_values qa_q3_capture_persistant_values(qa_q3_client_persistant *s) {
  qa_q3_persistant_values v = {s->connected, s->local_client, s->initial_spawn, s->predict_item_pickup, s->pmove_fixed,
    dupstr(s->netname ? s->netname : ""), s->max_health, s->enter_time, s->vote_count, s->team_vote_count, s->team_info}; return v;
}
void qa_q3_persistant_values_destroy(qa_q3_persistant_values *v) { free(v->netname); v->netname = NULL; }
void qa_q3_restore_persistant_values(qa_q3_client_persistant *t, qa_q3_persistant_values *s) {
  t->connected = s->connected; t->local_client = s->local_client; t->initial_spawn = s->initial_spawn;
  t->predict_item_pickup = s->predict_item_pickup; t->pmove_fixed = s->pmove_fixed; set_owned(&t->netname, s->netname);
  t->max_health = s->max_health; t->enter_time = s->enter_time; t->vote_count = s->vote_count; t->team_vote_count = s->team_vote_count; t->team_info = s->team_info;
}
qa_q3_team_values qa_q3_capture_team_values(qa_q3_player_team_state *s) { return *s; }
void qa_q3_restore_team_values(qa_q3_player_team_state *t, qa_q3_team_values s) { *t = s; }
qa_q3_session_values qa_q3_capture_session_values(qa_q3_client_session *s) { return *s; }
void qa_q3_restore_session_values(qa_q3_client_session *t, qa_q3_session_values s) { *t = s; }
qa_q3_network_values qa_q3_capture_network_values(qa_q3_entity_state_record *s) {
  qa_q3_network_values v; memset(&v, 0, sizeof v);
  v.number = s->number; v.e_type = s->e_type; v.e_flags = s->e_flags; v.time = s->time; v.time2 = s->time2;
  v.origin = s->origin; v.origin2 = s->origin2; v.angles = s->angles; v.angles2 = s->angles2;
  v.other_entity_num = s->other_entity_num; v.other_entity_num2 = s->other_entity_num2; v.ground_entity_num = s->ground_entity_num; v.constant_light = s->constant_light;
  v.loop_sound = s->loop_sound; v.modelindex = s->modelindex; v.modelindex2 = s->modelindex2; v.client_num = s->client_num;
  v.frame = s->frame; v.solid = s->solid; v.event = s->event; v.event_parm = s->event_parm; v.powerups = s->powerups; v.weapon = s->weapon;
  v.legs_anim = s->legs_anim; v.torso_anim = s->torso_anim; v.generic1 = s->generic1;
  return v;
}
void qa_q3_restore_network_values(qa_q3_entity_state_record *t, qa_q3_network_values s) {
  t->number = s.number; t->e_type = s.e_type; t->e_flags = s.e_flags; t->time = s.time; t->time2 = s.time2;
  t->origin = s.origin; t->origin2 = s.origin2; t->angles = s.angles; t->angles2 = s.angles2;
  t->other_entity_num = s.other_entity_num; t->other_entity_num2 = s.other_entity_num2; t->ground_entity_num = s.ground_entity_num; t->constant_light = s.constant_light;
  t->loop_sound = s.loop_sound; t->modelindex = s.modelindex; t->modelindex2 = s.modelindex2; t->client_num = s.client_num;
  t->frame = s.frame; t->solid = s.solid; t->event = s.event; t->event_parm = s.event_parm; t->powerups = s.powerups; t->weapon = s.weapon;
  t->legs_anim = s.legs_anim; t->torso_anim = s.torso_anim; t->generic1 = s.generic1;
}
qa_q3_level_values qa_q3_capture_level_values(qa_q3_game_level *s) {
  qa_q3_level_values v; v.time = s->base.time; v.start_time = s->base.start_time; v.warmup_time = s->base.warmup_time;
  v.warmup_modification_count = s->base.warmup_modification_count; v.restarted = s->base.restarted;
  v.num_connected_clients = s->base.num_connected_clients; v.num_non_spectator_clients = s->base.num_non_spectator_clients;
  v.num_playing_clients = s->base.num_playing_clients; v.num_voting_clients = s->base.num_voting_clients;
  v.follow1 = s->base.follow1; v.follow2 = s->base.follow2; v.intermission_time = s->base.intermission_time;
  v.intermission_queued = s->base.intermission_queued; v.intermission_origin = s->base.intermission_origin;
  v.intermission_angle = s->base.intermission_angle; v.changemap = dupstr(s->base.changemap); v.ready_to_exit = s->base.ready_to_exit;
  v.exit_time = s->base.exit_time; v.frame_num = s->frame_num; v.previous_time = s->previous_time; v.new_session = s->new_session; v.fry_sound = s->fry_sound; return v;
}
void qa_q3_level_values_destroy(qa_q3_level_values *v) { free(v->changemap); v->changemap = NULL; }
void qa_q3_restore_level_values(qa_q3_game_level *t, qa_q3_level_values *s) {
  t->base.time = s->time; t->base.start_time = s->start_time; t->base.warmup_time = s->warmup_time;
  t->base.warmup_modification_count = s->warmup_modification_count; t->base.restarted = s->restarted;
  t->base.num_connected_clients = s->num_connected_clients; t->base.num_non_spectator_clients = s->num_non_spectator_clients;
  t->base.num_playing_clients = s->num_playing_clients; t->base.num_voting_clients = s->num_voting_clients;
  t->base.follow1 = s->follow1; t->base.follow2 = s->follow2; t->base.intermission_time = s->intermission_time;
  t->base.intermission_queued = s->intermission_queued; t->base.intermission_origin = s->intermission_origin;
  t->base.intermission_angle = s->intermission_angle; set_owned(&t->base.changemap, s->changemap); t->base.ready_to_exit = s->ready_to_exit;
  t->base.exit_time = s->exit_time; t->frame_num = s->frame_num; t->previous_time = s->previous_time; t->new_session = s->new_session; t->fry_sound = s->fry_sound;
}
/* Reader helpers: field may be absent only where TS reader would throw; mirrors read present fields. */
static double rf_num(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); double v = qa_save_reader_number(&f); qa_save_reader_destroy(&f); return v; }
static bool rf_bool(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); bool v = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f); return v; }
static char *rf_str(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); if (qa_save_reader_is_null(&f)) { qa_save_reader_destroy(&f); return NULL; } char *v = dupstr(qa_save_reader_string(&f)); qa_save_reader_destroy(&f); return v; }
qa_vec3 qa_q3_read_vector(const qa_save_reader *r) { qa_vec3 v = {rf_num(r, "x"), rf_num(r, "y"), rf_num(r, "z")}; return v; }
static qa_vec3 rf_vec(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); qa_vec3 v = qa_q3_read_vector(&f); qa_save_reader_destroy(&f); return v; }
qa_q3_entity_values qa_q3_read_entity_values(const qa_save_reader *r) {
  qa_q3_entity_values v; v.spawnflags = rf_num(r, "spawnflags"); v.never_free = rf_bool(r, "neverFree"); v.flags = rf_num(r, "flags");
  v.model = rf_str(r, "model"); v.model2 = rf_str(r, "model2"); v.freetime = rf_num(r, "freetime"); v.event_time = rf_num(r, "eventTime");
  v.free_after_event = rf_bool(r, "freeAfterEvent"); v.unlink_after_event = rf_bool(r, "unlinkAfterEvent"); v.physics_object = rf_bool(r, "physicsObject");
  v.physics_bounce = rf_num(r, "physicsBounce"); v.clipmask = rf_num(r, "clipmask"); v.mover_state = rf_num(r, "moverState");
  v.sound_pos1 = rf_num(r, "soundPos1"); v.sound_1to2 = rf_num(r, "sound1to2"); v.sound_2to1 = rf_num(r, "sound2to1");
  v.sound_pos2 = rf_num(r, "soundPos2"); v.sound_loop = rf_num(r, "soundLoop"); v.pos1 = rf_vec(r, "pos1"); v.pos2 = rf_vec(r, "pos2");
  v.message = rf_str(r, "message"); v.timestamp = rf_num(r, "timestamp"); v.angle = rf_num(r, "angle");
  v.target = rf_str(r, "target"); v.targetname = rf_str(r, "targetname"); v.team = rf_str(r, "team");
  v.target_shader_name = rf_str(r, "targetShaderName"); v.target_shader_new_name = rf_str(r, "targetShaderNewName");
  v.speed = rf_num(r, "speed"); v.movedir = rf_vec(r, "movedir"); v.pain_debounce_time = rf_num(r, "painDebounceTime");
  v.fly_sound_debounce_time = rf_num(r, "flySoundDebounceTime"); v.last_move_time = rf_num(r, "lastMoveTime");
  v.damage = rf_num(r, "damage"); v.splash_damage = rf_num(r, "splashDamage"); v.splash_radius = rf_num(r, "splashRadius");
  v.method_of_death = rf_num(r, "methodOfDeath"); v.splash_method_of_death = rf_num(r, "splashMethodOfDeath"); v.count = rf_num(r, "count");
  v.kamikaze_time = rf_num(r, "kamikazeTime"); v.kamikaze_shock_time = rf_num(r, "kamikazeShockTime"); v.watertype = rf_num(r, "watertype");
  v.waterlevel = rf_num(r, "waterlevel"); v.noise_index = rf_num(r, "noiseIndex"); v.wait = rf_num(r, "wait"); v.random = rf_num(r, "random"); return v;
}
qa_q3_client_values qa_q3_read_client_values(const qa_save_reader *r) {
  qa_q3_client_values v; v.ready_to_exit = rf_bool(r, "readyToExit"); v.noclip = rf_bool(r, "noclip");
  v.last_cmd_time = rf_num(r, "lastCmdTime"); v.buttons = rf_num(r, "buttons"); v.old_buttons = rf_num(r, "oldButtons");
  v.latched_buttons = rf_num(r, "latchedButtons"); v.old_origin = rf_vec(r, "oldOrigin"); v.damage_armor = rf_num(r, "damageArmor");
  v.damage_blood = rf_num(r, "damageBlood"); v.damage_knockback = rf_num(r, "damageKnockback"); v.damage_from = rf_vec(r, "damageFrom");
  v.damage_from_world = rf_bool(r, "damageFromWorld"); v.accurate_count = rf_num(r, "accurateCount");
  v.accuracy_shots = rf_num(r, "accuracyShots"); v.accuracy_hits = rf_num(r, "accuracyHits");
  v.last_killed_client = rf_num(r, "lastKilledClient"); v.last_hurt_client = rf_num(r, "lastHurtClient"); v.last_hurt_mod = rf_num(r, "lastHurtMod");
  v.respawn_time = rf_num(r, "respawnTime"); v.inactivity_time = rf_num(r, "inactivityTime"); v.inactivity_warning = rf_bool(r, "inactivityWarning");
  v.reward_time = rf_num(r, "rewardTime"); v.air_out_time = rf_num(r, "airOutTime"); v.last_kill_time = rf_num(r, "lastKillTime");
  v.fire_held = rf_bool(r, "fireHeld"); v.switch_team_time = rf_num(r, "switchTeamTime"); v.time_residual = rf_num(r, "timeResidual");
  v.portal_id = rf_num(r, "portalID"); v.invulnerability_time = rf_num(r, "invulnerabilityTime"); return v;
}
qa_q3_player_values qa_q3_read_player_values(const qa_save_reader *r) {
  qa_q3_player_values v; memset(&v, 0, sizeof v);
  v.command_time = rf_num(r, "commandTime"); v.pm_type = rf_num(r, "pmType"); v.bob_cycle = rf_num(r, "bobCycle");
  v.pm_flags = rf_num(r, "pmFlags"); v.pm_time = rf_num(r, "pmTime"); v.weapon_time = rf_num(r, "weaponTime"); v.gravity = rf_num(r, "gravity");
  v.speed = rf_num(r, "speed"); v.delta_angles = rf_vec(r, "deltaAngles"); v.ground_entity_num = rf_num(r, "groundEntityNum"); v.legs_timer = rf_num(r, "legsTimer");
  v.legs_anim = rf_num(r, "legsAnim"); v.torso_timer = rf_num(r, "torsoTimer"); v.torso_anim = rf_num(r, "torsoAnim");
  v.movement_dir = rf_num(r, "movementDir"); v.grapple_point = rf_vec(r, "grapplePoint"); v.e_flags = rf_num(r, "eFlags");
  v.event_sequence = rf_num(r, "eventSequence"); v.external_event = rf_num(r, "externalEvent"); v.external_event_parm = rf_num(r, "externalEventParm");
  v.external_event_time = rf_num(r, "externalEventTime"); v.client_num = rf_num(r, "clientNum"); v.weapon = rf_num(r, "weapon");
  v.weapon_state = rf_num(r, "weaponState"); v.viewangles = rf_vec(r, "viewangles"); v.viewheight = rf_num(r, "viewheight");
  v.damage_event = rf_num(r, "damageEvent"); v.damage_yaw = rf_num(r, "damageYaw"); v.damage_pitch = rf_num(r, "damagePitch");
  v.damage_count = rf_num(r, "damageCount"); v.generic1 = rf_num(r, "generic1"); v.loop_sound = rf_num(r, "loopSound");
  v.jumppad_ent = rf_num(r, "jumppadEnt"); v.ping = rf_num(r, "ping"); v.pmove_framecount = rf_num(r, "pmoveFramecount");
  v.jumppad_frame = rf_num(r, "jumppadFrame"); v.entity_event_sequence = rf_num(r, "entityEventSequence"); return v;
}
qa_q3_persistant_values qa_q3_read_persistant_values(const qa_save_reader *r) {
  qa_q3_persistant_values v; v.connected = rf_num(r, "connected"); v.local_client = rf_bool(r, "localClient");
  v.initial_spawn = rf_bool(r, "initialSpawn"); v.predict_item_pickup = rf_bool(r, "predictItemPickup"); v.pmove_fixed = rf_bool(r, "pmoveFixed");
  v.netname = rf_str(r, "netname"); v.max_health = rf_num(r, "maxHealth"); v.enter_time = rf_num(r, "enterTime");
  v.vote_count = rf_num(r, "voteCount"); v.team_vote_count = rf_num(r, "teamVoteCount"); v.team_info = rf_bool(r, "teamInfo"); return v;
}
qa_q3_team_values qa_q3_read_team_values(const qa_save_reader *r) {
  qa_q3_team_values v; v.state = rf_num(r, "state"); v.location = rf_num(r, "location"); v.captures = rf_num(r, "captures");
  v.base_defense = rf_num(r, "baseDefense"); v.carrier_defense = rf_num(r, "carrierDefense"); v.flag_recovery = rf_num(r, "flagRecovery");
  v.frag_carrier = rf_num(r, "fragCarrier"); v.assists = rf_num(r, "assists"); v.last_hurt_carrier = rf_num(r, "lastHurtCarrier");
  v.last_returned_flag = rf_num(r, "lastReturnedFlag"); v.flag_since = rf_num(r, "flagSince"); v.last_fragged_carrier = rf_num(r, "lastFraggedCarrier"); return v;
}
qa_q3_session_values qa_q3_read_session_values(const qa_save_reader *r) {
  qa_q3_session_values v; v.session_team = rf_num(r, "sessionTeam"); v.spectator_time = rf_num(r, "spectatorTime");
  v.spectator_state = rf_num(r, "spectatorState"); v.spectator_client = rf_num(r, "spectatorClient"); v.wins = rf_num(r, "wins");
  v.losses = rf_num(r, "losses"); v.team_leader = rf_num(r, "teamLeader"); return v;
}
qa_q3_network_values qa_q3_read_network_values(const qa_save_reader *r) {
  qa_q3_network_values v; memset(&v, 0, sizeof v);
  v.number = rf_num(r, "number"); v.e_type = rf_num(r, "eType"); v.e_flags = rf_num(r, "eFlags"); v.time = rf_num(r, "time"); v.time2 = rf_num(r, "time2");
  v.origin = rf_vec(r, "origin"); v.origin2 = rf_vec(r, "origin2"); v.angles = rf_vec(r, "angles"); v.angles2 = rf_vec(r, "angles2");
  v.other_entity_num = rf_num(r, "otherEntityNum"); v.other_entity_num2 = rf_num(r, "otherEntityNum2"); v.ground_entity_num = rf_num(r, "groundEntityNum");
  v.constant_light = rf_num(r, "constantLight"); v.loop_sound = rf_num(r, "loopSound"); v.modelindex = rf_num(r, "modelindex");
  v.modelindex2 = rf_num(r, "modelindex2"); v.client_num = rf_num(r, "clientNum"); v.frame = rf_num(r, "frame"); v.solid = rf_num(r, "solid");
  v.event = rf_num(r, "event"); v.event_parm = rf_num(r, "eventParm"); v.powerups = rf_num(r, "powerups"); v.weapon = rf_num(r, "weapon");
  v.legs_anim = rf_num(r, "legsAnim"); v.torso_anim = rf_num(r, "torsoAnim"); v.generic1 = rf_num(r, "generic1"); return v;
}
qa_q3_level_values qa_q3_read_level_values(const qa_save_reader *r) {
  qa_q3_level_values v; v.time = rf_num(r, "time"); v.start_time = rf_num(r, "startTime"); v.warmup_time = rf_num(r, "warmupTime");
  v.warmup_modification_count = rf_num(r, "warmupModificationCount"); v.restarted = rf_bool(r, "restarted");
  v.num_connected_clients = rf_num(r, "numConnectedClients"); v.num_non_spectator_clients = rf_num(r, "numNonSpectatorClients");
  v.num_playing_clients = rf_num(r, "numPlayingClients"); v.num_voting_clients = rf_num(r, "numVotingClients");
  v.follow1 = rf_num(r, "follow1"); v.follow2 = rf_num(r, "follow2"); v.intermission_time = rf_num(r, "intermissionTime");
  v.intermission_queued = rf_num(r, "intermissionQueued"); v.intermission_origin = rf_vec(r, "intermissionOrigin");
  v.intermission_angle = rf_vec(r, "intermissionAngle"); v.changemap = rf_str(r, "changemap"); v.ready_to_exit = rf_bool(r, "readyToExit");
  v.exit_time = rf_num(r, "exitTime"); v.frame_num = rf_num(r, "frameNum"); v.previous_time = rf_num(r, "previousTime");
  v.new_session = rf_bool(r, "newSession"); v.fry_sound = rf_num(r, "frySound"); return v;
}
