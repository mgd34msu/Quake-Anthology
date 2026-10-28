/* Port of src/app/bootstrap/q3-client/userinfo.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/userinfo.h"
#include <stdio.h>
#include <string.h>
static void reg_ignored(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_register(cvars, name, value, flags, &ignored)) qa_cvar_read_free(&ignored);
}
static void set_ignored(qa_cvar_registry *cvars, const char *name, const char *value, bool force) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_set(cvars, name, value, force, &ignored)) qa_cvar_read_free(&ignored);
}
static const char *const model_names[4] = {"model", "headmodel", "team_model", "team_headmodel"};
const char *qa_app_q3_client_model(const char *character, const char *model) {
  return character != NULL && strcmp(character, "q3") == 0 ? model : "sarge";
}
void qa_app_apply_q3_proxy_model(qa_cvar_registry *cvars, const char *character) {
  if (character != NULL && strcmp(character, "q3") == 0) return;
  for (size_t i = 0; i < 4; i++) {
    qa_cvar_read read;
    if (qa_cvar_registry_find(cvars, model_names[i], &read)) {
      qa_cvar_read_free(&read);
      set_ignored(cvars, model_names[i], "sarge/default", true);
    }
  }
}
void qa_app_initialize_q3_client_cvars(qa_cvar_registry *cvars, qa_app_q3_client_identity identity) {
  reg_ignored(cvars, "cl_timeNudge", "0", QA_CVAR_FLAG_TEMPORARY);
  reg_ignored(cvars, "rate", "25000", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO);
  reg_ignored(cvars, "cl_maxpackets", "30", QA_CVAR_FLAG_ARCHIVE);
  reg_ignored(cvars, "cl_packetdup", "1", QA_CVAR_FLAG_ARCHIVE);
  reg_ignored(cvars, "snaps", "20", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO);
  reg_ignored(cvars, "name", identity.name, QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO);
  char model_default[1024];
  snprintf(model_default, sizeof model_default, "%s/default", identity.model);
  for (size_t i = 0; i < 4; i++)
    reg_ignored(cvars, model_names[i], model_default, QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO);
  static const char *const pairs[][2] = {{"color1", "4"}, {"color2", "5"}, {"sex", "male"},
                                         {"cl_anonymous", "0"}, {"cg_predictItems", "1"}};
  for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++)
    reg_ignored(cvars, pairs[i][0], pairs[i][1], QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO);
  reg_ignored(cvars, "teamtask", "0", QA_CVAR_FLAG_USER_INFO);
  reg_ignored(cvars, "password", "", QA_CVAR_FLAG_USER_INFO);
  reg_ignored(cvars, "handicap", "100", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO);
  reg_ignored(cvars, "cl_maxPing", "800", QA_CVAR_FLAG_ARCHIVE);
  reg_ignored(cvars, "cl_serverStatusResendTime", "750", 0);
  reg_ignored(cvars, "sv_master1", "master.quake3arena.com", 0);
}
