#include "library_internal.h"
#include "qa/ui_menu_save.h"
#include "qa/source_save.h"

static bool blob(qa_source_save_io *io, qa_buffer *value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t maximum = reading ? io->input.size - io->offset : SIZE_MAX;
    if (!qa_source_save_count(io, &value->size, maximum)) return false;
    if (reading && value->size) {
        value->data = malloc(value->size);
        if (!value->data) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating library query"); return false; }
    }
    return qa_source_save_bytes(io, value->data, value->size);
}
static bool text(qa_source_save_io *io, char **owned)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t length = reading ? 0 : (*owned ? strlen(*owned) : 0);
    if ((!reading && !*owned) || !qa_source_save_count(io, &length,
        reading ? io->input.size - io->offset : SIZE_MAX - 1) || length == SIZE_MAX) return false;
    if (reading) {
        *owned = malloc(length + 1);
        if (!*owned) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating library profile text"); return false; }
    }
    if (!qa_source_save_bytes(io, *owned, length)) return false;
    if (reading) {
        (*owned)[length] = 0;
        if (memchr(*owned, 0, length)) {
            qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "library profile text contains NUL"); return false;
        }
    }
    return true;
}
static bool optional_text(qa_source_save_io *io, char **owned)
{
    bool present = *owned != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    return !present || text(io, owned);
}
static bool profiles(qa_source_save_io *io, qa_ui_library *saved)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = saved->local_player_count;
    size_t maximum = reading ? (io->input.size - io->offset) / 51 : SIZE_MAX / sizeof(library_profile);
    if (maximum > SIZE_MAX / sizeof(library_profile)) maximum = SIZE_MAX / sizeof(library_profile);
    if (!qa_source_save_count(io, &count, maximum) || !count) return false;
    if (reading) {
        saved->local_players = calloc(count, sizeof(*saved->local_players));
        if (!saved->local_players) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating library local roster"); return false; }
        saved->local_player_count = count;
    }
    bool own_seat = false;
    for (size_t i = 0; i < count; ++i) {
        library_profile copy = reading ? (library_profile){0} : saved->local_players[i];
        library_profile *p = reading ? &saved->local_players[i] : &copy;
        if (!reading && (p->seat.name != p->name || p->seat.team != p->team ||
            p->seat.character_model != p->character_model || p->seat.character_skin != p->character_skin ||
            p->seat.character_head_model != p->character_head_model ||
            p->seat.character_head_skin != p->character_head_skin)) return false;
        if (!qa_source_save_u32(io, &p->seat.id) ||
            !qa_source_save_u64(io, &p->seat.actor.registry) ||
            !qa_source_save_u64(io, &p->seat.actor.generation) ||
            !qa_source_save_u32(io, &p->seat.actor.slot) ||
            !text(io, &p->name) || !text(io, &p->team) ||
            !optional_text(io, &p->character_model) || !optional_text(io, &p->character_skin) ||
            !optional_text(io, &p->character_head_model) || !optional_text(io, &p->character_head_skin) ||
            !qa_source_save_u32(io, &p->seat.input_device) ||
            !qa_source_save_bool(io, &p->seat.local) ||
            !qa_source_save_bool(io, &p->seat.spectator) ||
            !qa_source_save_bool(io, &p->seat.bot) ||
            !qa_source_save_f32(io, &p->seat.bot_skill) ||
            !p->seat.local || p->seat.bot || p->seat.actor.registry) return false;
        if (reading) {
            p->seat.name = p->name; p->seat.team = p->team;
            p->seat.character_model = p->character_model; p->seat.character_skin = p->character_skin;
            p->seat.character_head_model = p->character_head_model;
            p->seat.character_head_skin = p->character_head_skin;
        }
        for (size_t j = 0; j < i; ++j)
            if (saved->local_players[j].seat.id == p->seat.id) return false;
        own_seat |= p->seat.id == saved->ui->options.seat;
    }
    return own_seat;
}
static bool same_text(const char *a, const char *b)
{
    return a && b ? !strcmp(a, b) : a == b;
}
static bool same_row(const qa_ui_row *a, const qa_ui_row *b)
{
    return same_text(a->key, b->key) && same_text(a->label, b->label) &&
        same_text(a->detail, b->detail) && a->enabled == b->enabled && !a->image;
}
static qa_ui_row product_row(const qa_product *p)
{
    return (qa_ui_row){.key = p->identity, .label = p->title,
        .detail = p->availability == QA_CONTENT_INSTALLED ? p->campaign : "Not installed",
        .enabled = p->availability == QA_CONTENT_INSTALLED};
}
static qa_ui_row map_row(bool authored, const qa_catalog_start *starts,
                         const qa_catalog_map *maps, size_t index)
{
    return (qa_ui_row){.key = authored ? starts[index].path : maps[index].path,
        .label = authored ? starts[index].title : maps[index].path,
        .detail = authored ? starts[index].episode : NULL, .enabled = true};
}
static bool cache(qa_source_save_io *io, qa_ui_library *saved)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    /* Refresh/search/failed factories invalidate borrowed row spans. Every
     * action/draw obtains the real factory, which rebuilds them before use. */
    if (saved->dirty) return true;
    size_t count = saved->product_count;
    size_t maximum = reading ? (io->input.size - io->offset) / 4 : SIZE_MAX / sizeof(qa_ui_row);
    if (maximum > SIZE_MAX / sizeof(qa_ui_row)) maximum = SIZE_MAX / sizeof(qa_ui_row);
    if (maximum > SIZE_MAX / sizeof(qa_product_id)) maximum = SIZE_MAX / sizeof(qa_product_id);
    if (!qa_source_save_count(io, &count, maximum)) return false;
    if (reading && count) {
        saved->products = calloc(count, sizeof(*saved->products));
        saved->product_ids = calloc(count, sizeof(*saved->product_ids));
        if (!saved->products || !saved->product_ids) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating library product cache"); return false;
        }
        saved->product_capacity = saved->id_capacity = count;
    }
    if (reading) saved->product_count = count;
    for (size_t i = 0; i < count; ++i) {
        uint32_t id = reading ? 0 : saved->product_ids[i];
        if (!qa_source_save_u32(io, &id)) return false;
        const qa_product *product = qa_catalog_product(saved->catalog, id);
        if (!product) return false;
        qa_ui_row row = product_row(product);
        if (reading) { saved->product_ids[i] = id; saved->products[i] = row; }
        else if (!same_row(&saved->products[i], &row)) return false;
    }
    size_t expected_count = 0;
    for (size_t i = 0; i < qa_catalog_count(saved->catalog); ++i) {
        const qa_product *p = qa_catalog_at(saved->catalog, i);
        bool matched;
        if (!ui_search(p->title, p->key, p->campaign,
            (qa_bytes){saved->query_lower.data, saved->query_lower.size}, &matched, io->error)) return false;
        if (!matched) continue;
        if (expected_count >= count || saved->product_ids[expected_count++] != p->id) return false;
    }
    if (expected_count != count) return false;
    size_t selected_product = count;
    for (size_t i = 0; i < count; ++i)
        if (saved->product_ids[i] == saved->product) selected_product = i;
    if (selected_product != saved->selected_product) return false;

    count = saved->map_count;
    maximum = reading ? (io->input.size - io->offset) / 8 : SIZE_MAX / sizeof(qa_ui_row);
    if (maximum > SIZE_MAX / sizeof(qa_ui_row)) maximum = SIZE_MAX / sizeof(qa_ui_row);
    if (maximum > SIZE_MAX / sizeof(size_t)) maximum = SIZE_MAX / sizeof(size_t);
    if (!qa_source_save_count(io, &count, maximum)) return false;
    size_t actual_count = 0;
    const qa_catalog_start *starts = NULL;
    const qa_catalog_map *maps = NULL;
    if (saved->product && selected_product < saved->product_count) {
        if (saved->starts) starts = qa_catalog_starts(saved->catalog, saved->product, NULL, &actual_count);
        else maps = qa_catalog_maps(saved->catalog, saved->product, &actual_count);
    }
    if (count != actual_count) return false;
    if (reading && count) {
        saved->maps = calloc(count, sizeof(*saved->maps));
        saved->map_indices = calloc(count, sizeof(*saved->map_indices));
        if (!saved->maps || !saved->map_indices) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating library map cache"); return false;
        }
        saved->map_capacity = saved->index_capacity = count;
    }
    if (reading) saved->map_count = count;
    for (size_t i = 0; i < count; ++i) {
        size_t index = reading ? 0 : saved->map_indices[i];
        if (!qa_source_save_count(io, &index, SIZE_MAX) || index != i) return false;
        qa_ui_row row = map_row(saved->starts, starts, maps, index);
        if (reading) { saved->map_indices[i] = index; saved->maps[i] = row; }
        else if (!same_row(&saved->maps[i], &row)) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io, qa_ui_library *saved,
                    const qa_ui_library *qualified, const qa_ui_menu_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','L','I','B'};
    uint32_t seat = qualified->ui->options.seat;
    uint64_t menu = qualified->menu, catalog = 0;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QLIB", 4) ||
        !qa_source_save_u32(io, &seat) || seat != qualified->ui->options.seat ||
        !qa_source_save_u64(io, &menu) || menu != qualified->menu) return false;
    if (!reading && !refs->catalog_encode(refs->context, saved->catalog, &catalog, io->error)) return false;
    if (!qa_source_save_u64(io, &catalog)) return false;
    if (reading) {
        qa_catalog *actual = NULL;
        if (!refs->catalog_decode(refs->context, catalog, &actual, io->error) || !actual) return false;
        qa_catalog_retain(actual); saved->catalog = actual;
    }
    if (!profiles(io, saved) || !qa_source_save_u32(io, &saved->product) ||
        (saved->product && !qa_catalog_product(saved->catalog, saved->product)) ||
        !qa_source_save_count(io, &saved->selected_product, SIZE_MAX) ||
        !qa_source_save_count(io, &saved->selected_map, SIZE_MAX) ||
        !qa_source_save_i32(io, &saved->skill) || !qa_source_save_bool(io, &saved->starts) ||
        !qa_source_save_bool(io, &saved->dirty) || !qa_source_save_u64(io, &saved->revision) ||
        !qa_source_save_bytes(io, saved->query, sizeof(saved->query)) || !memchr(saved->query, 0, sizeof(saved->query)) ||
        !qa_source_save_bytes(io, saved->status, sizeof(saved->status)) || !memchr(saved->status, 0, sizeof(saved->status)) ||
        !blob(io, &saved->query_lower)) return false;
    return cache(io, saved);
}
bool qa_ui_library_checkpoint(const qa_ui_library *menu, const qa_ui_menu_checkpoint_refs *refs,
                               qa_buffer *out, qa_error *error)
{
    if (!menu || !refs || !refs->catalog_encode || !out || out->data || out->size ||
        menu->ui->handling || menu->ui->drawing || !menu->catalog || !menu->local_player_count)
        return ui_fail(error, "library capture requires idle catalog and roster owners");
    qa_ui_library saved = *menu;
    qa_source_save_io io = {0};
    bool success = qa_source_save_writer(&io, NULL, error) && fields(&io, &saved, menu, refs) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!success && error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, 0, "library continuation leaves its actual owner domains");
    return success;
}
bool qa_ui_library_restore(qa_ui_library *menu, const qa_ui_menu_checkpoint_refs *refs,
                            qa_bytes bytes, qa_error *error)
{
    if (!menu || !refs || !refs->catalog_decode || menu->ui->handling || menu->ui->drawing)
        return ui_fail(error, "library restore requires its idle controller and catalog resolver");
    qa_ui_library saved = {.ui = menu->ui, .application = menu->application, .menu = menu->menu};
    qa_source_save_io io = {0};
    bool success = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, &saved, menu, refs) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!success) {
        ui_library_clear(&saved);
        if (error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "unqualified library continuation");
        return false;
    }
    qa_ui_library displaced = *menu; *menu = saved; ui_library_clear(&displaced); return true;
}
