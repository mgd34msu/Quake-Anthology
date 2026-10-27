/* Port of src/core/q3-product-policy.ts. GPL-2.0-or-later. */
#include "qa/core/q3_product_policy.h"

#include <string.h>

/* CvarFlag.Init from cvars/index.ts (not yet ported). */
#define QA_CVAR_FLAG_INIT 16

const char *qa_q3_product_kind_name(qa_q3_product_kind kind) {
  switch (kind) {
    case QA_Q3_PRODUCT_PRERELEASE_DEMO: return "prerelease-demo";
    case QA_Q3_PRODUCT_PRERELEASE_TA_DEMO: return "prerelease-ta-demo";
    default: return "retail";
  }
}

bool qa_q3_product_kind_parse(const char *text, qa_q3_product_kind *out) {
  if (strcmp(text, "retail") == 0) *out = QA_Q3_PRODUCT_RETAIL;
  else if (strcmp(text, "prerelease-demo") == 0)
    *out = QA_Q3_PRODUCT_PRERELEASE_DEMO;
  else if (strcmp(text, "prerelease-ta-demo") == 0)
    *out = QA_Q3_PRODUCT_PRERELEASE_TA_DEMO;
  else return false;
  return true;
}

const char *qa_q3_team_arena_ui_name(qa_q3_team_arena_ui ui) {
  return ui == QA_Q3_TEAM_ARENA_UI_DEMO ? "demo" : "retail";
}

bool qa_q3_team_arena_ui_parse(const char *text, qa_q3_team_arena_ui *out) {
  if (strcmp(text, "retail") == 0) *out = QA_Q3_TEAM_ARENA_UI_RETAIL;
  else if (strcmp(text, "demo") == 0) *out = QA_Q3_TEAM_ARENA_UI_DEMO;
  else return false;
  return true;
}

const char *qa_q3_mount_kind_name(qa_q3_mount_kind kind) {
  return kind == QA_Q3_MOUNT_DEMO ? "demo" : "none";
}

bool qa_q3_mount_kind_parse(const char *text, qa_q3_mount_kind *out) {
  if (strcmp(text, "none") == 0) *out = QA_Q3_MOUNT_NONE;
  else if (strcmp(text, "demo") == 0) *out = QA_Q3_MOUNT_DEMO;
  else return false;
  return true;
}

qa_q3_product_policy qa_q3_product_policy_make(
    bool prerelease_demo, bool prerelease_team_arena_demo) {
  qa_q3_product_policy policy;
  if (prerelease_demo) {
    policy.kind = QA_Q3_PRODUCT_PRERELEASE_DEMO;
    policy.team_arena_ui = prerelease_team_arena_demo
                               ? QA_Q3_TEAM_ARENA_UI_DEMO
                               : QA_Q3_TEAM_ARENA_UI_RETAIL;
  } else if (prerelease_team_arena_demo) {
    policy.kind = QA_Q3_PRODUCT_PRERELEASE_TA_DEMO;
    policy.team_arena_ui = QA_Q3_TEAM_ARENA_UI_RETAIL;
  } else {
    policy.kind = QA_Q3_PRODUCT_RETAIL;
    policy.team_arena_ui = QA_Q3_TEAM_ARENA_UI_RETAIL;
  }
  return policy;
}

qa_q3_product_policy qa_register_q3_product_policy(
    const qa_q3_cvar_registrar *cvars) {
  int64_t demo_value = 0, team_arena_value = 0;
  if (!cvars->register_cvar(cvars->ctx, "com_prereleaseDemo", "0",
                            QA_CVAR_FLAG_INIT, &demo_value)) {
    demo_value = 0;
  }
  if (!cvars->register_cvar(cvars->ctx, "com_prereleaseTeamArenaDemo", "0",
                            QA_CVAR_FLAG_INIT, &team_arena_value)) {
    team_arena_value = 0;
  }
  return qa_q3_product_policy_make(demo_value != 0, team_arena_value != 0);
}

bool qa_q3_prerelease_demo(qa_q3_product_policy policy) {
  return policy.kind == QA_Q3_PRODUCT_PRERELEASE_DEMO;
}

bool qa_q3_team_arena_demo(qa_q3_product_policy policy) {
  return policy.kind == QA_Q3_PRODUCT_PRERELEASE_TA_DEMO ||
         (policy.kind == QA_Q3_PRODUCT_PRERELEASE_DEMO &&
          policy.team_arena_ui == QA_Q3_TEAM_ARENA_UI_DEMO);
}

qa_q3_mount_restriction qa_q3_mount_restriction_make(
    qa_q3_product_policy policy, bool fs_restrict) {
  qa_q3_mount_restriction restriction;
  if (fs_restrict || qa_q3_prerelease_demo(policy)) {
    restriction.kind = QA_Q3_MOUNT_DEMO;
    restriction.directory = "demota";
    restriction.pak_checksum = 437558517;
  } else {
    restriction.kind = QA_Q3_MOUNT_NONE;
    restriction.directory = NULL;
    restriction.pak_checksum = 0;
  }
  return restriction;
}

static const char *const demo_map_commands[] = {"map"};
static const char *const retail_map_commands[] = {"map", "devmap", "spmap",
                                                  "spdevmap"};

const char *const *qa_q3_product_map_commands(qa_q3_product_policy policy,
                                              size_t *out_count) {
  if (qa_q3_prerelease_demo(policy)) {
    *out_count = 1;
    return demo_map_commands;
  }
  *out_count = 4;
  return retail_map_commands;
}

qa_q3_team_arena_catalog_policy qa_q3_team_arena_catalog_policy_make(
    qa_q3_product_policy policy) {
  bool demo = qa_q3_team_arena_demo(policy);
  qa_q3_team_arena_catalog_policy catalog;
  catalog.game_info = demo ? "demogameinfo.txt" : "gameinfo.txt";
  catalog.team_info = demo ? "demoteaminfo.txt" : "teaminfo.txt";
  catalog.additional_teams = !demo;
  catalog.additional_arenas = !demo;
  return catalog;
}
