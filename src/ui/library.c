#include "internal.h"
#include <stdio.h>

struct qa_ui_library {
    qa_ui *ui;
    qa_application *application;
    qa_ui_id menu;
    qa_catalog *catalog;
    qa_product_id product;
    qa_ui_row *products, *maps;
    qa_product_id *product_ids;
    size_t *map_indices;
    size_t product_count, product_capacity, id_capacity, map_count, map_capacity, index_capacity;
    size_t selected_product, selected_map;
    int32_t skill;
    bool starts, dirty;
    uint64_t revision;
    char query[321], status[256];
    qa_buffer query_lower;
    qa_ui_control controls[8];
    qa_launch_seat local_player;
    char *player_name, *player_team;
};
enum { LIB_SEARCH = 1, LIB_PRODUCTS, LIB_MAPS, LIB_STARTS, LIB_SKILL, LIB_LAUNCH, LIB_REFRESH, LIB_STATUS };
static void select_product(qa_ui_library *menu, qa_product_id product) {
    menu->product = product;
    const qa_product *selected = qa_catalog_product(menu->catalog, product);
    menu->skill = selected && selected->family == QA_GAME_Q3 ? 2 : 1;
    menu->selected_map = 0;
    menu->dirty = true;
}
bool qa_ui_library_refresh(qa_ui_library *menu, qa_error *error) {
    if (!menu || menu->ui->drawing) return ui_fail(error, "invalid library refresh");
    if (!qa_application_rediscover(menu->application, true, error)) return false;
    qa_catalog *catalog = qa_application_catalog(menu->application);
    qa_catalog_retain(catalog);
    const qa_product *old = qa_catalog_product(menu->catalog, menu->product);
    const qa_product *next = old ? qa_catalog_find(catalog, old->identity) : NULL;
    qa_catalog_release(menu->catalog);
    menu->catalog = catalog;
    menu->product = next ? next->id : 0;
    menu->dirty = true; menu->selected_map = 0;
    snprintf(menu->status, sizeof(menu->status), "Content refreshed");
    return true;
}
static bool rows(qa_ui_library *menu, qa_error *error) {
    if (!menu->dirty) return true;
    menu->product_count = 0; menu->map_count = 0;
    for (size_t i = 0; i < qa_catalog_count(menu->catalog); ++i) {
        const qa_product *product = qa_catalog_at(menu->catalog, i);
        bool matched;
        if (!ui_search(product->title, product->key, product->campaign,
            (qa_bytes){menu->query_lower.data, menu->query_lower.size}, &matched, error)) return false;
        if (!matched) continue;
        size_t count = menu->product_count + 1;
        if (!ui_reserve((void **)&menu->products, &menu->product_capacity, count, sizeof(*menu->products), error) ||
            !ui_reserve((void **)&menu->product_ids, &menu->id_capacity, count, sizeof(*menu->product_ids), error)) return false;
        size_t index = menu->product_count++;
        menu->products[index] = (qa_ui_row){.key = product->identity, .label = product->title,
            .detail = product->availability == QA_CONTENT_INSTALLED ? product->campaign : "Not installed",
            .enabled = product->availability == QA_CONTENT_INSTALLED};
        menu->product_ids[index] = product->id;
    }
    menu->selected_product = menu->product_count;
    for (size_t i = 0; i < menu->product_count; ++i)
        if (menu->product_ids[i] == menu->product) menu->selected_product = i;
    if (menu->selected_product == menu->product_count) {
        for (size_t i = 0; i < menu->product_count; ++i)
            if (menu->products[i].enabled) { menu->selected_product = i; select_product(menu, menu->product_ids[i]); break; }
    }
    const qa_product *product = qa_catalog_product(menu->catalog, menu->product);
    if (product && menu->selected_product < menu->product_count) {
        size_t count;
        const qa_catalog_start *starts = qa_catalog_starts(menu->catalog, product->id, NULL, &count);
        const qa_catalog_map *maps = NULL;
        if (!menu->starts) maps = qa_catalog_maps(menu->catalog, product->id, &count);
        if (!ui_reserve((void **)&menu->maps, &menu->map_capacity, count, sizeof(*menu->maps), error) ||
            !ui_reserve((void **)&menu->map_indices, &menu->index_capacity, count, sizeof(*menu->map_indices), error)) return false;
        for (size_t i = 0; i < count; ++i) {
            menu->map_indices[menu->map_count] = i;
            menu->maps[menu->map_count++] = (qa_ui_row){.key = menu->starts ? starts[i].path : maps[i].path,
                .label = menu->starts ? starts[i].title : maps[i].path,
                .detail = menu->starts ? starts[i].episode : NULL, .enabled = true};
        }
    }
    if (menu->selected_map >= menu->map_count) menu->selected_map = 0;
    ++menu->revision;
    menu->dirty = false;
    return true;
}
static bool launch(qa_ui_library *menu, qa_error *error) {
    if (menu->selected_map >= menu->map_count) return ui_fail(error, "select an installed map or authored start");
    qa_launch_draft *draft = NULL;
    const char *map = menu->maps[menu->selected_map].key;
    if (!qa_launch_draft_create(menu->catalog, menu->product, map, &draft, error)) return false;
    qa_launch_world world = qa_launch_draft_choices(draft)->world;
    world.skill = menu->skill;
    if (menu->starts) {
        size_t count;
        const qa_catalog_episode *episode;
        const qa_catalog_start *starts = qa_catalog_starts(menu->catalog, menu->product, &episode, &count);
        size_t index = menu->map_indices[menu->selected_map];
        if (index >= count) { qa_launch_draft_destroy(draft); return ui_fail(error, "authored start is unavailable"); }
        world.start_command = starts[index].bsp;
        if (index == 0 && episode && episode->command && *episode->command)
            world.start_command = episode->command;
    }
    const qa_launch_snapshot *snapshot = qa_application_launch(menu->application);
    const qa_launch_choices *active = snapshot ? qa_launch_snapshot_choices(snapshot) : NULL;
    bool ok = qa_launch_set_world(draft, &world, error);
    if (active && active->seat_count) {
        for (size_t i = 0; i < active->seat_count && ok; ++i)
            ok = qa_launch_set_seat(draft, &active->seats[i], error);
    } else if (ok) ok = qa_launch_set_seat(draft, &menu->local_player, error);
    if (ok) ok = qa_application_apply(menu->application, draft, error);
    qa_launch_draft_destroy(draft);
    if (!ok) {
        snprintf(menu->status, sizeof(menu->status), "%s", error ? error->message : "Launch rejected");
        return false;
    }
    return qa_ui_close_all(menu->ui, menu->ui->time_ms, error);
}
static bool action(void *context, uint32_t seat, qa_ui_id control,
                    const qa_ui_action *event, qa_error *error) {
    qa_ui_library *menu = context;
    (void)seat;
    if (control == LIB_SEARCH && event->kind == QA_UI_CHANGE_TEXT) {
        const char *value = event->value.text ? event->value.text : "";
        qa_buffer lower = {0};
        if (!qa_utf8_lower((qa_bytes){(const uint8_t *)value, strlen(value)}, &lower, error)) return false;
        snprintf(menu->query, sizeof(menu->query), "%s", value);
        qa_buffer_free(&menu->query_lower); menu->query_lower = lower; menu->dirty = true;
        return true;
    }
    if (control == LIB_PRODUCTS && (event->kind == QA_UI_SELECT || event->kind == QA_UI_ROW_ACTIVATE)) {
        if (event->value.row < menu->product_count) select_product(menu, menu->product_ids[event->value.row]);
        return true;
    }
    if (control == LIB_MAPS) {
        if (event->kind == QA_UI_SELECT) { menu->selected_map = event->value.row; return true; }
        if (event->kind == QA_UI_ROW_ACTIVATE) { menu->selected_map = event->value.row; return launch(menu, error); }
    }
    if (control == LIB_STARTS && event->kind == QA_UI_CHANGE_NUMBER) {
        menu->starts = event->value.number != 0; menu->dirty = true; menu->selected_map = 0; return true;
    }
    if (control == LIB_SKILL && event->kind == QA_UI_SELECT) {
        const qa_product *product = qa_catalog_product(menu->catalog, menu->product);
        menu->skill = (int32_t)event->value.row + (product && product->family == QA_GAME_Q3 ? 1 : 0);
        return true;
    }
    if (event->kind == QA_UI_ACTIVATE && control == LIB_LAUNCH) return launch(menu, error);
    if (event->kind == QA_UI_ACTIVATE && control == LIB_REFRESH) return qa_ui_library_refresh(menu, error);
    return true;
}
static bool factory(void *context, uint32_t seat, qa_ui_menu *out, qa_error *error) {
    qa_ui_library *menu = context;
    (void)seat;
    if (!rows(menu, error)) return false;
    for (size_t i = 0; i < 8; ++i)
        menu->controls[i] = (qa_ui_control){.id = i + 1, .kind = QA_UI_BUTTON,
            .enabled = true, .visible = true, .context = menu, .action = action};
    menu->controls[0].kind = QA_UI_FIELD; menu->controls[0].label = "Search games";
    menu->controls[0].rect = (qa_scene_rect_f){40, 88, 560, 28};
    menu->controls[0].value.field.text = menu->query; menu->controls[0].value.field.maximum = 80;
    for (size_t i = 0; i < 2; ++i) {
        qa_ui_control *control = &menu->controls[i + 1];
        control->kind = QA_UI_LIST; control->rect = (qa_scene_rect_f){40 + (float)i * 284, 124, 276, 220};
        control->value.list.rows = i ? menu->maps : menu->products;
        control->value.list.count = i ? menu->map_count : menu->product_count;
        control->value.list.selected = i ? menu->selected_map : menu->selected_product;
        control->value.list.row_height = 28;
        control->value.list.revision = menu->revision;
        control->enabled = control->value.list.count != 0;
    }
    menu->controls[3].kind = QA_UI_TOGGLE; menu->controls[3].label = "Authored campaign starts";
    menu->controls[3].rect = (qa_scene_rect_f){40, 352, 350, 28}; menu->controls[3].value.checked = menu->starts;
    const qa_product *product = qa_catalog_product(menu->catalog, menu->product);
    static const char *q3_skills[] = {"1", "2", "3", "4", "5"};
    static const char *classic_skills[] = {"Easy", "Normal", "Hard", "Nightmare"};
    bool q3 = product && product->family == QA_GAME_Q3;
    menu->controls[4].kind = QA_UI_CHOICE; menu->controls[4].label = "Skill";
    menu->controls[4].rect = (qa_scene_rect_f){400, 352, 200, 28};
    menu->controls[4].value.choice.labels = q3 ? q3_skills : classic_skills;
    menu->controls[4].value.choice.count = q3 ? 5 : 4;
    menu->controls[4].value.choice.selected = (size_t)(menu->skill - (q3 ? 1 : 0));
    menu->controls[5].label = "Start selected game"; menu->controls[5].enabled = menu->map_count != 0;
    menu->controls[5].rect = (qa_scene_rect_f){40, 392, 350, 28};
    menu->controls[6].label = "Refresh content"; menu->controls[6].rect = (qa_scene_rect_f){400, 392, 200, 28};
    menu->controls[7].label = menu->status; menu->controls[7].enabled = false;
    menu->controls[7].rect = (qa_scene_rect_f){40, 432, 560, 32};
    *out = (qa_ui_menu){.id = menu->menu, .title = "Games and maps", .controls = menu->controls,
                        .count = 8, .fullscreen = true};
    return true;
}
bool qa_ui_library_create(qa_ui *ui, qa_application *application, qa_ui_id id,
                          const qa_launch_seat *local_player, qa_ui_library **out, qa_error *error) {
    if (!ui || !application || !id || !out || !local_player || local_player->id != ui->options.seat ||
        !local_player->local || local_player->bot || local_player->actor.registry || !local_player->name)
        return ui_fail(error, "game library requires an actual local seat/profile");
    qa_ui_library *menu = calloc(1, sizeof(*menu));
    if (!menu) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating game library"); return false; }
    menu->ui = ui; menu->application = application; menu->menu = id; menu->starts = true;
    size_t name_length = strlen(local_player->name), team_length = strlen(local_player->team ? local_player->team : "");
    menu->player_name = malloc(name_length + 1); menu->player_team = malloc(team_length + 1);
    if (!menu->player_name || !menu->player_team) {
        free(menu->player_name); free(menu->player_team); free(menu);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining local seat profile"); return false;
    }
    memcpy(menu->player_name, local_player->name, name_length + 1);
    memcpy(menu->player_team, local_player->team ? local_player->team : "", team_length + 1);
    menu->local_player = *local_player;
    menu->local_player.name = menu->player_name; menu->local_player.team = menu->player_team;
    menu->catalog = qa_application_catalog(application); qa_catalog_retain(menu->catalog);
    menu->dirty = true; menu->skill = 1;
    const qa_launch_snapshot *snapshot = qa_application_launch(application);
    if (snapshot) {
        const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
        const qa_product *current = qa_catalog_product(qa_launch_snapshot_catalog(snapshot), choices->world.preset);
        const qa_product *product = current ? qa_catalog_find(menu->catalog, current->identity) : NULL;
        if (product) select_product(menu, product->id);
    }
    if (!qa_ui_register(ui, &(qa_ui_menu_registration){.id = id, .context = menu, .factory = factory}, error)) {
        qa_catalog_release(menu->catalog); free(menu->player_name); free(menu->player_team); free(menu); return false;
    }
    *out = menu;
    return true;
}
bool qa_ui_library_destroy(qa_ui_library *menu, double time, qa_error *error) {
    if (!menu) return true;
    if (menu->ui->handling) return ui_fail(error, "game library callback is active");
    if (!qa_ui_unregister(menu->ui, menu->menu, time, error)) return false;
    qa_catalog_release(menu->catalog); qa_buffer_free(&menu->query_lower);
    free(menu->player_name); free(menu->player_team);
    free(menu->products); free(menu->product_ids); free(menu->maps); free(menu->map_indices); free(menu);
    return true;
}
