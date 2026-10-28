/* Port of src/app/bootstrap/network/client-download-policy.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/client_download_policy.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
struct qa_app_client_download_permission { qa_cvar_registry *cvars; bool family_q3; };
const char *qa_app_client_download_category_name(qa_app_client_download_category category) {
  switch (category) {
    case QA_APP_DL_METADATA: return "metadata";
    case QA_APP_DL_PACKAGE: return "package";
    case QA_APP_DL_MAP: return "map";
    case QA_APP_DL_MODEL: return "model";
    case QA_APP_DL_SOUND: return "sound";
    case QA_APP_DL_PLAYER: return "player";
    case QA_APP_DL_PICTURE: return "picture";
  }
  return "picture";
}
static bool ends_with_ci(const char *s, size_t n, const char *suffix) {
  size_t m = strlen(suffix);
  if (n < m) return false;
  for (size_t i = 0; i < m; i++)
    if (tolower((unsigned char)s[n - m + i]) != suffix[i]) return false;
  return true;
}
static bool starts_with_ci(const char *s, const char *prefix) {
  for (size_t i = 0; prefix[i]; i++)
    if (tolower((unsigned char)s[i]) != prefix[i]) return false;
  return true;
}
qa_app_client_download_category qa_app_client_download_category_for(const char *path) {
  size_t n = strlen(path);
  if (ends_with_ci(path, n, ".pak") || ends_with_ci(path, n, ".pkz") || ends_with_ci(path, n, ".pk3"))
    return QA_APP_DL_PACKAGE;
  if (starts_with_ci(path, "maps/") || starts_with_ci(path, "env/") || starts_with_ci(path, "textures/"))
    return QA_APP_DL_MAP;
  if (starts_with_ci(path, "players/")) return QA_APP_DL_PLAYER;
  if (starts_with_ci(path, "models/")) return QA_APP_DL_MODEL;
  if (starts_with_ci(path, "sound/")) return QA_APP_DL_SOUND;
  return QA_APP_DL_PICTURE;
}
qa_app_client_download_permission *qa_app_create_client_download_permission(
    qa_cvar_registry *cvars, bool family_q3) {
  qa_app_client_download_permission *p = malloc(sizeof *p);
  if (!p) return NULL;
  p->cvars = cvars; p->family_q3 = family_q3;
  double archive = family_q3 ? QA_CVAR_FLAG_ARCHIVE : QA_Q2_CVAR_FLAG_ARCHIVE;
  qa_cvar_snapshot registered;
  if (family_q3) {
    qa_cvar_registry_register(cvars, "cl_allowDownload", "0", archive, &registered);
    return p;
  }
  qa_cvar_registry_register(cvars, "allow_download", "1", archive, &registered);
  qa_cvar_registry_register(cvars, "cl_http_downloads", "1", archive, &registered);
  static const char *names[] = {"allow_download_maps", "allow_download_models",
    "allow_download_sounds", "allow_download_players"};
  for (size_t i = 0; i < 4; i++) qa_cvar_registry_register(cvars, names[i], "1", archive, &registered);
  return p;
}
static bool category_allowed(qa_cvar_registry *cvars, qa_app_client_download_category category) {
  switch (category) {
    case QA_APP_DL_MAP: return qa_cvar_registry_variable_value(cvars, "allow_download_maps") != 0;
    case QA_APP_DL_MODEL: return qa_cvar_registry_variable_value(cvars, "allow_download_models") != 0;
    case QA_APP_DL_SOUND: return qa_cvar_registry_variable_value(cvars, "allow_download_sounds") != 0;
    case QA_APP_DL_PLAYER: return qa_cvar_registry_variable_value(cvars, "allow_download_players") != 0;
    case QA_APP_DL_PACKAGE:
      return qa_cvar_registry_variable_value(cvars, "allow_download_maps") != 0
        && qa_cvar_registry_variable_value(cvars, "allow_download_models") != 0
        && qa_cvar_registry_variable_value(cvars, "allow_download_sounds") != 0
        && qa_cvar_registry_variable_value(cvars, "allow_download_players") != 0;
    case QA_APP_DL_METADATA: case QA_APP_DL_PICTURE: return true;
  }
  return true;
}
bool qa_app_client_download_permitted(const qa_app_client_download_permission *permission,
    qa_app_download_transport transport, qa_app_client_download_category category) {
  qa_cvar_registry *cvars = permission->cvars;
  if (permission->family_q3) {
    qa_cvar_snapshot snap; double value = 0;
    if (qa_cvar_registry_get(cvars, "cl_allowDownload", &snap)) {
      value = snap.integer_value; qa_cvar_read_free(&snap);
    }
    return value != 0;
  }
  return qa_cvar_registry_variable_value(cvars, "allow_download") > 0
    && (transport != QA_APP_DL_HTTP || qa_cvar_registry_variable_value(cvars, "cl_http_downloads") != 0)
    && category_allowed(cvars, category);
}
void qa_app_client_download_permission_destroy(qa_app_client_download_permission *permission) { free(permission); }
