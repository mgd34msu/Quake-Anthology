/* Port of src/content/q3/foundation/animation-config.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_ANIMATION_CONFIG_H
#define QA_CONTENT_Q3_FOUNDATION_ANIMATION_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/core/common_parse.h"

enum { QA_Q3_SOURCE_ANIMATION_COUNT = 31, QA_Q3_TOTAL_ANIMATION_COUNT = 37,
       QA_Q3_MAX_ANIMATIONS_SENTINEL = 31 };

typedef enum qa_q3_player_footsteps { QA_Q3_FOOTSTEPS_NORMAL, QA_Q3_FOOTSTEPS_BOOT,
  QA_Q3_FOOTSTEPS_FLESH, QA_Q3_FOOTSTEPS_MECH, QA_Q3_FOOTSTEPS_ENERGY } qa_q3_player_footsteps;
typedef enum qa_q3_player_gender { QA_Q3_GENDER_MALE, QA_Q3_GENDER_FEMALE, QA_Q3_GENDER_NEUTER } qa_q3_player_gender;
const char *qa_q3_player_footsteps_name(qa_q3_player_footsteps value);
const char *qa_q3_player_gender_name(qa_q3_player_gender value);

typedef struct qa_q3_animation {
  double first_frame, num_frames, loop_frames, frame_lerp, initial_lerp;
  bool reversed, flipflop;
} qa_q3_animation;
typedef qa_q3_animation qa_q3_animation_cell;

typedef struct qa_q3_player_animation_target {
  qa_q3_player_footsteps footsteps; qa_vec3 head_offset; qa_q3_player_gender gender;
  bool fixed_legs, fixed_torso;
  qa_q3_animation *animations; size_t animation_count;
} qa_q3_player_animation_target;

typedef struct qa_q3_player_animation_parse_context {
  qa_q3_player_animation_target *target; /* NULL = TS default target */
  qa_common_parse_state *parser;         /* NULL = TS new state */
  void *print_ctx;
  void (*print)(void *ctx, const char *message); /* NULL = no context print */
} qa_q3_player_animation_parse_context;

typedef struct qa_q3_animation_warning { double line, column; char *message; /* owned */ } qa_q3_animation_warning;

typedef struct qa_q3_player_animation_config {
  qa_q3_player_footsteps footsteps; qa_vec3 head_offset; qa_q3_player_gender gender;
  bool fixed_legs, fixed_torso;
  qa_q3_animation animations[QA_Q3_TOTAL_ANIMATION_COUNT];
  bool animation_present[QA_Q3_TOTAL_ANIMATION_COUNT]; /* false at sentinel 31 (TS null) */
  qa_q3_animation_warning *warnings; size_t warning_count;
} qa_q3_player_animation_config;

/* source NULL = "<animation.cfg>"; context NULL = TS null. Parse errors abort. */
void qa_q3_parse_player_animation_config(const char *text, const char *source,
    const qa_q3_player_animation_parse_context *context, qa_q3_player_animation_config *out);
void qa_q3_player_animation_config_destroy(qa_q3_player_animation_config *config);
#endif
