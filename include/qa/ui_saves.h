#ifndef QA_UI_SAVES_H
#define QA_UI_SAVES_H
#include "qa/ui.h"
typedef struct qa_ui_saves qa_ui_saves;
typedef struct qa_ui_save_entry {
    const char *id, *label, *map, *game, *unavailable;
    int64_t saved_at_ms;
    bool requires_product;
} qa_ui_save_entry;
typedef struct qa_ui_saves_service {
    void *context;
    bool (*list)(void *, bool saving, const qa_ui_save_entry **, size_t *, const char **, qa_error *);
    bool (*refresh)(void *, qa_error *);
    const char *(*unavailable)(void *, bool saving);
    bool (*busy)(void *);
    bool (*save)(void *, const char *name, const char *overwrite_id, qa_error *);
    bool (*load)(void *, const char *id, qa_error *);
    /* Original source saves may require an explicit installed product choice. */
    bool (*load_choice)(void *, qa_ui_control *, bool *, qa_error *);
} qa_ui_saves_service;
typedef struct qa_ui_saves_menus { qa_ui_id load, save, name, overwrite; } qa_ui_saves_menus;
bool qa_ui_saves_create(qa_ui *, qa_ui_saves_menus, const qa_ui_saves_service *, qa_ui_saves **, qa_error *);
bool qa_ui_saves_destroy(qa_ui_saves **, double time_ms, qa_error *);
#endif
