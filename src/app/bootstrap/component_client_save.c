/* Port of src/app/bootstrap/component-client-save.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_client_save.h"
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/provider_ownership.h"
static const char *const SCHEMA = "app:component-clients";
qa_save_value *qa_app_read_component_clients(const qa_save_image *image) {
  const qa_provider_checkpoint *row = NULL;
  size_t count = 0;
  for (size_t i = 0; i < image->provider_count; i++)
    if (image->providers[i].schema && strcmp(image->providers[i].schema, SCHEMA) == 0) { row = &image->providers[i]; count++; }
  if (count == 0) return NULL;
  if (count != 1 || !row) qa_range_error("Duplicate component client checkpoint");
  qa_validate_save_provider_owner(*row, image->recipe.map.entities.provider);
  qa_save_value *value = qa_decode_checkpoint_value(row->bytes.bytes, row->bytes.length);
  qa_save_reader reader, version;
  qa_save_reader_init(&reader, value, SCHEMA);
  qa_save_reader_field(&reader, "version", &version);
  qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader_destroy(&reader);
  return value;
}
void qa_app_save_component_clients(const qa_save_image *image, const qa_save_value *value,
                                   qa_provider_checkpoint *out_checkpoint, uint8_t **out_bytes, size_t *out_length) {
  for (size_t i = 0; i < image->provider_count; i++)
    if (image->providers[i].schema && strcmp(image->providers[i].schema, SCHEMA) == 0)
      qa_range_error("Component client checkpoint is already attached");
  qa_provider_checkpoint_ref ref = qa_save_provider_contract(SCHEMA, image->recipe.map.entities.provider);
  *out_bytes = qa_encode_checkpoint_value(value, out_length);
  out_checkpoint->provider = ref.provider;
  out_checkpoint->schema = ref.schema;
  out_checkpoint->version = ref.version;
  out_checkpoint->bytes.bytes = *out_bytes;
  out_checkpoint->bytes.length = *out_length;
}
void qa_app_require_component_client_presentation(const qa_save_image *image) {
  qa_save_value *value = qa_app_read_component_clients(image);
  if (!value) return;
  qa_save_reader reader, seats;
  qa_save_reader_init(&reader, value, SCHEMA);
  qa_save_reader_field(&reader, "seats", &seats);
  size_t count = qa_save_reader_list_count(&seats);
  qa_save_reader_destroy(&seats);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
  if (count != 0) qa_range_error("Saved component viewing clients require a graphical destination");
}
