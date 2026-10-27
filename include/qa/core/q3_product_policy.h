/*
 * Port of src/core/q3-product-policy.ts: PRE_RELEASE_DEMO /
 * PRE_RELEASE_TADEMO product selectors from id Software's files.c,
 * sv_ccmds.c and ui_main.c. C accommodations: the discriminated
 * unions become kind enums plus payloads, with name/parse helpers
 * returning the exact TS strings. registerQ3ProductPolicy uses only
 * CvarRegistry.register (cvars/index, not yet ported), which becomes
 * a one-method callback interface; CvarFlag.Init (16) is passed
 * through it. GPL-2.0-or-later.
 */
#ifndef QA_CORE_Q3_PRODUCT_POLICY_H
#define QA_CORE_Q3_PRODUCT_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum qa_q3_product_kind {
  QA_Q3_PRODUCT_RETAIL,
  QA_Q3_PRODUCT_PRERELEASE_DEMO,
  QA_Q3_PRODUCT_PRERELEASE_TA_DEMO,
} qa_q3_product_kind;

typedef enum qa_q3_team_arena_ui {
  QA_Q3_TEAM_ARENA_UI_RETAIL,
  QA_Q3_TEAM_ARENA_UI_DEMO,
} qa_q3_team_arena_ui;

const char *qa_q3_product_kind_name(qa_q3_product_kind kind);
bool qa_q3_product_kind_parse(const char *text, qa_q3_product_kind *out);
const char *qa_q3_team_arena_ui_name(qa_q3_team_arena_ui ui);
bool qa_q3_team_arena_ui_parse(const char *text, qa_q3_team_arena_ui *out);

typedef struct qa_q3_product_policy {
  qa_q3_product_kind kind;
  qa_q3_team_arena_ui team_arena_ui; /* only for PRERELEASE_DEMO */
} qa_q3_product_policy;

qa_q3_product_policy qa_q3_product_policy_make(bool prerelease_demo,
                                               bool prerelease_team_arena_demo);

typedef struct qa_q3_cvar_registrar {
  void *ctx;
  /* Mirrors register(...).integerValue ?? 0: false means no snapshot. */
  bool (*register_cvar)(void *ctx, const char *name,
                        const char *default_value, int64_t flags,
                        int64_t *out_integer_value);
} qa_q3_cvar_registrar;

/* Call after startup +set values have been applied, before mounting. */
qa_q3_product_policy qa_register_q3_product_policy(
    const qa_q3_cvar_registrar *cvars);

bool qa_q3_prerelease_demo(qa_q3_product_policy policy);
bool qa_q3_team_arena_demo(qa_q3_product_policy policy);

typedef enum qa_q3_mount_kind {
  QA_Q3_MOUNT_NONE,
  QA_Q3_MOUNT_DEMO,
} qa_q3_mount_kind;

const char *qa_q3_mount_kind_name(qa_q3_mount_kind kind);
bool qa_q3_mount_kind_parse(const char *text, qa_q3_mount_kind *out);

typedef struct qa_q3_mount_restriction {
  qa_q3_mount_kind kind;
  const char *directory; /* "demota" for DEMO, NULL for NONE */
  int64_t pak_checksum;  /* 437558517 for DEMO, 0 for NONE */
} qa_q3_mount_restriction;

qa_q3_mount_restriction qa_q3_mount_restriction_make(
    qa_q3_product_policy policy, bool fs_restrict);

/* Borrowed static string array. */
const char *const *qa_q3_product_map_commands(qa_q3_product_policy policy,
                                              size_t *out_count);

typedef struct qa_q3_team_arena_catalog_policy {
  const char *game_info;
  const char *team_info;
  bool additional_teams;
  bool additional_arenas;
} qa_q3_team_arena_catalog_policy;

qa_q3_team_arena_catalog_policy qa_q3_team_arena_catalog_policy_make(
    qa_q3_product_policy policy);

typedef struct qa_q3_application_product {
  qa_q3_product_policy policy;
  qa_q3_mount_restriction restriction;
} qa_q3_application_product;

#endif
