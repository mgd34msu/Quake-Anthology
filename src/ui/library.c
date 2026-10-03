#include "library_internal.h"
#include "qa/ui_menu_save.h"
#include "qa/application_character_selection.h"
#include <stdio.h>
enum { LIB_SEARCH = 1, LIB_PRODUCTS, LIB_MAPS, LIB_STARTS, LIB_SKILL, LIB_LAUNCH, LIB_REFRESH, LIB_STATUS, LIB_EXECUTION };
static void select_product(qa_ui_library *menu, qa_product_id product) {
    menu->product = product;
    const qa_product *selected = qa_catalog_product(menu->catalog, product);
    menu->skill = selected && selected->family == QA_GAME_Q3 ? 2 : 1;
    menu->selected_map = 0;
    menu->original = false;
    free(menu->game_type); menu->game_type = NULL;
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
    const qa_catalog_mod *mod = qa_catalog_mod_find(catalog, menu->game_type);
    if (menu->game_type && (!mod || mod->product != menu->product || mod->unavailable)) {
        free(menu->game_type); menu->game_type = NULL;
    }
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
static bool gameplay_rows(qa_ui_library *menu, size_t *selected, qa_error *error) {
    const qa_product *product = qa_catalog_product(menu->catalog, menu->product);
    size_t maximum = qa_catalog_mod_count(menu->catalog) + 2;
    if (!ui_reserve((void **)&menu->gameplay, &menu->gameplay_capacity, maximum, sizeof(*menu->gameplay), error) ||
        !ui_reserve((void **)&menu->gameplay_labels, &menu->gameplay_label_capacity, maximum, sizeof(*menu->gameplay_labels), error)) return false;
    menu->gameplay_count = 0; *selected = 0;
    if (!product) return true;
    bool external = product->program_kind != QA_PROGRAM_BUILTIN;
    menu->gameplay[0] = (library_gameplay){.original = external};
    menu->gameplay_labels[0] = external ? "Original" : "Anthology";
    menu->gameplay_count = 1;
    if (!external && product->program && *product->program) {
        menu->gameplay[1] = (library_gameplay){.original = true};
        menu->gameplay_labels[1] = "Original"; menu->gameplay_count = 2;
        if (menu->original) *selected = 1;
    }
    for (size_t i = 0; i < qa_catalog_mod_count(menu->catalog); ++i) {
        const qa_catalog_mod *mod = qa_catalog_mod_at(menu->catalog, i);
        if (mod->product != product->id || mod->purpose != QA_MOD_GAME_TYPE || mod->unavailable) continue;
        size_t row = menu->gameplay_count++;
        menu->gameplay[row] = (library_gameplay){.component = mod->key};
        menu->gameplay_labels[row] = mod->title;
        if (menu->game_type && !strcmp(menu->game_type, mod->key)) *selected = row;
    }
    return true;
}
static bool launch_seat(qa_launch_draft *draft, const qa_launch_seat *input, qa_error *error) {
    qa_launch_seat seat = *input;
    if (seat.local && !seat.character_model && !seat.character_skin &&
        !seat.character_head_model && !seat.character_head_skin) {
        const qa_launch_choices *choices = qa_launch_draft_choices(draft);
        const qa_launch_binding *binding = qa_launch_binding_for(choices,
            (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat.id}, QA_ROLE_CHARACTER, "");
        const qa_launch_provider *provider = NULL;
        for (size_t i = 0; binding && i < choices->provider_count; ++i)
            if (!strcmp(choices->providers[i].instance, binding->instance)) {
                provider = &choices->providers[i]; break;
            }
        const qa_product *product = provider ? qa_catalog_product(qa_launch_draft_catalog(draft), provider->product) : NULL;
        qa_native_q3_character_declaration declaration;
        if (!product) return ui_fail(error, "menu character constructor lacks its selected product");
        if (!qa_native_q3_character_default_declaration(product->family, &declaration, error)) return false;
        seat.character_model = declaration.model; seat.character_skin = declaration.skin;
        seat.character_head_model = declaration.head_model; seat.character_head_skin = declaration.head_skin;
    }
    return qa_launch_set_seat(draft, &seat, error);
}
static bool launch(qa_ui_library *menu, qa_error *error) {
    if (menu->selected_map >= menu->map_count) return ui_fail(error, "select an installed map or authored start");
    qa_launch_draft *draft = NULL;
    const char *map = menu->maps[menu->selected_map].key;
    if (!qa_launch_draft_create(menu->catalog, menu->product, map, &draft, error)) return false;
    if (menu->original && !qa_launch_select_original(draft, "native:primary", error)) {
        qa_launch_draft_destroy(draft);
        return false;
    }
    if (menu->game_type && !qa_launch_select_game_type(draft, menu->game_type, error)) {
        qa_launch_draft_destroy(draft); return false;
    }
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
            ok = launch_seat(draft, &active->seats[i], error);
    } else {
        for (size_t i = 0; i < menu->local_player_count && ok; ++i)
            ok = launch_seat(draft, &menu->local_players[i].seat, error);
    }
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
    if (control == LIB_EXECUTION && event->kind == QA_UI_SELECT) {
        size_t selected;
        if (!gameplay_rows(menu, &selected, error)) return false;
        if (event->value.row >= menu->gameplay_count) return ui_fail(error, "Gameplay selection is unavailable");
        library_gameplay choice = menu->gameplay[event->value.row];
        char *component = NULL;
        if (choice.component) {
            component = malloc(strlen(choice.component) + 1);
            if (!component) { qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining selected game type"); return false; }
            strcpy(component, choice.component);
        }
        free(menu->game_type); menu->game_type = component;
        menu->original = choice.original;
        snprintf(menu->status, sizeof(menu->status), "%s", component ? "Map entities; preset players" : "");
        return true;
    }
    if (event->kind == QA_UI_ACTIVATE && control == LIB_LAUNCH) return launch(menu, error);
    if (event->kind == QA_UI_ACTIVATE && control == LIB_REFRESH) return qa_ui_library_refresh(menu, error);
    return true;
}
static bool factory(void *context, uint32_t seat, qa_ui_menu *out, qa_error *error) {
    qa_ui_library *menu = context;
    (void)seat;
    size_t selected_gameplay;
    if (!rows(menu, error) || !gameplay_rows(menu, &selected_gameplay, error)) return false;
    for (size_t i = 0; i < 9; ++i)
        menu->controls[i] = (qa_ui_control){.id = i + 1, .kind = QA_UI_BUTTON,
            .enabled = true, .visible = true, .context = menu, .action = action};
    menu->controls[0].kind = QA_UI_FIELD; menu->controls[0].label = "Search games";
    menu->controls[0].rect = (qa_scene_rect_f){40, 88, 560, 28};
    menu->controls[0].value.field.text = menu->query; menu->controls[0].value.field.maximum = 80;
    for (size_t i = 0; i < 2; ++i) {
        qa_ui_control *control = &menu->controls[i + 1];
        control->kind = QA_UI_LIST; control->rect = (qa_scene_rect_f){40 + (float)i * 284, 124, 276, 180};
        control->value.list.rows = i ? menu->maps : menu->products;
        control->value.list.count = i ? menu->map_count : menu->product_count;
        control->value.list.selected = i ? menu->selected_map : menu->selected_product;
        control->value.list.row_height = 28;
        control->value.list.revision = menu->revision;
        control->enabled = control->value.list.count != 0;
    }
    menu->controls[3].kind = QA_UI_TOGGLE; menu->controls[3].label = "Authored campaign starts";
    menu->controls[3].rect = (qa_scene_rect_f){40, 312, 350, 28}; menu->controls[3].value.checked = menu->starts;
    const qa_product *product = qa_catalog_product(menu->catalog, menu->product);
    static const char *q3_skills[] = {"1", "2", "3", "4", "5"};
    static const char *classic_skills[] = {"Easy", "Normal", "Hard", "Nightmare"};
    bool q3 = product && product->family == QA_GAME_Q3;
    menu->controls[4].kind = QA_UI_CHOICE; menu->controls[4].label = "Skill";
    menu->controls[4].rect = (qa_scene_rect_f){400, 312, 200, 28};
    menu->controls[4].value.choice.labels = q3 ? q3_skills : classic_skills;
    menu->controls[4].value.choice.count = q3 ? 5 : 4;
    menu->controls[4].value.choice.selected = (size_t)(menu->skill - (q3 ? 1 : 0));
    menu->controls[5].label = "Start game"; menu->controls[5].enabled = menu->map_count != 0;
    menu->controls[5].rect = (qa_scene_rect_f){40, 392, 160, 28};
    menu->controls[6].label = "Refresh content"; menu->controls[6].rect = (qa_scene_rect_f){400, 392, 200, 28};
    menu->controls[7].label = menu->status; menu->controls[7].enabled = false;
    menu->controls[7].rect = (qa_scene_rect_f){40, 432, 560, 32};
    menu->controls[8].kind = QA_UI_CHOICE; menu->controls[8].label = "Map gameplay";
    menu->controls[8].rect = (qa_scene_rect_f){40, 352, 560, 28};
    menu->controls[8].value.choice.labels = menu->gameplay_labels;
    menu->controls[8].value.choice.count = menu->gameplay_count;
    menu->controls[8].value.choice.selected = selected_gameplay;
    menu->controls[8].enabled = menu->gameplay_count > 1;
    *out = (qa_ui_menu){.id = menu->menu, .title = "Games and maps", .controls = menu->controls,
                        .count = 9, .fullscreen = true};
    return true;
}
static void release_profiles(qa_ui_library *menu) {
    for (size_t i = 0; i < menu->local_player_count; ++i) {
        free(menu->local_players[i].name); free(menu->local_players[i].team);
        free(menu->local_players[i].character_model); free(menu->local_players[i].character_skin);
        free(menu->local_players[i].character_head_model); free(menu->local_players[i].character_head_skin);
    }
    free(menu->local_players);
}
void ui_library_clear(qa_ui_library *menu) {
    qa_catalog_release(menu->catalog); qa_buffer_free(&menu->query_lower);
    release_profiles(menu);
    free(menu->products); free(menu->product_ids); free(menu->maps); free(menu->map_indices);
    free(menu->game_type); free(menu->gameplay); free(menu->gameplay_labels);
}
const qa_catalog *qa_ui_library_catalog(const qa_ui_library *menu) { return menu ? menu->catalog : NULL; }
bool qa_ui_library_create_restored(qa_ui *ui, qa_application *application, qa_ui_id id,
                                   qa_ui_library **out, qa_error *error) {
    if (!ui || !application || !id || !out || *out || ui->handling || ui->drawing)
        return ui_fail(error, "library restore requires an idle controller and empty output");
    qa_ui_library *menu = calloc(1, sizeof(*menu));
    if (!menu) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating restored game library"); return false; }
    menu->ui = ui; menu->application = application; menu->menu = id;
    if (!qa_ui_register(ui, &(qa_ui_menu_registration){.id = id, .context = menu, .factory = factory}, error)) {
        free(menu); return false;
    }
    *out = menu; return true;
}
bool qa_ui_library_create(qa_ui *ui, qa_application *application, qa_ui_id id,
                          const qa_launch_seat *local_players, size_t local_player_count,
                          qa_ui_library **out, qa_error *error) {
    if (!ui || !application || !id || !out || !local_players || !local_player_count ||
        local_player_count > SIZE_MAX / sizeof(library_profile))
        return ui_fail(error, "game library requires an actual local roster");
    bool own_seat = false;
    for (size_t i = 0; i < local_player_count; ++i) {
        const qa_launch_seat *player = &local_players[i];
        if (!player->local || player->bot || player->actor.registry || !player->name)
            return ui_fail(error, "game library local profile is invalid");
        for (size_t j = 0; j < i; ++j)
            if (player->id == local_players[j].id)
                return ui_fail(error, "game library local roster repeats a seat");
        own_seat |= player->id == ui->options.seat;
    }
    if (!own_seat) return ui_fail(error, "game library roster does not include its menu seat");
    qa_ui_library *menu = calloc(1, sizeof(*menu));
    if (!menu) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating game library"); return false; }
    menu->ui = ui; menu->application = application; menu->menu = id; menu->starts = true;
    menu->local_players = calloc(local_player_count, sizeof(*menu->local_players));
    if (!menu->local_players) {
        free(menu); qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating local roster profiles"); return false;
    }
    menu->local_player_count = local_player_count;
    for (size_t i = 0; i < local_player_count; ++i) {
        const qa_launch_seat *player = &local_players[i];
        const char *team = player->team ? player->team : "";
        size_t name_length = strlen(player->name), team_length = strlen(team);
        library_profile *profile = &menu->local_players[i];
        profile->name = malloc(name_length + 1); profile->team = malloc(team_length + 1);
        if (!profile->name || !profile->team) {
            release_profiles(menu); free(menu);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining local roster profiles"); return false;
        }
        memcpy(profile->name, player->name, name_length + 1);
        memcpy(profile->team, team, team_length + 1);
        profile->seat = *player;
        profile->seat.name = profile->name; profile->seat.team = profile->team;
        const char *source[] = {player->character_model, player->character_skin,
            player->character_head_model, player->character_head_skin};
        char **owned[] = {&profile->character_model, &profile->character_skin,
            &profile->character_head_model, &profile->character_head_skin};
        for (size_t j = 0; j < 4; ++j) {
            if (!source[j]) continue;
            size_t length = strlen(source[j]);
            *owned[j] = malloc(length + 1);
            if (!*owned[j]) {
                release_profiles(menu); free(menu);
                qa_error_set(error, QA_ERROR_MEMORY, 0, "retaining menu CHARACTER choices"); return false;
            }
            memcpy(*owned[j], source[j], length + 1);
        }
        profile->seat.character_model = profile->character_model;
        profile->seat.character_skin = profile->character_skin;
        profile->seat.character_head_model = profile->character_head_model;
        profile->seat.character_head_skin = profile->character_head_skin;
    }
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
        qa_catalog_release(menu->catalog); release_profiles(menu); free(menu); return false;
    }
    *out = menu;
    return true;
}
bool qa_ui_library_destroy(qa_ui_library *menu, double time, qa_error *error) {
    if (!menu) return true;
    if (menu->ui->handling) return ui_fail(error, "game library callback is active");
    if (!qa_ui_unregister(menu->ui, menu->menu, time, error)) return false;
    ui_library_clear(menu); free(menu);
    return true;
}
