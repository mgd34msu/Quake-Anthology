/*
 * Port of src/content/model-attachment.ts: readModelAttachment and
 * readModelGrip over a SaveReader. Types are the contracts
 * qa_model_attachment_definition / qa_model_transform. The file form
 * parses JSON via qa_parse_json_value (no fatal UTF-8 check, unlike
 * the TS TextDecoder); the digest/name strings in `out` are malloc'd
 * copies freed by qa_content_model_attachment_free. False only on
 * OOM; validation aborts with the exact TS Error messages.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODEL_ATTACHMENT_H
#define QA_CONTENT_MODEL_ATTACHMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/model_attachment.h"
#include "qa/contracts/scene.h"
#include "qa/persistence/value.h"

qa_model_transform qa_read_model_grip(const qa_save_reader *grip);
bool qa_content_read_model_attachment(const uint8_t *bytes, size_t length,
                                      qa_model_attachment_definition *out);
void qa_content_model_attachment_free(qa_model_attachment_definition *value);

#endif
