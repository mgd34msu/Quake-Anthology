/* Port of src/text/localization-resources.ts. GPL-2.0-or-later. */
#include "qa/text/localization_resources.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void merge_one(qa_localization_table *table, const char *path,
                      qa_localization_read_fn read, void *read_ctx,
                      const qa_loc_reload_options *options) {
  size_t length = 0;
  uint8_t *bytes = read(read_ctx, path, &length);
  if (bytes == NULL) return;
  qa_byte_view view = {bytes, length};
  qa_localization_table_merge(table, view, options);
  free(bytes);
}

/* Sequential on purpose, as in the TS. */
static void load_resources(qa_localization_table *table, const char *language,
                           qa_localization_read_fn read, void *read_ctx,
                           const qa_lib_log *log) {
  /* TS options = log === undefined ? {} : { log } (NULL options is {}). */
  qa_loc_reload_options options;
  memset(&options, 0, sizeof options);
  options.log = log;
  const qa_loc_reload_options *merge_options = log == NULL ? NULL : &options;
  bool english = strcmp(language, "english") == 0;
  const char *names[2] = {"english", language};
  size_t name_count = english ? 1 : 2;
  for (size_t i = 0; i < name_count; i++) {
    char path[512];
    snprintf(path, sizeof path, "localization/loc_%s.txt", names[i]);
    merge_one(table, path, read, read_ctx, merge_options);
    snprintf(path, sizeof path, "localization/loc_%s_mod.txt", names[i]);
    merge_one(table, path, read, read_ctx, merge_options);
  }
}

void qa_load_localization_resources_logged(qa_seat_id seat, const char *language,
                                           qa_localization_read_fn read, void *read_ctx,
                                           qa_loc_profile profile, const qa_lib_log *log,
                                           qa_localization_catalog *out) {
  qa_localization_catalog_init(out, seat, profile);
  load_resources(&out->table, language, read, read_ctx, log);
}

void qa_load_server_localization_resources_logged(const char *language,
                                                  qa_localization_read_fn read,
                                                  void *read_ctx,
                                                  qa_loc_profile profile,
                                                  const qa_lib_log *log,
                                                  qa_localization_table *out) {
  qa_localization_table_init(out, profile);
  load_resources(out, language, read, read_ctx, log);
}

void qa_load_localization_resources(qa_seat_id seat, const char *language,
                                    qa_localization_read_fn read, void *read_ctx,
                                    qa_loc_profile profile,
                                    qa_localization_catalog *out) {
  qa_load_localization_resources_logged(seat, language, read, read_ctx, profile, NULL, out);
}

void qa_load_server_localization_resources(const char *language,
                                           qa_localization_read_fn read,
                                           void *read_ctx,
                                           qa_loc_profile profile,
                                           qa_localization_table *out) {
  qa_load_server_localization_resources_logged(language, read, read_ctx, profile, NULL, out);
}
