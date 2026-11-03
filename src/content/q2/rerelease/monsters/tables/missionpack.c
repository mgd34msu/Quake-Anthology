/* Port of src/content/q2/rerelease/monsters/tables/missionpack.ts. Every entry
 * was read from the shipped mmove_t of game_x64.dll (see the TS file).
 * GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/tables/missionpack.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

#define STEP "monster_footstep"
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define MOVE(name, cbs) {name, cbs, N(cbs), false, NULL}

static const qa_q2rm_frame_callback GEKK_DEATH1[] = {{5, "gekk_shrink"}};
static const qa_q2rm_frame_callback FIXBOT_STAND2[] = {{18, "change_to_roam"}};
static const qa_q2rm_frame_callback STALKER_RUN[] = {{0, STEP}, {2, STEP}};
static const qa_q2rm_frame_callback STALKER_WALK[] = {{0, STEP}, {4, STEP}};
static const qa_q2rm_frame_callback STALKER_FALSE_DEATH_END[] = {{3, STEP}};
static const qa_q2rm_frame_callback STALKER_SWING_L[] = {{3, STEP}, {4, "stalker_swing_attack"}, {7, STEP}};
static const qa_q2rm_frame_callback STALKER_SWING_R[] = {{1, STEP}, {2, "stalker_swing_attack"}, {4, STEP}};
static const qa_q2rm_frame_callback STALKER_JUMP_STRAIGHTUP[] = {{0, "stalker_jump_straightup"}, {1, "stalker_jump_wait_land"}, {2, STEP}};
static const qa_q2rm_frame_callback STALKER_JUMP_UP[] = {{4, "stalker_jump_up"}, {5, "stalker_jump_wait_land"}, {6, STEP}};
static const qa_q2rm_frame_callback STALKER_JUMP_DOWN[] = {{4, "stalker_jump_down"}, {5, "stalker_jump_wait_land"}, {6, STEP}};
static const qa_q2rm_frame_callback STALKER_DEATH[] = {{8, STEP}};
static const qa_q2rm_frame_callback HOVER_DEATH1[] = {
  {1, "hover_dying"}, {3, "hover_dying"}, {5, "hover_dying"}, {6, "hover_dying"}, {8, "hover_dying"}, {9, "hover_dying"}};
static const qa_q2rm_frame_callback CARRIER_DEATH[] = {{0, "BossExplode"}};
static const qa_q2rm_frame_callback CARRIER_SPAWN[] = {
  {6, "carrier_prep_spawn"}, {7, "carrier_start_spawn"}, {8, "carrier_ready_spawn"}, {11, "carrier_spawn_check"}};
static const qa_q2rm_frame_callback WIDOW2_WALK[] = {{0, "widow2_step"}, {5, "widow2_step"}};
static const qa_q2rm_frame_callback WIDOW2_RUN[] = {{0, "widow2_step"}, {5, "widow2_step"}};
static const qa_q2rm_frame_callback WIDOW2_ATTACK_PRE_BEAM[] = {{1, "widow2_step"}, {3, "widow2_attack_beam"}};

static const qa_q2rm_move_callbacks GEKK[] = {MOVE("gekk_move_death1", GEKK_DEATH1)};
static const qa_q2rm_move_callbacks FIXBOT[] = {MOVE("fixbot_move_stand2", FIXBOT_STAND2)};
static const qa_q2rm_move_callbacks STALKER[] = {
  MOVE("stalker_move_run", STALKER_RUN),
  MOVE("stalker_move_walk", STALKER_WALK),
  MOVE("stalker_move_false_death_end", STALKER_FALSE_DEATH_END),
  MOVE("stalker_move_swing_l", STALKER_SWING_L),
  MOVE("stalker_move_swing_r", STALKER_SWING_R),
  MOVE("stalker_move_jump_straightup", STALKER_JUMP_STRAIGHTUP),
  MOVE("stalker_move_jump_up", STALKER_JUMP_UP),
  MOVE("stalker_move_jump_down", STALKER_JUMP_DOWN),
  MOVE("stalker_move_death", STALKER_DEATH),
};
static const qa_q2rm_move_callbacks DAEDALUS[] = {MOVE("hover_move_death1", HOVER_DEATH1)};
static const qa_q2rm_move_callbacks CARRIER[] = {
  MOVE("carrier_move_death", CARRIER_DEATH),
  {"carrier_move_spawn", CARRIER_SPAWN, N(CARRIER_SPAWN), true, "carrier_run"},
};
static const qa_q2rm_move_callbacks WIDOW2[] = {
  MOVE("widow2_move_walk", WIDOW2_WALK),
  MOVE("widow2_move_run", WIDOW2_RUN),
  MOVE("widow2_move_attack_pre_beam", WIDOW2_ATTACK_PRE_BEAM),
};
static const struct { const char *classname; const qa_q2rm_move_callbacks *moves; size_t count; } CALLBACKS[] = {
  {"monster_gekk", GEKK, N(GEKK)},
  {"monster_fixbot", FIXBOT, N(FIXBOT)},
  {"monster_stalker", STALKER, N(STALKER)},
  {"monster_daedalus", DAEDALUS, N(DAEDALUS)},
  {"monster_carrier", CARRIER, N(CARRIER)},
  {"monster_widow2", WIDOW2, N(WIDOW2)},
};
const qa_q2rm_move_callbacks *qa_q2rm_rerelease_mission_pack_callbacks(const char *classname, size_t *out_count) {
  for (size_t i = 0; i < N(CALLBACKS); i++)
    if (strcmp(CALLBACKS[i].classname, classname) == 0) { *out_count = CALLBACKS[i].count; return CALLBACKS[i].moves; }
  *out_count = 0;
  return NULL;
}

/* The gekk swims and stands underwater on gekk_move_swim_start's frames 291-322 and distances (0x146e08, 0x146da8,
 * 0x147098), with no per-frame callbacks. */
#define SWIM(ai, source, d) {ai, source, d, NULL, 0, -1}
#define SWIM_FRAMES(ai, source) \
  SWIM(ai, source, 14), SWIM(ai, source, 14), SWIM(ai, source, 14), SWIM(ai, source, 14), SWIM(ai, source, 16), \
  SWIM(ai, source, 16), SWIM(ai, source, 16), SWIM(ai, source, 18), SWIM(ai, source, 18), SWIM(ai, source, 18), \
  SWIM(ai, source, 20), SWIM(ai, source, 20), SWIM(ai, source, 22), SWIM(ai, source, 22), SWIM(ai, source, 24), \
  SWIM(ai, source, 24), SWIM(ai, source, 26), SWIM(ai, source, 26), SWIM(ai, source, 24), SWIM(ai, source, 24), \
  SWIM(ai, source, 22), SWIM(ai, source, 22), SWIM(ai, source, 22), SWIM(ai, source, 22), SWIM(ai, source, 22), \
  SWIM(ai, source, 22), SWIM(ai, source, 22), SWIM(ai, source, 22), SWIM(ai, source, 18), SWIM(ai, source, 18), \
  SWIM(ai, source, 18), SWIM(ai, source, 18)
static const qa_q2m_frame GEKK_SWIM_RUN[] = {SWIM_FRAMES(QA_Q2M_AI_RUN, NULL)};
static const qa_q2m_frame GEKK_SWIM_STAND2[] = {SWIM_FRAMES(QA_Q2M_AI_SOURCE, "ai_stand2")};
static const qa_q2m_move GEKK_REPLACEMENTS[] = {
  {"gekk_move_swim_loop", 291, 322, "gekk_swim_loop", 0, GEKK_SWIM_RUN, N(GEKK_SWIM_RUN)},
  {"gekk_move_standunderwater", 291, 322, NULL, 0, GEKK_SWIM_STAND2, N(GEKK_SWIM_STAND2)},
};
const qa_q2m_move *qa_q2rm_rerelease_mission_pack_replacements(const char *classname, size_t *out_count) {
  if (strcmp(classname, "monster_gekk") == 0) { *out_count = N(GEKK_REPLACEMENTS); return GEKK_REPLACEMENTS; }
  *out_count = 0;
  return NULL;
}

static const qa_q2rm_move_callbacks *find_spec(const qa_q2rm_move_callbacks *specs, size_t count, const char *move) {
  for (size_t i = 0; i < count; i++) if (strcmp(specs[i].move, move) == 0) return &specs[i];
  return NULL;
}
static const qa_q2m_move *find_replacement(const qa_q2m_move *replacements, size_t count, const char *move) {
  for (size_t i = 0; i < count; i++) if (strcmp(replacements[i].name, move) == 0) return &replacements[i];
  return NULL;
}

qa_q2rm_mission_pack_moves qa_q2rm_rerelease_mission_pack_moves(const char *classname, const qa_q2m_move *moves, size_t count) {
  qa_q2rm_mission_pack_moves out = {0};
  size_t spec_count = 0, replacement_count = 0;
  const qa_q2rm_move_callbacks *specs = qa_q2rm_rerelease_mission_pack_callbacks(classname, &spec_count);
  const qa_q2m_move *replacements = qa_q2rm_rerelease_mission_pack_replacements(classname, &replacement_count);
  if (specs == NULL && replacements == NULL) return out;
  size_t frame_total = 0, action_total = 0;
  for (size_t i = 0; i < count; i++) {
    if (find_replacement(replacements, replacement_count, moves[i].name) != NULL) continue;
    const qa_q2rm_move_callbacks *spec = find_spec(specs, spec_count, moves[i].name);
    if (spec == NULL) continue;
    frame_total += moves[i].frame_count;
    action_total += spec->callback_count;
  }
  out.moves = malloc((count ? count : 1) * sizeof *out.moves);
  out.frames = malloc((frame_total ? frame_total : 1) * sizeof *out.frames);
  out.actions = malloc((action_total ? action_total : 1) * sizeof *out.actions);
  if (!out.moves || !out.frames || !out.actions) qa_range_error("Out of memory");
  out.count = count;
  size_t frame_at = 0, action_at = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_q2m_move *replacement = find_replacement(replacements, replacement_count, moves[i].name);
    if (replacement != NULL) { out.moves[i] = *replacement; continue; }
    const qa_q2rm_move_callbacks *spec = find_spec(specs, spec_count, moves[i].name);
    out.moves[i] = moves[i];
    if (spec == NULL) continue;
    if (spec->has_end) out.moves[i].end = spec->end;
    qa_q2m_frame *frames = out.frames + frame_at;
    for (size_t f = 0; f < moves[i].frame_count; f++) {
      frames[f] = moves[i].frames[f];
      frames[f].actions = NULL;
      frames[f].action_count = 0;
      for (size_t k = 0; k < spec->callback_count; k++) {
        if (spec->callbacks[k].frame != f) continue;
        out.actions[action_at] = (qa_q2m_action){false, spec->callbacks[k].name, 0, false};
        frames[f].actions = &out.actions[action_at++];
        frames[f].action_count = 1;
      }
    }
    out.moves[i].frames = frames;
    frame_at += moves[i].frame_count;
  }
  return out;
}

void qa_q2rm_mission_pack_moves_free(qa_q2rm_mission_pack_moves *moves) {
  free(moves->moves); free(moves->frames); free(moves->actions);
  memset(moves, 0, sizeof *moves);
}
