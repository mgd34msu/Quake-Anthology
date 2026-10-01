#ifndef QA_Q3_NATIVE_VIEW_INTERNAL_H
#define QA_Q3_NATIVE_VIEW_INTERNAL_H
#include "view.h"
#include "player_state_internal.h"
struct q3n_view {
    q3n_view_options options;
    const qa_q3_game *source_game;
    qa_q3_product product;
    q3n_view_state state;
    char test_model_name[64];
    qa_q3_ref_entity test_model;
    bool busy;
};
#endif
