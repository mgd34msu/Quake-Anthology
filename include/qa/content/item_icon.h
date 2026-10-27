/*
 * Port of src/content/item-icon.ts: item icon declarations and their
 * resolution to HUD icons. Types are the contracts
 * qa_source_item_icon_declaration ("kind" keeps its exact TS string)
 * and qa_weapon_hud_icon. Strings in reader results are malloc'd
 * copies freed by qa_content_item_icon_free /
 * qa_content_weapon_hud_icon_free. False only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_ITEM_ICON_H
#define QA_CONTENT_ITEM_ICON_H

#include <stdbool.h>

#include "qa/contracts/content.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/ui.h"
#include "qa/persistence/value.h"

bool qa_read_item_icon_declaration(const qa_save_reader *reader,
                                   qa_source_item_icon_declaration *out);
void qa_content_item_icon_free(qa_source_item_icon_declaration *value);

qa_weapon_hud_icon qa_resolve_item_icon(qa_source_item_icon_declaration icon,
                                        qa_content_id content);
bool qa_read_source_item_icon(const qa_save_reader *reader, qa_content_id content,
                              qa_weapon_hud_icon *out);
void qa_content_weapon_hud_icon_free(qa_weapon_hud_icon *value);

#endif
