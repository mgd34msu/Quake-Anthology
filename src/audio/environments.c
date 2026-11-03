/* Port of src/audio/environments.ts. GPL-2.0-or-later. */
#include "qa/audio/environments.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include "qa/foundation/js.h"

static noreturn void plain_error(const char *message) {
  qa_raise("Error", 0, 0, "%s", message);
}

static char *dup_string(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static bool is_record(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_OBJECT;
}

static bool is_array(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_ARRAY;
}

bool qa_parse_environments(const char *text, qa_environment_warn_fn warn,
                           void *warn_ctx, qa_reverb_environment **out,
                           size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  qa_save_value *root =
      qa_parse_json_value((const uint8_t *)text, strlen(text), false);
  if (root == NULL) return false;
  bool ok = false;
  qa_reverb_environment *environments = NULL;
  size_t count = 0;
  if (!is_record(root))
    plain_error("Sound environments require an environments array");
  const qa_save_value *entries = qa_save_value_get(root, "environments");
  if (!is_array(entries))
    plain_error("Sound environments require an environments array");
  count = entries->item_count;
  if (count > 0) {
    environments = calloc(count, sizeof *environments);
    if (environments == NULL) goto done;
  }
  for (size_t i = 0; i < count; i++) {
    const qa_save_value *value = entries->items[i];
    if (!is_record(value)) plain_error("Sound environment must be an object");
    const qa_save_value *dimension = qa_save_value_get(value, "dimension");
    double dim = 0;
    if (dimension != NULL && dimension->kind != QA_SAVE_NULL &&
        dimension->kind != QA_SAVE_UNDEFINED) {
      if (dimension->kind != QA_SAVE_NUMBER || !isfinite(dimension->number))
        plain_error("Invalid sound environment dimension");
      dim = dimension->number;
    }
    environments[i].dimension = dim;
    const qa_save_value *reverbs = qa_save_value_get(value, "reverbs");
    if (reverbs != NULL && reverbs->kind != QA_SAVE_NULL &&
        reverbs->kind != QA_SAVE_UNDEFINED && !is_array(reverbs))
      plain_error("Sound environment reverbs must be an array");
    size_t reverb_count = is_array(reverbs) ? reverbs->item_count : 0;
    environments[i].reverb_count = reverb_count;
    if (reverb_count > 0) {
      environments[i].reverbs =
          calloc(reverb_count, sizeof *environments[i].reverbs);
      if (environments[i].reverbs == NULL) goto done;
    }
    for (size_t j = 0; j < reverb_count; j++) {
      const qa_save_value *entry = reverbs->items[j];
      if (!is_record(entry)) plain_error("Sound reverb must be an object");
      qa_reverb_material *material = &environments[i].reverbs[j];
      const qa_save_value *names = qa_save_value_get(entry, "materials");
      if (names != NULL && names->kind != QA_SAVE_UNDEFINED) {
        if (names->kind == QA_SAVE_STRING) {
          if (names->text[0] != '*')
            plain_error("Sound reverb wildcard must begin with *");
          /* TS leaves materials null for the wildcard string. */
        } else if (is_array(names)) {
          material->material_count = names->item_count;
          if (names->item_count > 0) {
            material->materials =
                calloc(names->item_count, sizeof *material->materials);
            if (material->materials == NULL) goto done;
          } else {
            /* Empty array is non-null in TS; mark with a dummy alloc. */
            material->materials = calloc(1, sizeof *material->materials);
            if (material->materials == NULL) goto done;
          }
          for (size_t k = 0; k < names->item_count; k++) {
            if (names->items[k]->kind != QA_SAVE_STRING)
              plain_error("Sound material must be text");
            material->materials[k] = dup_string(names->items[k]->text);
            if (material->materials[k] == NULL) goto done;
          }
        } else {
          plain_error("Sound reverb materials must be an array or wildcard");
        }
      }
      const qa_save_value *preset = qa_save_value_get(entry, "preset");
      int64_t preset_index = 0;
      if (preset != NULL && preset->kind != QA_SAVE_UNDEFINED) {
        if (preset->kind != QA_SAVE_STRING)
          plain_error("Sound reverb preset must be text");
        preset_index = -1;
        for (size_t k = 0; k < qa_reverb_preset_name_count; k++)
          if (strcmp(qa_reverb_preset_names[k], preset->text) == 0) {
            preset_index = (int64_t)k;
            break;
          }
        if (preset_index == -1) {
          if (warn != NULL) {
            char message[512];
            snprintf(message, sizeof message,
                     "Missing sound environment preset %s", preset->text);
            warn(message, warn_ctx);
          }
          preset_index = QA_REVERB_PRESET_PLAIN;
        }
      }
      material->preset_index = preset_index;
    }
  }
  *out = environments;
  *out_count = count;
  environments = NULL;
  ok = true;
done:
  if (environments != NULL) qa_reverb_environments_free(environments, count);
  qa_save_value_free(root);
  return ok;
}

void qa_reverb_environments_free(qa_reverb_environment *environments,
                                 size_t count) {
  if (environments == NULL) return;
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < environments[i].reverb_count; j++) {
      qa_reverb_material *material = &environments[i].reverbs[j];
      for (size_t k = 0; k < material->material_count; k++)
        free(material->materials[k]);
      free(material->materials);
    }
    free(environments[i].reverbs);
  }
  free(environments);
}

qa_efx_reverb_params qa_reverb_preset(int64_t index) {
  if (index < 0 || (uint64_t)index >= qa_reverb_preset_count)
    qa_range_error("Unknown reverb preset %lld", (long long)index);
  return qa_reverb_presets[index];
}

static const qa_vec3 probes[14] = {
    {0, 0, -1}, {0, 0, 1},
    {0.707106769, 0, 0.707106769},
    {0.353553385, 0.612372458, 0.707106769},
    {-0.353553444, 0.612372458, 0.707106769},
    {-0.707106769, -6.18172393e-8, 0.707106769},
    {-0.353553325, -0.612372518, 0.707106769},
    {0.353553355, -0.612372458, 0.707106769},
    {1, 0, -4.37113883e-8},
    {0.49999997, 0.866025448, -4.37113883e-8},
    {-0.50000006, 0.866025388, -4.37113883e-8},
    {-1, -8.74227766e-8, -4.37113883e-8},
    {-0.499999911, -0.866025448, -4.37113883e-8},
    {0.499999911, -0.866025448, -4.37113883e-8},
};

static qa_efx_reverb_params interpolate(const qa_efx_reverb_params *a,
                                        const qa_efx_reverb_params *b,
                                        double f) {
  qa_efx_reverb_params out;
#define LERP(field) out.field = a->field + f * (b->field - a->field)
  LERP(density); LERP(diffusion); LERP(gain); LERP(gain_hf); LERP(gain_lf);
  LERP(decay_time); LERP(decay_hf_ratio); LERP(decay_lf_ratio);
  LERP(reflections_gain); LERP(reflections_delay);
  LERP(late_reverb_gain); LERP(late_reverb_delay);
  LERP(echo_time); LERP(echo_depth);
  LERP(modulation_time); LERP(modulation_depth);
  LERP(air_absorption_gain_hf); LERP(hf_reference); LERP(lf_reference);
  LERP(room_rolloff_factor);
#undef LERP
  out.decay_hf_limit = f >= 0.5 ? b->decay_hf_limit : a->decay_hf_limit;
  return out;
}

void qa_environment_reverb_init(qa_environment_reverb *reverb,
                                const qa_reverb_environment *environments,
                                size_t environment_count,
                                qa_audio_trace_query_fn trace, void *ctx) {
  memset(reverb, 0, sizeof *reverb);
  reverb->environments = environments;
  reverb->environment_count = environment_count;
  reverb->trace = trace;
  reverb->trace_ctx = ctx;
  reverb->environment_index =
      environment_count > 0 ? (int64_t)environment_count - 1 : 0;
  reverb->current_preset = QA_REVERB_PRESET_PLAIN;
  reverb->active = qa_reverb_preset(QA_REVERB_PRESET_PLAIN);
  reverb->from = reverb->active;
  reverb->to = reverb->active;
  reverb->enabled = true;
  reverb->lerp_seconds = 3;
}

const qa_efx_reverb_params *qa_environment_reverb_params(
    const qa_environment_reverb *reverb) {
  return reverb->enabled && reverb->environment_count > 0 ? &reverb->active
                                                           : NULL;
}

int64_t qa_environment_reverb_preset_index(
    const qa_environment_reverb *reverb) {
  return reverb->current_preset;
}

static bool equals_ignore_case(const char *a, const char *b) {
  /* TS toLowerCase comparison; materials are ASCII in practice. */
  while (*a != '\0' && *b != '\0') {
    char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
    char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
    if (ca != cb) return false;
    a++;
    b++;
  }
  return *a == *b;
}

void qa_environment_reverb_update(qa_environment_reverb *reverb,
                                  qa_vec3 origin, double milliseconds) {
  if (!isfinite(milliseconds)) qa_range_error("Reverb time must be finite");
  if (reverb->environment_count == 0) return;
  const qa_vec3 zero = {0, 0, 0};
  if (milliseconds >= reverb->probe_time) {
    reverb->probe_time = milliseconds + 13;
    qa_vec3 direction = probes[reverb->probe_index];
    qa_vec3 end = {origin.x + 8192 * direction.x,
                   origin.y + 8192 * direction.y,
                   origin.z + 8192 * direction.z};
    qa_audio_trace hit = reverb->trace(origin, end, zero, zero, reverb->trace_ctx);
    reverb->results[reverb->probe_index] =
        (qa_vec3){hit.end.x - origin.x, hit.end.y - origin.y,
                  hit.end.z - origin.z +
                      (reverb->probe_index == 1 && hit.sky ? 4096 : 0)};
    double min_x = reverb->results[0].x, max_x = min_x;
    double min_y = reverb->results[0].y, max_y = min_y;
    double min_z = reverb->results[0].z, max_z = min_z;
    for (int i = 1; i < 14; i++) {
      qa_vec3 p = reverb->results[i];
      if (p.x < min_x) min_x = p.x;
      if (p.x > max_x) max_x = p.x;
      if (p.y < min_y) min_y = p.y;
      if (p.y > max_y) max_y = p.y;
      if (p.z < min_z) min_z = p.z;
      if (p.z > max_z) max_z = p.z;
    }
    double average =
        (max_x - min_x + max_y - min_y + max_z - min_z) / 3;
    int64_t index = reverb->environment_index;
    while (index < (int64_t)reverb->environment_count - 1 &&
           average > reverb->environments[index].dimension)
      index++;
    if (index == reverb->environment_index)
      while (index > 0 && average < reverb->environments[index - 1].dimension)
        index--;
    reverb->environment_index = index;
    reverb->probe_index = (reverb->probe_index + 1) % 14;
  }
  qa_vec3 start = {origin.x, origin.y, origin.z + 1};
  qa_vec3 floor_end = {start.x, start.y, start.z - 256};
  qa_vec3 mins = {-16, -16, 0}, maxs = {16, 16, 0};
  qa_audio_trace floor =
      reverb->trace(start, floor_end, mins, maxs, reverb->trace_ctx);
  int64_t selected = reverb->current_preset;
  if (floor.fraction >= 1 || floor.sky) {
    selected = QA_REVERB_PRESET_PLAIN;
  } else {
    const qa_reverb_environment *environment =
        &reverb->environments[reverb->environment_index];
    for (size_t i = 0; i < environment->reverb_count; i++) {
      const qa_reverb_material *entry = &environment->reverbs[i];
      bool match = entry->materials == NULL;
      if (!match && floor.material != NULL)
        for (size_t j = 0; j < entry->material_count; j++)
          if (equals_ignore_case(entry->materials[j], floor.material)) {
            match = true;
            break;
          }
      if (match) {
        selected = entry->preset_index;
        break;
      }
    }
  }
  if (selected != reverb->current_preset) {
    reverb->current_preset = selected;
    reverb->from = reverb->active;
    reverb->to = qa_reverb_preset(selected);
    reverb->lerp_start = milliseconds;
    reverb->lerp_end =
        milliseconds + qa_js_max(0, reverb->lerp_seconds) * 1000;
  }
  if (milliseconds >= reverb->lerp_end) {
    reverb->active = reverb->to;
  } else {
    double t = (milliseconds - reverb->lerp_start) /
               (reverb->lerp_end - reverb->lerp_start);
    double f = 1 - qa_js_pow(1 - t, 3);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    reverb->active = interpolate(&reverb->from, &reverb->to, f);
  }
}
