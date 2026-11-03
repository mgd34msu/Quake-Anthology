/* Port of src/formats/q3-model/replacements.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_model/replacements.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

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

typedef struct {
  const char *directory;
  size_t directory_length;
  const char *filename;
  size_t stem_length;
} split_path;

static split_path split(const char *path) {
  const char *slash = strrchr(path, '/');
  const char *filename = slash != NULL ? slash + 1 : path;
  const char *dot = strrchr(filename, '.');
  split_path result;
  result.directory = path;
  result.directory_length = slash != NULL ? (size_t)(slash + 1 - path) : 0;
  result.filename = filename;
  result.stem_length = dot != NULL ? (size_t)(dot - filename) : strlen(filename);
  return result;
}

static char *join3(const char *a, size_t a_length, const char *b, const char *c, size_t c_length,
                   const char *d) {
  size_t b_length = strlen(b), d_length = strlen(d);
  char *result = xmalloc(a_length + b_length + c_length + d_length + 1);
  char *p = result;
  memcpy(p, a, a_length);
  p += a_length;
  memcpy(p, b, b_length);
  p += b_length;
  memcpy(p, c, c_length);
  p += c_length;
  memcpy(p, d, d_length);
  p += d_length;
  *p = '\0';
  return result;
}

void qa_md5_paths_for(const char *model_path, const char *family, qa_md5_paths *out) {
  split_path parts = split(model_path);
  const char *middle = strcmp(family, "q2") == 0 ? "md5/" : "";
  out->mesh_path = join3(parts.directory, parts.directory_length, middle, parts.filename, parts.stem_length,
                         ".md5mesh");
  out->anim_path = join3(parts.directory, parts.directory_length, middle, parts.filename, parts.stem_length,
                         ".md5anim");
  out->scale_path = join3(parts.directory, parts.directory_length, middle, parts.filename, parts.stem_length,
                          ".md5scale");
}

void qa_md5_paths_destroy(qa_md5_paths *paths) {
  if (paths == NULL) return;
  free(paths->mesh_path);
  free(paths->anim_path);
  free(paths->scale_path);
  memset(paths, 0, sizeof *paths);
}

char *qa_md5_skin_path_for(const char *md2_skin_name) {
  split_path parts = split(md2_skin_name);
  return join3(parts.directory, parts.directory_length, "md5/", parts.filename, strlen(parts.filename), "");
}

bool qa_md5_replacement_allowed(bool has_md2_rank, double md2_rank, bool has_mesh_rank, double mesh_rank) {
  return !has_md2_rank || !has_mesh_rank || mesh_rank <= md2_rank;
}

void qa_md2_replacement_skin_selection(const char *const *skins, size_t skin_count, double source_frame_count,
                                       const char *scale_source, const char *const *diagnostics,
                                       size_t diagnostic_count, qa_md5_skin_selection *out) {
  memset(out, 0, sizeof *out);
  out->kind = "q2-md2-replacement";
  const char **mapped = xmalloc(skin_count * sizeof *mapped);
  for (size_t i = 0; i < skin_count; i++) mapped[i] = qa_md5_skin_path_for(skins[i]);
  out->skins.values = mapped;
  out->skins.count = skin_count;
  out->source_frame_count = source_frame_count;
  out->scale_source = scale_source != NULL ? xstrdup(scale_source) : NULL;
  const char **copied = xmalloc(diagnostic_count * sizeof *copied);
  for (size_t i = 0; i < diagnostic_count; i++) copied[i] = xstrdup(diagnostics[i]);
  out->diagnostics.values = copied;
  out->diagnostics.count = diagnostic_count;
}

char *qa_q1_md5_skin_path(const char *shader, double group, double frame) {
  if (group != trunc(group) || group < 0 || frame != trunc(frame) || frame < 0) {
    qa_range_error("Invalid Q1 MD5 skin group/frame");
  }
  size_t length = strlen(shader) + 64;
  char *result = xmalloc(length);
  /* String(n).padStart(2, "0"): a C %02d of a value past int range is undefined. */
  char group_text[40], frame_text[40];
  qa_net_js_number(group, group_text, sizeof group_text);
  qa_net_js_number(frame, frame_text, sizeof frame_text);
  snprintf(result, length, "progs/%s_%s%s_%s%s", shader, group_text[1] == '\0' ? "0" : "", group_text,
           frame_text[1] == '\0' ? "0" : "", frame_text);
  return result;
}

qa_replacement_animation_timing qa_q1_md5_animation_timing(double alias_frame_count,
                                                           const qa_md5_model *model) {
  qa_replacement_animation_timing timing;
  if (alias_frame_count == (double)model->frame_count) {
    timing.kind = "entity-frame";
    timing.frame_rate = 0;
  } else {
    timing.kind = "elapsed-time";
    timing.frame_rate = 2;
  }
  return timing;
}

void qa_q1_replacement_skin_selection(const qa_md5_model *model, const qa_q1_alias_model *alias,
                                      qa_md5_skin_selection *out) {
  memset(out, 0, sizeof *out);
  out->kind = "q1-mdl-replacement";
  out->flags = alias->flags;
  qa_replacement_animation_timing timing = qa_q1_md5_animation_timing((double)alias->frame_count, model);
  out->timing_kind = timing.kind;
  out->timing_frame_rate = timing.frame_rate;
  qa_timed_frames **rows = xmalloc(model->mesh_count * sizeof *rows);
  size_t *row_counts = xmalloc(model->mesh_count * sizeof *row_counts);
  for (size_t m = 0; m < model->mesh_count; m++) {
    const char *shader = model->meshes[m].shader;
    qa_timed_frames *row = xmalloc(alias->skin_count * sizeof *row);
    for (size_t group = 0; group < alias->skin_count; group++) {
      const qa_timed_frames *skin = &alias->skins[group];
      if (!skin->is_group) {
        row[group].is_group = false;
        row[group].single_frame = qa_q1_md5_skin_path(shader, (double)group, 0);
        row[group].frames = NULL;
        row[group].frame_count = 0;
      } else {
        qa_timed_frame_entry *entries = xmalloc(skin->frame_count * sizeof *entries);
        for (size_t f = 0; f < skin->frame_count; f++) {
          entries[f].interval_seconds = skin->frames[f].interval_seconds;
          entries[f].frame = qa_q1_md5_skin_path(shader, (double)group, (double)f);
        }
        row[group].is_group = true;
        row[group].single_frame = NULL;
        row[group].frames = entries;
        row[group].frame_count = skin->frame_count;
      }
    }
    rows[m] = row;
    row_counts[m] = alias->skin_count;
  }
  out->mesh_skin_group_rows = (const qa_timed_frames *const *)rows;
  out->mesh_skin_group_row_counts = row_counts;
  out->mesh_skin_group_row_count = model->mesh_count;
}

void qa_md5_skin_selection_destroy(qa_md5_skin_selection *selection) {
  if (selection == NULL) return;
  for (size_t m = 0; m < selection->mesh_skin_group_row_count; m++) {
    qa_timed_frames *row = (qa_timed_frames *)selection->mesh_skin_group_rows[m];
    size_t count = selection->mesh_skin_group_row_counts[m];
    for (size_t i = 0; i < count; i++) {
      if (!row[i].is_group) {
        free((void *)row[i].single_frame);
      } else {
        for (size_t f = 0; f < row[i].frame_count; f++) free((void *)row[i].frames[f].frame);
        free((void *)row[i].frames);
      }
    }
    free(row);
  }
  free((void *)selection->mesh_skin_group_rows);
  free((void *)selection->mesh_skin_group_row_counts);
  for (size_t i = 0; i < selection->skins.count; i++) free((void *)selection->skins.values[i]);
  free((void *)selection->skins.values);
  free((void *)selection->scale_source);
  for (size_t i = 0; i < selection->diagnostics.count; i++) free((void *)selection->diagnostics.values[i]);
  free((void *)selection->diagnostics.values);
  memset(selection, 0, sizeof *selection);
}
