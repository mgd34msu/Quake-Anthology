/* Port of src/app/bootstrap/q3-product.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_product.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/startup_commands.h"
#include "qa/content/q3/product_restriction.h"
#include "qa/core/cvars/index.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
typedef struct registrar_ctx { qa_cvar_registry *cvars; } registrar_ctx;
static bool registrar_register(void *ctx, const char *name, const char *default_value,
    int64_t flags, int64_t *out_integer_value) {
  registrar_ctx *registrar = ctx;
  qa_cvar_snapshot snap;
  if (!qa_cvar_registry_register(registrar->cvars, name, default_value, (double)flags, &snap)) return false;
  *out_integer_value = (int64_t)snap.integer_value;
  qa_cvar_read_free(&snap);
  return true;
}
static bool name_is_ci(const char *a, const char *b) {
  if (strlen(a) != strlen(b)) return false;
  for (size_t i = 0; a[i] != '\0'; i++)
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
  return true;
}
typedef struct read_ctx {
  const qa_app_q3_product_host *host;
  const qa_catalog_discovered_product *selected;
} read_ctx;
static bool read_product_id(void *ctx, const uint8_t **out, size_t *out_length) {
  read_ctx *read = ctx;
  const uint8_t *bytes = read->host->product_id_bytes(read->host->ctx, read->selected, out_length);
  if (bytes == NULL) return false;
  *out = bytes;
  return true;
}
qa_app_prepared_q3_product qa_app_prepare_q3_application_product(
    const qa_content_catalog *catalog, const char *content, const qa_app_options *options,
    const qa_app_q3_product_host *host) {
  qa_app_prepared_q3_product out = {catalog, false, {{0, 0}, {QA_Q3_MOUNT_NONE, NULL, 0}}};
  const qa_catalog_discovered_product *selected = qa_content_catalog_product(catalog, content);
  if (selected->expectation.family != QA_GAME_FAMILY_Q3) return out;
  qa_q3_application_product product;
  bool has_product = options != NULL && options->has_q3_product;
  if (has_product) {
    product = options->q3_product;
  } else {
    qa_identity_owner identity = {0};
    if (!qa_identity_owner_create(&identity, "q3-product-policy")) qa_range_error("Out of memory");
    qa_cvar_registry_options cvar_options = {0};
    cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
    cvar_options.context.session = identity.session;
    cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
    qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
    if (cvars == NULL) qa_range_error("Out of memory");
    const char *const *lines = options != NULL ? (const char *const *)options->startup_commands : NULL;
    size_t line_count = options != NULL ? options->startup_command_count : 0;
    qa_app_startup_phases phases = qa_app_startup_command_phases(lines, line_count, QA_COMMAND_DIALECT_Q3);
    for (size_t i = 0; i < phases.variable_count; i++) {
      const char *name = phases.variables[i].name;
      if (name_is_ci(name, "com_prereleasedemo") || name_is_ci(name, "com_prereleaseteamarenademo")
          || name_is_ci(name, "fs_restrict")) {
        qa_cvar_snapshot snap;
        if (qa_cvar_registry_set(cvars, name, phases.variables[i].value, true, &snap)) qa_cvar_read_free(&snap);
      }
    }
    qa_app_startup_phases_free(&phases);
    registrar_ctx registrar = {cvars};
    qa_q3_cvar_registrar adapter = {&registrar, registrar_register};
    qa_q3_product_policy policy = qa_register_q3_product_policy(&adapter);
    int64_t forced_value = 0;
    bool forced_snapshot = registrar_register(&registrar, "fs_restrict", "0", QA_CVAR_FLAG_INIT, &forced_value);
    bool forced = (forced_snapshot ? forced_value : 0) != 0;
    read_ctx read = {host, selected};
    qa_q3_mount_restriction restriction = qa_q3_resolve_mount_restriction(policy, forced, read_product_id, &read);
    qa_cvar_registry_destroy(cvars);
    qa_identity_owner_destroy(&identity);
    product.policy = policy;
    product.restriction = restriction;
    has_product = true;
  }
  out.has_q3_product = has_product;
  out.q3_product = product;
  if (product.restriction.kind == QA_Q3_MOUNT_NONE) return out;
  const qa_content_catalog *restricted = NULL;
  const char *error = host->restrict_catalog(host->ctx, catalog, selected, &restricted);
  if (error != NULL) qa_range_error("%s", error);
  out.catalog = restricted;
  return out;
}
