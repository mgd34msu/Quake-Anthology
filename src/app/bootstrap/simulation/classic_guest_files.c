/* Port of src/app/bootstrap/simulation/classic-guest-files.ts:
 * name normalization, disposition and range validation. Descriptor
 * open/read/write ride qa_open_contained_parent (landed) at the guest
 * capability call site. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/classic_guest_files.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/platform/files/contained.h"
void qa_app_sim_classic_guest_file_position(double offset, double length) {
  bool ok = offset == trunc(offset) && fabs(offset) <= 9007199254740991.0 && offset >= 0
      && length == trunc(length) && fabs(length) <= 9007199254740991.0 && length >= 0
      && (offset + length) == trunc(offset + length) && fabs(offset + length) <= 9007199254740991.0;
  if (!ok) qa_range_error("Invalid native file range");
}
char *qa_app_sim_classic_guest_file_name(const char *path) {
  size_t n = strlen(path); char *out = malloc(n + 1); if (out == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < n; i++) out[i] = path[i] == '\\' ? '/' : path[i];
  out[n] = '\0';
  char *start = out; while (strncmp(start, "./", 2) == 0) start += 2;
  if (start != out) memmove(out, start, strlen(start) + 1);
  size_t count = 0; char **parts = qa_contained_file_parts(out, &count);
  qa_contained_file_parts_free(parts, count);
  return out;
}
bool qa_app_sim_classic_guest_file_allowed(qa_app_sim_classic_guest_file_options options) {
  if (!(options.creation == trunc(options.creation)) || options.creation < 1 || options.creation > 5)
    qa_range_error("Invalid native file creation disposition");
  return options.write || options.creation == 3;
}
