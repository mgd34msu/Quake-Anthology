#ifndef QA_FRONTEND_INVENTORY_MENU_H
#define QA_FRONTEND_INVENTORY_MENU_H
#include "internal.h"
#include "qa/hud_q2.h"
typedef struct frontend_inventory_menu frontend_inventory_menu;
typedef bool (*frontend_inventory_menu_reader)(void *,qa_hud_data *,qa_strings **,qa_error *);
bool frontend_inventory_menu_create(frontend_seat *,qa_error *);
void frontend_inventory_menu_destroy(frontend_inventory_menu **);
void frontend_inventory_menu_source(frontend_seat *,void *,frontend_inventory_menu_reader,bool visible);
void frontend_inventory_menu_clear(frontend_seat *,const void *);
void frontend_inventory_menu_player(frontend_seat *,qa_actor_id,qa_item_id,bool visible);
bool frontend_inventory_menu_sync(frontend_seat *,qa_error *);
bool frontend_inventory_rows_read(frontend_seat *,qa_actor_id,qa_item_id,qa_hud_data *,qa_error *);
bool frontend_inventory_menu_wire_rows(frontend_seat *,const qa_hud_q2_names *,
    const int32_t *,size_t,int32_t selected,qa_hud_data *,qa_error *);
#endif
