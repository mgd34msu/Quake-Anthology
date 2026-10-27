/* Port of src/formats/q3-model/animation.ts. Owned result; a NULL
 * animation slot is the TS null (MAX_ANIMATIONS). GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q3_MODEL_ANIMATION_H
#define QA_FORMATS_Q3_MODEL_ANIMATION_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/formats/q3_model/md5.h"

const char *const *qa_q3_animation_names(size_t *out_count);

typedef struct qa_player_animation {
  const char *name; /* borrowed literal */
  double first_frame;
  double num_frames;
  double loop_frames;
  double frame_lerp;
  double initial_lerp;
  bool reversed;
  bool flipflop;
} qa_player_animation;

typedef struct qa_player_animation_config {
  char *source;              /* owned */
  const char *footsteps;     /* "normal"|"boot"|"flesh"|"mech"|"energy" */
  qa_vec3 head_offset;
  const char *gender;        /* "male"|"female"|"neuter" */
  bool fixed_legs;
  bool fixed_torso;
  qa_player_animation **animations; /* owned slots, may contain NULL */
  size_t animation_count;
  qa_md5_string_list diagnostics;
} qa_player_animation_config;

/* `source` NULL means "<animation.cfg>". */
void qa_parse_player_animation_config(const char *text, const char *source,
                                      qa_player_animation_config *out);
void qa_player_animation_config_destroy(qa_player_animation_config *config);

#endif
