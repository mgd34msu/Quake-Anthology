/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/info.h"
#include "qa/core/cvars/info.h"
char *qa_bot_info_set_value_for_key(const char *input, const char *key, const char *value,
                                    void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_info_options o = {
      QA_COMMAND_DIALECT_Q3, 1024, QA_CVAR_INFO_CLIENT_USERINFO, true, print, print_ctx};
  return qa_set_info_value(input, key, value, &o);
}
