/* Port of src/app/bootstrap/simulation/classic-guest-files.ts.
 * classicGuestFiles(root, resources) returns the WindowsCapabilities
 * openFile closure; in C it is a factory object whose open function is
 * the closure (ctx = the factory). File objects stay allocated until the
 * factory is destroyed, so a closed handle fails loudly ("Native file is
 * closed") instead of dangling. TS `throw new Error` inside a file
 * method raises "Error" through qa_raise (aborts when nothing catches).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_FILES_H
#define QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_FILES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/guest/runtime/windows/contracts.h"
typedef struct qa_app_sim_classic_guest_file_options { double creation; bool read; bool write; } qa_app_sim_classic_guest_file_options;
/* TS position(): aborts "Invalid native file range" unless both are safe integers >=0 with safe sum. */
void qa_app_sim_classic_guest_file_position(double offset, double length);
/* Returns normalized name (malloc'd): backslashes to slashes, leading ./ stripped, contained parts validated. */
char *qa_app_sim_classic_guest_file_name(const char *path);
/* The disposition check: aborts outside 1..5; false where TS returns null for a read-only non-OPEN_EXISTING open. */
bool qa_app_sim_classic_guest_file_allowed(qa_app_sim_classic_guest_file_options options);

/* TS ReadonlyMap<string, Uint8Array> resources (bytes borrowed). */
typedef struct qa_app_sim_classic_guest_resource { const char *name; const uint8_t *bytes; size_t length; } qa_app_sim_classic_guest_resource;
typedef struct qa_app_sim_classic_guest_files qa_app_sim_classic_guest_files;
/* NULL only on OOM. root and resources are borrowed. */
qa_app_sim_classic_guest_files *qa_app_sim_classic_guest_files_create(const char *root, const qa_app_sim_classic_guest_resource *resources, size_t resource_count);
void qa_app_sim_classic_guest_files_destroy(qa_app_sim_classic_guest_files *files);
/* The openFile closure: ctx is the factory. NULL is the TS null. */
qa_windows_file *qa_app_sim_classic_guest_files_open(void *ctx, const char *path, bool read, bool write, int creation);
#endif
