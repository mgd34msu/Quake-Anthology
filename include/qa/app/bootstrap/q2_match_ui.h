/* Port of src/app/bootstrap/q2-match-ui.ts. The landed composition
 * event payloads are opaque, so the fields this file reads ride
 * the view below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q2_MATCH_UI_H
#define QA_APP_BOOTSTRAP_Q2_MATCH_UI_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/hud/index.h"
typedef struct qa_app_q2_match_score_row {
  const char *name; double score, ping;
} qa_app_q2_match_score_row;
typedef struct qa_app_q2_match_menu_entry {
  const char *label;
  bool has_action;          /* TS "action" in entry */
  const char *action;       /* NULL = TS null */
  const char *command;      /* NULL = TS null */
} qa_app_q2_match_menu_entry;
typedef struct qa_app_q2_match_setting {
  const char *key;
  bool is_boolean;
  bool bool_value;
  const char *text_value;   /* String(value) for non-booleans */
} qa_app_q2_match_setting;
/* TS Extract<Q2CompositionEvent,{kind:"ctf"|"lmctf"}>.event fields. */
typedef struct qa_app_q2_match_event {
  const char *kind; /* grapple-cable|match-status|score-log|hud|scoreboard|menu|admin-settings */
  bool has_actor;
  qa_actor_id actor;
  const char *text;              /* match-status */
  const char *name;              /* score-log */
  double amount;                 /* score-log */
  double team;                   /* hud */
  bool has_captures;
  double captures[2];
  const char *flag_states[2];
  const char *tech;              /* NULL = TS null */
  const char *match;
  bool has_id_target;
  qa_actor_id id_target;
  bool has_blink_team;
  double blink_team;
  const char *rune;              /* NULL = TS null */
  bool carried_flag;
  const qa_app_q2_match_score_row *rows; size_t row_count;             /* scoreboard rows form */
  const qa_app_q2_match_score_row *red; size_t red_count;               /* scoreboard split form */
  const qa_app_q2_match_score_row *blue; size_t blue_count;
  const qa_app_q2_match_score_row *spectators; size_t spectator_count;
  bool has_rows;
  const char *title;             /* menu */
  const qa_app_q2_match_menu_entry *entries; size_t entry_count;
  const qa_app_q2_match_setting *settings; size_t setting_count;       /* admin-settings */
} qa_app_q2_match_event;
typedef void (*qa_app_q2_match_command_fn)(void *ctx, const char *name,
    const char *const *args, size_t arg_count);
typedef void (*qa_app_q2_match_print_fn)(void *ctx, const char *text);
typedef struct qa_app_q2_match_ui qa_app_q2_match_ui;
qa_app_q2_match_ui *qa_app_q2_match_ui_create(qa_actor_id actor, qa_native_ui_controller *controller,
    qa_app_q2_match_command_fn command, void *command_ctx,
    qa_app_q2_match_print_fn print, void *print_ctx);
void qa_app_q2_match_ui_destroy(qa_app_q2_match_ui *ui);
void qa_app_q2_match_ui_name(qa_app_q2_match_ui *ui, qa_actor_id actor, const char *name);
/* Borrowed prompts (owned strings inside the ui). */
const qa_ui_hud_prompt *qa_app_q2_match_ui_prompts(const qa_app_q2_match_ui *ui, size_t *out_count);
/* Aborts on an unknown event kind (TS exhaustive Error). */
void qa_app_q2_match_ui_receive(qa_app_q2_match_ui *ui, qa_app_q2_match_event event);
void qa_app_q2_match_ui_close(qa_app_q2_match_ui *ui);
#endif
