/* Port of src/bots/behavior/rerelease/data/source-files.ts. Reuses
 * qa_bot_asset_files (owner: behavior/assets.ts, bots-a). GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_DATA_SOURCE_FILES_H
#define QA_BOTS_BEHAVIOR_RERELEASE_DATA_SOURCE_FILES_H
#include "qa/bots/behavior/assets.h"
/* Malloc'd byte-char text (trailing NUL bytes stripped), caller frees; NULL = TS null/OOM. */
char *qa_read_bot_source_text(qa_bot_asset_files *files, const char *path);
#endif
