/* Port of src/content/q1/missionpacks/obituaries.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_OBITUARIES_H
#define QA_CONTENT_Q1_MISSIONPACKS_OBITUARIES_H
#include "qa/content/q1/missionpacks/types.h"

/* Mirrors (TS owner: src/content/q1/base/rules.ts, unlanded):
 * q1Obituary input/result. Strings are borrowed. */
typedef struct qa_q1mp_obituary_person {
  qa_actor_id actor; bool has_actor; const char *name; const char *classname; const char *weapon;
  bool is_player; bool is_monster; bool brush; double team; double water_level; const char *water_type;
} qa_q1mp_obituary_person;
typedef struct qa_q1mp_obituary_input {
  qa_q1mp_obituary_person victim; bool has_attacker; qa_q1mp_obituary_person attacker;
  const char *edition; const char *death_type; double teamplay;
  bool has_telefrag_owner; qa_q1mp_obituary_person telefrag_owner;
  void *random_ctx; double (*random)(void *ctx);
} qa_q1mp_obituary_input;
typedef struct qa_q1mp_obituary {
  bool has_message; char text[512]; const char *args[4]; size_t arg_count;
  bool has_score; qa_actor_id score_actor; double score_delta; const char *achievement;
} qa_q1mp_obituary;
/* Mirrored q1Obituary (same TS owner; ext until base/rules lands).
 * The port replays firstRoll exactly as the TS fallback closure. */
qa_q1mp_obituary qa_q1mp_ext_q1_obituary(const qa_q1mp_obituary_input *input, bool has_attacker_override, bool attacker_null);
typedef struct qa_q1mp_obituary_context {
  qa_q1mp_pack pack; const char *inflictor_classname; const char *attacker_death_type;
  double victim_saved_team; double gamecfg; void *tag_ctx; double (*tag_score)(void *ctx); /* NULL absent */
} qa_q1mp_obituary_context;
qa_q1mp_obituary qa_q1mp_mission_pack_obituary(const qa_q1mp_obituary_input *input, const qa_q1mp_obituary_context *context);

#endif
