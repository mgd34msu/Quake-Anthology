/*
 * Port of src/persistence/qvm-grapple.ts. The returned definition
 * borrows strings from the checkpoint tree; list fields
 * (fire_arguments, movement words, initial cvars, view attachments)
 * are malloc'd arrays the caller frees. The definition itself is
 * malloc'd (contracts hold it by pointer); NULL only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_QVM_GRAPPLE_H
#define QA_PERSISTENCE_QVM_GRAPPLE_H

#include "qa/contracts/content.h"
#include "qa/contracts/qvm_grapple.h"
#include "qa/persistence/value.h"

qa_qvm_grapple_definition *qa_read_qvm_grapple_definition(const qa_save_reader *reader);

#endif
