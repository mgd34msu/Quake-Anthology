/* Port of src/settings/server/common.ts. GPL-2.0-or-later. */
#include "qa/settings/server/common.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS String(number): plain decimal in [1e-6, 1e21), exponent form
 * outside it. Finds the shortest round-tripping decimal in the
 * matching notation. */
static void js_number_text(double value, char *out, size_t size) {
  qa_net_js_number(value, out, size);
}

void qa_server_limit(qa_server_setting_definition *out, qa_server_setting_id id,
                     const char *name, const char *label, double default_value,
                     const char *description, qa_server_apply_at apply_at,
                     bool integer) {
  memset(out, 0, sizeof *out);
  out->id = id;
  out->label = label;
  out->description = description;
  js_number_text(default_value, out->default_text, sizeof out->default_text);
  out->default_value = out->default_text;
  out->apply_at = apply_at;
  out->target.is_bit = false;
  out->target.name = name;
  out->kind = QA_SERVER_SETTING_SLIDER;
  out->minimum = 0;
  out->maximum = 2147483647;
  out->step = integer ? 1 : 0.25;
  out->integer = integer;
}

qa_server_setting_definition qa_server_toggle(qa_server_setting_id id,
                                              qa_server_setting_target target,
                                              const char *label,
                                              bool default_value,
                                              const char *description,
                                              qa_server_apply_at apply_at) {
  qa_server_setting_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.id = id;
  definition.target = target;
  definition.label = label;
  definition.description = description;
  definition.apply_at = apply_at;
  definition.default_value = default_value ? "1" : "0";
  definition.kind = QA_SERVER_SETTING_TOGGLE;
  return definition;
}

qa_server_setting_definition qa_server_friendly_fire(
    qa_server_setting_target target, bool default_value) {
  return qa_server_toggle(
      "server:friendly-fire", target, "Friendly fire", default_value,
      "Allow damage to teammates using the selected combat rules. "
      "Self-damage follows those rules.",
      QA_SERVER_APPLY_LIVE);
}
