/* Port of src/app/bootstrap/weapon-behavior-tool.ts. Catalog/mount/QC/
 * QVM/PE work (landed content+compat modules) runs through the host;
 * dispatch order and guard errors are kept here. The atomic document
 * write is real (temp + rename, 0600). Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_WEAPON_BEHAVIOR_TOOL_H
#define QA_APP_BOOTSTRAP_WEAPON_BEHAVIOR_TOOL_H
#include "qa/app/bootstrap/weapon_behavior_tool_options.h"
typedef struct qa_app_weapon_behavior_tool_host {
  void *ctx;
  /* discoverInstalledContent + require + mountsFor + openMountPlan.
   * Returns an opaque mounted-content handle; fills product facts. */
  const void *(*open_product)(void *ctx, const qa_app_weapon_behavior_tool_command *command,
                              const char **out_family, const char **out_edition, const char **out_expectation_id,
                              const char **out_content_directory, const char **out_product_id);
  void (*close_product)(void *ctx, const void *content);
  /* Per-family bodies (QC inspect/declare, QVM, native): error or NULL. */
  const char *(*run_qc)(void *ctx, const qa_app_weapon_behavior_tool_command *command, const void *content,
                        void (*print)(void *print_ctx, const char *text), void *print_ctx);
  const char *(*run_qvm)(void *ctx, const qa_app_weapon_behavior_tool_command *command, const void *content,
                         void (*print)(void *print_ctx, const char *text), void *print_ctx);
  const char *(*run_native)(void *ctx, const qa_app_weapon_behavior_tool_command *command, const void *content,
                            void (*print)(void *print_ctx, const char *text), void *print_ctx);
} qa_app_weapon_behavior_tool_host;
void qa_app_run_weapon_behavior_tool(qa_app_weapon_behavior_tool_host host, qa_app_weapon_behavior_tool_command command,
                                     void (*print)(void *ctx, const char *text), void *print_ctx);
/* TS writeBehaviorDocument: returns malloc'd destination path. */
char *qa_app_write_behavior_document(const char *directory, const char *name, const char *bytes);
#endif
