/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_reachability_types.h"
#include <stdlib.h>
#include <string.h>

void qa_aas_movement_settings_init(qa_aas_movement_settings *s) { memset(s, 0, sizeof *s); }
void qa_init_aas_movement_settings(qa_aas_movement_settings *s, void *ctx, qa_aas_lib_var_value value) {
#define READ(field, name, initial) s->field = (float)value(ctx, name, initial)
  s->gravity_direction = (qa_vec3){0, 0, -1};
  READ(friction, "phys_friction", "6"); READ(stop_speed, "phys_stopspeed", "100"); READ(gravity, "phys_gravity", "800");
  READ(water_friction, "phys_waterfriction", "1"); READ(water_gravity, "phys_watergravity", "400");
  READ(max_velocity, "phys_maxvelocity", "320"); READ(max_walk_velocity, "phys_maxwalkvelocity", "320");
  READ(max_crouch_velocity, "phys_maxcrouchvelocity", "100"); READ(max_swim_velocity, "phys_maxswimvelocity", "150");
  READ(walk_accelerate, "phys_walkaccelerate", "10"); READ(air_accelerate, "phys_airaccelerate", "1"); READ(swim_accelerate, "phys_swimaccelerate", "4");
  READ(max_step, "phys_maxstep", "19"); READ(max_steepness, "phys_maxsteepness", "0.7"); READ(max_water_jump, "phys_maxwaterjump", "18");
  READ(max_barrier, "phys_maxbarrier", "33"); READ(jump_velocity, "phys_jumpvel", "270");
  READ(fall_delta5, "phys_falldelta5", "40"); READ(fall_delta10, "phys_falldelta10", "60");
  READ(water_jump_time, "rs_waterjump", "400"); READ(teleport_time, "rs_teleport", "50"); READ(barrier_jump_time, "rs_barrierjump", "100");
  READ(start_crouch_time, "rs_startcrouch", "300"); READ(start_grapple_time, "rs_startgrapple", "500");
  READ(start_walk_off_ledge_time, "rs_startwalkoffledge", "70"); READ(start_jump_time, "rs_startjump", "300");
  READ(rocket_jump_time, "rs_rocketjump", "500"); READ(bfg_jump_time, "rs_bfgjump", "500"); READ(jump_pad_time, "rs_jumppad", "250");
  READ(air_controlled_jump_pad_time, "rs_aircontrolledjumppad", "300"); READ(func_bob_time, "rs_funcbob", "300");
  READ(start_elevator_time, "rs_startelevator", "50"); READ(fall_damage5_time, "rs_falldamage5", "300");
  READ(fall_damage10_time, "rs_falldamage10", "500"); READ(max_fall_height, "rs_maxfallheight", "0"); READ(max_jump_fall_height, "rs_maxjumpfallheight", "450");
#undef READ
}
static double number_value(void *ctx, const char *name, const char *initial) {
  (void)ctx; (void)name; return strtod(initial, NULL);
}
void qa_aas_default_movement_settings(qa_aas_movement_settings *s) {
  qa_aas_movement_settings_init(s);
  qa_init_aas_movement_settings(s, NULL, number_value);
}
