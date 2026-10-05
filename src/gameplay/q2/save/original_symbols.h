#ifndef QA_Q2_ORIGINAL_SYMBOLS_H
#define QA_Q2_ORIGINAL_SYMBOLS_H

#include "qa/game_q2.h"

typedef enum q2_original_symbol_kind {
    Q2_ORIGINAL_ITEM, Q2_ORIGINAL_FUNCTION, Q2_ORIGINAL_MOVE
} q2_original_symbol_kind;

typedef struct q2_original_library {
    char date[16];
    uint32_t init_game, mmove, item_list;
} q2_original_library;

/* The original compiler can fold identical callbacks to one address.
 * Reverse enumeration retains those names for the actual Source owner to
 * qualify; encoding always addresses the requested Source identity. */
bool q2_original_symbol_encode(q2_original_symbol_kind, qa_q2_product,
    const char *name, int32_t *value, qa_error *);
const char *q2_original_symbol_next(q2_original_symbol_kind, qa_q2_product,
    int32_t value, const char *previous);
size_t q2_original_item_count(qa_q2_product);
const q2_original_library *q2_original_library_for(qa_q2_product);

#endif
