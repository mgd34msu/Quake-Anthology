#ifndef QA_BOT_BSP_SAVE_H
#define QA_BOT_BSP_SAVE_H

#include "qa/bot_bsp.h"

/* Source spans remain borrowed from the qualified immutable entity bytes.
 * Exact tables, allocation capacities, duplicate epairs and partial source
 * parse results are restored directly; no lexer or diagnostic callback runs.
 * Candidate bytes must remain alive until the restored BSP owner closes. */
bool qa_bot_bsp_capture(const qa_bot_bsp *, qa_bytes source, qa_buffer *, qa_error *);
bool qa_bot_bsp_restore(qa_bytes record, qa_bytes source, qa_bot_bsp **, qa_error *);

#endif
