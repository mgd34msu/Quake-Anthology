/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/actions.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define JUMPED 0x80
static qa_bot_input *inp(qa_bot_action_buffer *b, int64_t c) {
  if (!b->has_inputs)
    qa_range_error("bot action input allocation has been shut down");
  if (c < 0 || c >= b->client_capacity)
    qa_range_error("bot action client %lld outside 0..%lld", (long long)c,
                   (long long)(b->client_capacity - 1));
  return &b->inputs[c];
}
static int32_t src_int(int64_t v, const char *n) {
  if (v < INT32_MIN || v > INT32_MAX)
    qa_range_error("%s must be a signed 32-bit integer", n);
  return (int32_t)v;
}
static void cmd(qa_bot_action_buffer *b, int64_t c, const char *prefix, const char *v,
                const char *nm) {
  size_t n = strlen(prefix) + strlen(v);
  if (n >= 32000)
    qa_range_error("%s exceeds the 32000-byte botlib va buffer", nm);
  char *buf = malloc(n + 1);
  if (!buf)
    return;
  memcpy(buf, prefix, strlen(prefix));
  memcpy(buf + strlen(prefix), v, strlen(v) + 1);
  if (b->client_command)
    b->client_command(b->host_ctx, c, buf);
  free(buf);
}
void qa_bot_action_buffer_init(qa_bot_action_buffer *b) { memset(b, 0, sizeof *b); }
void qa_bot_action_buffer_destroy(qa_bot_action_buffer *b) {
  free(b->inputs);
  b->inputs = NULL;
  b->has_inputs = 0;
}
void qa_bot_action_buffer_setup(qa_bot_action_buffer *b, int64_t mc) {
  if (mc < 0 || mc * 40 > 2147483647LL)
    qa_range_error("bot action maxClients %lld exceeds the source signed allocation range",
                   (long long)mc);
  free(b->inputs);
  b->inputs = mc ? calloc((size_t)mc, sizeof(qa_bot_input)) : NULL;
  b->client_capacity = mc;
  b->has_inputs = 1;
}
void qa_bot_action_buffer_shutdown(qa_bot_action_buffer *b) {
  free(b->inputs);
  b->inputs = NULL;
  b->has_inputs = 0;
}
void qa_bot_action_buffer_dispose_resources(qa_bot_action_buffer *b) {
  b->has_inputs = 0;
  b->inputs = NULL;
}
int64_t qa_bot_action_buffer_get_max_clients(const qa_bot_action_buffer *b) {
  return b->client_capacity;
}
void qa_bot_action_say(qa_bot_action_buffer *b, int64_t c, const char *t) {
  cmd(b, c, "say ", t, "bot say text");
}
void qa_bot_action_say_team(qa_bot_action_buffer *b, int64_t c, const char *t) {
  cmd(b, c, "say_team ", t, "bot team say text");
}
void qa_bot_action_tell(qa_bot_action_buffer *b, int64_t c, int64_t r, const char *t) {
  char p[64];
  snprintf(p, sizeof p, "tell %d, ", src_int(r, "bot tell recipient"));
  cmd(b, c, p, t, "bot tell text");
}
void qa_bot_action_use_item(qa_bot_action_buffer *b, int64_t c, const char *t) {
  cmd(b, c, "use ", t, "bot item name");
}
void qa_bot_action_drop_item(qa_bot_action_buffer *b, int64_t c, const char *t) {
  cmd(b, c, "drop ", t, "bot item name");
}
void qa_bot_action_use_inventory(qa_bot_action_buffer *b, int64_t c, const char *t) {
  cmd(b, c, "invuse ", t, "bot inventory name");
}
void qa_bot_action_drop_inventory(qa_bot_action_buffer *b, int64_t c, const char *t) {
  cmd(b, c, "invdrop ", t, "bot inventory name");
}
void qa_bot_action_command(qa_bot_action_buffer *b, int64_t c, const char *t) {
  if (b->client_command)
    b->client_command(b->host_ctx, c, t);
}
void qa_bot_action_add_flag(qa_bot_action_buffer *b, int64_t c, int32_t f) {
  inp(b, c)->action_flags |= f;
}
void qa_bot_action_action(qa_bot_action_buffer *b, int64_t c, int64_t f) {
  inp(b, c)->action_flags |= src_int(f, "bot action flags");
}
void qa_bot_action_select_weapon(qa_bot_action_buffer *b, int64_t c, int64_t w) {
  inp(b, c)->weapon = src_int(w, "bot weapon");
}
void qa_bot_action_jump(qa_bot_action_buffer *b, int64_t c) {
  qa_bot_input *i = inp(b, c);
  if (i->action_flags & JUMPED)
    i->action_flags &= ~QA_BOT_ACTION_JUMP;
  else
    i->action_flags |= QA_BOT_ACTION_JUMP;
}
void qa_bot_action_delayed_jump(qa_bot_action_buffer *b, int64_t c) {
  qa_bot_input *i = inp(b, c);
  if (i->action_flags & JUMPED)
    i->action_flags &= ~QA_BOT_ACTION_DELAYED_JUMP;
  else
    i->action_flags |= QA_BOT_ACTION_DELAYED_JUMP;
}
void qa_bot_action_move(qa_bot_action_buffer *b, int64_t c, qa_vec3 d, double sp) {
  qa_bot_input *i = inp(b, c);
  i->direction = (qa_vec3){(float)d.x, (float)d.y, (float)d.z};
  float s = (float)sp;
  i->speed = s > 400 ? 400 : s < -400 ? -400 : s;
}
void qa_bot_action_view(qa_bot_action_buffer *b, int64_t c, qa_vec3 a) {
  inp(b, c)->view_angles = (qa_vec3){(float)a.x, (float)a.y, (float)a.z};
}
void qa_bot_action_end_regular(qa_bot_action_buffer *b, int64_t c, double t) {
  (void)b;
  (void)c;
  (void)t;
}
qa_bot_input qa_bot_action_get_input(qa_bot_action_buffer *b, int64_t c, double t) {
  qa_bot_input *i = inp(b, c);
  i->think_time = (float)t;
  return *i;
}
void qa_bot_action_reset_input(qa_bot_action_buffer *b, int64_t c) {
  qa_bot_input *i = inp(b, c);
  i->action_flags &= ~JUMPED;
  i->think_time = 0;
  i->direction = (qa_vec3){0, 0, 0};
  i->speed = 0;
  int j = (i->action_flags & QA_BOT_ACTION_JUMP) != 0;
  i->action_flags = j ? JUMPED : 0;
}
