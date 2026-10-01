#include "internal.h"
#include "../library/internal.h"
#include "../chat/internal.h"
#include "../save_fields.h"
#include "qa/bot_runtime_save.h"
#include "qa/bot_runtime_assets_save.h"
#include "qa/bot_library_save.h"
#include "qa/bot_actions_save.h"
#include "qa/bot_bsp_save.h"
#include "qa/bot_goals_save.h"
#include "qa/bot_chat_system_save.h"
#include "qa/bot_movement_save.h"
#include "qa/bot_observations_save.h"
#include "qa/script_defines_save.h"

enum { VARIABLES, ASSETS, ACTIONS, BSP, GOALS, CHAT, MOVES, OBSERVATIONS, HANDLES, GLOBALS, PART_COUNT };
typedef struct runtime_state {
    uint32_t maximum, minimum, profile;
    bool debug, initialized, library_initialized, loaded, bsp_loaded, closed;
    bool library, actions, bsp, goals, chat, moves, reload;
    float time;
    qa_bot_runtime_saved_map map;
} runtime_state;
static const uint8_t magic[8] = {'Q', 'A', 'B', 'R', 'U', 'N', 'T', 0};
static const uint8_t handle_magic[8] = {'Q', 'A', 'B', 'H', 'N', 'D', 'L', 0};

static bool fail(qa_error *error, const char *message)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false; }

static bool signature(qa_source_save_io *io)
{
    uint8_t bytes[8]; memcpy(bytes, magic, sizeof(bytes)); uint32_t version = 2;
    return qa_source_save_bytes(io, bytes, sizeof(bytes)) && !memcmp(bytes, magic, sizeof(bytes)) &&
        qa_source_save_u32(io, &version) && version == 2 ? true :
        bot_save_fail(io, QA_ERROR_FORMAT, "Unsupported bot runtime continuation schema");
}

void qa_bot_runtime_saved_map_free(qa_bot_runtime_saved_map *map)
{ if (map) { free((void *)map->name); *map = (qa_bot_runtime_saved_map){0}; } }

static bool entity_digest(const qa_entities *entities, qa_sha256_digest *out, qa_error *error)
{
    *out = (qa_sha256_digest){0};
    if (!entities) return true;
    if ((entities->count && !entities->records) || (entities->property_count && !entities->properties))
        return fail(error, "Bot runtime map has incomplete immutable entity tables");
    qa_source_save_io io = {0};
    size_t count = entities->count, properties = entities->property_count;
    bool ok = qa_source_save_writer(&io, NULL, error) && qa_source_save_count(&io, &count, SIZE_MAX) &&
        qa_source_save_count(&io, &properties, SIZE_MAX);
    for (size_t i = 0; ok && i < count; ++i) {
        qa_entity_record record = entities->records[i];
        ok = record.first_property <= properties && record.property_count <= properties - record.first_property &&
            qa_source_save_count(&io, &record.first_property, SIZE_MAX) && qa_source_save_count(&io, &record.property_count, SIZE_MAX);
    }
    for (size_t i = 0; ok && i < properties; ++i) {
        qa_entity_property property = entities->properties[i];
        ok = qa_source_save_count(&io, &property.key.size, SIZE_MAX) &&
            qa_source_save_bytes(&io, (void *)property.key.data, property.key.size) &&
            qa_source_save_count(&io, &property.value.size, SIZE_MAX) &&
            qa_source_save_bytes(&io, (void *)property.value.data, property.value.size);
    }
    qa_buffer buffer = {0};
    if (ok) ok = qa_source_save_finish(&io, &buffer);
    if (ok) qa_sha256((qa_bytes){buffer.data, buffer.size}, out);
    qa_buffer_free(&buffer); qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid bot map entity partition");
    return ok;
}
static bool state_fields(qa_source_save_io *io, runtime_state *state)
{
    qa_bot_runtime_saved_map *map = &state->map;
    bool ok = qa_source_save_u32(io, &state->maximum) && state->maximum && state->maximum <= INT32_MAX &&
        qa_source_save_u32(io, &state->minimum) && qa_source_save_u32(io, &state->profile) &&
        state->profile <= QA_BOT_OBSERVATION_MODULE && qa_source_save_bool(io, &state->debug) &&
        qa_source_save_bool(io, &state->initialized) && qa_source_save_bool(io, &state->library_initialized) &&
        qa_source_save_bool(io, &state->loaded) && qa_source_save_bool(io, &state->bsp_loaded) &&
        qa_source_save_bool(io, &state->closed) && qa_source_save_f32(io, &state->time) && isfinite(state->time) &&
        qa_source_save_bool(io, &state->library) && qa_source_save_bool(io, &state->actions) &&
        qa_source_save_bool(io, &state->bsp) && qa_source_save_bool(io, &state->goals) &&
        qa_source_save_bool(io, &state->chat) && qa_source_save_bool(io, &state->moves) &&
        qa_source_save_bool(io, &state->reload) && bot_save_text(io, &map->name) &&
        qa_source_save_bool(io, &map->entities) && qa_source_save_bool(io, &map->source) &&
        qa_source_save_bool(io, &map->navigation) && qa_source_save_count(io, &map->source_bytes, SIZE_MAX) &&
        qa_source_save_bytes(io, map->entity_digest.bytes, sizeof(map->entity_digest.bytes)) &&
        qa_source_save_bytes(io, map->source_digest.bytes, sizeof(map->source_digest.bytes));
    const qa_sha256_digest zero = {0};
    if (ok) ok = state->actions &&
        (state->closed ? (!state->library && !state->goals && !state->chat && !state->moves && !state->reload &&
                         !state->initialized && !state->library_initialized && !state->loaded) :
                        (state->library && state->goals && state->chat && state->moves)) &&
        (state->library_initialized || !state->initialized) &&
        (!state->loaded || (state->library_initialized && map->name && map->navigation && state->bsp_loaded)) &&
        (!state->bsp || (state->bsp_loaded && (map->source || !map->entities))) &&
        (map->source || (!map->source_bytes && qa_sha256_equal(&map->source_digest, &zero))) &&
        (map->entities || qa_sha256_equal(&map->entity_digest, &zero)) &&
        (map->name || (!map->entities && !map->source && !map->navigation)) &&
        (!state->closed || state->profile == QA_BOT_OBSERVATION_MODULE ||
                         (!state->bsp && !state->bsp_loaded && !map->name));
    if (!ok && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Inconsistent bot runtime owner presence");
    return ok;
}
static bool present(const runtime_state *state, size_t index)
{
    switch (index) {
    case VARIABLES: case ASSETS: return state->library;
    case ACTIONS: return state->actions;
    case BSP: return state->bsp;
    case GOALS: return state->goals;
    case CHAT: return state->chat;
    case MOVES: return state->moves;
    default: return true;
    }
}
static bool read_parts(qa_source_save_io *io, const runtime_state *state, qa_bytes parts[PART_COUNT])
{
    for (size_t i = 0; i < PART_COUNT; ++i) {
        size_t size = 0;
        if (!qa_source_save_count(io, &size, SIZE_MAX) || io->offset > io->input.size ||
            size > io->input.size - io->offset || (size != 0) != present(state, i))
            return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid framed bot runtime component extent");
        parts[i] = (qa_bytes){io->input.data + io->offset, size}; io->offset += size;
    }
    return qa_source_save_finish(io, NULL);
}
bool qa_bot_runtime_save_map_read(qa_bytes bytes, qa_bot_runtime_saved_map *out, qa_error *error)
{
    if (!out || out->name) return fail(error, "Bot runtime map inspection requires an empty output");
    runtime_state state = {0}; qa_bytes parts[PART_COUNT] = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && signature(&io) &&
        state_fields(&io, &state) && read_parts(&io, &state, parts);
    if (ok) *out = state.map; else qa_bot_runtime_saved_map_free(&state.map);
    qa_source_save_dispose(&io); return ok;
}
static bool reference(qa_source_save_io *io, const qa_bot_saved_assets *assets,
                      qa_bot_saved_asset_kind kind, void **object)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, exists = !reading && *object;
    uint64_t id = 0;
    if (!qa_source_save_bool(io, &exists)) return false;
    if (!exists) { if (reading) *object = NULL; return true; }
    bool ok = reading || qa_bot_saved_asset_id(assets, kind, *object, &id, io->error);
    if (ok) ok = qa_source_save_u64(io, &id);
    if (ok && reading) {
        const void *borrowed = NULL;
        ok = qa_bot_saved_asset_resolve(assets, kind, id, &borrowed, io->error);
        if (ok) *object = (void *)borrowed;
    }
    if (!ok) io->failed = true;
    return ok;
}
static bool chat_encode(void *context, const qa_bot_chat_asset *asset, uint64_t *out, qa_error *error)
{ return qa_bot_saved_asset_id(context, QA_BOT_SAVED_CHAT, asset, out, error); }
static bool chat_decode(void *context, uint64_t id, qa_bot_chat_asset **out, qa_error *error)
{
    const void *object;
    if (!qa_bot_saved_asset_resolve(context, QA_BOT_SAVED_CHAT, id, &object, error)) return false;
    *out = (qa_bot_chat_asset *)object; return true;
}
static bool handles_fields(qa_source_save_io *io, qa_bot_runtime *runtime, const qa_bot_saved_assets *assets,
                           const qa_bot_chat_restored_states *chat_states)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    void *object = runtime->weapon_config;
    bool ok = reference(io, assets, QA_BOT_SAVED_WEAPONS, &object);
    if (reading && ok) { runtime->weapon_config = object; qa_bot_weapons_retain(object); }
    size_t count = runtime->options.maximum_states;
    if (ok) ok = qa_source_save_count(io, &count, runtime->options.maximum_states) && count == runtime->options.maximum_states;
    if (ok && reading && count > (io->input.size - io->offset) / 5)
        ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot runtime physical handles");
    for (size_t i = 0; ok && i < count; ++i) {
        object = runtime->characters[i];
        ok = reference(io, assets, QA_BOT_SAVED_CHARACTER, &object);
        if (ok && object)
            for (size_t j = 0; j < i; ++j) if (runtime->characters[j] == object) ok = false;
        if (reading && ok) { runtime->characters[i] = object; qa_bot_character_retain(object); }
        bot_weapon_state *weapon = &runtime->weapons[i];
        if (ok) ok = qa_source_save_bool(io, &weapon->used);
        object = weapon->weights;
        if (ok) ok = reference(io, assets, QA_BOT_SAVED_WEIGHTS, &object);
        if (reading && ok) { weapon->weights = object; qa_bot_weights_retain(object); }
        bool selector = !reading && weapon->selector != NULL;
        if (ok) ok = qa_source_save_bool(io, &selector);
        if (ok && selector) {
            void *config = reading ? NULL : weapon->selector->config;
            void *weights = reading ? NULL : weapon->selector->weights;
            ok = reference(io, assets, QA_BOT_SAVED_WEAPONS, &config) && config && config == runtime->weapon_config &&
                 reference(io, assets, QA_BOT_SAVED_WEIGHTS, &weights) && weights && weights == weapon->weights;
            size_t indices = config ? qa_bot_weapons_read(config)->weapon_capacity : 0;
            if (ok) ok = qa_source_save_count(io, &indices, SIZE_MAX) && indices == qa_bot_weapons_read(config)->weapon_capacity;
            if (ok && reading) {
                if (indices > (io->input.size - io->offset) / 4)
                    ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot weapon selector mapping");
                if (ok) ok = qa_bot_weapon_selector_create(config, weights, &weapon->selector, io->error);
                if (!ok) io->failed = true;
            }
            for (size_t j = 0; ok && j < indices; ++j) {
                int32_t value = reading ? 0 : weapon->selector->indices[j];
                ok = qa_source_save_i32(io, &value) && value == weapon->selector->indices[j];
            }
        }
        bool chat = !reading && runtime->chats[i] != NULL; size_t index = 0;
        if (ok) ok = qa_source_save_bool(io, &chat);
        if (ok && chat) {
            ok = reading || qa_bot_chat_system_state_index(runtime->chat_system, runtime->chats[i], &index);
            if (ok) ok = qa_source_save_count(io, &index, SIZE_MAX);
            if (ok && reading) {
                ok = chat_states && index < chat_states->count;
                if (ok) {
                    qa_bot_chat *state = chat_states->states[index];
                    for (size_t j = 0; j < i; ++j) if (runtime->chats[j] == state) ok = false;
                    if (ok) runtime->chats[i] = state;
                }
            }
        }
        if (ok) ok = weapon->used || (!weapon->weights && !weapon->selector);
    }
    if (!ok && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid runtime bot asset or handle mapping");
    return ok;
}
static bool capture_parts(qa_session *session, const qa_bot_runtime *runtime, qa_buffer parts[PART_COUNT], qa_error *error)
{
    qa_bot_saved_assets *assets = NULL;
    bool ok = !runtime->library || (qa_bot_library_variables_capture(runtime->library, &parts[VARIABLES], error) &&
        qa_bot_runtime_assets_capture(runtime, &parts[ASSETS], &assets, error));
    if (ok && runtime->actions) ok = qa_bot_actions_capture(runtime->actions, &parts[ACTIONS], error);
    if (ok && runtime->bsp) ok = qa_bot_bsp_capture(runtime->bsp, runtime->map.source_entities, &parts[BSP], error);
    if (ok && runtime->goals) ok = qa_bot_goals_save_capture(session, runtime->goals, assets, &parts[GOALS], error);
    qa_bot_chat_asset_save_refs refs = {.context = assets, .encode = chat_encode, .decode = chat_decode};
    if (ok && runtime->chat_system) ok = qa_bot_chat_system_capture(runtime->chat_system, &refs, &parts[CHAT], error);
    if (ok && runtime->moves) ok = qa_bot_moves_save_capture(runtime->moves, &parts[MOVES], error);
    if (ok) ok = qa_bot_observations_capture(session, runtime, &parts[OBSERVATIONS], error);
    if (ok) ok = qa_script_defines_save_capture(runtime->globals, &parts[GLOBALS], error);
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, session, error) && bot_save_signature(&io, handle_magic) &&
        handles_fields(&io, (qa_bot_runtime *)runtime, assets, NULL) && qa_source_save_finish(&io, &parts[HANDLES]);
    qa_source_save_dispose(&io); qa_bot_saved_assets_free(assets); return ok;
}
bool qa_bot_runtime_save_capture(qa_session *session, const qa_bot_runtime *runtime, qa_buffer *out, qa_error *error)
{
    if (!session || !runtime || !out || !qa_bot_runtime_can_destroy(runtime) || runtime->restore_pending)
        return fail(error, "Bot runtime capture requires its complete idle source owner");
    if (!runtime->globals || runtime->options.library.preprocessor.globals != runtime->globals ||
        (runtime->library && runtime->library->options.preprocessor.globals != runtime->globals))
        return fail(error, "Bot runtime global macro aliases differ from their actual owner");
    runtime_state state = {.maximum = runtime->options.maximum_states, .minimum = runtime->options.minimum_clients,
        .profile = runtime->options.observations, .debug = runtime->options.debug, .initialized = runtime->initialized,
        .library_initialized = runtime->library_initialized, .loaded = runtime->loaded, .bsp_loaded = runtime->bsp_loaded,
        .closed = runtime->closed, .time = runtime->time, .library = runtime->library != NULL,
        .actions = runtime->actions != NULL, .bsp = runtime->bsp != NULL, .goals = runtime->goals != NULL,
        .chat = runtime->chat_system != NULL, .moves = runtime->moves != NULL,
        .reload = runtime->library && runtime->library->options.reload_characters,
        .map = {.name = runtime->map_name, .entities = runtime->map.entities != NULL,
                .source = runtime->map.source_entities.data != NULL, .navigation = runtime->map.navigation != NULL,
                .source_bytes = runtime->map.source_entities.size}};
    if (!runtime->characters || !runtime->weapons || !runtime->chats ||
        (!runtime->map.source_entities.data && runtime->map.source_entities.size))
        return fail(error, "Bot runtime has incomplete actual storage or immutable map bytes");
    bool ok = entity_digest(runtime->map.entities, &state.map.entity_digest, error);
    if (ok && state.map.source) qa_sha256(runtime->map.source_entities, &state.map.source_digest);
    qa_buffer parts[PART_COUNT] = {0}; qa_source_save_io io = {0};
    if (ok) ok = capture_parts(session, runtime, parts, error) && qa_source_save_writer(&io, NULL, error) &&
        signature(&io) && state_fields(&io, &state);
    for (size_t i = 0; ok && i < PART_COUNT; ++i)
        ok = (parts[i].size != 0) == present(&state, i) && qa_source_save_count(&io, &parts[i].size, SIZE_MAX) &&
             qa_source_save_bytes(&io, parts[i].data, parts[i].size);
    if (ok) ok = qa_source_save_finish(&io, out);
    for (size_t i = 0; i < PART_COUNT; ++i) qa_buffer_free(&parts[i]);
    qa_source_save_dispose(&io); return ok;
}
static bool map_matches(const runtime_state *state, const qa_bot_runtime_map *map, qa_error *error)
{
    const qa_bot_runtime_saved_map *saved = &state->map;
    if (!saved->name) return !map || (!map->name && !map->entities && !map->source_entities.data &&
                                      !map->source_entities.size && !map->navigation) ||
                            fail(error, "Saved bot runtime has no attached source map");
    if (!map || !map->name || strcmp(map->name, saved->name) || (map->entities != NULL) != saved->entities ||
        (map->source_entities.data != NULL) != saved->source || (map->navigation != NULL) != saved->navigation ||
        map->source_entities.size != saved->source_bytes)
        return fail(error, "Qualified bot runtime map differs from the captured binding");
    qa_sha256_digest entities, source = {0};
    if (!entity_digest(map->entities, &entities, error)) return false;
    if (saved->source) qa_sha256(map->source_entities, &source);
    return (qa_sha256_equal(&entities, &saved->entity_digest) && qa_sha256_equal(&source, &saved->source_digest)) ||
        fail(error, "Qualified bot immutable map content differs");
}
bool qa_bot_runtime_save_restore(qa_session *session, qa_bot_runtime *runtime, qa_bytes bytes,
                                 const qa_bot_runtime_map *map, qa_error *error)
{
    if (!session || !runtime || !qa_bot_runtime_can_destroy(runtime) || runtime->restore_pending || runtime->closed ||
        runtime->initialized || runtime->library_initialized || runtime->loaded || runtime->bsp || runtime->map_name ||
        runtime->weapon_config || !runtime->library || !runtime->actions || !runtime->moves || !runtime->goals ||
        !runtime->chat_system || runtime->chat_system->states || runtime->entity_capacity)
        return fail(error, "Bot runtime import requires its actual empty detached constructor");
    for (size_t i = 0; i < runtime->options.maximum_states; ++i)
        if (runtime->characters[i] || runtime->chats[i] || runtime->weapons[i].used ||
            runtime->weapons[i].weights || runtime->weapons[i].selector)
            return fail(error, "Detached bot runtime already owns handle continuations");
    runtime_state state = {0}; qa_bytes parts[PART_COUNT] = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && signature(&io) &&
        state_fields(&io, &state) && read_parts(&io, &state, parts) &&
        state.maximum == runtime->options.maximum_states && state.minimum == runtime->options.minimum_clients &&
        state.profile == (uint32_t)runtime->options.observations && state.debug == runtime->options.debug && map_matches(&state, map, error);
    qa_source_save_dispose(&io);
    qa_bot_saved_assets *assets = NULL; qa_bot_chat_restored_states chats = {0};
    qa_script_defines *globals = NULL;
    if (ok) ok = qa_script_defines_save_restore(parts[GLOBALS], &globals, error);
    if (ok) {
        runtime->restore_pending = true;
        qa_script_defines_release((qa_script_defines *)runtime->library->options.preprocessor.globals);
        qa_script_defines_release(runtime->globals);
        runtime->globals = globals; globals = NULL;
        runtime->options.library.preprocessor.globals = runtime->globals;
        runtime->library->options.preprocessor.globals = runtime->globals;
        qa_script_defines_retain(runtime->globals);
        runtime->map_name = (char *)state.map.name; state.map.name = NULL;
        runtime->map = map ? *map : (qa_bot_runtime_map){0}; runtime->map.name = runtime->map_name;
        if (state.library) {
            qa_bot_library_reload(runtime->library, state.reload);
            ok = qa_bot_library_variables_restore(runtime->library, parts[VARIABLES], error) &&
                 qa_bot_runtime_assets_restore(runtime, parts[ASSETS], &assets, error);
        } else {
            qa_bot_moves_destroy(runtime->moves); runtime->moves = NULL;
            qa_bot_goals_destroy(runtime->goals); runtime->goals = NULL;
            qa_bot_chat_system_destroy(runtime->chat_system); runtime->chat_system = NULL;
            qa_bot_library_destroy(runtime->library); runtime->library = NULL;
        }
    }
    if (ok) ok = qa_bot_actions_restore_bytes(runtime->actions, parts[ACTIONS], error);
    if (ok && state.bsp) ok = qa_bot_bsp_restore(parts[BSP], runtime->map.source_entities, &runtime->bsp, error);
    if (ok && state.goals) ok = qa_bot_goals_save_restore(session, runtime->goals, parts[GOALS], assets,
        runtime->bsp ? qa_bot_bsp_entities(runtime->bsp) : runtime->map.entities, error);
    qa_bot_chat_asset_save_refs refs = {.context = assets, .encode = chat_encode, .decode = chat_decode};
    if (ok && state.chat) ok = qa_bot_chat_system_restore_bytes(runtime->chat_system, parts[CHAT], &refs, &chats, error);
    if (ok && state.moves) ok = qa_bot_moves_save_restore(runtime->moves, parts[MOVES], error);
    if (ok) ok = qa_bot_observations_restore(session, runtime, parts[OBSERVATIONS], error);
    if (ok) ok = qa_source_save_reader(&io, session, parts[HANDLES], error) && bot_save_signature(&io, handle_magic) &&
                 handles_fields(&io, runtime, assets, &chats) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        runtime->time = state.time; runtime->initialized = state.initialized;
        runtime->library_initialized = state.library_initialized; runtime->loaded = state.loaded;
        runtime->bsp_loaded = state.bsp_loaded; runtime->closed = state.closed; runtime->restore_pending = false;
    }
    qa_script_defines_release(globals);
    qa_bot_chat_restored_states_free(&chats); qa_bot_saved_assets_free(assets); qa_bot_runtime_saved_map_free(&state.map);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid complete bot runtime continuation");
    return ok;
}
