/* Port of src/content/q1/missionpacks/obituaries.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_OBITUARIES_H
#define QA_CONTENT_Q1_MISSIONPACKS_OBITUARIES_H
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/missionpacks/types.h"

/* TS Q1MissionPackObituaryInput is q1Obituary's input (qa_q1_obituary_input). */
/* TS MissionPackObituaryContext; tag_score NULL = TS absent. */
typedef struct qa_q1mp_obituary_context {
  qa_q1mp_pack pack;
  const char *inflictor_classname;
  const char *attacker_death_type;
  double victim_saved_team;
  double gamecfg;
  void *tag_ctx; double (*tag_score)(void *ctx);
} qa_q1mp_obituary_context;
/* Message text and arguments borrow the input's strings. */
qa_q1_obituary qa_q1mp_mission_pack_obituary(const qa_q1_obituary_input *input, const qa_q1mp_obituary_context *context);

#endif
