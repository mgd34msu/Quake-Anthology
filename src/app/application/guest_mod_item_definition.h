#ifndef APPLICATION_GUEST_MOD_ITEM_DEFINITION_H
#define APPLICATION_GUEST_MOD_ITEM_DEFINITION_H
#include "qa/json.h"
#include "qa/inventory.h"
#include "qa/strings.h"
#include "qa/unified_frame_player.h"

typedef struct application_mod_item_icon {
    qa_unified_inventory_icon_kind kind;
    qa_string_id path, lump;
} application_mod_item_icon;

bool application_mod_item_text(const qa_json_document *,qa_json_id,char **,qa_error *);
bool application_mod_item_identity(const qa_json_document *,qa_json_id,qa_strings *,qa_item_id *,qa_error *);
bool application_mod_item_definition(const qa_json_document *,qa_json_id,qa_strings *,
    qa_item_admission *,qa_buffer *icon,qa_buffer *held,application_mod_item_icon *,qa_json_id actions[2],qa_error *);
bool application_mod_pickup_definition(const qa_json_document *, qa_json_id, qa_strings *,
    uint32_t *, qa_item_id **, size_t *, qa_pickup_write **, size_t *, qa_error *);
#endif
