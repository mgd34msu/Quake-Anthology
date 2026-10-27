/* Port of src/ui/settings/language.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/language.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"

static void seat_name(int64_t seat, char *out, size_t size) {
  snprintf(out, size, "ui_seat%lld_language", (long long)(seat + 1));
}
static bool lowercase_name(const char *value) {
  if (value[0] == '\0') return false;
  for (const char *p = value; *p != '\0'; p++)
    if (*p < 'a' || *p > 'z') return false;
  return true;
}
static char *validate_language(void *ctx, const char *value) {
  (void)ctx;
  return lowercase_name(value)
             ? NULL
             : qa_input_dup("Expected a lowercase language name");
}

void qa_ui_register_language_settings(qa_cvar_registry *cvars) {
  for (int64_t seat = 0; seat < 4; seat++) {
    char key[64];
    seat_name(seat, key, sizeof key);
    qa_cvar_snapshot out;
    if (qa_cvar_registry_register(cvars, key, "english",
                                  QA_CVAR_FLAG_ARCHIVE, &out))
      qa_cvar_read_free(&out);
    qa_cvar_value_binding binding = { NULL, validate_language, NULL };
    qa_cvar_registry_bind_value(cvars, key, binding);
  }
}

char *qa_ui_read_seat_language(qa_cvar_registry *cvars, int64_t seat) {
  char key[64];
  seat_name(seat, key, sizeof key);
  qa_cvar_read read;
  if (qa_cvar_registry_find(cvars, key, &read)) {
    char *value = qa_input_dup(read.value);
    qa_cvar_read_free(&read);
    return value;
  }
  return qa_input_dup("english");
}

void qa_ui_write_seat_language(qa_cvar_registry *cvars, int64_t seat,
                               const char *language) {
  if (!lowercase_name(language))
    qa_range_error("Expected a lowercase language name");
  char key[64];
  seat_name(seat, key, sizeof key);
  qa_cvar_snapshot out;
  if (qa_cvar_registry_set(cvars, key, language, false, &out))
    qa_cvar_read_free(&out);
}
