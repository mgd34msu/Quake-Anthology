/* Port of src/ui/library/match.ts. GPL-2.0-or-later. */
#include "qa/ui/library/match.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"

typedef struct match_state match_state;
typedef struct mctx {
  match_state *state;
  int action;
} mctx;
struct match_state {
  qa_native_ui_controller *controller;
  qa_ui_match_command_fn command;
  void *command_ctx;
  char *target;
  char *team;
  char *vote;
  char *bot;
  char *skill;
  int handle;
  qa_ui_control controls[14];
  mctx ctxs[14];
  qa_ui_menu_scroll scroll;
};

enum { M_TEAM, M_JOIN, M_TARGET, M_FOLLOW, M_VOTE, M_CALLVOTE, M_YES, M_NO,
       M_BOT, M_SKILL, M_ADDBOT, M_REMOVEBOT, M_BACK };

static const qa_ui_choice team_choices[4] = {
  { "red", "Red" }, { "blue", "Blue" }, { "free", "Free for all" },
  { "spectator", "Spectator" }
};
static const qa_ui_choice vote_choices[4] = {
  { "map_restart", "Restart map" }, { "nextmap", "Next map" },
  { "map", "Change map" }, { "kick", "Kick player" }
};
static const qa_ui_choice skill_choices[5] = {
  { "1", "1" }, { "2", "2" }, { "3", "3" }, { "4", "4" }, { "5", "5" }
};
static const qa_ui_control_id scroll_ids[12] = {
  "ui:match:team", "ui:match:join", "ui:match:target", "ui:match:follow",
  "ui:match:vote", "ui:match:callvote", "ui:match:yes", "ui:match:no",
  "ui:match:bot", "ui:match:skill", "ui:match:addbot", "ui:match:removebot"
};

static void set_str(char **slot, const char *value) {
  free(*slot);
  *slot = qa_input_dup(value);
  if (*slot == NULL) qa_range_error("Out of memory");
}
static bool blank_trim(const char *text) {
  char *trimmed = qa_input_js_trim(text);
  if (trimmed == NULL) qa_range_error("Out of memory");
  bool empty = trimmed[0] == '\0';
  free(trimmed);
  return empty;
}
static void run_command(match_state *s, const char *name,
                        const char *const *args, size_t count) {
  s->command(s->command_ctx, name, args, count);
}

static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  mctx *c = v;
  match_state *s = c->state;
  const char *one[1];
  const char *two[2];
  const char *three[3];
  switch (c->action) {
    case M_JOIN:
      one[0] = s->team;
      run_command(s, "team", one, 1);
      break;
    case M_FOLLOW:
      one[0] = s->target;
      run_command(s, "follow", one, 1);
      break;
    case M_CALLVOTE:
      if (strcmp(s->vote, "map") == 0 || strcmp(s->vote, "kick") == 0) {
        two[0] = s->vote;
        two[1] = s->target;
        run_command(s, "callvote", two, 2);
      } else {
        one[0] = s->vote;
        run_command(s, "callvote", one, 1);
      }
      break;
    case M_YES:
      one[0] = "yes";
      run_command(s, "vote", one, 1);
      break;
    case M_NO:
      one[0] = "no";
      run_command(s, "vote", one, 1);
      break;
    case M_ADDBOT:
      three[0] = s->bot;
      three[1] = s->skill;
      three[2] = s->team;
      run_command(s, "addbot", three, 3);
      break;
    case M_REMOVEBOT:
      one[0] = s->bot;
      run_command(s, "kick", one, 1);
      break;
    case M_BACK: qa_native_ui_controller_close_menu(s->controller); break;
    default: break;
  }
}
static void on_select(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  mctx *c = v;
  match_state *s = c->state;
  if (c->action == M_TEAM) set_str(&s->team, value);
  else if (c->action == M_VOTE) set_str(&s->vote, value);
  else if (c->action == M_SKILL) set_str(&s->skill, value);
}
static void on_change(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  mctx *c = v;
  match_state *s = c->state;
  if (c->action == M_TARGET) set_str(&s->target, value);
  else if (c->action == M_BOT) set_str(&s->bot, value);
}
static void on_noop(void *v, qa_seat_id seat) {
  (void)v;
  (void)seat;
}

static qa_ui_control *next_control(match_state *s, size_t *at, int action) {
  qa_ui_control *control = &s->controls[*at];
  memset(control, 0, sizeof *control);
  s->ctxs[*at].state = s;
  s->ctxs[*at].action = action;
  control->ctx = &s->ctxs[*at];
  control->visible = true;
  (*at)++;
  return control;
}
static void add_button(match_state *s, size_t *at, const char *id,
                       const char *label, double row, int action, bool enabled) {
  qa_ui_control *control = next_control(s, at, action);
  control->id = id;
  control->label = label;
  control->kind = "button";
  control->rect = (qa_rect){ 64, 96 + row * 34, 496, 30 };
  control->enabled = enabled;
  control->activate_seat = on_activate;
}
static void add_choice(match_state *s, size_t *at, const char *id,
                       const char *label, const char *selected,
                       const qa_ui_choice *choices, size_t choice_count,
                       double y, int action) {
  qa_ui_control *control = next_control(s, at, action);
  control->id = id;
  control->kind = "choice";
  control->label = label;
  control->selected = selected;
  control->choices = choices;
  control->choice_count = choice_count;
  control->rect = (qa_rect){ 64, y, 496, 30 };
  control->enabled = true;
  control->select_choice = on_select;
}
static void add_text(match_state *s, size_t *at, const char *id,
                     const char *label, const char *text, double y,
                     int action) {
  qa_ui_control *control = next_control(s, at, action);
  control->id = id;
  control->kind = "text-entry";
  control->label = label;
  control->text = text;
  control->maximum_length = 64;
  control->rect = (qa_rect){ 64, y, 496, 30 };
  control->enabled = true;
  control->change_text = on_change;
  control->submit_text = on_change;
}

static qa_ui_menu factory(void *v) {
  match_state *s = v;
  size_t at = 0;
  add_choice(s, &at, "ui:match:team", "Team", s->team, team_choices, 4, 96,
             M_TEAM);
  add_button(s, &at, "ui:match:join", "Join team", 1, M_JOIN, true);
  add_text(s, &at, "ui:match:target", "Player or map", s->target, 164,
           M_TARGET);
  add_button(s, &at, "ui:match:follow", "Follow player", 3, M_FOLLOW,
             !blank_trim(s->target));
  add_choice(s, &at, "ui:match:vote", "Vote", s->vote, vote_choices, 4, 232,
             M_VOTE);
  bool vote_needs_target =
      strcmp(s->vote, "map") == 0 || strcmp(s->vote, "kick") == 0;
  add_button(s, &at, "ui:match:callvote", "Call vote", 5, M_CALLVOTE,
             !vote_needs_target || !blank_trim(s->target));
  add_button(s, &at, "ui:match:yes", "Vote yes", 6, M_YES, true);
  add_button(s, &at, "ui:match:no", "Vote no", 7, M_NO, true);
  add_text(s, &at, "ui:match:bot", "Bot name", s->bot, 368, M_BOT);
  add_choice(s, &at, "ui:match:skill", "Bot difficulty", s->skill,
             skill_choices, 5, 402, M_SKILL);
  add_button(s, &at, "ui:match:addbot", "Add bot", 10, M_ADDBOT,
             !blank_trim(s->bot));
  add_button(s, &at, "ui:match:removebot", "Remove named bot", 11,
             M_REMOVEBOT, !blank_trim(s->bot));
  add_button(s, &at, "ui:match:back", "Back", 10, M_BACK, true);
  s->controls[at - 1].rect = (qa_rect){ 64, 414, 496, 30 };
  s->scroll =
      (qa_ui_menu_scroll){ (qa_rect){ 64, 92, 512, 306 }, 442, scroll_ids, 12 };
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:application:match";
  menu.title = "Match controls";
  menu.full_screen = true;
  menu.scroll = &s->scroll;
  menu.controls = s->controls;
  menu.control_count = at;
  menu.ctx = s;
  menu.open = on_noop;
  menu.close = on_noop;
  return menu;
}

static void match_dispose(qa_ui_settings_menus *menus) {
  match_state *s = menus->state;
  if (s == NULL) return;
  qa_native_ui_controller_unregister(s->controller, s->handle);
  free(s->target);
  free(s->team);
  free(s->vote);
  free(s->bot);
  free(s->skill);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_match_menu(
    qa_native_ui_controller *controller, qa_ui_match_command_fn command,
    void *command_ctx) {
  match_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->command = command;
  s->command_ctx = command_ctx;
  set_str(&s->target, "");
  set_str(&s->team, "red");
  set_str(&s->vote, "map_restart");
  set_str(&s->bot, "sarge");
  set_str(&s->skill, "3");
  int handle = qa_native_ui_controller_register(
      controller, "menu:application:match", factory, s);
  if (handle < 0) qa_range_error("Out of memory");
  s->handle = handle;
  qa_ui_settings_menus menus = { "menu:application:match", s, match_dispose };
  return menus;
}
