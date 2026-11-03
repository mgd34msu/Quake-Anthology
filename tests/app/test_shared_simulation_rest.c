/* Checks for the runtime-rest rows of the SharedSimulation port (runtime.ts):
 * native-pmove-contacts.ts, nativeMoveSlices (native-commands.ts), the
 * rerelease ammo limits and edition-aware mission inventory, the real-typed
 * registerSelectedQ2MissionWeapons (q2-expansion-arsenal.ts), and the
 * bindNativeInput / captureNativeQ2Travel guards.
 *
 * Expected lines come from the TS modules run under bun on the same inputs:
 * tests/app/oracles/shared_simulation_rest.ts (`bun tests/app/oracles/shared_simulation_rest.ts`).
 * The C side prints each case in the oracle's format and compares the line.
 * Every callback checks its sentinel context. GPL-2.0-or-later. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"
#include "qa/app/bootstrap/simulation/native_commands.h"
#include "qa/app/bootstrap/simulation/native_pmove_contacts.h"
#include "qa/content/composition/q2_expansion_arsenal.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/rerelease_ammo.h"
#include "qa/content/q2/missionpacks/items.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); } } while (0)
static int SLOT_CTX, WEAPON_CTX, MISSION_CTX, GRAVITY_CTX;
#define EXPECT_CTX(ctx, sentinel) CHECK((ctx) == &(sentinel))

static const char *const EXPECTED[] = {
  "touches1 5:-1,0,0,-100 3:0,0,1,24",
  "touches2 2:-1,0,0,-100 7:1,0,0,4",
  "ground 0,0,1,24 null null 0,0.6,0.8,3",
  "overlap -1,0,0,-100",
  "overlap 0,0,1,0",
  "overlap 0,-1,0,-80",
  "overlap 1,0,0,16",
  "overlap 1,0,0,1",
  "slices nq 100 0 100 0",
  "slices nq 250 0 100,100,50 0",
  "slices q3 100 0 100 0",
  "slices qw 100 0 50,50 0",
  "slices qw 120 0 50,50,20 0",
  "slices qw 0 0 0 0",
  "slices fixed 100 0 40,40 20",
  "slices fixed 100 20 40,40,40 0",
  "slices fixed 33 0  33",
  "slices fixed -50 0  30",
  "slices fixed NaN 0  NaN",
  "slices q2 75 0 75 0",
  "mission xatrix classic q2:ammo_magslug=50 q2:ammo_trap=5 q2:weapon_boomer=1 q2:weapon_phalanx=1",
  "mission xatrix rerelease q2:ammo_magslug=50 q2:ammo_trap=5 q2:weapon_boomer=1 q2:weapon_phalanx=1",
  "mission rogue classic q2:ammo_flechettes=200 q2:ammo_prox=50 q2:ammo_tesla=50 q2:ammo_disruptor=100 q2:weapon_proxlauncher=1 q2:weapon_chainfist=1 q2:weapon_disintegrator=1 q2:weapon_etf_rifle=1 q2:weapon_plasmabeam=1",
  "mission rogue rerelease q2:ammo_flechettes=200 q2:ammo_prox=50 q2:ammo_tesla=5 q2:ammo_disruptor=12 q2:weapon_proxlauncher=1 q2:weapon_chainfist=1 q2:weapon_disintegrator=1 q2:weapon_etf_rifle=1 q2:weapon_plasmabeam=1",
  "limits ammo_trap 7,5,null,null",
  "limits ammo_tesla 9,5,null,null",
  "limits ammo_disruptor 10,12,21,30",
  "limits ammo_prox 11,50,null,null",
  "limits ammo_shells 1,100,150,200",
  "limits ammo_none undefined",
  "register classic baseq2 1 q2:weapon_blaster,q2:weapon_shotgun,q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_chaingun,q2:ammo_grenades,q2:weapon_grenadelauncher,q2:weapon_rocketlauncher,q2:weapon_hyperblaster,q2:weapon_railgun,q2:weapon_bfg q2:ammo_shells=100 q2:ammo_bullets=200 q2:ammo_cells=200 q2:ammo_rockets=50 q2:ammo_slugs=50 q2:ammo_grenades=50 q2:weapon_blaster=32767 q2:weapon_shotgun=32767 q2:weapon_supershotgun=32767 q2:weapon_machinegun=32767 q2:weapon_chaingun=32767 q2:weapon_grenadelauncher=32767 q2:weapon_rocketlauncher=32767 q2:weapon_hyperblaster=32767 q2:weapon_railgun=32767 q2:weapon_bfg=32767 blaster,shotgun,supershotgun,machinegun,chaingun,grenades,grenadelauncher,rocketlauncher,hyperblaster,railgun,bfg",
  "register classic xatrix 1 q2:weapon_blaster,q2:weapon_shotgun,q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_chaingun,q2:ammo_grenades,q2:ammo_trap,q2:weapon_grenadelauncher,q2:weapon_rocketlauncher,q2:weapon_hyperblaster,q2:weapon_boomer,q2:weapon_railgun,q2:weapon_phalanx,q2:weapon_bfg q2:ammo_shells=100 q2:ammo_bullets=200 q2:ammo_cells=200 q2:ammo_rockets=50 q2:ammo_slugs=50 q2:ammo_grenades=50 q2:weapon_blaster=32767 q2:weapon_shotgun=32767 q2:weapon_supershotgun=32767 q2:weapon_machinegun=32767 q2:weapon_chaingun=32767 q2:weapon_grenadelauncher=32767 q2:weapon_rocketlauncher=32767 q2:weapon_hyperblaster=32767 q2:weapon_railgun=32767 q2:weapon_bfg=32767 q2:ammo_magslug=50 q2:ammo_trap=5 q2:weapon_boomer=1 q2:weapon_phalanx=1 blaster,shotgun,supershotgun,machinegun,chaingun,grenades,grenadelauncher,rocketlauncher,hyperblaster,railgun,bfg,trap,ionripper,phalanx",
  "register classic rogue 1 q2:weapon_blaster,q2:weapon_chainfist,q2:weapon_shotgun,q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_etf_rifle,q2:weapon_chaingun,q2:ammo_grenades,q2:ammo_tesla,q2:weapon_grenadelauncher,q2:weapon_proxlauncher,q2:weapon_rocketlauncher,q2:weapon_hyperblaster,q2:weapon_plasmabeam,q2:weapon_railgun,q2:weapon_disintegrator,q2:weapon_bfg q2:ammo_shells=100 q2:ammo_bullets=200 q2:ammo_cells=200 q2:ammo_rockets=50 q2:ammo_slugs=50 q2:ammo_grenades=50 q2:weapon_blaster=32767 q2:weapon_shotgun=32767 q2:weapon_supershotgun=32767 q2:weapon_machinegun=32767 q2:weapon_chaingun=32767 q2:weapon_grenadelauncher=32767 q2:weapon_rocketlauncher=32767 q2:weapon_hyperblaster=32767 q2:weapon_railgun=32767 q2:weapon_bfg=32767 q2:ammo_flechettes=200 q2:ammo_prox=50 q2:ammo_tesla=50 q2:ammo_disruptor=100 q2:weapon_proxlauncher=1 q2:weapon_chainfist=1 q2:weapon_disintegrator=1 q2:weapon_etf_rifle=1 q2:weapon_plasmabeam=1 blaster,shotgun,supershotgun,machinegun,chaingun,grenades,grenadelauncher,rocketlauncher,hyperblaster,railgun,bfg,tesla,proxlauncher,chainfist,disintegrator,etf_rifle,heatbeam",
  "register classic mg2 1 q2:weapon_blaster,q2:weapon_shotgun,q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_chaingun,q2:ammo_grenades,q2:weapon_grenadelauncher,q2:weapon_rocketlauncher,q2:weapon_hyperblaster,q2:weapon_railgun,q2:weapon_bfg q2:ammo_shells=100 q2:ammo_bullets=200 q2:ammo_cells=200 q2:ammo_rockets=50 q2:ammo_slugs=50 q2:ammo_grenades=50 q2:weapon_blaster=32767 q2:weapon_shotgun=32767 q2:weapon_supershotgun=32767 q2:weapon_machinegun=32767 q2:weapon_chaingun=32767 q2:weapon_grenadelauncher=32767 q2:weapon_rocketlauncher=32767 q2:weapon_hyperblaster=32767 q2:weapon_railgun=32767 q2:weapon_bfg=32767 blaster,shotgun,supershotgun,machinegun,chaingun,grenades,grenadelauncher,rocketlauncher,hyperblaster,railgun,bfg",
  "register rerelease baseq2 1 q2:weapon_blaster,q2:weapon_chainfist,q2:weapon_shotgun,q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_etf_rifle,q2:weapon_chaingun,q2:ammo_grenades,q2:ammo_trap,q2:ammo_tesla,q2:weapon_grenadelauncher,q2:weapon_proxlauncher,q2:weapon_rocketlauncher,q2:weapon_hyperblaster,q2:weapon_boomer,q2:weapon_plasmabeam,q2:weapon_railgun,q2:weapon_phalanx,q2:weapon_disintegrator,q2:weapon_bfg q2:ammo_shells=100 q2:ammo_bullets=200 q2:ammo_cells=200 q2:ammo_rockets=50 q2:ammo_slugs=50 q2:ammo_grenades=50 q2:weapon_blaster=32767 q2:weapon_shotgun=32767 q2:weapon_supershotgun=32767 q2:weapon_machinegun=32767 q2:weapon_chaingun=32767 q2:weapon_grenadelauncher=32767 q2:weapon_rocketlauncher=32767 q2:weapon_hyperblaster=32767 q2:weapon_railgun=32767 q2:weapon_bfg=32767 q2:ammo_magslug=50 q2:ammo_trap=5 q2:weapon_boomer=1 q2:weapon_phalanx=1 q2:ammo_flechettes=200 q2:ammo_prox=50 q2:ammo_tesla=5 q2:ammo_disruptor=12 q2:weapon_proxlauncher=1 q2:weapon_chainfist=1 q2:weapon_disintegrator=1 q2:weapon_etf_rifle=1 q2:weapon_plasmabeam=1 blaster,shotgun,supershotgun,machinegun,chaingun,grenades,grenadelauncher,rocketlauncher,hyperblaster,railgun,bfg,trap,ionripper,phalanx,tesla,proxlauncher,chainfist,disintegrator,etf_rifle,heatbeam",
  "register rerelease xatrix 1 q2:weapon_blaster,q2:weapon_chainfist,q2:weapon_shotgun,q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_etf_rifle,q2:weapon_chaingun,q2:ammo_grenades,q2:ammo_trap,q2:ammo_tesla,q2:weapon_grenadelauncher,q2:weapon_proxlauncher,q2:weapon_rocketlauncher,q2:weapon_hyperblaster,q2:weapon_boomer,q2:weapon_plasmabeam,q2:weapon_railgun,q2:weapon_phalanx,q2:weapon_disintegrator,q2:weapon_bfg q2:ammo_shells=100 q2:ammo_bullets=200 q2:ammo_cells=200 q2:ammo_rockets=50 q2:ammo_slugs=50 q2:ammo_grenades=50 q2:weapon_blaster=32767 q2:weapon_shotgun=32767 q2:weapon_supershotgun=32767 q2:weapon_machinegun=32767 q2:weapon_chaingun=32767 q2:weapon_grenadelauncher=32767 q2:weapon_rocketlauncher=32767 q2:weapon_hyperblaster=32767 q2:weapon_railgun=32767 q2:weapon_bfg=32767 q2:ammo_magslug=50 q2:ammo_trap=5 q2:weapon_boomer=1 q2:weapon_phalanx=1 q2:ammo_flechettes=200 q2:ammo_prox=50 q2:ammo_tesla=5 q2:ammo_disruptor=12 q2:weapon_proxlauncher=1 q2:weapon_chainfist=1 q2:weapon_disintegrator=1 q2:weapon_etf_rifle=1 q2:weapon_plasmabeam=1 blaster,shotgun,supershotgun,machinegun,chaingun,grenades,grenadelauncher,rocketlauncher,hyperblaster,railgun,bfg,trap,ionripper,phalanx,tesla,proxlauncher,chainfist,disintegrator,etf_rifle,heatbeam",
  "register rerelease rogue 1 q2:weapon_blaster,q2:weapon_chainfist,q2:weapon_shotgun,q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_etf_rifle,q2:weapon_chaingun,q2:ammo_grenades,q2:ammo_trap,q2:ammo_tesla,q2:weapon_grenadelauncher,q2:weapon_proxlauncher,q2:weapon_rocketlauncher,q2:weapon_hyperblaster,q2:weapon_boomer,q2:weapon_plasmabeam,q2:weapon_railgun,q2:weapon_phalanx,q2:weapon_disintegrator,q2:weapon_bfg q2:ammo_shells=100 q2:ammo_bullets=200 q2:ammo_cells=200 q2:ammo_rockets=50 q2:ammo_slugs=50 q2:ammo_grenades=50 q2:weapon_blaster=32767 q2:weapon_shotgun=32767 q2:weapon_supershotgun=32767 q2:weapon_machinegun=32767 q2:weapon_chaingun=32767 q2:weapon_grenadelauncher=32767 q2:weapon_rocketlauncher=32767 q2:weapon_hyperblaster=32767 q2:weapon_railgun=32767 q2:weapon_bfg=32767 q2:ammo_magslug=50 q2:ammo_trap=5 q2:weapon_boomer=1 q2:weapon_phalanx=1 q2:ammo_flechettes=200 q2:ammo_prox=50 q2:ammo_tesla=5 q2:ammo_disruptor=12 q2:weapon_proxlauncher=1 q2:weapon_chainfist=1 q2:weapon_disintegrator=1 q2:weapon_etf_rifle=1 q2:weapon_plasmabeam=1 blaster,shotgun,supershotgun,machinegun,chaingun,grenades,grenadelauncher,rocketlauncher,hyperblaster,railgun,bfg,trap,ionripper,phalanx,tesla,proxlauncher,chainfist,disintegrator,etf_rifle,heatbeam",
  "register rerelease mg2 1 q2:weapon_blaster,q2:weapon_chainfist,q2:weapon_shotgun,q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_etf_rifle,q2:weapon_chaingun,q2:ammo_grenades,q2:ammo_trap,q2:ammo_tesla,q2:weapon_grenadelauncher,q2:weapon_proxlauncher,q2:weapon_rocketlauncher,q2:weapon_hyperblaster,q2:weapon_boomer,q2:weapon_plasmabeam,q2:weapon_railgun,q2:weapon_phalanx,q2:weapon_disintegrator,q2:weapon_bfg q2:ammo_shells=100 q2:ammo_bullets=200 q2:ammo_cells=200 q2:ammo_rockets=50 q2:ammo_slugs=50 q2:ammo_grenades=50 q2:weapon_blaster=32767 q2:weapon_shotgun=32767 q2:weapon_supershotgun=32767 q2:weapon_machinegun=32767 q2:weapon_chaingun=32767 q2:weapon_grenadelauncher=32767 q2:weapon_rocketlauncher=32767 q2:weapon_hyperblaster=32767 q2:weapon_railgun=32767 q2:weapon_bfg=32767 q2:ammo_magslug=50 q2:ammo_trap=5 q2:weapon_boomer=1 q2:weapon_phalanx=1 q2:ammo_flechettes=200 q2:ammo_prox=50 q2:ammo_tesla=5 q2:ammo_disruptor=12 q2:weapon_proxlauncher=1 q2:weapon_chainfist=1 q2:weapon_disintegrator=1 q2:weapon_etf_rifle=1 q2:weapon_plasmabeam=1 blaster,shotgun,supershotgun,machinegun,chaingun,grenades,grenadelauncher,rocketlauncher,hyperblaster,railgun,bfg,trap,ionripper,phalanx,tesla,proxlauncher,chainfist,disintegrator,etf_rifle,heatbeam"
};
static size_t next_line;
/* Compares one produced line with the oracle's next line. */
static void line(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void line(const char *format, ...) {
  char text[8192];
  va_list args; va_start(args, format); vsnprintf(text, sizeof text, format, args); va_end(args);
  checks++;
  if (next_line >= sizeof EXPECTED / sizeof *EXPECTED || strcmp(EXPECTED[next_line], text) != 0) {
    failures++;
    fprintf(stderr, "line %zu differs\n  C:  %s\n  TS: %s\n", next_line, text, next_line < sizeof EXPECTED / sizeof *EXPECTED ? EXPECTED[next_line] : "(none)");
  }
  next_line++;
}
/* String(n) */
static const char *num(double value, char buffer[64]) {
  if (value == 0 && signbit(value)) { strcpy(buffer, "-0"); return buffer; }
  qa_net_js_number(value, buffer, 64); return buffer;
}
static void append(char *out, size_t capacity, const char *text) { size_t length = strlen(out); snprintf(out + length, capacity - length, "%s", text); }
static void plane_text(const qa_plane *plane, char *out, size_t capacity) {
  char a[64], b[64], c[64], d[64];
  if (plane == NULL) { snprintf(out, capacity, "null"); return; }
  snprintf(out, capacity, "%s,%s,%s,%s", num(plane->normal.x, a), num(plane->normal.y, b), num(plane->normal.z, c), num(plane->distance, d));
}
#define NO_ACTOR ((qa_actor_id){.slot = 0})
static qa_actor_id actor_id(int64_t slot) { qa_actor_id id; memset(&id, 0, sizeof id); id.slot = slot; id.generation = 1; return id; }

/* pmoveTouchList's slotOf over a fixed actor -> slot table. */
typedef struct slot_table { qa_actor_id actors[4]; double slots[4]; size_t count; } slot_table;
static slot_table SLOTS;
static bool slot_of(void *ctx, qa_actor_id actor, double *out) {
  EXPECT_CTX(ctx, SLOT_CTX);
  for (size_t i = 0; i < SLOTS.count; i++) if (qa_same_actor(SLOTS.actors[i], actor)) { *out = SLOTS.slots[i]; return true; }
  return false;
}
static void touches_line(const char *name, const qa_app_sim_native_pmove_touch *touches, size_t count) {
  char text[2048] = "", item[256], plane[200], slot[64];
  for (size_t i = 0; i < count; i++) {
    plane_text(&touches[i].plane, plane, sizeof plane);
    snprintf(item, sizeof item, "%s%s:%s", i ? " " : "", num(touches[i].slot, slot), plane);
    append(text, sizeof text, item);
  }
  line("%s %s", name, text);
}
static void test_pmove_contacts(void) {
  qa_plane up = {{0, 0, 1}, 24}, wall = {{-1, 0, 0}, -100};
  qa_plane slope = {{0, 0.6, 0.8}, 3}, steep = {{0, 0.8, 0.6}, 5};
  qa_actor_id a = actor_id(3), b = actor_id(5);
  SLOTS = (slot_table){{a, b}, {3, 5}, 2};
  qa_app_sim_native_slice_contact first[] = {{true, b, true, wall}, {false, NO_ACTOR, true, up}, {true, a, true, up}, {true, b, true, up}};
  size_t count = 0;
  qa_app_sim_native_pmove_touch *touches = qa_app_sim_pmove_touch_list(first, 4, NULL, 0, slot_of, &SLOT_CTX, &count);
  touches_line("touches1", touches, count); free(touches);
  qa_actor_id named = actor_id(2), door = actor_id(7), loose = actor_id(9);
  SLOTS = (slot_table){{named, door}, {2, 7}, 2};
  qa_plane derived = {{1, 0, 0}, 4};
  qa_app_sim_native_slice_contact second[] = {{true, named, true, wall}, {true, door, false, {{0, 0, 0}, 0}}};
  qa_app_sim_native_overlap overlapped[] = {{named, up}, {door, derived}, {loose, up}};
  touches = qa_app_sim_pmove_touch_list(second, 2, overlapped, 3, slot_of, &SLOT_CTX, &count);
  touches_line("touches2", touches, count); free(touches);
  qa_app_sim_native_slice_contact g1[] = {{false, NO_ACTOR, true, up}, {false, NO_ACTOR, true, wall}}, g2[] = {{false, NO_ACTOR, true, wall}};
  qa_app_sim_native_slice_contact g4[] = {{false, NO_ACTOR, true, slope}, {false, NO_ACTOR, true, steep}, {false, NO_ACTOR, false, {{0, 0, 0}, 0}}};
  qa_plane found; char p1[200], p2[200], p3[200], p4[200];
  plane_text(qa_app_sim_reported_ground_plane(g1, 2, &found) ? &found : NULL, p1, sizeof p1);
  plane_text(qa_app_sim_reported_ground_plane(g2, 1, &found) ? &found : NULL, p2, sizeof p2);
  plane_text(qa_app_sim_reported_ground_plane(NULL, 0, &found) ? &found : NULL, p3, sizeof p3);
  plane_text(qa_app_sim_reported_ground_plane(g4, 3, &found) ? &found : NULL, p4, sizeof p4);
  line("ground %s %s %s %s", p1, p2, p3, p4);
  qa_bounds bounds = {{-16, -16, -24}, {16, 16, 32}};
  struct { qa_vec3 origin; qa_bounds other; } cases[] = {
    {{85, 0, 30}, {{100, -64, 0}, {108, 64, 128}}}, {{0, 0, 24.5}, {{-64, -64, -8}, {64, 64, 0}}},
    {{-20, 70, 0}, {{-64, 80, -8}, {64, 96, 64}}}, {{0, 0, 0}, {{-16, -16, -24}, {16, 16, 32}}}, {{NAN, 0, 0}, {{0, 0, 0}, {1, 1, 1}}}};
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    qa_plane plane = qa_app_sim_overlap_plane(cases[i].origin, bounds, cases[i].other);
    char text[200]; plane_text(&plane, text, sizeof text);
    line("overlap %s", text);
  }
}

static void test_move_slices(void) {
  qa_clock_profile netquake = {0}; netquake.kind = QA_CLOCK_Q1_NETQUAKE; netquake.minimum_frame_seconds = 0.001; netquake.maximum_frame_seconds = 0.1;
  qa_clock_profile fixed = netquake; fixed.has_fixed_frame_seconds = true; fixed.fixed_frame_seconds = 0.04;
  qa_clock_profile quakeworld = {0}; quakeworld.kind = QA_CLOCK_Q1_QUAKEWORLD; quakeworld.maximum_command_milliseconds = 50;
  qa_clock_profile quake3 = {0}; quake3.kind = QA_CLOCK_Q3; quake3.server_frame_milliseconds = 50; quake3.maximum_command_milliseconds = 200;
  qa_clock_profile q2 = {0}; q2.kind = QA_CLOCK_Q2_CLASSIC; q2.frame_milliseconds = 100;
  struct { const char *name; const qa_clock_profile *clock; double ms, carry; } cases[] = {
    {"nq", &netquake, 100, 0}, {"nq", &netquake, 250, 0}, {"q3", &quake3, 100, 0}, {"qw", &quakeworld, 100, 0}, {"qw", &quakeworld, 120, 0},
    {"qw", &quakeworld, 0, 0}, {"fixed", &fixed, 100, 0}, {"fixed", &fixed, 100, 20}, {"fixed", &fixed, 33, 0}, {"fixed", &fixed, -50, 0},
    {"fixed", &fixed, NAN, 0}, {"q2", &q2, 75, 0}};
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    qa_app_sim_native_move_slices quoted = qa_app_sim_native_move_slices_quote(*cases[i].clock, cases[i].ms, cases[i].carry);
    char slices[512] = "", a[64], b[64], c[64];
    for (size_t j = 0; j < quoted.count; j++) { if (j) append(slices, sizeof slices, ","); append(slices, sizeof slices, num(quoted.slices[j], a)); }
    line("slices %s %s %s %s %s", cases[i].name, num(cases[i].ms, a), num(cases[i].carry, b), slices, num(quoted.carry, c));
    free(quoted.slices);
  }
}

static void test_mission_inventory(void) {
  const qa_q2mp_pack packs[] = {QA_Q2MP_PACK_XATRIX, QA_Q2MP_PACK_ROGUE};
  const qa_q2_edition editions[] = {QA_Q2_EDITION_CLASSIC, QA_Q2_EDITION_RERELEASE};
  for (size_t p = 0; p < 2; p++) for (size_t e = 0; e < 2; e++) {
    qa_q2mp_weapon_inventory_entry entries[32];
    size_t count = qa_q2mp_mission_weapon_inventory_edition(packs[p], editions[e], entries, 32);
    char text[2048] = "", item[160], value[64];
    for (size_t i = 0; i < count; i++) { snprintf(item, sizeof item, "%s%s=%s", i ? " " : "", entries[i].item, num(entries[i].capacity, value)); append(text, sizeof text, item); }
    line("mission %s %s %s", p == 0 ? "xatrix" : "rogue", e == 0 ? "classic" : "rerelease", text);
  }
  const char *names[] = {"ammo_trap", "ammo_tesla", "ammo_disruptor", "ammo_prox", "ammo_shells", "ammo_none"};
  for (size_t i = 0; i < 6; i++) {
    const qa_q2_ammo_limits *limits = qa_q2_rerelease_ammo_limits(names[i]);
    char a[64], b[64], c[64], d[64];
    if (limits == NULL) line("limits %s undefined", names[i]);
    else line("limits %s %s,%s,%s,%s", names[i], num(limits->tag, a), num(limits->capacity, b),
        limits->has_bandolier ? num(limits->bandolier, c) : "null", limits->has_pack ? num(limits->pack, d) : "null");
  }
}

/* Q2Weapons and mission-pack projectile hooks: registration never calls them; each still checks its sentinel. */
static void w_emit(void *c, qa_q2w_event e) { EXPECT_CTX(c, WEAPON_CTX); (void)e; }
static void w_noise(void *c, qa_actor_id a, qa_vec3 o, bool s) { EXPECT_CTX(c, WEAPON_CTX); (void)a; (void)o; (void)s; }
static void w_dodge(void *c, qa_actor_id m, qa_actor_id a, double e, qa_trace_result t) { EXPECT_CTX(c, WEAPON_CTX); (void)m; (void)a; (void)e; (void)t; }
static struct qa_q2m_context *m_monster(void *c, qa_actor_id a) { EXPECT_CTX(c, MISSION_CTX); (void)a; return NULL; }
static void m_effect(void *c, qa_q2mp_player_effect e) { EXPECT_CTX(c, MISSION_CTX); (void)e; }
static double m_gravity(void *c) { EXPECT_CTX(c, GRAVITY_CTX); return 800; }
static void test_register_mission_weapons(void) {
  const qa_q2_edition editions[] = {QA_Q2_EDITION_CLASSIC, QA_Q2_EDITION_RERELEASE};
  for (size_t e = 0; e < 2; e++) for (int program = 0; program < 4; program++) {
    qa_q2w_hooks hooks; memset(&hooks, 0, sizeof hooks);
    hooks.ctx = &WEAPON_CTX; hooks.emit = w_emit; hooks.noise = w_noise; hooks.dodge = w_dodge; hooks.lag_is_history = false;
    qa_q2w_weapons weapons;
    CHECK(qa_q2w_weapons_init(&weapons, hooks));
    qa_q2_game_services game; memset(&game, 0, sizeof game);
    game.options.edition = editions[e]; game.source_callbacks = qa_q2_source_callbacks_create();
    qa_q2mp_projectile_hooks mission; memset(&mission, 0, sizeof mission);
    mission.ctx = &MISSION_CTX; mission.monster = m_monster; mission.player_effect = m_effect;
    mission.gravity_ctx = &GRAVITY_CTX; mission.gravity = m_gravity;
    qa_comp_selected_q2_mission_weapons *result = calloc(1, sizeof *result);
    CHECK(qa_comp_register_selected_q2_mission_weapons(result, &game, &weapons, (qa_comp_selected_q2_program)program, mission));
    char order[4096] = "", definitions[8192] = "", names[2048] = "", item[160], value[64];
    for (size_t i = 0; i < result->pickup_order_count; i++) { if (i) append(order, sizeof order, ","); append(order, sizeof order, result->pickup_order[i]); }
    for (size_t i = 0; i < result->inventory_definition_count; i++) {
      snprintf(item, sizeof item, "%s%s=%s", i ? " " : "", result->inventory_definitions[i].item, num(result->inventory_definitions[i].capacity, value));
      append(definitions, sizeof definitions, item);
    }
    size_t count = 0;
    const qa_q2w_definition *const *registered = qa_q2w_weapons_registered_definitions(&weapons, &count);
    for (size_t i = 0; i < count; i++) { if (i) append(names, sizeof names, ","); append(names, sizeof names, registered[i]->name); }
    /* The oracle counts sourceCallbacks.register calls: the C registration makes exactly one. */
    line("register %s %s 1 %s %s %s", e == 0 ? "classic" : "rerelease", qa_comp_selected_q2_program_name((qa_comp_selected_q2_program)program), order, definitions, names);
    qa_comp_selected_q2_mission_weapons_destroy(result); free(result);
    qa_q2w_weapons_destroy(&weapons);
    qa_q2_source_callbacks_destroy(game.source_callbacks);
  }
}

/* bindNativeInput returns at once for a source that is not q2-native; captureNativeQ2Travel refuses it. */
typedef struct travel_call { qa_app_shared_simulation *sim; } travel_call;
static void capture_travel(void *ctx) {
  travel_call *call = ctx;
  qa_app_shared_sim_capture_native_q2_travel(call->sim, false, "", NULL, NULL);
}
static void test_native_guards(void) {
  qa_app_shared_simulation *sim = calloc(1, sizeof *sim);
  sim->source.kind = QA_SS_SOURCE_Q1;
  qa_app_shared_sim_bind_native_input(sim);
  CHECK(sim->native_primary_weapons == NULL && sim->native_primary_commands == NULL && !sim->has_dispose_native_pickup_supply);
  travel_call call = {sim};
  qa_caught_error caught;
  CHECK(!qa_try(capture_travel, &call, &caught));
  CHECK(strcmp(caught.message, "Native map travel requires a completed source frame") == 0);
  sim->source.kind = QA_SS_SOURCE_Q2_NATIVE; sim->stepping = true;
  CHECK(!qa_try(capture_travel, &call, &caught));
  CHECK(strcmp(caught.message, "Native map travel requires a completed source frame") == 0);
  free(sim);
}

int main(void) {
  test_pmove_contacts();
  test_move_slices();
  test_mission_inventory();
  test_register_mission_weapons();
  test_native_guards();
  CHECK(next_line == sizeof EXPECTED / sizeof *EXPECTED);
  if (failures) { fprintf(stderr, "%d of %d checks failed\n", failures, checks); return 1; }
  printf("test_shared_simulation_rest: %d checks passed\n", checks);
  return 0;
}
