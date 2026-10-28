/* Port of src/app/bootstrap/audio/output-settings.ts.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/audio/output_settings.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static void reg_ignored(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_register(cvars, name, value, flags, &ignored)) qa_cvar_read_free(&ignored);
}
static void set_ignored(qa_cvar_registry *cvars, const char *name, const char *value, bool force) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_set(cvars, name, value, force, &ignored)) qa_cvar_read_free(&ignored);
}
/* field: 0 sampleRate, 1 sampleBits, 2 channels (TS outputFields). */
typedef struct field_info { const char *name; int field; } field_info;
static const field_info fields[3] = {
  {"s_outputRate", 0}, {"s_outputBits", 1}, {"s_outputChannels", 2}
};
const char *const qa_app_audio_output_cvar_names[] = {"s_outputRate", "s_outputBits", "s_outputChannels"};
const size_t qa_app_audio_output_cvar_name_count = 3;
static int64_t field_value(qa_audio_output_format format, int field) {
  return field == 0 ? format.sample_rate : field == 1 ? format.sample_bits : format.channels;
}
static void set_field(qa_audio_output_format *format, int field, double value) {
  if (field == 0) format->sample_rate = (int64_t)value;
  else if (field == 1) format->sample_bits = (int64_t)value;
  else format->channels = (int64_t)value;
}
/* TS audioOutputFormat throw check, as a predicate (the landed
 * validated form aborts; the TS validate catches the message). */
static bool format_valid(qa_audio_output_format f, double raw[3]) {
  (void)f;
  double rate = raw[0], bits = raw[1], channels = raw[2];
  if (rate != trunc(rate) || rate < 8000 || rate > 192000) return false;
  if (channels != 1 && channels != 2) return false;
  if (bits != 8 && bits != 16) return false;
  return true;
}
typedef struct validate_ctx { qa_audio_output_format defaults; int field; } validate_ctx;
static validate_ctx contexts[3];
static bool is_trim_empty(const char *text) {
  for (; *text != '\0'; text++) if (!isspace((unsigned char)*text)) return false;
  return true;
}
static char *validate_field(void *ctx, const char *text) {
  validate_ctx *c = ctx;
  if (is_trim_empty(text)) return strdup("Expected an integer audio format value");
  double raw[3] = {(double)c->defaults.sample_rate, (double)c->defaults.sample_bits,
                   (double)c->defaults.channels};
  /* JS Number(text): strtod over the trimmed text, junk -> NaN. */
  char *end = NULL;
  double value = strtod(text, &end);
  if (end == text) value = NAN;
  else { while (*end != '\0' && isspace((unsigned char)*end)) end++; if (*end != '\0') value = NAN; }
  if (c->field == 0) raw[0] = value; else if (c->field == 1) raw[1] = value; else raw[2] = value;
  if (!format_valid(c->defaults, raw))
    return strdup("Audio output requires 8000\xe2\x80\x93" "192000 Hz, 1 or 2 channels, and 8 or 16 bits");
  return NULL;
}
static void changed_noop(void *ctx, const char *value) { (void)ctx; (void)value; }
static char *khz_read(void *ctx, const char *value) {
  (void)ctx;
  if (strcmp(value, "11025") == 0) return strdup("11");
  if (strcmp(value, "22050") == 0) return strdup("22");
  if (strcmp(value, "44100") == 0) return strdup("44");
  if (strcmp(value, "48000") == 0) return strdup("48");
  char buf[64];
  /* JS String(Number(value) / 1000): integral values print plain. */
  double scaled = strtod(value, NULL) / 1000.0;
  if (scaled == trunc(scaled) && fabs(scaled) < 1e15) snprintf(buf, sizeof buf, "%lld", (long long)scaled);
  else snprintf(buf, sizeof buf, "%g", scaled);
  return strdup(buf);
}
static bool khz_write(void *ctx, const char *value, char **out_value, char **out_message) {
  (void)ctx;
  int64_t rate = 0;
  if (!qa_audio_khz_rate(value, &rate)) {
    *out_message = strdup("Use s_khz 11, 22, 44 or 48");
    return false;
  }
  char buf[32];
  snprintf(buf, sizeof buf, "%lld", (long long)rate);
  *out_value = strdup(buf);
  return true;
}
static const char *const khz_examples[] = {"s_khz 44"};
void qa_app_register_audio_output_cvars(qa_cvar_registry *cvars, const qa_audio_output_format *defaults_in) {
  qa_audio_output_format defaults = defaults_in != NULL ? *defaults_in : qa_default_audio_output_format;
  for (size_t i = 0; i < 3; i++) {
    int64_t value = field_value(defaults, fields[i].field);
    char text[32];
    snprintf(text, sizeof text, "%lld", (long long)value);
    reg_ignored(cvars, fields[i].name, text, QA_CVAR_FLAG_ARCHIVE);
    contexts[i].defaults = defaults;
    contexts[i].field = fields[i].field;
    qa_cvar_registry_bind_value(cvars, fields[i].name,
        (qa_cvar_value_binding){&contexts[i], validate_field, changed_noop});
    char usage[64], example[64];
    snprintf(usage, sizeof usage, "%s <value>", fields[i].name);
    snprintf(example, sizeof example, "%s %lld", fields[i].name, (long long)value);
    /* Documentation strings must outlive the call: leak small copies
     * (process-lifetime cvar docs, as the TS strings are GC-rooted). */
    char *usage_copy = strdup(usage), *example_copy = strdup(example);
    if (usage_copy == NULL || example_copy == NULL) qa_range_error("Out of memory");
    const char **examples = malloc(sizeof *examples);
    if (examples == NULL) qa_range_error("Out of memory");
    examples[0] = example_copy;
    qa_cvar_registry_document(cvars, fields[i].name, (qa_command_documentation){
      "Shared output format; apply with snd_restart or the audio menu.",
      usage_copy, examples, 1, NULL, 0});
  }
  qa_cvar_registry_register_alias(cvars, (qa_cvar_alias){"s_khz", "s_outputRate",
    (qa_command_documentation){"Source sample-rate convention for the shared output; apply with snd_restart.",
      "s_khz <11|22|44|48>", khz_examples, 1, NULL, 0},
    (qa_cvar_alias_conversion){NULL, false, khz_read, khz_write}});
}
qa_audio_output_format qa_app_read_audio_output_cvars(qa_cvar_registry *cvars) {
  qa_audio_output_format out = qa_default_audio_output_format;
  double raw[3] = {0, 0, 0};
  for (size_t i = 0; i < 3; i++) {
    double value = qa_cvar_registry_variable_value(cvars, fields[i].name);
    set_field(&out, fields[i].field, value);
    if (fields[i].field == 0) raw[0] = value;
    else if (fields[i].field == 1) raw[1] = value;
    else raw[2] = value;
  }
  if (!format_valid(out, raw))
    qa_range_error("Audio output requires 8000\xe2\x80\x93" "192000 Hz, 1 or 2 channels, and 8 or 16 bits");
  return out;
}
void qa_app_write_audio_output_cvars(qa_cvar_registry *cvars, qa_audio_output_format format) {
  for (size_t i = 0; i < 3; i++) {
    char text[32];
    snprintf(text, sizeof text, "%lld", (long long)field_value(format, fields[i].field));
    set_ignored(cvars, fields[i].name, text, false);
  }
}
