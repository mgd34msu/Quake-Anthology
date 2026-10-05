#ifndef QA_Q3_NATIVE_LOADING_INTERNAL_H
#define QA_Q3_NATIVE_LOADING_INTERNAL_H
#include "loading.h"
#include "../q3/internal.h"
#include "qa/q3_presentation_save.h"
#include "qa/ui_preferences.h"
#include "qa/game_q3.h"
#include <stdio.h>

/* CGAME's actual cg_info continuation, separate from renderer caches. */
typedef struct q3n_loading_state {
    char text[1024];
    int32_t player_icons[16], item_icons[26];
    uint32_t player_count, item_count;
} q3n_loading_state;
struct q3n_loading {
    q3n_loading_options options;
    qa_q3_product product;
    q3n_loading_state state;
    const q3n_frame *active_frame;
    int32_t active_sequence;
    bool busy, painting, painted;
};
bool q3nl_fail(qa_error *, qa_status, const char *);
bool q3nl_basis(const q3n_loading_options *, qa_q3_product *, qa_error *);
bool q3nl_checkpoint_basis(const q3n_loading_options *, qa_q3_product *, qa_error *);
#endif
