/* Port of src/content/q1/foundation/types.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/types.h"

#include <math.h>
#include <string.h>

const qa_provider_id QA_Q1_PROVIDER = "q1:official";
const qa_vec3 QA_Q1_ZERO = {0, 0, 0};
const qa_bounds QA_Q1_POINT = {{0, 0, 0}, {0, 0, 0}};
const qa_bounds QA_Q1_PLAYER_BOUNDS = {{-16, -16, -24}, {16, 16, 32}};
const qa_q1_base_weapon QA_Q1_WEAPONS[8] = {"axe", "shotgun", "supershotgun", "nailgun",
  "supernailgun", "grenadelauncher", "rocketlauncher", "lightning"};
const qa_q1_weapon QA_Q1_WEAPON_IDS[20] = {"axe", "shotgun", "supershotgun", "nailgun",
  "supernailgun", "grenadelauncher", "rocketlauncher", "lightning",
  "hipnotic:laser", "hipnotic:mjolnir", "hipnotic:proximity", "rogue:lava-nailgun",
  "rogue:lava-supernailgun", "rogue:multi-grenade", "rogue:multi-rocket", "rogue:plasma",
  "rogue:grapple", "mg3:laser", "mg3:mjolnir", "ctf:grapple"};
const qa_q1_powerup QA_Q1_POWERUP_IDS[12] = {"quad", "invulnerability", "invisibility", "suit",
  "hipnotic:wetsuit", "hipnotic:empathy", "rogue:shield", "rogue:antigrav", "mg3:lavasuit",
  "q3:haste", "q3:regen", "q3:flight"};

double qa_q1_weapon_bit(qa_q1_base_weapon weapon) {
  if (!strcmp(weapon, "axe")) return 4096;
  if (!strcmp(weapon, "shotgun")) return 1;
  if (!strcmp(weapon, "supershotgun")) return 2;
  if (!strcmp(weapon, "nailgun")) return 4;
  if (!strcmp(weapon, "supernailgun")) return 8;
  if (!strcmp(weapon, "grenadelauncher")) return 16;
  if (!strcmp(weapon, "rocketlauncher")) return 32;
  return 64; /* lightning; TS switch is exhaustive over Q1BaseWeapon */
}
bool qa_q1_is_base_weapon(qa_q1_weapon weapon) {
  for (size_t i = 0; i < 8; i++) if (!strcmp(weapon, QA_Q1_WEAPONS[i])) return true;
  return false;
}
qa_item_id qa_q1_weapon_item(qa_q1_weapon weapon) {
  static const char *const items[20] = {
    "q1:weapon/axe", "q1:weapon/shotgun", "q1:weapon/supershotgun", "q1:weapon/nailgun",
    "q1:weapon/supernailgun", "q1:weapon/grenadelauncher", "q1:weapon/rocketlauncher",
    "q1:weapon/lightning", "q1:weapon/hipnotic:laser", "q1:weapon/hipnotic:mjolnir",
    "q1:weapon/hipnotic:proximity", "q1:weapon/rogue:lava-nailgun",
    "q1:weapon/rogue:lava-supernailgun", "q1:weapon/rogue:multi-grenade",
    "q1:weapon/rogue:multi-rocket", "q1:weapon/rogue:plasma", "q1:weapon/rogue:grapple",
    "q1:weapon/mg3:laser", "q1:weapon/mg3:mjolnir", "q1:weapon/ctf:grapple"};
  for (size_t i = 0; i < 20; i++) if (!strcmp(weapon, QA_Q1_WEAPON_IDS[i])) return items[i];
  /* Foreign weapon id: rotating buffers (TS allocates a fresh string). */
  static char buffers[4][128]; static unsigned next;
  char *buffer = buffers[next++ % 4];
  size_t n = 0; const char *prefix = "q1:weapon/";
  while (prefix[n] && n < 127) { buffer[n] = prefix[n]; n++; }
  for (size_t i = 0; weapon[i] && n < 127; i++) buffer[n++] = weapon[i];
  buffer[n] = '\0'; return buffer;
}
qa_vec3 qa_q1_vadd(qa_vec3 a, qa_vec3 b) { return qa_add3(a, b); }
qa_vec3 qa_q1_vsub(qa_vec3 a, qa_vec3 b) { return qa_sub3(a, b); }
qa_vec3 qa_q1_vscale(qa_vec3 a, double s) { return qa_scale3(a, s); }
double qa_q1_dot(qa_vec3 a, qa_vec3 b) { return qa_dot3(a, b); }
double qa_q1_length(qa_vec3 a) { return qa_length3(a); }
qa_vec3 qa_q1_normalize(qa_vec3 a) {
  double magnitude = qa_length3(a);
  return magnitude == 0 ? QA_Q1_ZERO : qa_scale3(a, 1 / magnitude);
}
bool qa_q1_overlaps(qa_bounds a, qa_bounds b) { return qa_math_bounds_overlap(a, b); }
double qa_q1_path_end_time(double time) { return (double)(float)((double)(float)time + 999999); }
double qa_q1_yaw_for(qa_vec3 direction) {
  double yaw = atan2(direction.y, direction.x) * 180 / 3.141592653589793;
  return yaw < 0 ? yaw + 360 : yaw;
}
qa_q1_basis qa_q1_vectors(qa_vec3 angles) {
  qa_angle_vectors v = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_Q1);
  qa_q1_basis basis = {v.forward, v.right, v.up};
  return basis;
}
double qa_q1_player_powerup_get(const qa_q1_player_state *p, qa_q1_powerup kind) {
  double value; return qa_q1_player_powerup_has(p, kind, &value) ? value : 0;
}
bool qa_q1_player_powerup_has(const qa_q1_player_state *p, qa_q1_powerup kind, double *out) {
  for (size_t i = 0; i < p->powerup_count; i++)
    if (!strcmp(p->powerups[i].kind, kind)) { if (out) *out = p->powerups[i].expires; return true; }
  return false;
}
void qa_q1_player_powerup_set(qa_q1_player_state *p, qa_q1_powerup kind, double expires) {
  for (size_t i = 0; i < p->powerup_count; i++)
    if (!strcmp(p->powerups[i].kind, kind)) { p->powerups[i].expires = expires; return; }
  if (p->powerup_count < 16) { p->powerups[p->powerup_count].kind = kind; p->powerups[p->powerup_count].expires = expires; p->powerup_count++; }
}
void qa_q1_player_powerup_delete(qa_q1_player_state *p, qa_q1_powerup kind) {
  for (size_t i = 0; i < p->powerup_count; i++) if (!strcmp(p->powerups[i].kind, kind)) {
    memmove(&p->powerups[i], &p->powerups[i + 1], (p->powerup_count - i - 1) * sizeof *p->powerups);
    p->powerup_count--; return;
  }
}

double qa_q1_options_skill(const qa_q1_options *options) {
  return options->live.skill != NULL ? options->live.skill(options->live.ctx) : options->skill;
}
double qa_q1_options_teamplay(const qa_q1_options *options) {
  if (options->live.teamplay != NULL) return options->live.teamplay(options->live.ctx);
  return options->has_teamplay ? options->teamplay : 0;
}
double qa_q1_options_gravity(const qa_q1_options *options) {
  return options->live.gravity != NULL ? options->live.gravity(options->live.ctx) : options->gravity;
}
