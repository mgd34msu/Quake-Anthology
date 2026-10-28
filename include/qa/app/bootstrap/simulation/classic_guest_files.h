/* Port of src/app/bootstrap/simulation/classic-guest-files.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_FILES_H
#define QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_FILES_H
#include <stdbool.h>
#include <stddef.h>
typedef struct qa_app_sim_classic_guest_file_options { double creation; bool read; bool write; } qa_app_sim_classic_guest_file_options;
/* TS position(): aborts "Invalid native file range" unless both are safe integers >=0 with safe sum. */
void qa_app_sim_classic_guest_file_position(double offset, double length);
/* Returns normalized name (malloc'd): backslashes to slashes, leading ./ stripped. */
char *qa_app_sim_classic_guest_file_name(const char *path);
bool qa_app_sim_classic_guest_file_allowed(qa_app_sim_classic_guest_file_options options);
#endif
