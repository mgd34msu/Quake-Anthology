/* Port of src/world/session/mod-callback-inputs.ts. GPL-2.0-or-later. */
#include "qa/world/session/mod_callback_inputs.h"
#include <string.h>

static bool same(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static void add(const char **set, size_t *count, const char *name) {
  for (size_t i = 0; i < *count; i++) if (strcmp(set[i], name) == 0) return;
  set[(*count)++] = name;
}
size_t qa_mod_callback_inputs(const char *stage, const char *operation, const char **out, size_t capacity) {
  static const char *const damage[] = {"attacker", "inflictor", "amount", "knockback", "direction", "point", "normal", NULL};
  static const char *const inventory[] = {"item", "amount", NULL};
  static const char *const use[] = {"other", "activator", NULL};
  static const char *const touch[] = {"other", NULL};
  static const char *const think[] = {"elapsed", NULL};
  static const char *const pain[] = {"attacker", "amount", "knockback", NULL};
  static const char *const otherwise[] = {"attacker", "inflictor", "amount", "knockback", "point", NULL};
  const char *set[16];
  size_t count = 0;
  add(set, &count, "self");
  add(set, &count, "time");
  if (same(stage, "observe")) add(set, &count, "result");
  const char *const *additional = same(operation, "damage")                                                ? damage
                                  : same(operation, "inventory.give") || same(operation, "inventory.consume") ? inventory
                                  : same(operation, "actor.use")                                             ? use
                                  : same(operation, "actor.touch")                                           ? touch
                                  : same(operation, "actor.think")                                           ? think
                                  : same(operation, "actor.pain")                                            ? pain
                                                                                                             : otherwise;
  for (size_t i = 0; additional[i] != NULL; i++) add(set, &count, additional[i]);
  for (size_t i = 0; i < count && i < capacity; i++) out[i] = set[i];
  return count;
}
bool qa_mod_callback_input_available(const char *stage, const char *operation, const char *name) {
  const char *set[16];
  size_t count = qa_mod_callback_inputs(stage, operation, set, 16);
  for (size_t i = 0; i < count; i++) if (name != NULL && strcmp(set[i], name) == 0) return true;
  return false;
}
