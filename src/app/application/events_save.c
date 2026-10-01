#include "events_save.h"
#include "match_intents.h"
#include "rankings.h"
#include "qa/source_save.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct event_store {
    application_event_record *builtin;
    qa_application_q2_map_event *q2_map;
    qa_application_q3_map_event *q3_map;
    qa_application_q2_player_event *q2_player;
    qa_application_protocol_event *protocol;
    size_t counts[5], capacities[5];
    size_t arena_block_size;
    uint64_t protocol_generation;
    qa_arena arena;
} event_store;

static const uint8_t event_magic[8] = {'Q','A','E','V','T','S',0,0};
static const size_t event_widths[5] = {
    sizeof(application_event_record), sizeof(qa_application_q2_map_event),
    sizeof(qa_application_q3_map_event), sizeof(qa_application_q2_player_event),
    sizeof(qa_application_protocol_event)
};
/* Every record has at least an enum/clock, one provider value and timestamp.
 * These lower bounds reject truncated row counts before allocating storage. */
static const size_t event_minimums[5] = {16, 13, 13, 13, 13};

static bool event_fail(qa_source_save_io *io, qa_status status, const char *text)
{
    if (!io->failed) qa_error_set(io->error, status, io->offset, "%s", text);
    io->failed = true;
    return false;
}

static bool signature(qa_source_save_io *io)
{
    uint8_t magic[sizeof(event_magic)];
    memcpy(magic, event_magic, sizeof(magic));
    uint32_t version = 3;
    return qa_source_save_bytes(io, magic, sizeof(magic)) &&
        qa_source_save_u32(io, &version) &&
        ((!memcmp(magic, event_magic, sizeof(magic)) && version == 3) ||
         event_fail(io, QA_ERROR_FORMAT, "Unsupported application event schema"));
}

static bool prefix(qa_source_save_io *io, event_store *store)
{
    if (!signature(io) ||
        !qa_source_save_u64(io, &store->protocol_generation) ||
        !qa_source_save_count(io, &store->arena_block_size, SIZE_MAX)) return false;
    for (size_t i = 0; i < 5; ++i) {
        if (!qa_source_save_count(io, &store->capacities[i], SIZE_MAX / event_widths[i]) ||
            !qa_source_save_count(io, &store->counts[i], store->capacities[i])) return false;
    }
    if (io->direction == QA_SOURCE_SAVE_READ) {
        size_t remaining = io->input.size - io->offset;
        for (size_t i = 0; i < 5; ++i) {
            if (store->counts[i] > remaining / event_minimums[i])
                return event_fail(io, QA_ERROR_FORMAT, "Truncated application event rows");
            remaining -= store->counts[i] * event_minimums[i];
        }
    }
    return true;
}

static bool enum_field(qa_source_save_io *io, uint32_t *value, uint32_t maximum)
{
    return qa_source_save_u32(io, value) &&
        (*value <= maximum || event_fail(io, QA_ERROR_FORMAT, "Invalid application event enum"));
}

static bool int_field(qa_source_save_io *io, int *value)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE &&
        ((int64_t)*value < INT32_MIN || (int64_t)*value > INT32_MAX))
        return event_fail(io, QA_ERROR_FORMAT, "Application event integer exceeds its source domain");
    int32_t number = io->direction == QA_SOURCE_SAVE_WRITE ? (int32_t)*value : 0;
    if (!qa_source_save_i32(io, &number)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if ((int64_t)number < INT_MIN || (int64_t)number > INT_MAX)
            return event_fail(io, QA_ERROR_FORMAT, "Application event integer exceeds this host");
        *value = (int)number;
    }
    return true;
}

static bool finite_field(qa_source_save_io *io, float *value)
{
    return qa_source_save_f32(io, value) &&
        (isfinite(*value) || event_fail(io, QA_ERROR_FORMAT, "Nonfinite application event scalar"));
}

static bool vector_field(qa_source_save_io *io, qa_vec3 *value)
{
    return qa_source_save_vec3(io, value) &&
        (qa_vec_finite(*value) || event_fail(io, QA_ERROR_FORMAT, "Nonfinite application event vector"));
}

static bool actor_field(qa_source_save_io *io, qa_actor_id *value)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE && !value->registry &&
        (value->generation || value->slot))
        return event_fail(io, QA_ERROR_FORMAT, "Absent application event actor has provenance");
    return qa_source_save_actor(io, value);
}

static bool provider_field(qa_source_save_io *io, qa_actor_owner *value, bool required)
{
    /* Names stay in the session table after a provider retires. This retains
     * that source identity without manufacturing an active provider binding. */
    return qa_source_save_string(io, value) &&
        (!required || *value || event_fail(io, QA_ERROR_FORMAT, "Application event has no source provider"));
}

static void *arena_array(qa_source_save_io *io, event_store *store,
                         size_t count, size_t width, size_t alignment)
{
    if (!count) return NULL;
    if (count > SIZE_MAX / width) {
        event_fail(io, QA_ERROR_FORMAT, "Application event array overflows");
        return NULL;
    }
    void *result = qa_arena_alloc(&store->arena, count * width, alignment, io->error);
    if (!result) io->failed = true;
    return result;
}

static bool text_field(qa_source_save_io *io, event_store *store, const char **value)
{
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) {
        if (io->direction == QA_SOURCE_SAVE_READ) *value = NULL;
        return true;
    }
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*value) : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)*value, length);
    if (length > io->input.size - io->offset)
        return event_fail(io, QA_ERROR_FORMAT, "Truncated application event text");
    char *text = arena_array(io, store, length + 1, 1, 1);
    if (!text || !qa_source_save_bytes(io, text, length)) return false;
    text[length] = 0;
    if (memchr(text, 0, length))
        return event_fail(io, QA_ERROR_FORMAT, "Application event text contains embedded NUL");
    *value = text;
    return true;
}

static bool arguments_field(qa_source_save_io *io, event_store *store,
                            const qa_builtin_message_arg **value, size_t *count)
{
    if (!qa_source_save_count(io, count, SIZE_MAX / sizeof(**value))) return false;
    qa_builtin_message_arg *decoded = NULL;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (*count > (io->input.size - io->offset) / 5)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated application event arguments");
        decoded = arena_array(io, store, *count, sizeof(*decoded), _Alignof(qa_builtin_message_arg));
        if (*count && !decoded) return false;
        *value = decoded;
    } else if (*count && !*value) {
        return event_fail(io, QA_ERROR_FORMAT, "Application event arguments have no storage");
    }
    for (size_t i = 0; i < *count; ++i) {
        qa_builtin_message_arg field = io->direction == QA_SOURCE_SAVE_WRITE ? (*value)[i] : (qa_builtin_message_arg){0};
        uint32_t kind = field.kind;
        if (!enum_field(io, &kind, QA_BUILTIN_MESSAGE_NUMBER)) return false;
        field.kind = (qa_builtin_message_arg_kind)kind;
        if (field.kind == QA_BUILTIN_MESSAGE_STRING) {
            if (!qa_source_save_string(io, &field.value.text)) return false;
        } else if (!qa_source_save_f64(io, &field.value.number) || !isfinite(field.value.number)) {
            return event_fail(io, QA_ERROR_FORMAT, "Nonfinite application message argument");
        }
        if (decoded) decoded[i] = field;
    }
    return true;
}

static bool builtin_field(qa_source_save_io *io, event_store *store, qa_builtin_event *event)
{
    uint32_t kind = event->kind, family = event->family;
    if (!enum_field(io, &kind, QA_BUILTIN_LOG) || !enum_field(io, &family, QA_GAME_Q3) ||
        !provider_field(io, &event->provider, false) ||
        !actor_field(io, &event->actor) || !actor_field(io, &event->other) ||
        !qa_source_save_u64(io, &event->time_ns) ||
        !qa_source_save_string(io, &event->resource) || !qa_source_save_string(io, &event->text) ||
        !vector_field(io, &event->origin) || !vector_field(io, &event->end) ||
        !vector_field(io, &event->direction) || !finite_field(io, &event->volume) ||
        !finite_field(io, &event->attenuation) || !finite_field(io, &event->value) ||
        !qa_source_save_i32(io, &event->code) || !qa_source_save_i32(io, &event->channel) ||
        !qa_source_save_i32(io, &event->count) || !qa_source_save_i32(io, &event->frame) ||
        !qa_source_save_u32(io, &event->flags) ||
        !arguments_field(io, store, &event->arguments, &event->argument_count)) return false;
    event->kind = (qa_builtin_event_kind)kind;
    event->family = (qa_game_family)family;
    if (kind == QA_BUILTIN_LOG &&
        (family != QA_GAME_Q3 || !event->provider || !event->text || event->argument_count ||
         event->actor.registry || event->other.registry))
        return event_fail(io, QA_ERROR_FORMAT, "Invalid provider-owned Q3 log event");
    return true;
}

static bool fog_field(qa_source_save_io *io, qa_q2_fog *fog)
{
    return finite_field(io, &fog->density) && finite_field(io, &fog->sky_factor) &&
        vector_field(io, &fog->color) && vector_field(io, &fog->start_color) &&
        vector_field(io, &fog->end_color) && finite_field(io, &fog->start_distance) &&
        finite_field(io, &fog->end_distance) && finite_field(io, &fog->falloff) &&
        finite_field(io, &fog->height_density);
}

static bool q2_map_field(qa_source_save_io *io, event_store *store,
                         qa_application_q2_map_event *record)
{
    qa_q2_map_event *event = &record->event;
    uint32_t kind = event->kind;
    if (!provider_field(io, &record->provider, true) || !qa_source_save_u64(io, &record->time_ns) ||
        !enum_field(io, &kind, QA_Q2_MAP_HELP_COMPUTER) || !actor_field(io, &event->actor) ||
        !actor_field(io, &event->recipient) || !actor_field(io, &event->target) ||
        !qa_source_save_string(io, &event->text) || !qa_source_save_string(io, &event->resource) ||
        !vector_field(io, &event->origin) || !vector_field(io, &event->direction) ||
        !vector_field(io, &event->color) || !fog_field(io, &event->fog) ||
        !finite_field(io, &event->value) || !finite_field(io, &event->duration) ||
        !finite_field(io, &event->radius) || !finite_field(io, &event->alpha) ||
        !finite_field(io, &event->intensity) || !finite_field(io, &event->fade_start) ||
        !finite_field(io, &event->fade_end) || !finite_field(io, &event->cone_cosine) ||
        !int_field(io, &event->count) || !int_field(io, &event->style) ||
        !int_field(io, &event->slot) || !qa_source_save_u32(io, &event->flags) ||
        !qa_source_save_u32(io, &event->resolution) || !qa_source_save_bool(io, &event->visible) ||
        !arguments_field(io, store, &event->arguments, &event->argument_count)) return false;
    event->kind = (qa_q2_map_event_kind)kind;
    return true;
}

static bool q3_map_field(qa_source_save_io *io, qa_application_q3_map_event *record)
{
    qa_q3_map_event *event = &record->event;
    uint32_t kind = event->kind;
    if (!provider_field(io, &record->provider, true) || !qa_source_save_u64(io, &record->time_ns) ||
        !enum_field(io, &kind, QA_Q3_MAP_AREA_PORTAL) || !actor_field(io, &event->actor) ||
        !actor_field(io, &event->other) || !qa_source_save_string(io, &event->name) ||
        !qa_source_save_string(io, &event->text) || !vector_field(io, &event->origin) ||
        !vector_field(io, &event->angles) || !vector_field(io, &event->direction) ||
        !vector_field(io, &event->destination) || !finite_field(io, &event->value) ||
        !qa_source_save_i32(io, &event->index) || !qa_source_save_i32(io, &event->points) ||
        !qa_source_save_i32(io, &event->team) || !qa_source_save_i32(io, &event->sound_interval_tenths) ||
        !qa_source_save_i32(io, &event->sound_random_tenths) || !qa_source_save_u32(io, &event->flags) ||
        !qa_source_save_bool(io, &event->no_bots) || !qa_source_save_bool(io, &event->no_humans)) return false;
    event->kind = (qa_q3_map_event_kind)kind;
    return true;
}

static bool view_field(qa_source_save_io *io, qa_q2_player_view *view)
{
    return qa_source_save_vec3(io, &view->angles) && qa_source_save_vec3(io, &view->offset) &&
        qa_source_save_vec3(io, &view->kick_angles) && qa_source_save_vec3(io, &view->gun_angles) &&
        qa_source_save_vec3(io, &view->gun_offset) && qa_source_save_f32(io, &view->blend.x) &&
        qa_source_save_f32(io, &view->blend.y) && qa_source_save_f32(io, &view->blend.z) &&
        qa_source_save_f32(io, &view->blend.w) && qa_source_save_f32(io, &view->fov) &&
        qa_source_save_f32(io, &view->health) && qa_source_save_f32(io, &view->armor) &&
        qa_source_save_f32(io, &view->ammo) && int_field(io, &view->score) &&
        int_field(io, &view->flashes) && int_field(io, &view->layouts) &&
        qa_source_save_i32(io, &view->hit_marker_damage) &&
        view->hit_marker_damage >= INT16_MIN && view->hit_marker_damage <= INT16_MAX &&
        qa_source_save_string(io, &view->selected_item) && qa_source_save_string(io, &view->timer_item) &&
        int_field(io, &view->timer_seconds) && qa_source_save_bool(io, &view->underwater) &&
        qa_source_save_bool(io, &view->spectator);
}

static bool inventory_field(qa_source_save_io *io, qa_inventory_entry *entry)
{
    uint32_t policy = entry->policy;
    if (!qa_source_save_string(io, &entry->item) || !entry->item ||
        !qa_source_save_f64(io, &entry->count) || !qa_source_save_f64(io, &entry->capacity) ||
        !enum_field(io, &policy, QA_COUNT_SOURCE_INT32)) return false;
    entry->policy = (qa_inventory_count_policy)policy;
    qa_inventory_entry normalized;
    if (!qa_inventory_validate_entry(entry, &normalized, io->error)) {
        io->failed = true;
        return false;
    }
    return (normalized.count == entry->count && normalized.capacity == entry->capacity) ||
        event_fail(io, QA_ERROR_FORMAT, "Application inventory event changes source arithmetic");
}

static bool player_arrays(qa_source_save_io *io, event_store *store, qa_q2_player_event *event)
{
    bool scores = io->direction == QA_SOURCE_SAVE_WRITE && event->scores != NULL;
    bool inventory = io->direction == QA_SOURCE_SAVE_WRITE && event->inventory != NULL;
    if (!qa_source_save_count(io, &event->count,
            SIZE_MAX / (sizeof(qa_q2_score_row) > sizeof(qa_inventory_entry) ?
                        sizeof(qa_q2_score_row) : sizeof(qa_inventory_entry))) ||
        !qa_source_save_bool(io, &scores) || !qa_source_save_bool(io, &inventory)) return false;
    if ((!event->count && (scores || inventory)) ||
        (event->kind == QA_Q2_PLAYER_SCOREBOARD && event->count && !scores) ||
        (event->kind == QA_Q2_PLAYER_INVENTORY && event->count && !inventory))
        return event_fail(io, QA_ERROR_FORMAT, "Application player event array presence differs");
    qa_q2_score_row *rows = NULL;
    qa_inventory_entry *items = NULL;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        size_t minimum = (scores ? 17u : 0u) + (inventory ? 21u : 0u);
        if (minimum && event->count > (io->input.size - io->offset) / minimum)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated application player event arrays");
        if (scores) rows = arena_array(io, store, event->count, sizeof(*rows), _Alignof(qa_q2_score_row));
        if (inventory) items = arena_array(io, store, event->count, sizeof(*items), _Alignof(qa_inventory_entry));
        if ((scores && !rows) || (inventory && !items)) return false;
        event->scores = rows;
        event->inventory = items;
    }
    for (size_t i = 0; scores && i < event->count; ++i) {
        qa_q2_score_row row = io->direction == QA_SOURCE_SAVE_WRITE ? event->scores[i] : (qa_q2_score_row){0};
        if (!qa_source_save_u32(io, &row.slot) || !text_field(io, store, &row.name) ||
            !int_field(io, &row.score) || !int_field(io, &row.ping) ||
            !int_field(io, &row.minutes) || !qa_source_save_bool(io, &row.spectator)) return false;
        if (rows) rows[i] = row;
    }
    for (size_t i = 0; inventory && i < event->count; ++i) {
        qa_inventory_entry entry = io->direction == QA_SOURCE_SAVE_WRITE ? event->inventory[i] : (qa_inventory_entry){0};
        if (!inventory_field(io, &entry)) return false;
        if (items) items[i] = entry;
    }
    return true;
}

static bool q2_player_field(qa_source_save_io *io, event_store *store,
                            qa_application_q2_player_event *record)
{
    qa_q2_player_event *event = &record->event;
    uint32_t kind = event->kind, status = event->respawn_status, hand = event->hand;
    if (!provider_field(io, &record->provider, true) || !qa_source_save_u64(io, &record->time_ns) ||
        !enum_field(io, &kind, QA_Q2_PLAYER_ALPHA) || !actor_field(io, &event->actor) ||
        !actor_field(io, &event->target) || !text_field(io, store, &event->text) ||
        !text_field(io, store, &event->skin) || !view_field(io, &event->view)) return false;
    event->kind = (qa_q2_player_event_kind)kind;
    if (!player_arrays(io, store, event) || !vector_field(io, &event->origin) ||
        !vector_field(io, &event->direction) || !qa_source_save_u64(io, &event->time_ns) ||
        !qa_source_save_string(io, &event->selected_item) || !qa_source_save_u32(io, &event->slot) ||
        !int_field(io, &event->level) || !int_field(io, &event->lives) ||
        !finite_field(io, &event->damage) || !finite_field(io, &event->alpha) ||
        !enum_field(io, &status, QA_Q2_RESPAWN_NO_LIVES) || !enum_field(io, &hand, QA_Q2_CENTER_HAND) ||
        !qa_source_save_bool(io, &event->visible) || !qa_source_save_bool(io, &event->reliable) ||
        !qa_source_save_bool(io, &event->health) || !qa_source_save_bool(io, &event->armor) ||
        !qa_source_save_bool(io, &event->shield) || !qa_source_save_bool(io, &event->first)) return false;
    event->respawn_status = (qa_q2_respawn_status)status;
    event->hand = (qa_q2_hand)hand;
    return true;
}

static bool protocol_field(qa_source_save_io *io, event_store *store,
                            qa_application_protocol_event *event)
{
    uint32_t dialect = event->dialect;
    if (!provider_field(io, &event->provider, true) || !enum_field(io, &dialect, QA_CLOCK_Q3) ||
        !qa_source_save_u64(io, &event->time_ns) || !actor_field(io, &event->recipient) ||
        !vector_field(io, &event->origin)) return false;
    size_t size = event->payload.size;
    if (!qa_source_save_count(io, &size, SIZE_MAX)) return false;
    uint8_t *payload = NULL;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (size > io->input.size - io->offset)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated application protocol payload");
        payload = arena_array(io, store, size, 1, 1);
        if (size && !payload) return false;
        event->payload = (qa_bytes){payload, size};
    } else if (size && !event->payload.data) {
        return event_fail(io, QA_ERROR_FORMAT, "Application protocol payload has no storage");
    }
    if (!qa_source_save_bytes(io, (void *)event->payload.data, size) ||
        !qa_source_save_count(io, &event->reference_count, SIZE_MAX / sizeof(*event->references))) return false;
    qa_application_protocol_reference *references = NULL;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (event->reference_count > (io->input.size - io->offset) / 22)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated application protocol references");
        references = arena_array(io, store, event->reference_count,
            sizeof(*references), _Alignof(qa_application_protocol_reference));
        if (event->reference_count && !references) return false;
        event->references = references;
    } else if (event->reference_count && !event->references) {
        return event_fail(io, QA_ERROR_FORMAT, "Application protocol references have no storage");
    }
    for (size_t i = 0; i < event->reference_count; ++i) {
        qa_application_protocol_reference reference = io->direction == QA_SOURCE_SAVE_WRITE ?
            event->references[i] : (qa_application_protocol_reference){0};
        if (!qa_source_save_count(io, &reference.offset, SIZE_MAX) ||
            !actor_field(io, &reference.actor) || !qa_source_save_bool(io, &reference.packed_sound)) return false;
        if (size < 2 || reference.offset > size - 2)
            return event_fail(io, QA_ERROR_FORMAT, "Application protocol actor reference exceeds payload");
        if (references) references[i] = reference;
    }
    if (!qa_source_save_i32(io, &event->destination) || !qa_source_save_bool(io, &event->reliable) ||
        !qa_source_save_bool(io, &event->multicast) || !qa_source_save_bool(io, &event->signon)) return false;
    event->dialect = (qa_clock_kind)dialect;
    return true;
}

static event_store borrow_store(qa_application *app)
{
    return (event_store){
        .builtin = app->events, .q2_map = app->q2_map_events, .q3_map = app->q3_map_events,
        .protocol_generation = app->protocol_events_generation,
        .q2_player = app->q2_player_events, .protocol = app->protocol_events,
        .counts = {app->event_count, app->q2_map_event_count, app->q3_map_event_count,
                   app->q2_player_event_count, app->protocol_event_count},
        .capacities = {app->event_capacity, app->q2_map_event_capacity, app->q3_map_event_capacity,
                       app->q2_player_event_capacity, app->protocol_event_capacity},
        .arena_block_size = app->event_arena.block_size
    };
}

static void dispose_store(event_store *store)
{
    free(store->builtin);
    free(store->q2_map);
    free(store->q3_map);
    free(store->q2_player);
    free(store->protocol);
    qa_arena_destroy(&store->arena);
    *store = (event_store){0};
}

static bool allocate_store(qa_source_save_io *io, event_store *store)
{
    qa_arena_init(&store->arena, store->arena_block_size);
    void *arrays[5] = {0};
    for (size_t i = 0; i < 5; ++i) {
        if (store->capacities[i]) arrays[i] = calloc(store->capacities[i], event_widths[i]);
        if (store->capacities[i] && !arrays[i]) {
            for (size_t j = 0; j < i; ++j) free(arrays[j]);
            return event_fail(io, QA_ERROR_MEMORY, "Allocating restored application event queues");
        }
    }
    store->builtin = arrays[0];
    store->q2_map = arrays[1];
    store->q3_map = arrays[2];
    store->q2_player = arrays[3];
    store->protocol = arrays[4];
    return true;
}

static bool rows(qa_source_save_io *io, event_store *store)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        const void *arrays[5] = {store->builtin, store->q2_map, store->q3_map, store->q2_player, store->protocol};
        for (size_t i = 0; i < 5; ++i)
            if ((store->capacities[i] != 0) != (arrays[i] != NULL))
                return event_fail(io, QA_ERROR_FORMAT, "Application event queue allocation differs");
    }
    for (size_t i = 0; i < store->counts[0]; ++i) {
        qa_builtin_event event = store->builtin[i].event;
        if (!builtin_field(io, store, &event)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) store->builtin[i].event = event;
    }
    for (size_t i = 0; i < store->counts[1]; ++i) {
        qa_application_q2_map_event event = store->q2_map[i];
        if (!q2_map_field(io, store, &event)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) store->q2_map[i] = event;
    }
    for (size_t i = 0; i < store->counts[2]; ++i) {
        qa_application_q3_map_event event = store->q3_map[i];
        if (!q3_map_field(io, &event)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) store->q3_map[i] = event;
    }
    for (size_t i = 0; i < store->counts[3]; ++i) {
        qa_application_q2_player_event event = store->q2_player[i];
        if (!q2_player_field(io, store, &event)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) store->q2_player[i] = event;
    }
    for (size_t i = 0; i < store->counts[4]; ++i) {
        qa_application_protocol_event event = store->protocol[i];
        if (!protocol_field(io, store, &event)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) store->protocol[i] = event;
    }
    return true;
}

static bool leased(qa_application *app, qa_error *error)
{
    if (!app || app->operation != APPLICATION_PERSISTING || !app->session ||
        app->destroy_requested || app->finalizing || app->q3_round_active || app->frame_preparing ||
        app->publication_started || !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event persistence requires its operation lease");
    return true;
}

bool application_events_save_capture(qa_application *app, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !leased(app, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event capture requires an empty output and leased owner");
    event_store store = borrow_store(app);
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, app->session, error) && prefix(&io, &store) &&
        rows(&io, &store) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

static bool queues_empty(const qa_application *app)
{
    return !app->event_count && !app->q2_map_event_count && !app->q3_map_event_count &&
        !app->q2_player_event_count && !app->protocol_event_count;
}

static void install_store(qa_application *app, event_store *store)
{
    free(app->events);
    free(app->q2_map_events);
    free(app->q3_map_events);
    free(app->q2_player_events);
    free(app->protocol_events);
    qa_arena_destroy(&app->event_arena);
    app->events = store->builtin;
    app->q2_map_events = store->q2_map;
    app->q3_map_events = store->q3_map;
    app->q2_player_events = store->q2_player;
    app->protocol_events = store->protocol;
    app->event_count = store->counts[0];
    app->q2_map_event_count = store->counts[1];
    app->q3_map_event_count = store->counts[2];
    app->q2_player_event_count = store->counts[3];
    app->protocol_event_count = store->counts[4];
    app->protocol_events_generation = store->protocol_generation;
    app->event_capacity = store->capacities[0];
    app->q2_map_event_capacity = store->capacities[1];
    app->q3_map_event_capacity = store->capacities[2];
    app->q2_player_event_capacity = store->capacities[3];
    app->protocol_event_capacity = store->capacities[4];
    /* Move the one arena owner with every array pointing into its blocks. */
    app->event_arena = store->arena;
    *store = (event_store){0};
}

bool application_events_save_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    if (!leased(app, error) || !queues_empty(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event import requires empty candidate queues");
    event_store store = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, app->session, bytes, error) && prefix(&io, &store) &&
        allocate_store(&io, &store) && rows(&io, &store) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) install_store(app, &store);
    dispose_store(&store);
    return ok;
}

bool application_events_save_validate(qa_application *app, qa_error *error)
{
    qa_buffer encoded = {0};
    bool ok = application_events_save_capture(app, &encoded, error);
    qa_buffer_free(&encoded);
    return ok;
}

static bool public_lease(qa_application *app, qa_error *error)
{
    if (!app || app->operation != APPLICATION_IDLE || !app->session || !app->world ||
        !app->configuration || !qa_application_launch(app) || app->destroy_requested || app->finalizing ||
        app->q3_round_active || app->frame_preparing || app->publication_started || app->pending_close ||
        app->routing_snapshot || app->routing_providers || app->routing_provider_count ||
        !qa_session_safe(app->session) || !qa_session_destroy_ready(app->session) ||
        !qa_world_idle(app->world) || !qa_combat_idle(app->combat) || !qa_console_idle(app->console) ||
        !application_guests_idle(app) || !application_bots_can_destroy(app) || !application_rankings_idle(app) ||
        (app->modes && !qa_modes_idle(app->modes)) ||
        (app->equipment && !qa_equipment_idle(app->equipment)) ||
        !application_match_intents_idle(app->match_intents) ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING))
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event checkpoint requires a committed idle owner");
    app->operation = APPLICATION_PERSISTING;
    return true;
}

bool qa_application_events_checkpoint(qa_application *app, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size)
        return application_fail(error, QA_ERROR_ARGUMENT, "Application event checkpoint output must be empty");
    if (!public_lease(app, error)) return false;
    bool ok = application_events_save_capture(app, out, error);
    app->operation = APPLICATION_IDLE;
    return ok;
}

bool qa_application_events_restore(qa_application *app, qa_bytes bytes, qa_error *error)
{
    if (!public_lease(app, error)) return false;
    bool ok = application_events_save_restore(app, bytes, error);
    app->operation = APPLICATION_IDLE;
    return ok;
}

bool qa_application_events_saved_counts_read(qa_bytes bytes,
    qa_application_saved_event_counts *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Application event extent output is absent");
    qa_source_save_io io = {0};
    event_store store = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && prefix(&io, &store);
    qa_source_save_dispose(&io);
    if (ok) *out = (qa_application_saved_event_counts){
        .builtin = store.counts[0], .q2_map = store.counts[1],
        .q3_map = store.counts[2], .q2_player = store.counts[3],
        .protocol = store.counts[4], .protocol_generation = store.protocol_generation};
    return ok;
}
