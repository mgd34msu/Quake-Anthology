#include "internal.h"
#include "../library/internal.h"
#include "../chat/internal.h"
#include "../save_fields.h"
#include "qa/bot_runtime_save.h"
#include "qa/bot_runtime_assets_save.h"
#include "qa/bot_library_save.h"
#include "qa/bot_actions_save.h"
#include "qa/bot_goals_save.h"
#include "qa/bot_chat_system_save.h"
#include "qa/bot_movement_save.h"
#include "qa/bot_observations_save.h"
#include "qa/script_defines_save.h"
#include "qa/bots_allocator_save.h"
#include "source_weapon_save.h"
#include "../library/source_fuzzy_store.h"
#include "source_weapon_setup.h"

enum { VARIABLES, ASSETS, ACTIONS, GOALS, CHAT, MOVES, OBSERVATIONS, HANDLES, GLOBALS, LOG, MEMORY, PART_COUNT };
typedef struct runtime_state {
    uint32_t maximum, minimum, profile;
    bool debug, initialized, library_initialized, loaded, bsp_loaded, closed;
    bool library, actions, bsp, goals, chat, moves, reload, source_action_client;
    float time;
    uint64_t weapon_generation,weapon_setup_revision;
    qa_bot_runtime_saved_map map;
} runtime_state;
static const uint8_t magic[8] = {'Q', 'A', 'B', 'R', 'U', 'N', 'T', 0};
static const uint8_t handle_magic[8] = {'Q', 'A', 'B', 'H', 'N', 'D', 'L', 1};

static bool fail(qa_error *error, const char *message)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false; }

static bool signature(qa_source_save_io *io)
{
    uint8_t bytes[8]; memcpy(bytes, magic, sizeof(bytes));
    return qa_source_save_bytes(io, bytes, sizeof(bytes)) && !memcmp(bytes, magic, sizeof(bytes)) ? true :
        bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot runtime continuation signature");
}

void qa_bot_runtime_saved_map_free(qa_bot_runtime_saved_map *map)
{ if (map) { free((void *)map->name); *map = (qa_bot_runtime_saved_map){0}; } }

static bool state_fields(qa_source_save_io *io, runtime_state *state)
{
    qa_bot_runtime_saved_map *map = &state->map;
    bool ok = qa_source_save_u32(io, &state->maximum) && state->maximum && state->maximum <= INT32_MAX &&
        qa_source_save_u32(io, &state->minimum) && qa_source_save_u32(io, &state->profile) &&
        state->profile <= QA_BOT_OBSERVATION_MODULE && qa_source_save_bool(io, &state->debug) &&
        qa_source_save_bool(io, &state->initialized) && qa_source_save_bool(io, &state->library_initialized) &&
        qa_source_save_bool(io, &state->loaded) && qa_source_save_bool(io, &state->bsp_loaded) &&
        qa_source_save_bool(io, &state->closed) && qa_source_save_f32(io, &state->time) && isfinite(state->time) &&
        qa_source_save_u64(io,&state->weapon_generation) && state->weapon_generation<=UINT64_C(9007199254740991) &&
        qa_source_save_u64(io,&state->weapon_setup_revision) && state->weapon_setup_revision<=UINT64_C(9007199254740991) &&
        qa_source_save_bool(io, &state->library) && qa_source_save_bool(io, &state->actions) &&
        qa_source_save_bool(io, &state->bsp) && qa_source_save_bool(io, &state->goals) &&
        qa_source_save_bool(io, &state->chat) && qa_source_save_bool(io, &state->moves) &&
        qa_source_save_bool(io, &state->source_action_client) &&
        qa_source_save_bool(io, &state->reload) && bot_save_text(io, &map->name) &&
        qa_source_save_bool(io, &map->entities) && qa_source_save_bool(io, &map->source) &&
        qa_source_save_bool(io, &map->navigation) && qa_source_save_count(io, &map->source_bytes, SIZE_MAX);
    if (ok) ok = state->actions &&
        (state->closed ? (!state->library && !state->goals && !state->chat && !state->moves && !state->reload &&
                         !state->initialized && !state->library_initialized && !state->loaded) :
                        (state->library && state->goals && state->chat && state->moves)) &&
        (state->library_initialized || !state->initialized) &&
        (!state->loaded || (state->library_initialized && map->name && map->navigation && state->bsp_loaded)) &&
        (!state->bsp || (state->bsp_loaded && (map->source || !map->entities))) &&
        (map->source || !map->source_bytes) &&
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
    case GOALS: return state->goals;
    case CHAT: return state->chat;
    case MOVES: return state->moves;
    case MEMORY: return !state->closed;
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
static bool weapon_reference(void *context,qa_bot_weights *weights,uint64_t *id,bool *found,qa_error *error) {
    *found=weights && weights->source && !weights->source->disposed;
    return !*found || qa_bot_saved_asset_id(context,QA_BOT_SAVED_WEIGHTS,weights,id,error);
}
static bool weapon_resolve(void *context,uint64_t id,qa_bot_weights **out,qa_error *error) {
    const void *object;
    if(!qa_bot_saved_asset_resolve(context,QA_BOT_SAVED_WEIGHTS,id,&object,error)) return false;
    *out=(qa_bot_weights *)object;return true;
}
static bool handles_fields(qa_source_save_io *io, qa_bot_runtime *runtime, const qa_bot_saved_assets *assets,
                           const qa_bot_chat_restored_states *chat_states)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    void *object = runtime->weapon_config;
    bool ok = reference(io, assets, QA_BOT_SAVED_WEAPONS, &object);
    if (reading && ok) { runtime->weapon_config = object; qa_bot_weapons_retain(object); }
    bot_weapon_weight_refs refs={.context=(void *)assets,.reference=weapon_reference,.resolve=weapon_resolve};
    if(ok) ok=bot_weapon_pointer_fields(io,runtime->memory,&runtime->weapon_pointers,&refs);
    if(ok) ok=bot_runtime_weapon_diagnostics_fields(io,runtime);
    size_t count = runtime->options.maximum_states;
    if (ok) ok = qa_source_save_count(io, &count, runtime->options.maximum_states) && count == runtime->options.maximum_states;
    if (ok && reading && count > (io->input.size - io->offset) / 2)
        ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot runtime physical handles");
    for (size_t i = 0; ok && i < count; ++i) {
        object = runtime->characters[i];
        ok = reference(io, assets, QA_BOT_SAVED_CHARACTER, &object);
        if (ok && object) {
            const qa_bot_character_view *view = NULL;
            ok = qa_bot_character_view_read(object, &view, io->error);
        }
        if (ok && object)
            for (size_t j = 0; j < i; ++j) if (runtime->characters[j] == object) ok = false;
        if (reading && ok) { runtime->characters[i] = object; qa_bot_character_retain(object); }
        bot_weapon_state *weapon = &runtime->weapons[i];
        if (ok) ok = qa_source_save_bool(io, &weapon->used);
        if(ok && weapon->used) {
            ok=bot_weapon_record_fields(io,runtime->memory,&weapon->record) &&
                qa_source_save_u64(io,&weapon->revision) && weapon->revision<=UINT64_C(9007199254740991);
            qa_bot_weights *weights;uint32_t index_pointer;
            if(ok) ok=bot_weapon_config_get(&runtime->weapon_pointers,&weapon->record,&weights,io->error) &&
                bot_weapon_record_read(&weapon->record,BOT_WEAPON_INDEX_POINTER,&index_pointer,io->error);
        }
    }
    for(size_t i=0;ok && i<64;++i) {
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
    }
    if (!ok && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid runtime bot asset or handle mapping");
    return ok;
}
static bool capture_parts(qa_session *session, const qa_bot_runtime *runtime, qa_buffer parts[PART_COUNT], qa_error *error)
{
    qa_bot_saved_assets *assets = NULL;
    bool ok=runtime->closed || qa_bot_memory_capture(runtime->memory,&parts[MEMORY],error);
    if(ok) ok = !runtime->library || (qa_bot_library_variables_capture(runtime->library, &parts[VARIABLES], error) &&
        qa_bot_runtime_assets_capture(runtime, &parts[ASSETS], &assets, error));
    if (ok && runtime->actions) ok = qa_bot_actions_source_capture(runtime->actions, &parts[ACTIONS], error);
    if (ok && runtime->goals) ok = qa_bot_goals_save_capture(session, runtime->goals, assets, &parts[GOALS], error);
    qa_bot_chat_asset_save_refs refs = {.context = assets, .encode = chat_encode, .decode = chat_decode};
    if (ok && runtime->chat_system) ok = qa_bot_chat_system_capture(runtime->chat_system, &refs, &parts[CHAT], error);
    if (ok && runtime->moves) ok = qa_bot_moves_save_capture(runtime->moves, &parts[MOVES], error);
    if (ok) ok = qa_bot_observations_capture(session, runtime, &parts[OBSERVATIONS], error);
    if (ok) ok = qa_script_defines_save_capture(runtime->globals, &parts[GLOBALS], error);
    if (ok) ok = qa_bot_log_capture(runtime->log,&parts[LOG],error);
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_writer(&io, session, error) && bot_save_signature(&io, handle_magic) &&
        handles_fields(&io, (qa_bot_runtime *)runtime, assets, NULL) && qa_source_save_finish(&io, &parts[HANDLES]);
    qa_source_save_dispose(&io); qa_bot_saved_assets_free(assets); return ok;
}
bool qa_bot_runtime_save_capture(qa_session *session, const qa_bot_runtime *runtime, qa_buffer *out, qa_error *error)
{
    if (!session || !runtime || !out || !qa_bot_runtime_can_destroy(runtime) || runtime->restore_pending)
        return fail(error, "Bot runtime capture requires its complete idle source owner");
    if (!runtime->globals || !runtime->log || !runtime->memory ||
        qa_bot_actions_memory(runtime->actions)!=runtime->memory ||
        qa_bot_memory_disposed(runtime->memory)!=runtime->closed ||
        (runtime->library && qa_bot_library_memory(runtime->library)!=runtime->memory) ||
        runtime->options.library.preprocessor.globals != runtime->globals ||
        (runtime->library && runtime->library->options.preprocessor.globals != runtime->globals))
        return fail(error, "Bot runtime global macro aliases differ from their actual owner");
    runtime_state state = {.maximum = runtime->options.maximum_states, .minimum = runtime->options.minimum_clients,
        .profile = runtime->options.observations, .debug = runtime->options.debug, .initialized = runtime->initialized,
        .library_initialized = runtime->library_initialized, .loaded = runtime->loaded, .bsp_loaded = runtime->bsp_loaded,
        .closed = runtime->closed, .time = runtime->time, .library = runtime->library != NULL,
        .weapon_generation=runtime->weapon_generation,.weapon_setup_revision=runtime->weapon_setup_revision,
        .actions = runtime->actions != NULL, .bsp = runtime->bsp != NULL, .goals = runtime->goals != NULL,
        .chat = runtime->chat_system != NULL, .moves = runtime->moves != NULL,
        .source_action_client = runtime->services.movement.source_action_client != NULL,
        .reload = runtime->library && runtime->library->options.reload_characters,
        .map = {.name = runtime->map_name, .entities = runtime->map.entities != NULL,
                .source = runtime->map.source_entities.data != NULL, .navigation = runtime->map.navigation != NULL,
                .source_bytes = runtime->map.source_entities.size}};
    if (!runtime->characters || !runtime->weapons || !runtime->chats ||
        (!runtime->map.source_entities.data && runtime->map.source_entities.size))
        return fail(error, "Bot runtime has incomplete actual storage or immutable map bytes");
    qa_buffer parts[PART_COUNT] = {0}; qa_source_save_io io = {0};
    bool ok = capture_parts(session, runtime, parts, error) && qa_source_save_writer(&io, NULL, error) &&
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
    return true;
}
bool qa_bot_runtime_save_restore(qa_session *session, qa_bot_runtime *runtime, qa_bytes bytes,
                                 const qa_bot_runtime_map *map, qa_error *error)
{
    if (!session || !runtime || !qa_bot_runtime_can_destroy(runtime) || runtime->restore_pending || runtime->closed ||
        runtime->initialized || runtime->library_initialized || runtime->loaded || runtime->bsp || runtime->map_name ||
        runtime->weapon_config || !runtime->library || !runtime->actions || !runtime->moves || !runtime->goals ||
        !runtime->chat_system || runtime->chat_system->states || runtime->entity_capacity || !runtime->globals ||
        !runtime->memory || qa_bot_memory_disposed(runtime->memory) ||
        qa_bot_memory_live_allocations(runtime->memory) ||
        qa_bot_library_memory(runtime->library)!=runtime->memory ||
        runtime->options.library.preprocessor.globals!=runtime->globals ||
        runtime->library->options.preprocessor.globals!=runtime->globals)
        return fail(error, "Bot runtime import requires its actual empty detached constructor");
    for (size_t i = 0; i < runtime->options.maximum_states; ++i)
        if (runtime->characters[i] || runtime->weapons[i].used)
            return fail(error, "Detached bot runtime already owns handle continuations");
    for(size_t i=0;i<64;++i) if(runtime->chats[i])
        return fail(error,"Detached bot runtime already owns source chat states");
    runtime_state state = {0}; qa_bytes parts[PART_COUNT] = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && signature(&io) &&
        state_fields(&io, &state) && read_parts(&io, &state, parts) &&
        state.maximum == runtime->options.maximum_states && state.minimum == runtime->options.minimum_clients &&
        state.profile == (uint32_t)runtime->options.observations && state.debug == runtime->options.debug &&
        state.source_action_client == (runtime->services.movement.source_action_client != NULL) &&
        map_matches(&state, map, error);
    qa_source_save_dispose(&io);
    qa_bot_saved_assets *assets = NULL; qa_bot_chat_restored_states chats = {0};
    if(ok && !state.closed) ok=qa_bot_memory_restore(runtime->memory,parts[MEMORY],error);
    if (ok) ok = qa_script_defines_save_restore_into(runtime->globals,parts[GLOBALS],error);
    if (ok) {
        runtime->restore_pending = true;
        runtime->weapon_generation=state.weapon_generation;
        runtime->weapon_setup_revision=state.weapon_setup_revision;
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
            if(!qa_bot_memory_dispose(runtime->memory,error)) ok=false;
        }
    }
    if (ok) ok = qa_bot_actions_source_restore(runtime->actions, parts[ACTIONS], error);
    if (ok && state.bsp) ok = bot_runtime_bsp_load(runtime, &runtime->bsp, error);
    if (ok && state.goals) ok = qa_bot_goals_save_restore(session, runtime->goals, parts[GOALS], assets,
        runtime->bsp ? qa_bot_bsp_entities(runtime->bsp) : runtime->map.entities, error);
    qa_bot_chat_asset_save_refs refs = {.context = assets, .encode = chat_encode, .decode = chat_decode};
    if (ok && state.chat) ok = qa_bot_chat_system_restore_bytes(runtime->chat_system, parts[CHAT], &refs, &chats, error);
    if (ok && state.moves) ok = qa_bot_moves_save_restore(runtime->moves, parts[MOVES], error);
    if (ok) ok = qa_bot_observations_restore(session, runtime, parts[OBSERVATIONS], error);
    if (ok) ok = qa_source_save_reader(&io, session, parts[HANDLES], error) && bot_save_signature(&io, handle_magic) &&
                 handles_fields(&io, runtime, assets, &chats) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if(ok) ok=qa_bot_log_restore(runtime->log,parts[LOG],error);
    if (ok) {
        runtime->time = state.time; runtime->initialized = state.initialized;
        runtime->library_initialized = state.library_initialized; runtime->loaded = state.loaded;
        runtime->bsp_loaded = state.bsp_loaded; runtime->closed = state.closed; runtime->restore_pending = false;
    }
    qa_bot_chat_restored_states_free(&chats); qa_bot_saved_assets_free(assets); qa_bot_runtime_saved_map_free(&state.map);
    if (!ok && (!error || error->code == QA_OK)) fail(error, "Invalid complete bot runtime continuation");
    return ok;
}
