/* Port of src/app/bootstrap/component-client-save.ts. The TS returns
 * new SaveImage values; here the caller supplies provider storage
 * (image copy + appended checkpoint bytes are owned). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_CLIENT_SAVE_H
#define QA_APP_BOOTSTRAP_COMPONENT_CLIENT_SAVE_H
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
/* Owned decoded value or NULL (TS null). Caller frees with qa_save_value_free. */
qa_save_value *qa_app_read_component_clients(const qa_save_image *image);
/* Fills out_checkpoint (+bytes) for the caller to append; out_image is
 * a shallow copy with providers = caller array of count+1 ending in it. */
void qa_app_save_component_clients(const qa_save_image *image, const qa_save_value *value,
                                   qa_provider_checkpoint *out_checkpoint, uint8_t **out_bytes, size_t *out_length);
void qa_app_require_component_client_presentation(const qa_save_image *image);
#endif
