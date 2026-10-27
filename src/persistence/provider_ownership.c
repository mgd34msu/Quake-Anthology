/* Port of src/persistence/provider-ownership.ts. GPL-2.0-or-later. */
#include "qa/persistence/provider_ownership.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *format, const char *argument) {
  fprintf(stderr, "Error: ");
  fprintf(stderr, format, argument);
  fputc('\n', stderr);
  abort();
}

const qa_provider_checkpoint_ref qa_campaign_unit_checkpoint = {
    "session:campaign-unit", "session:campaign-unit", 1};

qa_provider_checkpoint_ref qa_save_provider_contract(const char *schema, qa_provider_id source) {
  if (strcmp(schema, "world:source-slots") == 0)
    return (qa_provider_checkpoint_ref){"world:actors", schema, 1};
  if (strcmp(schema, "world:source-items") == 0 ||
      strcmp(schema, "world:primary-protection") == 0)
    return (qa_provider_checkpoint_ref){"world:gameplay", schema, 1};
  if (strcmp(schema, "session:campaign-unit") == 0)
    return qa_campaign_unit_checkpoint;
  if (strcmp(schema, "app:component-clients") == 0)
    return (qa_provider_checkpoint_ref){"app:component-clients", schema, 1};
  if (strncmp(schema, "session:", 8) == 0)
    qa_error("Unsupported session checkpoint %s", schema);
  return (qa_provider_checkpoint_ref){source, schema,
                                      strcmp(schema, "world:simulation") == 0 ? 11 : 1};
}

void qa_validate_save_provider_owner(qa_provider_checkpoint record, qa_provider_id source) {
  qa_provider_checkpoint_ref expected = qa_save_provider_contract(record.schema, source);
  if (strcmp(record.provider, expected.provider) != 0)
    qa_error("Saved provider %s has a different owner", record.schema);
  if (record.version != expected.version)
    qa_error("Unsupported saved provider version %s", record.schema);
}
