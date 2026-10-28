/* Port of src/app/bootstrap/save-requests.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/save_requests.h"
#include <string.h>
#include "qa/foundation/support.h"
qa_app_save_request qa_app_parse_save_request(const char *const *args, size_t count) {
  const char *name = count > 0 ? args[0] : NULL;
  const char *format = count > 1 ? args[1] : "shared";
  if (name == NULL || name[0] == '\0' || count > 2 ||
      (strcmp(format, "shared") != 0 && strcmp(format, "v5") != 0 && strcmp(format, "v6") != 0))
    qa_range_error("Usage: save <name or path> [shared|v5|v6]");
  qa_app_save_request r = {name, format};
  return r;
}
qa_app_load_request qa_app_parse_load_request(const char *const *args, size_t count) {
  const char *name = count > 0 ? args[0] : NULL;
  const char *source = count > 1 ? args[1] : NULL;
  if (name == NULL || name[0] == '\0' || count > 2 || (source != NULL && source[0] == '\0'))
    qa_range_error("Usage: load <name or path> [source-product]");
  qa_app_load_request r = {name, source};
  return r;
}
const qa_command_documentation *qa_app_save_command_documentation(const char *name) {
  static const char *const save_ex[] = {"save quicksave", "save original v5"};
  static const char *const load_ex[] = {"load quicksave", "load original q1-classic-id1"};
  static const qa_command_documentation save_doc = {
      "Save the world; v5/v6 export the active singleplayer NetQuake source in its original format.",
      "save <name or path> [shared|v5|v6]", save_ex, 2, NULL, 0};
  static const qa_command_documentation load_doc = {
      "Restore a save. Specify the source product when an original Quake save has ambiguous content.",
      "load <name or path> [source-product]", load_ex, 2, NULL, 0};
  if (strcmp(name, "save") == 0) return &save_doc;
  if (strcmp(name, "load") == 0) return &load_doc;
  return NULL;
}
