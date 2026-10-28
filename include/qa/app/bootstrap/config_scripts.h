/* Port of src/app/bootstrap/config-scripts.ts. Mount/catalog access
 * becomes host callbacks; async becomes synchronous with the read
 * refcount/write chain kept as state. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CONFIG_SCRIPTS_H
#define QA_APP_BOOTSTRAP_CONFIG_SCRIPTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/settings/config.h"
/* TS sourceScriptReader services: owners are the product chain from
 * source through baseProduct (host walks the landed catalog); open
 * reads through the reordered mounts limited to those owners. */
typedef struct qa_app_source_script_host {
  void *ctx;
  /* Aborts "Cyclic configuration source dependency: <source>" itself
   * is the caller's catalog walk; open returns malloc'd bytes or false. */
  bool (*open)(void *ctx, const char *name, uint8_t **out_bytes, size_t *out_count);
} qa_app_source_script_reader;
typedef struct qa_app_legacy_console_config_sources {
  const char *shared_root;
  const char *const *game_roots; size_t game_root_count;
} qa_app_legacy_console_config_sources;
typedef struct qa_app_console_script_entry { char *name; const char *kind; } qa_app_console_script_entry; /* kind "seat"|"product" */
typedef struct qa_app_read_console_script_options {
  const char *name;
  qa_command_context source;
  const char *console_root;
  const qa_config_store *settings;
  qa_app_source_script_reader mounted; bool has_mounted;
  qa_app_source_script_reader mounted_script; bool has_mounted_script;
  bool has_legacy_config; qa_app_legacy_console_config_sources legacy_config;
} qa_app_read_console_script_options;
typedef struct qa_app_console_script_files qa_app_console_script_files;
typedef struct qa_app_console_script_files_options {
  const char *console_root;
  const qa_config_store *settings; /* borrowed */
  qa_app_source_script_reader mounted; bool has_mounted;
  qa_app_source_script_reader mounted_script; bool has_mounted_script;
  /* mountedFiles / mountedResource hosts */
  void *mounted_ctx;
  char **(*mounted_files)(void *ctx, const char *directory, const char *extension, size_t *out_count);
  bool (*mounted_resource)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_count);
  bool has_legacy_config; qa_app_legacy_console_config_sources legacy_config;
  const char *(*retire_mounted)(void *ctx);
} qa_app_console_script_files_options;
/* Malloc'd; caller frees (user_content_root NULL = TS undefined). */
char *qa_app_console_config_root(const char *user_content_root);
/* TS seatConsoleConfig: fills a store rooted at <root>/settings/seat-<index>. */
bool qa_app_seat_console_config(const char *root, qa_seat_id seat, qa_config_store *out);
/* Malloc'd latin1 text (bytes preserved) or NULL for TS undefined. */
char *qa_app_read_console_script(qa_app_read_console_script_options options);
qa_app_console_script_files *qa_app_console_script_files_create(qa_app_console_script_files_options options);
void qa_app_console_script_files_destroy(qa_app_console_script_files *files);
const char *qa_app_console_script_files_root(const qa_app_console_script_files *files);
qa_app_console_script_entry *qa_app_console_script_files_list(qa_app_console_script_files *files, qa_command_context source, size_t *out_count);
void qa_app_console_script_entries_free(qa_app_console_script_entry *entries, size_t count);
char *qa_app_console_script_files_read(qa_app_console_script_files *files, const char *name, qa_command_context source);
/* False = TS undefined. */
bool qa_app_console_script_files_read_mounted_script(qa_app_console_script_files *files, const char *name, uint8_t **out_bytes, size_t *out_count);
bool qa_app_console_script_files_read_mounted(qa_app_console_script_files *files, const char *name, uint8_t **out_bytes, size_t *out_count);
char **qa_app_console_script_files_list_mounted(qa_app_console_script_files *files, const char *directory, const char *extension, qa_command_context source, size_t *out_count);
/* False = TS null. */
bool qa_app_console_script_files_open_mounted(qa_app_console_script_files *files, const char *path, qa_command_context source, uint8_t **out_bytes, size_t *out_count);
void qa_app_console_script_files_close(qa_app_console_script_files *files);
/* TS write(): runs operation synchronously in call order; error or NULL. */
const char *qa_app_console_script_files_write(qa_app_console_script_files *files, const char *(*operation)(void *ctx), void *ctx);
#endif
