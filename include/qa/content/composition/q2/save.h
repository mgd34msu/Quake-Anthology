/*
 * Port of src/content/composition/q2/save.ts: captureQ2Product and
 * restoreQ2Product. The source-private records are the module captures
 * encoded with the src/persistence/q2-*.ts codecs; shared tables are saved
 * by the session. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_SAVE_H
#define QA_CONTENT_COMPOSITION_Q2_SAVE_H

#include <stddef.h>

#include "qa/content/composition/q2/index.h"
#include "qa/contracts/session.h"

/** TS captureQ2Product(source). Returns a malloc'd record array (count in
 * *out_count); each record owns its bytes and schema. Release with
 * qa_q2_product_checkpoints_free. */
qa_provider_checkpoint *qa_capture_q2_product(qa_q2_product_runtime *source, size_t *out_count);
void qa_q2_product_checkpoints_free(qa_provider_checkpoint *records, size_t count);

/** TS restoreQ2Product(source, checkpoints). The caller restores shared
 * actor/body/combat/inventory identities first. A missing, duplicated or
 * mismatched record raises the TS Error (qa_raise "Error"). */
void qa_restore_q2_product(qa_q2_product_runtime *source, const qa_provider_checkpoint *checkpoints, size_t count);

#endif
