/* Port of src/formats/q3-model/replacements.ts. Returned strings and
 * skin selections are owned. GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q3_MODEL_REPLACEMENTS_H
#define QA_FORMATS_Q3_MODEL_REPLACEMENTS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/scene.h"

typedef struct qa_md5_paths {
  char *mesh_path;  /* owned */
  char *anim_path;  /* owned */
  char *scale_path; /* owned */
} qa_md5_paths;

/* family is "q1" | "q2". */
void qa_md5_paths_for(const char *model_path, const char *family, qa_md5_paths *out);
void qa_md5_paths_destroy(qa_md5_paths *paths);
char *qa_md5_skin_path_for(const char *md2_skin_name);
/* has_* false is the TS null rank. */
bool qa_md5_replacement_allowed(bool has_md2_rank, double md2_rank, bool has_mesh_rank, double mesh_rank);
void qa_md2_replacement_skin_selection(const char *const *skins, size_t skin_count, double source_frame_count,
                                       const char *scale_source, const char *const *diagnostics,
                                       size_t diagnostic_count, qa_md5_skin_selection *out);
char *qa_q1_md5_skin_path(const char *shader, double group, double frame);

typedef struct qa_replacement_animation_timing {
  const char *kind; /* "entity-frame" | "elapsed-time" */
  double frame_rate;
} qa_replacement_animation_timing;

qa_replacement_animation_timing qa_q1_md5_animation_timing(double alias_frame_count,
                                                           const qa_md5_model *model);
void qa_q1_replacement_skin_selection(const qa_md5_model *model, const qa_q1_alias_model *alias,
                                      qa_md5_skin_selection *out);
/* Frees owned strings/rows built by the two selection builders. */
void qa_md5_skin_selection_destroy(qa_md5_skin_selection *selection);

#endif
