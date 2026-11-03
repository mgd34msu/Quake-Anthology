/* Port of src/app/bootstrap/demo-playback.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/demo_playback.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"
static bool ends_ci(const char *text, const char *suffix) {
  size_t t = strlen(text), s = strlen(suffix);
  if (t < s) return false;
  for (size_t i = 0; i < s; i++)
    if (tolower((unsigned char)text[t - s + i]) != tolower((unsigned char)suffix[i])) return false;
  return true;
}
qa_app_demo_family qa_app_demo_family_of(const char *name, qa_app_demo_family fallback) {
  if (ends_ci(name, ".qwd")) return QA_APP_DEMO_QW;
  if (ends_ci(name, ".dem")) return QA_APP_DEMO_Q1;
  if (ends_ci(name, ".dm2") || ends_ci(name, ".mvd")) return QA_APP_DEMO_Q2;
  /* .dm_<digits> */
  const char *dot = strrchr(name, '.');
  if (dot && (dot[1] == 'd' || dot[1] == 'D') && (dot[2] == 'm' || dot[2] == 'M') && dot[3] == '_' && isdigit((unsigned char)dot[4])) {
    bool digits = true;
    for (const char *p = dot + 4; *p; p++) if (!isdigit((unsigned char)*p)) digits = false;
    if (digits) return QA_APP_DEMO_Q3;
  }
  return fallback;
}
static bool has_extension(const char *path) {
  const char *dot = strrchr(path, '.'), *slash = strrchr(path, '/');
  return dot && (!slash || dot > slash);
}
static char *join2(const char *a, const char *b) {
  size_t n = strlen(a) + strlen(b) + 1;
  char *p = malloc(n);
  if (p) snprintf(p, n, "%s%s", a, b);
  return p;
}
qa_app_demo_resource qa_app_open_demo_resource(qa_app_demo_request request, qa_app_demo_reader reader) {
  char *name = qa_normalize_resource_path(request.name);
  qa_app_demo_resource out = {request.family, NULL, NULL, 0, 0};
  if (request.family != QA_APP_DEMO_Q3) {
    const char *extension = request.family == QA_APP_DEMO_Q1 ? ".dem" : request.family == QA_APP_DEMO_QW ? ".qwd" : ".dm2";
    char *filename = has_extension(name) ? name : join2(name, extension);
    if (filename != name) free(name);
    char *path = filename;
    if (request.family == QA_APP_DEMO_Q2 && strncmp(filename, "demos/", 6) != 0) { path = join2("demos/", filename); free(filename); }
    if (!reader.read(reader.ctx, path, &out.bytes, &out.byte_count)) qa_range_error("Couldn't open demo %s", path);
    out.path = path;
    return out;
  }
  const char *filename = strncmp(name, "demos/", 6) == 0 ? name + 6 : name;
  /* suffix .dm_(\d+) */
  double requested = -1; /* Number(suffix[1]); -1 for no suffix */
  size_t stem_len = strlen(filename);
  const char *dot = strrchr(filename, '.');
  if (dot && (dot[1] == 'd' || dot[1] == 'D') && (dot[2] == 'm' || dot[2] == 'M') && dot[3] == '_' && isdigit((unsigned char)dot[4])) {
    bool digits = true;
    for (const char *p = dot + 4; *p; p++) if (!isdigit((unsigned char)*p)) digits = false;
    if (digits) { requested = qa_js_string_to_number(dot + 4); stem_len = (size_t)(dot - filename); }
  }
  bool supported = requested == 66 || requested == 67 || requested == 68;
  char stem[1024]; snprintf(stem, sizeof stem, "%.*s", (int)stem_len, filename);
  if (requested >= 0 && !supported) {
    char msg[128]; snprintf(msg, sizeof msg, "Protocol %s not supported for demos\n", QA_JS_NUMBER(requested));
    reader.print(reader.ctx, msg);
  }
  static const long natives[] = {66, 67, 68};
  size_t candidate_count = supported ? 1 : 3;
  for (size_t i = 0; i < candidate_count; i++) {
    long protocol = supported ? (long)requested : natives[i];
    char *path;
    if (supported) { path = join2("demos/", filename); }
    else { char buf[1200]; snprintf(buf, sizeof buf, "demos/%s.dm_%ld", stem, protocol); path = join2(buf, ""); }
    if (!reader.read(reader.ctx, path, &out.bytes, &out.byte_count)) {
      if (!supported) { char msg[1300]; snprintf(msg, sizeof msg, "Not found: %s\n", path); reader.print(reader.ctx, msg); }
      free(path);
      continue;
    }
    if (protocol != 66 && protocol != 67 && protocol != 68) qa_range_error("Invalid Quake III demo protocol selection");
    out.path = path;
    out.protocol = (double)protocol;
    free(name);
    return out;
  }
  qa_range_error("Couldn't open demo %s", name);
}
void qa_app_demo_resource_destroy(qa_app_demo_resource *resource) {
  if (!resource) return;
  free(resource->path);
  free(resource->bytes);
  *resource = (qa_app_demo_resource){QA_APP_DEMO_Q1, NULL, NULL, 0, 0};
}
