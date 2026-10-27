/* Port of src/formats/q3-model/animation.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_model/animation.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/q3_model/text.h"
#include "qa/foundation/support.h"

static const char *const animation_names[] = {
    "BOTH_DEATH1", "BOTH_DEAD1", "BOTH_DEATH2", "BOTH_DEAD2", "BOTH_DEATH3", "BOTH_DEAD3",
    "TORSO_GESTURE", "TORSO_ATTACK", "TORSO_ATTACK2", "TORSO_DROP", "TORSO_RAISE", "TORSO_STAND",
    "TORSO_STAND2", "LEGS_WALKCR", "LEGS_WALK", "LEGS_RUN", "LEGS_BACK", "LEGS_SWIM", "LEGS_JUMP",
    "LEGS_LAND", "LEGS_JUMPB", "LEGS_LANDB", "LEGS_IDLE", "LEGS_IDLECR", "LEGS_TURN",
    "TORSO_GETFLAG", "TORSO_GUARDBASE", "TORSO_PATROL", "TORSO_FOLLOWME", "TORSO_AFFIRMATIVE",
    "TORSO_NEGATIVE", "MAX_ANIMATIONS", "LEGS_BACKCR", "LEGS_BACKWALK", "FLAG_RUN", "FLAG_STAND",
    "FLAG_STAND2RUN"};

const char *const *qa_q3_animation_names(size_t *out_count) {
  *out_count = sizeof animation_names / sizeof animation_names[0];
  return animation_names;
}

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *copy = xmalloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

static char *lowered(const char *text) {
  char *copy = xstrdup(text);
  for (char *p = copy; *p != '\0'; p++) *p = (char)tolower((unsigned char)*p);
  return copy;
}

static void list_push(qa_md5_string_list *list, char *value) {
  list->values = realloc(list->values, (list->count + 1) * sizeof *list->values);
  if (list->values == NULL) qa_range_error("Out of memory");
  list->values[list->count++] = value;
}

void qa_parse_player_animation_config(const char *text, const char *source,
                                      qa_player_animation_config *out) {
  memset(out, 0, sizeof *out);
  const char *name = source != NULL ? source : "<animation.cfg>";
  out->source = xstrdup(name);
  out->footsteps = "normal";
  out->gender = "male";
  qa_model_tokens tokens;
  qa_model_tokens_init(&tokens, text, name);
  char *token = qa_model_tokens_token(&tokens);
  while (!isdigit((unsigned char)token[0])) {
    char *lower = lowered(token);
    if (strcmp(lower, "footsteps") == 0) {
      char *raw = qa_model_tokens_token(&tokens);
      char *value = lowered(raw);
      free(raw);
      if (strcmp(value, "default") == 0 || strcmp(value, "normal") == 0) out->footsteps = "normal";
      else if (strcmp(value, "boot") == 0) out->footsteps = "boot";
      else if (strcmp(value, "flesh") == 0) out->footsteps = "flesh";
      else if (strcmp(value, "mech") == 0) out->footsteps = "mech";
      else if (strcmp(value, "energy") == 0) out->footsteps = "energy";
      else {
        char message[1024];
        snprintf(message, sizeof message, "Unknown footsteps %s", value);
        list_push(&out->diagnostics, xstrdup(message));
      }
      free(value);
    } else if (strcmp(lower, "sex") == 0) {
      char *raw = qa_model_tokens_token(&tokens);
      char first = raw[0] != '\0' ? (char)tolower((unsigned char)raw[0]) : '\0';
      out->gender = first == 'f' ? "female" : first == 'n' ? "neuter" : "male";
      free(raw);
    } else if (strcmp(lower, "headoffset") == 0) {
      out->head_offset.x = qa_model_tokens_float(&tokens);
      out->head_offset.y = qa_model_tokens_float(&tokens);
      out->head_offset.z = qa_model_tokens_float(&tokens);
    } else if (strcmp(lower, "fixedlegs") == 0) {
      out->fixed_legs = true;
    } else if (strcmp(lower, "fixedtorso") == 0) {
      out->fixed_torso = true;
    } else {
      char message[1024];
      snprintf(message, sizeof message, "Unknown animation token %s", token);
      list_push(&out->diagnostics, xstrdup(message));
    }
    free(lower);
    free(token);
    token = qa_model_tokens_token(&tokens);
  }
  qa_player_animation source_animations[31];
  char *first = token;
  double leg_offset = 0;
  for (int index = 0; index < 31; index++) {
    const char *anim_name = animation_names[qa_q3_model_at(index, 37, "animation name")];
    if (first == NULL) {
      if (index < 25) {
        char message[256];
        snprintf(message, sizeof message, "missing animation %s", anim_name);
        qa_model_tokens_fail(&tokens, message);
      }
      source_animations[index] = source_animations[6];
      source_animations[index].name = anim_name;
      source_animations[index].reversed = false;
      source_animations[index].flipflop = false;
      continue;
    }
    char *end = NULL;
    double first_frame = strtod(first, &end);
    if (end == first || *end != '\0' || first_frame != trunc(first_frame) || first_frame < 0) {
      char message[1024];
      snprintf(message, sizeof message, "invalid first frame %s", first);
      qa_model_tokens_fail(&tokens, message);
    }
    if (index == 13) leg_offset = first_frame - source_animations[6].first_frame;
    if (index >= 13 && index < 25) first_frame -= leg_offset;
    double count = qa_model_tokens_integer(&tokens, -2147483647.0, 2147483647.0);
    double loop_frames = qa_model_tokens_integer(&tokens, 0, 2147483647.0);
    double fps = qa_model_tokens_float(&tokens);
    if (fps == 0) fps = 1;
    double frame_lerp = trunc((double)(float)(1000 / fps));
    source_animations[index].name = anim_name;
    source_animations[index].first_frame = first_frame;
    source_animations[index].num_frames = fabs(count);
    source_animations[index].loop_frames = loop_frames;
    source_animations[index].frame_lerp = frame_lerp;
    source_animations[index].initial_lerp = frame_lerp;
    source_animations[index].reversed = count < 0;
    source_animations[index].flipflop = false;
    free(first);
    first = NULL;
    if (index < 30) first = qa_model_tokens_next(&tokens);
  }
  free(first);
  out->animations = xmalloc(37 * sizeof *out->animations);
  out->animation_count = 37;
  for (int i = 0; i < 31; i++) {
    out->animations[i] = xmalloc(sizeof *out->animations[i]);
    *out->animations[i] = source_animations[i];
  }
  out->animations[31] = NULL;
  qa_player_animation extras[5] = {
      source_animations[13], source_animations[14],
      {"FLAG_RUN", 0, 16, 16, 66, 66, false, false},
      {"FLAG_STAND", 16, 5, 0, 50, 50, false, false},
      {"FLAG_STAND2RUN", 16, 5, 1, 66, 66, true, false}};
  extras[0].name = "LEGS_BACKCR";
  extras[0].reversed = true;
  extras[1].name = "LEGS_BACKWALK";
  extras[1].reversed = true;
  for (int i = 0; i < 5; i++) {
    out->animations[32 + i] = xmalloc(sizeof *out->animations[32 + i]);
    *out->animations[32 + i] = extras[i];
  }
}

void qa_player_animation_config_destroy(qa_player_animation_config *config) {
  if (config == NULL) return;
  free(config->source);
  for (size_t i = 0; i < config->animation_count; i++) free(config->animations[i]);
  free(config->animations);
  for (size_t i = 0; i < config->diagnostics.count; i++) free(config->diagnostics.values[i]);
  free(config->diagnostics.values);
  memset(config, 0, sizeof *config);
}
