#ifndef QA_UI_MENU_SAVE_H
#define QA_UI_MENU_SAVE_H
#include "qa/ui.h"
#include "qa/launch_save.h"
typedef struct qa_ui_menu_checkpoint_refs {
    void *context;
    bool (*catalog_encode)(void *, const qa_catalog *, uint64_t *, qa_error *);
    /* Borrows an actual qualified immutable catalog snapshot from the outer
     * candidate inventory. The menu takes its own reference after admission. */
    bool (*catalog_decode)(void *, uint64_t, qa_catalog **, qa_error *);
} qa_ui_menu_checkpoint_refs;
const qa_catalog *qa_ui_mods_catalog(const qa_ui_mods *);
/* Installs the actual menu factory on an existing stable UI controller, with
 * an empty draft/cache. No current launch copy or menu/input action runs. */
bool qa_ui_mods_create_restored(qa_ui *, qa_application *, qa_ui_id, qa_ui_mods **, qa_error *);
bool qa_ui_mods_checkpoint(const qa_ui_mods *, const qa_ui_menu_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_ui_mods_restore(qa_ui_mods *, const qa_ui_menu_checkpoint_refs *, qa_bytes, qa_error *);
const qa_catalog *qa_ui_library_catalog(const qa_ui_library *);
bool qa_ui_library_create_restored(qa_ui *, qa_application *, qa_ui_id, qa_ui_library **, qa_error *);
bool qa_ui_library_checkpoint(const qa_ui_library *, const qa_ui_menu_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_ui_library_restore(qa_ui_library *, const qa_ui_menu_checkpoint_refs *, qa_bytes, qa_error *);
#endif
