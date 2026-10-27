/* Port of src/compat/qvm/legacy-bot-abi.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_LEGACY_BOT_ABI_H
#define QA_COMPAT_QVM_LEGACY_BOT_ABI_H
#include <stdbool.h>
#include "qa/compat/qvm/abi.h"
/* TS decodeLegacyQvmGameImport(code): QvmGameImport|null. */
bool qa_qvm_decode_legacy_game_import(double code, qa_qvm_game_import *out);
#endif
