#ifndef QA_APPLICATION_NATIVE_Q2_INVENTORY_SCANNER_H
#define QA_APPLICATION_NATIVE_Q2_INVENTORY_SCANNER_H
#include "qa/native.h"
#include "qa/inventory.h"
struct application_native_q2;
struct application_provider;
typedef struct application_native_q2_inventory_scanner application_native_q2_inventory_scanner;
typedef enum application_native_q2_inventory_presentation_kind {
    APPLICATION_NATIVE_INVENTORY_PRESENTATION_NONE,
    APPLICATION_NATIVE_INVENTORY_PRESENTATION_WEAPON,
    APPLICATION_NATIVE_INVENTORY_PRESENTATION_AMMUNITION,
    APPLICATION_NATIVE_INVENTORY_PRESENTATION_ITEM
} application_native_q2_inventory_presentation_kind;
typedef enum application_native_q2_inventory_icon_kind {
    APPLICATION_NATIVE_INVENTORY_ICON_NONE,
    APPLICATION_NATIVE_INVENTORY_ICON_IMAGE,
    APPLICATION_NATIVE_INVENTORY_ICON_WAD_PICTURE,
    APPLICATION_NATIVE_INVENTORY_ICON_SHADER
} application_native_q2_inventory_icon_kind;
typedef struct application_native_q2_inventory_presentation {
    qa_actor_owner source;
    application_native_q2_inventory_presentation_kind kind;
    qa_item_id weapon;
    application_native_q2_inventory_icon_kind icon_kind;
    const char *icon, *lump;
} application_native_q2_inventory_presentation;
typedef struct application_native_q2_inventory_row {
    qa_item_id item;
    const char *label;
    double count;
    uint32_t source_index;
    bool selected, presence_only;
    application_native_q2_inventory_presentation presentation;
} application_native_q2_inventory_row;
typedef struct application_native_q2_inventory_scanner_options {
    void *context;
    bool (*rows)(void *, qa_actor_id, const application_native_q2_inventory_row **,
        size_t *, bool *present, qa_error *);
    bool (*use)(void *, qa_actor_id, qa_item_id, qa_error *);
    bool (*print)(void *, qa_actor_id, const char *, qa_error *);
} application_native_q2_inventory_scanner_options;
typedef struct application_native_q2_inventory_readout {
    application_native_q2_inventory_row *rows;
    size_t count;
    qa_item_id selected;
    application_native_q2_inventory_presentation selected_presentation;
    bool present;
} application_native_q2_inventory_readout;
/* The profile is the acquired primary declaration, never a callback document.
 * Returned rows borrow only their copied readout until readout_free. */
bool application_native_q2_inventory_scanner_create(struct application_native_q2 *,
    const application_native_q2_inventory_scanner_options *,
    application_native_q2_inventory_scanner **, qa_error *);
bool application_native_q2_inventory_scanner_activate(application_native_q2_inventory_scanner *, qa_error *);
bool application_native_q2_inventory_scanner_suspend(application_native_q2_inventory_scanner *, qa_error *);
bool application_native_q2_inventory_scanner_destroy(application_native_q2_inventory_scanner *, qa_error *);
bool application_native_q2_inventory_scanner_idle(const application_native_q2_inventory_scanner *);
bool application_native_q2_inventory_scanner_returned(const application_native_q2_inventory_scanner *);
bool application_native_q2_inventory_scanner_read(application_native_q2_inventory_scanner *,
    qa_actor_id, application_native_q2_inventory_readout *, qa_error *);
void application_native_q2_inventory_readout_free(application_native_q2_inventory_readout *);
bool application_native_q2_inventory_scanner_restore_selection(application_native_q2_inventory_scanner *,
    qa_actor_id, qa_item_id, qa_error *);
void application_native_q2_inventory_scanner_release(application_native_q2_inventory_scanner *, qa_actor_id);
bool application_native_q2_inventory_scanner_capture(application_native_q2_inventory_scanner *, qa_buffer *, qa_error *);
bool application_native_q2_inventory_scanner_restore(application_native_q2_inventory_scanner *, qa_bytes, qa_error *);
bool application_native_q2_inventory_scanner_finish_restore(application_native_q2_inventory_scanner *, qa_error *);
bool application_native_q2_inventory_mixed_read(struct application_provider *, qa_actor_id,
    application_native_q2_inventory_readout *, qa_error *);
#endif
