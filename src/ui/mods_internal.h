#ifndef QA_UI_MODS_INTERNAL_H
#define QA_UI_MODS_INTERNAL_H
#include "internal.h"
typedef struct mod_row {
    const char *component, *instance;
    bool enabled;
    char state[96];
} mod_row;
struct qa_ui_mods {
    qa_ui *ui;
    qa_application *application;
    qa_ui_id menu;
    qa_launch_draft *draft;
    qa_catalog *catalog;
    qa_ui_row *rows;
    mod_row *selections;
    qa_arena rows_storage;
    size_t count, selected;
    qa_ui_control controls[7];
    char query[321], status[256], display[512];
    qa_buffer query_lower;
    bool dirty;
    uint64_t revision, configuration_generation;
};
void ui_mods_cache_release(qa_ui_mods *);
#endif
