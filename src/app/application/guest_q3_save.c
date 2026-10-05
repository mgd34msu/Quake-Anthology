#include "guest_q3_save.h"
#include "guest_q3_catalog.h"
#include "guest_checkpoint.h"
#include "guest_input_private.h"
#include "guest_projection_private.h"
#include "bots_private.h"
#include "map_players_private.h"
#include "portals.h"
#include "qa/qvm_save.h"
#include "qa/q3_host_save.h"
#include "qa/q3_abi.h"
#include "qa/source_save.h"
#include "guest_q3_console.h"
#include "guest_q3_client_console.h"
#include "qa/cvars_save.h"
#include "startup_flow.h"
#include "guest_q3_functions.h"
#include "qa/persistence_content.h"
#include "qa/native_module_save.h"
#include "qa/persistence_application.h"
#include "save_native_q2.h"
#include "qa/map_sidecars.h"
#include "save_private.h"

static bool level_only(const qa_application *app)
{
    return app->native_restore_image
        ? qa_save_image_metadata(app->native_restore_image)->purpose==QA_SAVE_TRANSITION
        : app->capture_purpose==QA_SAVE_TRANSITION;
}

static bool state_fail(qa_source_save_io *io, qa_status status, const char *message)
{
    if (!io->failed) qa_error_set(io->error, status, io->offset, "%s", message);
    io->failed = true;
    return false;
}

static bool state_signature(qa_source_save_io *io)
{
    uint8_t magic[8] = {'Q','A','G','3','S','T',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','S','T',0,0};
    return qa_source_save_bytes(io, magic, sizeof(magic)) &&
        ((!memcmp(magic, expected, sizeof(magic))) ||
         state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 state signature"));
}

static bool owned_text(qa_source_save_io *io, char **text)
{
    bool present = *text != NULL;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) + 1 : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX) || !length)
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 text extent");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 text");
        *text = malloc(length);
        if (!*text) return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 text");
    }
    return qa_source_save_bytes(io, *text, length) &&
        ((!(*text)[length - 1] && !memchr(*text, 0, length - 1)) ||
         state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 text terminator"));
}

static bool tokens(qa_source_save_io *io, qa_command_tokens *value)
{
    if (!qa_source_save_count(io, &value->count, 1024)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (value->count > (io->input.size - io->offset) / 9)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 token inventory");
        value->values = value->count ? calloc(value->count, sizeof(*value->values)) : NULL;
        value->storage = value->count ? malloc(9216) : NULL;
        if (value->count && (!value->values || !value->storage))
            return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 arguments");
    } else if (value->count && (!value->values || !value->storage)) {
        return state_fail(io, QA_ERROR_FORMAT, "Application Q3 token storage is absent");
    }
    size_t used = 0;
    for (size_t i = 0; i < value->count; ++i) {
        char *text = io->direction == QA_SOURCE_SAVE_WRITE ? value->values[i] : NULL;
        if (!owned_text(io, &text)) {
            if (io->direction == QA_SOURCE_SAVE_READ) free(text);
            return false;
        }
        if (!text) return state_fail(io, QA_ERROR_FORMAT, "Application Q3 token is absent");
        size_t length = strlen(text) + 1;
        if (length > 9216 - used) {
            if (io->direction == QA_SOURCE_SAVE_READ) free(text);
            return state_fail(io, QA_ERROR_FORMAT, "Application Q3 tokens exceed the source extent");
        }
        if (io->direction == QA_SOURCE_SAVE_READ) {
            value->values[i] = value->storage + used;
            memcpy(value->values[i], text, length);
            free(text);
        }
        used += length;
    }
    if (!owned_text(io, &value->args_text)) return false;
    if (!value->args_text)
        return !value->count || state_fail(io, QA_ERROR_FORMAT, "Application Q3 token arguments are absent");
    size_t offset = 0;
    for (size_t i = 1; i < value->count; ++i) {
        if (i > 1 && value->args_text[offset++] != ' ')
            return state_fail(io, QA_ERROR_FORMAT, "Application Q3 arguments differ from their tokens");
        size_t length = strlen(value->values[i]);
        if (strncmp(value->args_text + offset, value->values[i], length))
            return state_fail(io, QA_ERROR_FORMAT, "Application Q3 arguments differ from their tokens");
        offset += length;
    }
    return !value->args_text[offset] ||
        state_fail(io, QA_ERROR_FORMAT, "Application Q3 arguments have an unowned suffix");
}

static bool record_write(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    (void)error;
    memcpy((uint8_t *)context + offset, bytes.data, bytes.size);
    return true;
}

static bool entity(qa_source_save_io *io, qa_q3_entity *value)
{
    uint8_t bytes[208] = {0};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN,
        .bytes = {bytes, sizeof(bytes)}, .context = bytes, .write = record_write};
    return (io->direction != QA_SOURCE_SAVE_WRITE ||
            qa_q3_abi_write_entity(&record, 0, true, value, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_entity(&record, 0, true, value, io->error));
}

static bool player(qa_source_save_io *io, qa_q3_player *value)
{
    uint8_t bytes[468] = {0};
    uint32_t product = value->product;
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN,
        .bytes = {bytes, sizeof(bytes)}, .context = bytes, .write = record_write};
    if (!qa_source_save_u32(io, &product) || product > QA_Q3_TEAM_ARENA)
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 player product");
    bool ok = (io->direction != QA_SOURCE_SAVE_WRITE ||
               qa_q3_abi_write_player(&record, 0, true, false, value, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_player(&record, 0, true, value, io->error));
    if (ok && io->direction == QA_SOURCE_SAVE_READ) value->product = (qa_q3_product)product;
    return ok;
}

static bool command(qa_source_save_io *io, qa_q3_usercmd *value)
{
    uint8_t bytes[24] = {0};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN,
        .bytes = {bytes, sizeof(bytes)}, .context = bytes, .write = record_write};
    return (io->direction != QA_SOURCE_SAVE_WRITE ||
            qa_q3_abi_write_usercmd(&record, 0, false, value, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_usercmd(&record, 0, value, io->error));
}

static bool gamestate(qa_source_save_io *io, qa_q3_gamestate *value)
{
    bool ok = qa_source_save_i32(io, &value->command_sequence) &&
        qa_source_save_i32(io, &value->client_number) &&
        qa_source_save_i32(io, &value->checksum_feed) &&
        qa_source_save_count(io, &value->string_bytes, QA_Q3_GAMESTATE_CHARS) &&
        qa_source_save_bytes(io, value->strings, sizeof(value->strings));
    if (!ok) return false;
    if (!value->string_bytes || value->strings[0])
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 gamestate strings");
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i)
        if (!qa_source_save_u16(io, &value->config_offsets[i]) ||
            !qa_q3_configstring(value, (unsigned)i))
            return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 configstring offset");
    for (size_t i = 0; i < QA_Q3_ENTITIES; ++i)
        if (!qa_source_save_bool(io, &value->baseline_present[i]) || !entity(io, &value->baselines[i]))
            return false;
    return true;
}

static bool snapshot(qa_source_save_io *io, q3g_snapshot *slot, size_t ordinal)
{
    qa_q3_snapshot *value = &slot->value;
    bool ok = qa_source_save_bool(io, &value->valid) &&
        qa_source_save_i32(io, &value->message_number) &&
        qa_source_save_i32(io, &value->server_time) &&
        qa_source_save_i32(io, &value->delta_number) &&
        qa_source_save_i32(io, &value->server_command_number) &&
        qa_source_save_u64(io, &value->parse_entities_number) &&
        qa_source_save_u8(io, &value->flags) && qa_source_save_u8(io, &value->area_bytes) &&
        qa_source_save_bytes(io, value->area_mask, sizeof(value->area_mask)) &&
        player(io, &value->player) && qa_source_save_count(io, &value->entity_count, 256) &&
        qa_source_save_count(io, &slot->capacity, 256) && qa_source_save_i32(io, &slot->ping);
    if (!ok) return false;
    if (value->area_bytes > sizeof(value->area_mask) || value->entity_count > slot->capacity ||
        (value->valid && (value->message_number < 0 ||
         ((uint32_t)value->message_number & (QA_Q3_PACKET_BACKUP - 1)) != ordinal)))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 snapshot history identity");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (slot->capacity > (io->input.size - io->offset) / 208)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 snapshot entities");
        slot->entities = slot->capacity ? calloc(slot->capacity, sizeof(*slot->entities)) : NULL;
        if (slot->capacity && !slot->entities)
            return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 snapshot entities");
        value->entities = slot->entities;
    } else if ((slot->capacity && !slot->entities) || value->entities != slot->entities) {
        return state_fail(io, QA_ERROR_FORMAT, "Application Q3 snapshot entity storage differs");
    }
    for (size_t i = 0; i < slot->capacity; ++i)
        if (!entity(io, &slot->entities[i])) return false;
    return true;
}

static bool client(qa_source_save_io *io, q3g_client *value)
{
    bool ok = qa_source_save_actor(io, &value->actor) && owned_text(io, &value->userinfo) &&
        owned_text(io, &value->retirement_reason) && command(io, &value->command);
    for (size_t i = 0; ok && i < QA_Q3_USERCMDS; ++i) ok = command(io, &value->commands[i]);
    ok = ok && qa_source_save_i32(io, &value->command_sequence) &&
        qa_source_save_i32(io, &value->snapshot_sequence) &&
        qa_source_save_i32(io, &value->consumed_server_command) &&
        qa_source_save_i32(io, &value->server_id) && qa_source_save_u64(io, &value->entered_ns) &&
        qa_source_save_i32(io, &value->reliable.sequence) &&
        qa_source_save_i32(io, &value->reliable.acknowledged) &&
        qa_source_save_bytes(io, value->reliable.text, sizeof(value->reliable.text));
    if (!ok) return false;
    int64_t outstanding = (int64_t)value->reliable.sequence - value->reliable.acknowledged;
    if (value->command_sequence < 0 || value->snapshot_sequence < 0 ||
        value->reliable.sequence < 0 || value->reliable.acknowledged < 0 ||
        outstanding < 0 || outstanding > QA_Q3_RELIABLE ||
        value->consumed_server_command < 0 || value->consumed_server_command > value->reliable.sequence)
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 client sequence");
    for (size_t i = 0; i < QA_Q3_RELIABLE; ++i)
        if (!memchr(value->reliable.text[i], 0, QA_Q3_COMMAND_CHARS))
            return state_fail(io, QA_ERROR_FORMAT, "Unterminated application Q3 reliable command");
    bool present = value->gamestate != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (present && io->direction == QA_SOURCE_SAVE_READ) {
        if (io->input.size - io->offset < 12 + 8 + QA_Q3_GAMESTATE_CHARS +
            QA_Q3_CONFIGSTRINGS * 2 + QA_Q3_ENTITIES * 209)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 client gamestate");
        value->gamestate = calloc(1, sizeof(*value->gamestate));
        if (!value->gamestate) return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 client gamestate");
    }
    if (present && !gamestate(io, value->gamestate)) return false;
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i)
        if (!snapshot(io, &value->snapshots[i], i)) return false;
    if (!qa_source_save_f32(io, &value->sensitivity) || !qa_source_save_i32(io, &value->weapon) ||
        !qa_source_save_count(io, &value->big_configstring_length, Q3G_BIG_INFO_CHARS - 1)) return false;
    present = value->big_configstring != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (present && io->direction == QA_SOURCE_SAVE_READ) {
        if (value->big_configstring_length + 1 > io->input.size - io->offset)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 continued configstring");
        value->big_configstring = malloc(Q3G_BIG_INFO_CHARS);
        if (!value->big_configstring)
            return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 continued configstring");
    }
    if (present && (!qa_source_save_bytes(io, value->big_configstring, value->big_configstring_length + 1) ||
        value->big_configstring[value->big_configstring_length] ||
        memchr(value->big_configstring, 0, value->big_configstring_length)))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 continued configstring");
#define FLAG(name) if (!qa_source_save_bool(io, &value->name)) return false
    FLAG(allocated); FLAG(connected); FLAG(begun); FLAG(bot); FLAG(has_snapshot);
    FLAG(pending_system_info); FLAG(pending_bot); FLAG(pending_retirement);
    FLAG(disconnect_pending); FLAG(disconnect_started); FLAG(roster_attached);
#undef FLAG
    if (!q3g_fire_fields(io, &value->fire) || !q3g_fire_valid(&value->fire, value->actor))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid original Q3 fire continuation");
    if (!isfinite(value->sensitivity) ||
        (!value->big_configstring && value->big_configstring_length) ||
        (value->connected && (!value->actor.registry || !value->allocated)) ||
        (value->has_snapshot && (!value->snapshots[(uint32_t)value->snapshot_sequence &
         (QA_Q3_PACKET_BACKUP - 1)].value.valid ||
         value->snapshots[(uint32_t)value->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value.message_number !=
         value->snapshot_sequence)))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 client continuation");
    return true;
}

static bool fields(qa_source_save_io *io, struct application_q3_guest *engine)
{
    uint32_t product = engine->product;
    bool ok = state_signature(io) && qa_source_save_u32(io, &product) &&
        product == engine->product && qa_source_save_u64(io, &engine->role_sequence) &&
        qa_source_save_i32(io, &engine->milliseconds) && qa_source_save_i32(io, &engine->random_seed) &&
        qa_source_save_bool(io, &engine->map_ready) &&
        qa_source_save_bool(io, &engine->loaded_compatibility) &&
        qa_source_save_i32(io, &engine->loaded_game_type) &&
        qa_source_save_i32(io, &engine->loaded_max_clients) &&
        qa_source_save_u8(io, &engine->local_snapshot_server_bit) &&
        owned_text(io, &engine->entity_text) && tokens(io, &engine->arguments) && gamestate(io, &engine->gamestate);
    if (!ok) return state_fail(io, QA_ERROR_FORMAT, "Application Q3 state product or envelope differs");
    if ((engine->loaded_compatibility && (!engine->map_ready ||
         engine->loaded_max_clients < 1 || engine->loaded_max_clients > 64)) ||
        (!engine->loaded_compatibility && (engine->loaded_game_type || engine->loaded_max_clients)) ||
        (engine->local_snapshot_server_bit != 0 && engine->local_snapshot_server_bit != 4))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 loaded compatibility");
    for (size_t i = 0; i < 64; ++i)
        if (!qa_source_save_u32(io, &engine->seats[i]) || !client(io, &engine->clients[i])) return false;
    return true;
}

static bool owner(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (engine && engine->video_leases)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 continuation retains an incomplete actual video restart");
    if (engine) for (size_t i = 0; i < 64; ++i)
        if (engine->clients[i].carry_pending || engine->clients[i].reserved)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source client still owns unfinished admission");
    return (engine && engine->round.phase == Q3G_ROUND_NONE && !engine->startup_restart && !engine->handoff_ready && !engine->calls && !engine->draining_clients &&
        qa_session_safe(provider->application->session) && application_q3_guest_idle(provider)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires an idle source owner");
}

bool application_guest_q3_state_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    if (!out || !owner(provider, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, provider->application->session, error) &&
        fields(&io, q3g_engine(provider)) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool application_guest_q3_state_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    if (!owner(provider, error)) return false;
    struct application_q3_guest *engine = q3g_engine(provider);
    if (provider->application->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires an isolated persistence candidate");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->initialized)
            return application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires unpublished source roles");
    for (size_t i = 0; i < 64; ++i) {
        const q3g_client *value = &engine->clients[i];
        if (value->actor.registry || value->userinfo || value->retirement_reason || value->gamestate ||
            value->big_configstring || value->allocated || value->connected)
            return application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires empty candidate clients");
        for (size_t j = 0; j < QA_Q3_PACKET_BACKUP; ++j)
            if (value->snapshots[j].entities)
                return application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires empty candidate history");
    }
    struct application_q3_guest *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return application_fail(error, QA_ERROR_MEMORY, "Restoring application Q3 client state");
    candidate->product = engine->product;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session, bytes, error) &&
        fields(&io, candidate) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok && candidate->random_seed != engine->random_seed)
        ok = application_fail(error, QA_ERROR_FORMAT, "Application Q3 initialization seed differs from its source owner");
    if (ok && ((engine->entity_text != NULL) != (candidate->entity_text != NULL) ||
        (engine->entity_text && strcmp(engine->entity_text, candidate->entity_text))))
        ok = application_fail(error, QA_ERROR_FORMAT, "Application Q3 entity text differs from its prepared immutable map");
    if (ok) {
        q3g_clients_clear(engine);
        memcpy(engine->clients, candidate->clients, sizeof(engine->clients));
        memset(candidate->clients, 0, sizeof(candidate->clients));
        qa_command_tokens_free(&engine->arguments);
        engine->arguments = candidate->arguments; candidate->arguments = (qa_command_tokens){0};
        /* Hosts borrow this exact prepared allocation. Keep it stable while
         * their parser and executor continuations are imported separately. */
        engine->gamestate = candidate->gamestate;
        memcpy(engine->seats, candidate->seats, sizeof(engine->seats));
        engine->milliseconds = candidate->milliseconds;
        engine->random_seed = candidate->random_seed;
        engine->map_ready = candidate->map_ready;
        engine->loaded_compatibility = candidate->loaded_compatibility;
        engine->loaded_game_type = candidate->loaded_game_type;
        engine->loaded_max_clients = candidate->loaded_max_clients;
        engine->local_snapshot_server_bit = candidate->local_snapshot_server_bit;
        engine->role_sequence = candidate->role_sequence;
    }
    q3g_clients_clear(candidate); qa_command_tokens_free(&candidate->arguments);
    free(candidate->entity_text); free(candidate);
    return ok;
}

enum { ROLE_INITIALIZED = 1, ROLE_RETIRED = 2, ROLE_READY = 4,
       ROLE_PRIMARY = 8, ROLE_COMMITTED = 32, ROLE_INIT_SUCCEEDED = 128 };

typedef struct saved_artifact {
    char *path;
    uint32_t kind, abi;
    bool qvm;
    qa_bytes module;
    qa_buffer module_storage;
    qa_sha256_digest digest, primary_digest;
    uint64_t pool, resource;
    qa_vfs_acquisition acquisition;
    uint64_t items_pool, items_resource;
    qa_sha256_digest items_digest;
    qa_vfs_acquisition items_acquisition;
    q3g_artifact *actual;
} saved_artifact;
typedef struct saved_projection {
    qa_actor_id actor;
    uint32_t slot;
    uint64_t serial;
    bool bound;
} saved_projection;
typedef struct saved_role {
    size_t artifact;
    uint32_t seat, flags;
    uint64_t sequence;
    qa_string_id owner;
    qa_command_tokens arguments;
    saved_projection *projections;
    size_t projection_count;
    qa_bytes input, executor, services, native_host, resources;
    qa_bytes lower_resources;
    qa_buffer input_storage, executor_storage, service_storage, native_host_storage, resource_storage;
    q3g_role *actual;
} saved_role;
typedef struct q3g_restore {
    qa_buffer storage, state_storage, cvar_storage;
    qa_bytes state, cvars;
    char *entity_text;
    uint32_t product;
    uint64_t sequence;
    size_t game, artifact_count, role_count;
    uint32_t seats[64];
    saved_artifact *artifacts;
    saved_role *roles;
    bool owns_text, imported;
} q3g_restore;

static void saved_free(q3g_restore *saved)
{
    if (!saved) return;
    for (size_t i = 0; saved->artifacts && i < saved->artifact_count; ++i) {
        qa_buffer_free(&saved->artifacts[i].module_storage);
        if (saved->owns_text) {
            free(saved->artifacts[i].path);
            qa_vfs_acquisition_dispose(&saved->artifacts[i].acquisition);
            qa_vfs_acquisition_dispose(&saved->artifacts[i].items_acquisition);
        }
    }
    for (size_t i = 0; saved->roles && i < saved->role_count; ++i) {
        saved_role *role = &saved->roles[i];
        if (saved->owns_text) qa_command_tokens_free(&role->arguments);
        free(role->projections);
        qa_buffer_free(&role->input_storage);
        qa_buffer_free(&role->executor_storage);
        qa_buffer_free(&role->service_storage);
        qa_buffer_free(&role->native_host_storage);
        qa_buffer_free(&role->resource_storage);
    }
    if (saved->owns_text) free(saved->entity_text);
    free(saved->artifacts); free(saved->roles);
    qa_buffer_free(&saved->state_storage); qa_buffer_free(&saved->storage);
    qa_buffer_free(&saved->cvar_storage);
    free(saved);
}

void application_guest_q3_save_clear(struct application_q3_guest *engine)
{
    if (!engine) return;
    saved_free(engine->restoration); engine->restoration = NULL;
}

static bool blob(qa_source_save_io *io, qa_bytes *bytes, size_t minimum)
{
    size_t length = bytes->size;
    if (!qa_source_save_count(io, &length, SIZE_MAX)) return false;
    if (length < minimum)
        return state_fail(io, QA_ERROR_FORMAT, "Q3 continuation nested owner is absent or truncated");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset)
            return state_fail(io, QA_ERROR_FORMAT, "Q3 continuation nested owner exceeds its enclosing record");
        *bytes = (qa_bytes){io->input.data + io->offset, length};
        io->offset += length;
        return true;
    }
    return qa_source_save_bytes(io, (void *)bytes->data, length);
}

static bool acquisition(qa_source_save_io *io,const qa_application_content_graph *graph,
    const qa_vfs *view,uint64_t pool,uint64_t id,qa_vfs_acquisition *value)
{
    const qa_resource *resource=qa_application_content_resource(graph,pool,id);
    return qa_application_content_acquisition(io,graph,view,resource,value) ||
        state_fail(io,QA_ERROR_FORMAT,"Q3 artifact cannot reopen its installed content");
}

static uint32_t role_flags(const q3g_role *role)
{
    uint32_t flags = (role->initialized ? ROLE_INITIALIZED : 0) |
        (role->retired ? ROLE_RETIRED : 0) | (role->ready ? ROLE_READY : 0) |
        (role->primary ? ROLE_PRIMARY : 0) | (role->committed ? ROLE_COMMITTED : 0) |
        (role->init_succeeded ? ROLE_INIT_SUCCEEDED : 0);
    const q3g_restore *pending = role->engine->restoration;
    if (pending && role->engine->restore_pending)
        for (size_t i = 0; i < pending->role_count; ++i)
            if (pending->roles[i].actual == role)
                flags |= pending->roles[i].flags & (ROLE_INITIALIZED | ROLE_INIT_SUCCEEDED);
    return flags;
}

static bool game_topology(const q3g_role *role, qa_error *error)
{
    return (role && role->kind == QA_QVM_GAME && !role->local_client &&
        !role->native_client && !role->client_source && !role->client_engine &&
        role->client == UINT32_MAX && !role->source_owner) ||
        application_fail(error, QA_ERROR_FORMAT, "Q3 GAME changed its actual server authority");
}

static bool portable_executor(qa_bytes bytes, qa_error *error)
{
    qa_bytes host;
    return qa_qvm_checkpoint_host(bytes, &host, error) &&
        qa_q3_host_checkpoint_portable_state(host, error);
}

static bool saved_fields(qa_source_save_io *io, q3g_restore *saved, const application_provider *provider)
{
    const qa_application_content_graph *graph = qa_application_content_graph_read(provider->application);
    if (!graph) return state_fail(io, QA_ERROR_FORMAT, "Q3 continuation lacks its actual content graph");
    uint8_t magic[8] = {'Q','A','G','3','P','V',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','P','V',0,0};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) ||
        memcmp(magic, expected, sizeof(magic)) ||
        !qa_source_save_u32(io, &saved->product) || saved->product > QA_Q3_TEAM_ARENA ||
        !qa_source_save_u64(io, &saved->sequence) || !owned_text(io, &saved->entity_text) ||
        !blob(io, &saved->state, 8))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid coupled Q3 provider envelope");
    bool console = saved->cvars.size != 0;
    if (!qa_source_save_bool(io, &console) || (console && !blob(io, &saved->cvars, 12)) ||
        !qa_source_save_count(io, &saved->artifact_count, SIZE_MAX / sizeof(*saved->artifacts)))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid original GAME console continuation");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!saved->artifact_count || saved->artifact_count > (io->input.size - io->offset) / 80)
            return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 artifact inventory extent");
        saved->artifacts = calloc(saved->artifact_count, sizeof(*saved->artifacts));
        if (!saved->artifacts) return state_fail(io, QA_ERROR_MEMORY, "Restoring Q3 artifact inventory");
    }
    for (size_t i = 0; i < saved->artifact_count; ++i) {
        saved_artifact *artifact = &saved->artifacts[i];
        if (!owned_text(io, &artifact->path) || !artifact->path || !*artifact->path ||
            !qa_source_save_u32(io, &artifact->kind) || artifact->kind != QA_QVM_GAME ||
            !qa_source_save_u32(io, &artifact->abi) || artifact->abi > QA_QVM_Q3_116N ||
            !qa_source_save_bool(io, &artifact->qvm) ||
            (!artifact->qvm && !blob(io, &artifact->module, 12)) ||
            !qa_source_save_bytes(io, artifact->digest.bytes, 32) ||
            !qa_source_save_bytes(io, artifact->primary_digest.bytes, 32) ||
            !qa_source_save_u64(io, &artifact->pool) || !artifact->pool ||
            !qa_source_save_u64(io, &artifact->resource) || !artifact->resource)
            return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source artifact declaration");
        const qa_vfs *view = provider->launch->content;
        if (!acquisition(io, graph, view, artifact->pool, artifact->resource, &artifact->acquisition))
            return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source artifact acquisition");
        bool items = artifact->items_resource != 0;
        if (!qa_source_save_bool(io, &items) || (items &&
            (artifact->kind != QA_QVM_GAME ||
             !qa_source_save_u64(io, &artifact->items_pool) || !artifact->items_pool ||
             !qa_source_save_u64(io, &artifact->items_resource) || !artifact->items_resource ||
             !qa_source_save_bytes(io, artifact->items_digest.bytes, 32) ||
             !acquisition(io, graph, view, artifact->items_pool, artifact->items_resource, &artifact->items_acquisition) ||
             strcmp(artifact->items_acquisition.path, artifact->qvm ? "qvm-items.json" : "native-q3-items.json"))))
            return state_fail(io, QA_ERROR_FORMAT, "Invalid retained Q3 item catalog declaration");
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(artifact->path, saved->artifacts[j].path))
                return state_fail(io, QA_ERROR_FORMAT, "Duplicate Q3 immutable source artifact");
    }
    if (!qa_source_save_count(io, &saved->role_count, 129) || !saved->role_count ||
        !qa_source_save_count(io, &saved->game, saved->role_count))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source role inventory");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (saved->role_count > (io->input.size - io->offset) / 28)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated Q3 source role inventory");
        saved->roles = calloc(saved->role_count, sizeof(*saved->roles));
        if (!saved->roles) return state_fail(io, QA_ERROR_MEMORY, "Restoring Q3 source role inventory");
    }
    size_t primaries = 0, games = 0;
    for (size_t i = 0; i < saved->role_count; ++i) {
        saved_role *role = &saved->roles[i];
        if (!qa_source_save_count(io, &role->artifact, saved->artifact_count - 1) ||
            !qa_source_save_u32(io, &role->seat) ||
            !qa_source_save_u64(io, &role->sequence) || !role->sequence || role->sequence > saved->sequence ||
            !qa_source_save_u32(io, &role->owner) || !role->owner ||
            !qa_source_save_u32(io, &role->flags) ||
            (role->flags & ~(uint32_t)(ROLE_INITIALIZED | ROLE_RETIRED | ROLE_READY |
                ROLE_PRIMARY | ROLE_COMMITTED | ROLE_INIT_SUCCEEDED)) || !(role->flags & ROLE_READY) ||
            ((role->flags & ROLE_INIT_SUCCEEDED) && !(role->flags & ROLE_INITIALIZED)) ||
            ((role->flags & ROLE_INITIALIZED) &&
             (!(role->flags & ROLE_COMMITTED) || (role->flags & ROLE_RETIRED))))
            return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source role identity or lifecycle");
        const saved_artifact *artifact = &saved->artifacts[role->artifact];
        primaries += (role->flags & ROLE_PRIMARY) != 0;
        games += artifact->kind == QA_QVM_GAME;
        if (saved->game != i + 1 || !(role->flags & ROLE_PRIMARY))
            return state_fail(io, QA_ERROR_FORMAT, "Q3 source role changes its actual GAME authority");
        for (size_t j = 0; j < i; ++j) {
            const saved_role *prior = &saved->roles[j];
            if (role->sequence == prior->sequence || role->owner == prior->owner ||
                (artifact->kind == saved->artifacts[prior->artifact].kind && role->seat == prior->seat))
                return state_fail(io, QA_ERROR_FORMAT, "Duplicate Q3 source role owner, generation or seat");
        }
        if (!tokens(io, &role->arguments) || !blob(io, &role->input, artifact->qvm ? 13 : 0) ||
            !blob(io, &role->services, 12) ||
            !qa_source_save_count(io, &role->projection_count, SIZE_MAX / sizeof(*role->projections))) return false;
        if (!artifact->qvm && role->input.size)
            return state_fail(io, QA_ERROR_FORMAT, "Native Q3 role contains a QVM callback child");
        if (io->direction == QA_SOURCE_SAVE_READ && role->projection_count) {
            if (role->projection_count > (io->input.size - io->offset) / 26)
                return state_fail(io, QA_ERROR_FORMAT, "Truncated Q3 source projection inventory");
            role->projections = calloc(role->projection_count, sizeof(*role->projections));
            if (!role->projections) return state_fail(io, QA_ERROR_MEMORY, "Restoring Q3 source projection inventory");
        }
        for (size_t j = 0; j < role->projection_count; ++j) {
            saved_projection *projection = &role->projections[j];
            if (!qa_source_save_actor(io, &projection->actor) || !projection->actor.registry ||
                !qa_source_save_u32(io, &projection->slot) || projection->slot >= 64 ||
                !qa_source_save_bool(io, &projection->bound) ||
                !qa_source_save_u64(io, &projection->serial) || (projection->bound && !projection->serial))
                return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source projection actor or lease");
            for (size_t k = 0; k < j; ++k)
                if (qa_actor_id_equal(role->projections[k].actor, projection->actor))
                    return state_fail(io, QA_ERROR_FORMAT, "Duplicate Q3 source projection actor");
        }
        if (artifact->qvm) {
            if (!blob(io, &role->executor, 156) || !portable_executor(role->executor, io->error)) return false;
        } else {
            bool committed = (role->flags & ROLE_COMMITTED) != 0;
            if (!blob(io, &role->executor, committed ? 12 : 0) ||
                !blob(io, &role->native_host, committed ? 72 : 0) ||
                (!committed && (role->executor.size || role->native_host.size)) ||
                !blob(io, &role->resources, 12) || role->projection_count)
                return state_fail(io, QA_ERROR_FORMAT, "Native Q3 role changes its actual process/resource presence");
        }
    }
    return (primaries == 1 && games == 1 && console != level_only(provider->application)) ||
        state_fail(io, QA_ERROR_FORMAT, "Q3 source inventory has no unique primary/game role");
}

static bool native_role_plain(const q3g_role *role, qa_error *error)
{
    return (role && !role->vm && !role->image && role->module && !role->input &&
        !role->projection && !role->weapons && !role->weapon_services && !role->combat &&
        !role->pickups && !role->equipment && !role->body && !role->weapon_models) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q3 role retains an unencoded application callback owner");
}

static bool saved_collect(application_provider *provider,
    const qa_application_native_resource_refs *resources, const q3g_restore *expected,
    q3g_restore **out, qa_error *error)
{
    if (provider->kind != APPLICATION_PROVIDER_QVM &&
        !(provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 capture requires its actual original provider");
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_restore *saved = calloc(1, sizeof(*saved));
    if (!saved) return application_fail(error, QA_ERROR_MEMORY, "Capturing Q3 source inventory");
    saved->product = engine->product; saved->sequence = engine->role_sequence;
    saved->entity_text = engine->entity_text;
    qa_application_content_graph *graph = qa_application_content_graph_read(provider->application);
    if (!graph) { saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Q3 source capture lacks actual content inventory"); }
    for (q3g_artifact *a = engine->artifacts; a; a = a->next)
        if (a->kind == QA_QVM_GAME) ++saved->artifact_count;
    for (q3g_role *r = engine->roles; r; r = r->next)
        if (r->kind == QA_QVM_GAME) ++saved->role_count;
    if (!saved->artifact_count || !saved->role_count || saved->role_count > 129 ||
        saved->artifact_count > SIZE_MAX / sizeof(*saved->artifacts)) {
        saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Invalid actual Q3 source inventory extent");
    }
    saved->artifacts = calloc(saved->artifact_count, sizeof(*saved->artifacts));
    saved->roles = calloc(saved->role_count, sizeof(*saved->roles));
    if (!saved->artifacts || !saved->roles) {
        /* Counts describe allocated cleanup inventories only. */
        if (!saved->artifacts) saved->artifact_count = 0;
        if (!saved->roles) saved->role_count = 0;
        saved_free(saved); return application_fail(error, QA_ERROR_MEMORY, "Capturing Q3 source inventory rows");
    }
    size_t i = 0;
    for (q3g_artifact *a = engine->artifacts; a; a = a->next) {
        if (a->kind != QA_QVM_GAME) continue;
        if (a->qvm ? !a->image || a->module || a->declaration :
            !a->module || a->image || a->declaration || a->primary.size || a->equipment_presentation.size ||
            a->body_resource || a->body_profile.present || a->weapon_models_resource || a->weapon_models_profile.present ||
            a->collision_profile.present || a->collision_scene.size || a->grapple_profile || a->combat_profile) {
            saved_free(saved); return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 artifact lacks its actual immutable backend continuation");
        }
        if (a->grapple_profile && (a->kind != QA_QVM_GAME ||
            application_q3_grapple_profile_image(a->grapple_profile) != a->image ||
            strcmp(application_q3_grapple_profile_path(a->grapple_profile), a->path))) {
            saved_free(saved); return application_fail(error, QA_ERROR_FORMAT,
                "GAME grapple metadata leaves its retained immutable artifact");
        }
        if (!a->resource || !qa_resource_digest(a->resource)) {
            saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Q3 artifact lost its retained immutable bytes");
        }
        saved->artifacts[i] = (saved_artifact){.path = a->path, .kind = a->kind,
            .abi = a->abi, .qvm = a->qvm, .digest = *qa_resource_digest(a->resource), .actual = a,
            .acquisition = a->acquisition};
        if (!a->resource || !qa_vfs_acquisition_retained(a->view, &a->acquisition, error) ||
            !qa_application_content_resource_id(graph, a->resource,
                &saved->artifacts[i].pool, &saved->artifacts[i].resource)) {
            saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Q3 artifact leaves its actual opening inventory");
        }
        if (!a->qvm) {
            saved_artifact *row = saved->artifacts + i;
            if (!qa_native_module_checkpoint(a->module, qa_resource_bytes(a->resource), &row->module_storage, error)) {
                saved_free(saved); return false;
            }
            row->module = (qa_bytes){row->module_storage.data, row->module_storage.size};
        }
        qa_sha256((qa_bytes){a->primary.data, a->primary.size}, &saved->artifacts[i].primary_digest);
        if (a->items_resource) {
            saved_artifact *row = saved->artifacts + i;
            row->items_acquisition = a->items_acquisition;
            row->items_digest = *qa_resource_digest(a->items_resource);
            if (a->kind != QA_QVM_GAME || a->primary.size ||
                strcmp(a->items_acquisition.path, a->qvm ? "qvm-items.json" : "native-q3-items.json") ||
                qa_resource_id(a->items_resource) != a->items_acquisition.resource_id ||
                !qa_vfs_acquisition_retained(a->view, &a->items_acquisition, error) ||
                !qa_application_content_resource_id(graph, a->items_resource,
                    &row->items_pool, &row->items_resource) ||
                qa_application_content_pool(graph, row->items_pool) != qa_vfs_resources(a->view)) {
                saved_free(saved); return application_fail(error, QA_ERROR_FORMAT,
                    "Q3 item catalog leaves its actual retained opening");
            }
        }
        ++i;
    }
    i = 0;
    for (q3g_role *r = engine->roles; r; r = r->next) {
        if (r->kind != QA_QVM_GAME) continue;
        saved_role *row = &saved->roles[i];
        if (!game_topology(r, error) || r != engine->game || !r->primary || r->init_argument_count ||
            r->client != UINT32_MAX || r->source_owner || r->native_client || r->source_cleared) {
            saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Saved GAME changes its actual role authority");
        }
        uint32_t flags = role_flags(r);
        if ((r->artifact->qvm ? !r->vm || !r->image || r->native || r->module :
            !native_role_plain(r, error)) || r->activation_failed || r->arguments_scoped || r->shutdown_entry) {
            saved_free(saved); return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 source role has an unqualified native executor or command scope");
        }
        size_t a = 0;
        while (a < saved->artifact_count && saved->artifacts[a].actual != r->artifact) ++a;
        if (a == saved->artifact_count) {
            saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Q3 source role has no retained artifact owner");
        }
        *row = (saved_role){.artifact = a, .seat = r->seat,
            .flags = flags, .sequence = r->service_sequence, .owner = r->service_owner,
            .arguments = r->arguments, .actual = r};
        if (r == engine->game) saved->game = i + 1;
        for (guest_projection_actor *p = r->projection ? r->projection->actors : NULL; p; p = p->next)
            ++row->projection_count;
        if (row->projection_count > SIZE_MAX / sizeof(*row->projections)) {
            saved_free(saved); return application_fail(error, QA_ERROR_MEMORY, "Q3 source projection inventory is too large");
        }
        row->projections = row->projection_count ? calloc(row->projection_count, sizeof(*row->projections)) : NULL;
        if (row->projection_count && !row->projections) {
            saved_free(saved); return application_fail(error, QA_ERROR_MEMORY, "Capturing Q3 source projection leases");
        }
        size_t j = 0;
        for (guest_projection_actor *p = r->projection ? r->projection->actors : NULL; p; p = p->next, ++j) {
            if (p->projection != r->projection || p->inventory_prepared ||
                (p->inventory_bound && !qa_inventory_primary_current(provider->application->inventory,
                    p->inventory_lease, p)) ||
                (p->inventory_lease.serial ? !qa_actor_id_equal(p->actor, p->inventory_lease.actor) : p->inventory_lease.actor.registry != 0)) {
                saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Q3 source projection has no complete private lease");
            }
            row->projections[j] = (saved_projection){p->actor, p->slot, p->inventory_lease.serial, p->inventory_bound};
        }
        if (r->artifact->qvm) {
            if (!qa_q3_host_checkpoint_services(r->host, &row->service_storage, error) ||
                !application_guest_q3_functions_checkpoint(r, &row->input_storage, error) ||
                !qa_qvm_checkpoint(r->vm, &row->executor_storage, error)) { saved_free(saved); return false; }
        } else {
            if (!r->process.resources || ((r->native != NULL) != r->committed) ||
                (r->native && qa_native_get_backend(qa_native_host_instance(r->native)) != QA_NATIVE_BACKEND_OWNED_PROCESS)) {
                saved_free(saved); return application_fail(error, QA_ERROR_UNSUPPORTED,
                    "Native Q3 continuation requires its actual retained owned process and bridge");
            }
            if (!qa_q3_host_checkpoint_services(r->host, &row->service_storage, error) ||
                (r->native && (!qa_native_host_checkpoint(r->native, &row->native_host_storage, error) ||
                    !qa_native_process_checkpoint(qa_native_host_instance(r->native), &row->executor_storage, error)))) {
                saved_free(saved); return false;
            }
            const saved_role *prior = NULL;
            for (size_t k = 0; expected && k < expected->role_count; ++k)
                if (expected->roles[k].owner == row->owner) prior = expected->roles + k;
            if (prior) {
                qa_bytes lower = prior->lower_resources;
                const qa_native_process_resources *held = NULL;
                if (!lower.size && (!resources || !resources->resolve ||
                    !resources->resolve(resources->context, provider->launch->selection.instance,
                        row->owner, prior->resources, &held, &lower, error))) { saved_free(saved); return false; }
                if (!qa_native_process_resources_validate(r->process.resources, lower, error)) { saved_free(saved); return false; }
                row->resources = prior->resources;
            } else {
                if (!resources || !resources->capture || !resources->resolve || !resources->attach) {
                    saved_free(saved); return application_fail(error, QA_ERROR_ARGUMENT,
                        "Native Q3 role capture requires its historical external capability graph");
                }
                if (!resources->capture(resources->context, provider->launch->selection.instance,
                    row->owner, r->process.resources, &row->resource_storage, error)) { saved_free(saved); return false; }
                row->resources = (qa_bytes){row->resource_storage.data, row->resource_storage.size};
            }
            row->native_host = (qa_bytes){row->native_host_storage.data, row->native_host_storage.size};
        }
        row->services = (qa_bytes){row->service_storage.data, row->service_storage.size};
        row->input = (qa_bytes){row->input_storage.data, row->input_storage.size};
        row->executor = (qa_bytes){row->executor_storage.data, row->executor_storage.size};
        ++i;
    }
    if (!application_guest_q3_state_capture(provider, &saved->state_storage, error)) { saved_free(saved); return false; }
    saved->state = (qa_bytes){saved->state_storage.data, saved->state_storage.size};
    qa_cvars *cvars = application_guest_q3_console_registry(provider);
    if ((cvars != NULL) != (engine->game != NULL) ||
        (cvars && qa_cvars_find(cvars, "sv_cheats"))) {
        saved_free(saved);
        return application_fail(error, QA_ERROR_FORMAT, "Original GAME console changes its actual engine ownership");
    }
    if (cvars && !level_only(provider->application)) {
        if (expected) {
            if (!qa_cvars_save_matches(cvars, expected->cvars, error)) { saved_free(saved); return false; }
            saved->cvars = expected->cvars;
        } else {
            if (!qa_cvars_save_capture(cvars, &saved->cvar_storage, error)) { saved_free(saved); return false; }
            saved->cvars = (qa_bytes){saved->cvar_storage.data, saved->cvar_storage.size};
        }
    }
    *out = saved; return true;
}

static bool clients_agree(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    const struct application_player_roster *roster = provider->application->players;
    for (uint32_t i = 0; i < 64; ++i) {
        const q3g_client *client = engine->clients + i;
        if (!client->actor.registry) continue;
        const qa_actor_record *actor = qa_actors_get(qa_session_actors(provider->application->session), client->actor);
        if (!actor) {
            if (!client->pending_retirement)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 source client has unqualified retired actor provenance");
            continue;
        }
        uint32_t slot;
        if (!engine->game || !qa_q3_host_actor_slot(engine->game->host, client->actor, &slot, error) || slot != i)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 source client disagrees with its actual host actor slot");
        bool retired;
        if (!qa_q3_host_checkpoint_input_retired(engine->game->host, i, &retired, error) ||
            retired != client->pending_retirement)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 pending source retirement changes its original input admission");
        if (client->connected && !client->bot) {
            uint32_t seat;
            if (!qa_application_player_seat(provider->application, client->actor, &seat) || engine->seats[i] != seat)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 source human client changes its canonical roster seat");
        }
        if (client->roster_attached) {
            bool admitted = false;
            for (size_t j = 0; roster && j < roster->count; ++j) {
                const application_player_record *player = roster->records + j;
                if (!qa_actor_id_equal(player->actor, client->actor)) continue;
                for (size_t k = 0; k < player->guest_count; ++k)
                    if (player->guests[k].owner == provider->owner && player->guests[k].source_slot == i &&
                        qa_sha256_equal(&player->guests[k].identity, &provider->launch->identity)) admitted = true;
            }
            if (!admitted)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 source bot attachment is absent from its exact saved roster");
        }
    }
    return true;
}

static bool capture_body(application_provider *provider,
    const qa_application_native_resource_refs *resources, const q3g_restore *expected,
    qa_buffer *out, qa_error *error)
{
    if (!out || !owner(provider, error)) return false;
    q3g_restore *saved = NULL;
    qa_source_save_io io = {0};
    bool ok = clients_agree(provider, error) && saved_collect(provider, resources, expected, &saved, error) &&
        qa_source_save_writer(&io, provider->application->session, error) &&
        saved_fields(&io, saved, provider) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); saved_free(saved);
    return ok;
}

bool application_guest_q3_save_capture(application_provider *provider,
    const qa_application_native_resource_refs *resources, qa_buffer *out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->restore_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source continuation is not published");
    if (q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME) {
        if (!out || !owner(provider, error)) return false;
        *out = (qa_buffer){0};
        return true;
    }
    return capture_body(provider, resources, NULL, out, error);
}

static bool equal_bytes(qa_bytes a, qa_bytes b)
{
    return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}

bool application_guest_q3_save_actor_client(application_provider *provider,
    qa_actor_id actor, uint32_t *slot)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    const q3g_restore *saved = engine ? engine->restoration : NULL;
    if (!slot || !saved || !saved->imported || !engine->restore_pending ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        provider->application->operation != APPLICATION_PERSISTING || engine->calls ||
        engine->draining_clients || !engine->game || !engine->game->host ||
        !qa_q3_host_restore_pending(engine->game->host) ||
        !qa_q3_host_checkpoint_portable_ready(engine->game->host, NULL)) return false;
    const saved_role *game = NULL;
    for (size_t i = 0; i < saved->role_count; ++i)
        if (saved->roles[i].actual == engine->game) {
            if (game) return false;
            game = saved->roles + i;
        }
    uint32_t required = ROLE_READY | ROLE_PRIMARY | ROLE_COMMITTED |
        ROLE_INITIALIZED | ROLE_INIT_SUCCEEDED;
    if (!game || (game->flags & required) != required || (game->flags & ROLE_RETIRED) ||
        game->owner != engine->game->service_owner || game->sequence != engine->game->service_sequence ||
        game->artifact >= saved->artifact_count ||
        saved->artifacts[game->artifact].actual != engine->game->artifact ||
        saved->artifacts[game->artifact].kind != QA_QVM_GAME) return false;
    uint32_t actual;
    if (!qa_q3_host_actor_slot(engine->game->host, actor, &actual, NULL) || actual >= 64) return false;
    const q3g_client *client = engine->clients + actual;
    if (!client->allocated || !client->connected || !client->begun || client->pending_retirement ||
        !qa_actor_id_equal(client->actor, actor)) return false;
    *slot = actual;
    return true;
}

bool application_guest_q3_save_matches(application_provider *provider, qa_bytes bytes,
    const qa_application_native_resource_refs *resources, qa_error *error)
{
    qa_bytes body;
    if (!application_guest_checkpoint_body(provider, bytes, &body, error)) return false;
    if (q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME)
        return !body.size || application_fail(error, QA_ERROR_FORMAT, "CLIENT provider contains private presentation state");
    q3g_restore *expected = calloc(1, sizeof(*expected));
    if (!expected) return application_fail(error, QA_ERROR_MEMORY, "Qualifying native Q3 capability references");
    expected->owns_text = true;
    qa_source_save_io io = {0};
    qa_buffer actual = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session, body, error) &&
        saved_fields(&io, expected, provider) && qa_source_save_finish(&io, NULL) &&
        capture_body(provider, resources, expected, &actual, error) &&
        equal_bytes(body, (qa_bytes){actual.data, actual.size});
    qa_source_save_dispose(&io);
    qa_buffer_free(&actual);
    saved_free(expected);
    return ok || application_fail(error, QA_ERROR_FORMAT, "Q3 retained provider continuation changed");
}

bool application_guest_q3_native_restore_recipe(q3g_role *role,
    const qa_native_process_resources **capture, qa_bytes *recipe, qa_bytes *executor, qa_error *error)
{
    q3g_restore *saved = role && role->engine ? role->engine->restoration : NULL;
    application_provider *provider = role && role->engine ? role->engine->provider : NULL;
    const qa_application_native_resource_refs *refs = provider ? provider->application->native_restore_resources : NULL;
    if (!saved || !capture || !recipe || !executor || !refs || !refs->resolve ||
        !role->engine->restore_pending || provider->application->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 restore requires its retained decoded capability graph");
    saved_role *found = NULL;
    for (size_t i = 0; i < saved->role_count; ++i)
        if (saved->roles[i].owner == role->service_owner) {
            if (found) return application_fail(error, QA_ERROR_FORMAT, "Native Q3 role capability owner is duplicated");
            found = saved->roles + i;
        }
    if (!found || found->sequence != role->service_sequence || found->seat != role->seat ||
        found->artifact >= saved->artifact_count || saved->artifacts[found->artifact].actual != role->artifact ||
        saved->artifacts[found->artifact].qvm)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q3 role leaves its decoded artifact and capability recipe");
    if (!refs->resolve(refs->context, provider->launch->selection.instance, found->owner,
        found->resources, capture, recipe, error)) return false;
    if (!recipe->data || !recipe->size)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q3 capability resolver returned no qualified recipe");
    found->lower_resources = *recipe;
    *executor = found->executor;
    return true;
}

static bool qualify_content(application_provider *provider, q3g_restore *saved, qa_error *error)
{
    qa_application_content_graph *graph = qa_application_content_graph_read(provider->application);
    if (!graph) return application_fail(error, QA_ERROR_FORMAT, "Restored Q3 lacks its actual content graph");
    for (size_t i = 0; i < saved->artifact_count; ++i) {
        saved_artifact *a = saved->artifacts + i;
        qa_vfs *view = provider->launch->content;
        const qa_resource *resource = qa_application_content_resource(graph, a->pool, a->resource);
        if (!view || !resource || qa_application_content_pool(graph, a->pool) != qa_vfs_resources(view) ||
            !qa_sha256_equal(qa_resource_digest(resource), &a->digest) ||
            strcmp(a->path, a->acquisition.path) || !qa_vfs_acquisition_retained(view, &a->acquisition, error))
            return application_fail(error, QA_ERROR_FORMAT, "Q3 artifact changes its true opening recipe");
        if (a->items_resource) {
            const qa_resource *items = qa_application_content_resource(graph, a->items_pool, a->items_resource);
            if (!items || qa_application_content_pool(graph, a->items_pool) != qa_vfs_resources(view) ||
                !qa_sha256_equal(qa_resource_digest(items), &a->items_digest) ||
                !qa_vfs_acquisition_retained(view, &a->items_acquisition, error))
                return application_fail(error, QA_ERROR_FORMAT,
                    "Q3 item catalog changes its retained source view or bytes");
        }
    }
    return true;
}

static bool prepare_artifact(application_provider *provider, struct application_q3_guest *engine,
                              saved_artifact *saved, bool primary, qa_error *error)
{
    q3g_artifact *artifact = calloc(1, sizeof(*artifact));
    if (!artifact) return application_fail(error, QA_ERROR_MEMORY, "Restoring immutable Q3 artifact owner");
    artifact->path = q3g_copy_text(saved->path, error);
    if (!artifact->path) { free(artifact); return false; }
    artifact->kind = (qa_qvm_role)saved->kind; artifact->qvm = saved->qvm;
    /* Attach before source qualification so every partial retained image has
     * the same lifetime as the isolated provider. */
    q3g_artifact **tail = &engine->artifacts;
    while (*tail) tail = &(*tail)->next;
    *tail = artifact; saved->actual = artifact;
    const qa_launch_instance *source = provider->launch;
    artifact->view = source->content;
    if (!qa_launch_instance_retain_metadata(source, &artifact->descriptor, error)) return false;
    qa_qvm_compatibility compatibility = {0};
    bool ok;
    artifact->resource = (qa_resource *)qa_application_content_resource(
        qa_application_content_graph_read(provider->application), saved->pool, saved->resource);
    qa_resource_retain(artifact->resource);
    if (!artifact->resource || !qa_vfs_acquisition_copy(&saved->acquisition, &artifact->acquisition, error)) return false;
    if (saved->items_resource) {
        artifact->items_resource = (qa_resource *)qa_application_content_resource(
            qa_application_content_graph_read(provider->application), saved->items_pool, saved->items_resource);
        qa_resource_retain(artifact->items_resource);
        if (!artifact->items_resource ||
            !qa_vfs_acquisition_copy(&saved->items_acquisition, &artifact->items_acquisition, error)) return false;
    }
    if (!saved->qvm) {
        qa_sha256_digest empty;
        qa_sha256((qa_bytes){0}, &empty);
        if (!qa_sha256_equal(&saved->primary_digest, &empty))
            return application_fail(error, QA_ERROR_FORMAT, "Native Q3 artifact contains a QVM-only declaration child");
        if (!qa_native_module_restore(saved->module, qa_resource_bytes(artifact->resource),
            &artifact->module, error)) return false;
        artifact->abi = (qa_qvm_abi)saved->abi;
        qa_native_module_info actual = qa_native_module_describe(artifact->module);
        if (actual.profile != QA_NATIVE_Q3_VMMAIN || !actual.source || strcmp(actual.source, artifact->path) ||
            artifact->abi != QA_QVM_Q3_MODERN || !qa_sha256_equal(&actual.image.digest, &saved->digest))
            return application_fail(error, QA_ERROR_FORMAT, "Native Q3 cache changed its actual module profile, opening or ABI");
        if (primary && source == provider->launch) {
            if (provider->kind != APPLICATION_PROVIDER_NATIVE || provider->state.native.module)
                return application_fail(error, QA_ERROR_FORMAT, "Native Q3 primary cache changed its actual provider backend");
            qa_native_module_retain(artifact->module);
            provider->state.native.module = artifact->module;
        }
        return true;
    }
    if (primary && source == provider->launch) {
        artifact->image = provider->state.qvm.image;
        qa_qvm_image_retain(artifact->image);
        ok = q3g_compatibility(source, saved->path, artifact->image, artifact->kind, primary, &compatibility, error);
    } else {
        ok = qa_qvm_image_load(qa_resource_bytes(artifact->resource), &artifact->image, error) &&
            q3g_compatibility(source, saved->path, artifact->image, artifact->kind,
                !strcmp(saved->path, source->selection.artifact), &compatibility, error);
    }
    if (ok) {
        qa_sha256_digest primary_digest;
        qa_sha256((qa_bytes){compatibility.primary.data, compatibility.primary.size}, &primary_digest);
        ok = compatibility.abi == (qa_qvm_abi)saved->abi &&
            qa_sha256_equal(qa_qvm_image_digest(artifact->image), &saved->digest) &&
            qa_sha256_equal(&primary_digest, &saved->primary_digest);
        if (!ok) application_fail(error, QA_ERROR_FORMAT, "Q3 artifact or source declaration differs from saved content");
    }
    if (ok) {
        artifact->abi = compatibility.abi;
        artifact->primary = compatibility.primary; compatibility.primary = (qa_buffer){0};
        if (ok && artifact->kind == QA_QVM_GAME)
            ok = application_q3_grapple_profile_create(artifact->image, artifact->kind,
                artifact->abi, artifact->path, &artifact->grapple_profile, error);
        if (ok && artifact->kind == QA_QVM_GAME)
            ok = application_q3_combat_profile_create(artifact->image, artifact->kind,
                artifact->abi, artifact->path, (qa_bytes){artifact->primary.data, artifact->primary.size},
                qa_session_strings(provider->application->session), &artifact->combat_profile, error);
    }
    qa_qvm_compatibility_free(&compatibility);
    return ok;
}

static bool qualify_base(application_provider *provider, qa_world *world, q3g_restore *saved,
                          const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    struct application_q3_guest *base = calloc(1, sizeof(*base));
    if (!base) return application_fail(error, QA_ERROR_MEMORY, "Qualifying Q3 application source state");
    base->product = !strcmp(product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session, saved->state, error) &&
        fields(&io, base) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok && (saved->product != (uint32_t)base->product || saved->sequence != base->role_sequence ||
        ((saved->entity_text != NULL) != (base->entity_text != NULL)) ||
        (saved->entity_text && strcmp(saved->entity_text, base->entity_text))))
        ok = application_fail(error, QA_ERROR_FORMAT, "Q3 provider envelope changes its nested source identity");
    for (size_t i = 0; ok && i < choices->seat_count; ++i) {
        if (base->seats[i] != choices->seats[i].id)
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 source seat inventory differs from restored configuration");
    }
    if (ok && base->entity_text) {
        const qa_bsp_view *map = qa_collision_bsp(qa_world_geometry(world));
        qa_bytes source = map ? map->lumps[QA_BSP_ENTITIES].bytes : (qa_bytes){0};
        const uint8_t *end = source.size ? memchr(source.data, 0, source.size) : NULL;
        size_t length = end ? (size_t)(end - source.data) : source.size;
        if (!map || strlen(base->entity_text) != length ||
            (length && memcmp(base->entity_text, source.data, length)))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 entity parser belongs to different immutable map source text");
    }
    if (ok) {
        memcpy(saved->seats, base->seats, sizeof(saved->seats));
    }
    q3g_clients_clear(base); qa_command_tokens_free(&base->arguments);
    free(base->entity_text); free(base);
    return ok;
}

static bool restore_client_sources(application_provider *provider,
    struct application_q3_guest *engine, qa_error *error)
{
    for (size_t index = 0;; ++index) {
        qa_application_startup_source source;
        if (!application_guest_q3_client_console_source(engine, index, &source)) break;
        if (!application_startup_tuple_restore(provider, &source, error)) return false;
    }
    return true;
}

bool application_guest_q3_save_prepare(application_provider *provider, qa_world *world,
    const qa_product *product, const qa_launch_choices *choices, const qa_save_record *record, qa_error *error)
{
    if (!provider || !provider->application || !provider->launch || !product || !choices || !record ||
        !world || product->family != QA_GAME_Q3 || choices->seat_count > 64 ||
        (provider->kind != APPLICATION_PROVIDER_QVM && provider->kind != APPLICATION_PROVIDER_NATIVE) ||
        (provider->kind == APPLICATION_PROVIDER_QVM ? !provider->state.qvm.image : provider->state.native.module != NULL) ||
        q3g_engine(provider) ||
        provider->application->operation != APPLICATION_PERSISTING ||
        record->owner.kind != QA_SAVE_PROVIDER || !record->owner.instance ||
        strcmp(record->owner.instance, provider->launch->selection.instance) ||
        !record->owner.schema || strcmp(record->owner.schema,
            provider->kind == APPLICATION_PROVIDER_QVM ? "qa.q3.qvm" : "qa.q3.external-native") ||
        !record->owner.backend || strcmp(record->owner.backend,
            provider->kind == APPLICATION_PROVIDER_QVM ? "qvm" : "native-owned"))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 restored construction requires its qualified provider record");
    qa_bytes payload = record->payload, body;
    if (!payload.data || payload.size < 28 || memcmp(payload.data, "QAPV", 4) ||
        qa_load_u32le(payload.data + 4) != (uint32_t)provider->kind ||
        qa_load_u32le(payload.data + 8) > 1 || qa_load_u64le(payload.data + 20) != payload.size - 28 ||
        !application_guest_checkpoint_body(provider, (qa_bytes){payload.data + 28, payload.size - 28}, &body, error))
        return application_fail(error, QA_ERROR_FORMAT, "Q3 restored provider wrapper differs from its actual source");
    if (q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME) {
        if (body.size) return application_fail(error, QA_ERROR_FORMAT, "CLIENT provider contains private presentation state");
        return application_construct_q3_guest(provider->application, provider, world, product, choices, error);
    }
    q3g_restore *saved = calloc(1, sizeof(*saved));
    if (!saved) return application_fail(error, QA_ERROR_MEMORY, "Retaining Q3 provider constructor state");
    saved->owns_text = true; saved->storage.data = malloc(body.size); saved->storage.size = body.size;
    if (!saved->storage.data) { saved_free(saved); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q3 provider bytes"); }
    memcpy(saved->storage.data, body.data, body.size);
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session,
        (qa_bytes){saved->storage.data, saved->storage.size}, error) &&
        saved_fields(&io, saved, provider) && qa_source_save_finish(&io, NULL) &&
        qualify_base(provider, world, saved, product, choices, error) &&
        qualify_content(provider, saved, error);
    qa_source_save_dispose(&io);
    size_t primary = SIZE_MAX;
    for (size_t i = 0; ok && i < saved->role_count; ++i) {
        saved_role *role = saved->roles + i;
        saved_artifact *artifact = saved->artifacts + role->artifact;
        const qa_launch_instance *source = provider->launch;
        if (role->flags & ROLE_PRIMARY) {
            primary = role->artifact;
            uint32_t seat = choices->seat_count ? choices->seats[0].id : UINT32_MAX;
            if (artifact->qvm != (provider->kind == APPLICATION_PROVIDER_QVM) ||
                strcmp(artifact->path, source->selection.artifact) ||
                artifact->kind != (uint32_t)q3g_primary_role(source->selection.artifact) ||
                role->seat != seat)
                ok = application_fail(error, QA_ERROR_FORMAT, "Q3 primary role differs from its selected source artifact");
        }
        char identity[65], name[160]; qa_sha256_hex(&source->identity, identity);
        snprintf(name, sizeof(name), "q3-service:%u:%s:%llu", provider->owner, identity,
            (unsigned long long)role->sequence);
        if (qa_strings_find(qa_session_strings(provider->application->session),
            (qa_bytes){(const uint8_t *)name, strlen(name)}) != role->owner)
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 role lifetime owner leaves its actual restored string registry");
    }
    if (!ok) { saved_free(saved); return false; }
    if (!application_guest_q3_create_empty(provider->application, provider, world, product, choices, true, error)) {
        saved_free(saved); return false;
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    engine->restoration = saved; engine->role_sequence = saved->sequence;
    qa_cvars *cvars = application_guest_q3_console_registry(provider);
    if ((cvars != NULL) != (saved->game != 0))
        return application_fail(error, QA_ERROR_FORMAT, "Restored original GAME has another private console owner");
    if (!application_guest_q3_client_consoles_prepare(engine, choices, error)) return false;
    memcpy(engine->seats, saved->seats, sizeof(engine->seats));
    if (saved->entity_text && !(engine->entity_text = q3g_copy_text(saved->entity_text, error))) return false;
    for (size_t i = 0; i < saved->artifact_count; ++i)
        if (!prepare_artifact(provider, engine, saved->artifacts + i, i == primary, error)) return false;
    /* Service heaps were originally admitted in registration order, even
     * though the role list itself is prepended by ordinary role creation. */
    for (size_t n = 0; n < saved->role_count; ++n) {
        saved_role *next = NULL;
        for (size_t i = 0; i < saved->role_count; ++i)
            if (!saved->roles[i].actual && (!next || saved->roles[i].sequence < next->sequence)) next = saved->roles + i;
        saved_artifact *artifact = saved->artifacts + next->artifact;
        q3g_role *role = NULL;
        bool created = q3g_role_create_restored(engine, (qa_qvm_role)artifact->kind, next->seat, artifact->path,
            (next->flags & ROLE_PRIMARY) != 0, next->sequence, next->owner, NULL, &role, error);
        if (!created) return false;
        next->actual = role; role->next = engine->roles; engine->roles = role;
        if (!game_topology(role, error)) return false;
        if (artifact->kind == QA_QVM_GAME) {
            engine->game = role; q3g_game_aliases(engine, role);
            if (!restore_client_sources(provider, engine, error)) return false;
            if ((next->flags & ROLE_COMMITTED) && application_bots_guest_runtime(provider->application, provider) &&
                !application_bots_guest_bind(provider->application, provider, role->host, error)) return false;
        }
    }
    engine->roles = saved->roles[0].actual;
    for (size_t i = 0; i < saved->role_count; ++i)
        saved->roles[i].actual->next = i + 1 < saved->role_count ? saved->roles[i + 1].actual : NULL;
    return true;
}

bool application_guest_q3_save_declarations(application_provider *provider,
    const qa_application_options *options, const qa_application_persistence_ops *ops, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME) return true;
    q3g_restore *saved = engine->restoration;
    qa_application *app = provider->application;
    qa_cvars *cvars = application_guest_q3_console_registry(provider);
    if (!saved || saved->imported || !engine->restore_pending || !engine->game || !cvars ||
        !saved->game || saved->game > saved->role_count || saved->roles[saved->game - 1].actual != engine->game ||
        !options || !ops || !owner(provider, error) || app->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 declarations require their cold actual GAME candidate");
    qa_application_native_baseline_services *services = NULL;
    struct application_native_q2_scratch *scratch = NULL;
    bool ok = true;
    if (saved->roles[saved->game - 1].flags & ROLE_INITIALIZED) {
        ok = application_native_q2_baseline_services_prepare(app, options, &services, error);
        if (ok && ops->prepare_native_baseline)
            ok = ops->prepare_native_baseline(ops->context, app, provider->owner, services, error);
        if (ok) ok = application_source_baseline_prepare(provider, qa_application_launch(app), services, &scratch, error);
        application_provider *source = application_source_baseline_provider(scratch);
        qa_bsp_view map;
        if (ok) ok = qa_bsp_open(qa_resource_bytes(app->map_resource), &map, error) &&
            qa_map_sidecars_apply_entities(app->map_sidecars, &map, error) &&
            application_q3_guest_spawn_map(source, &map, NULL, source->application->current_map, QA_STRING_NONE, error) &&
            qa_cvars_copy_declarations(cvars, application_guest_q3_console_registry(source), error) &&
            application_source_baseline_ready(scratch, error);
    }
    /* The separate normal Source supplies current declarations and reset
     * defaults. Saved values never become registration metadata. */
    application_provider *current=application_save_current_provider(provider);
    qa_buffer settings={0};
    qa_bytes values=saved->cvars;
    if (ok && current) {
        qa_cvars *current_cvars=current->kind==provider->kind
            ? application_guest_q3_console_registry(current) : NULL;
        ok=current_cvars && qa_cvars_save_capture(current_cvars,&settings,error);
        if (ok) values=(qa_bytes){settings.data,settings.size};
        else if (!error || error->code==QA_OK)
            application_fail(error,QA_ERROR_FORMAT,"Q3 level restore lacks its current GAME registry");
    }
    qa_cvars_restore *ticket = NULL;
    if (ok) ok = qa_cvars_save_prepare(cvars, values, &ticket, error) &&
        qa_cvars_save_commit(ticket, error);
    if (!ok) qa_cvars_save_abort(ticket);
    qa_buffer_free(&settings);
    if (ok && qa_cvars_find(cvars, "sv_cheats"))
        ok = application_fail(error, QA_ERROR_FORMAT, "Restored original GAME shadows shared engine sv_cheats");
    if (ok && scratch) ok = application_native_q2_baselines_destroy(app, error);
    return ok;
}

bool application_guest_q3_save_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_restore *saved = engine ? engine->restoration : NULL;
    if (engine && q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME)
        return !bytes.size || application_fail(error, QA_ERROR_FORMAT, "CLIENT provider contains private presentation state");
    if (!saved || saved->imported || !engine->restore_pending || !owner(provider, error) ||
        provider->application->operation != APPLICATION_PERSISTING ||
        !equal_bytes(bytes, (qa_bytes){saved->storage.data, saved->storage.size}))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source import requires its isolated prepared record");
    if (!application_guest_q3_state_restore(provider, saved->state, error)) return false;
    for (size_t i = 0; i < saved->role_count; ++i) {
        saved_role *row = saved->roles + i; q3g_role *role = row->actual;
        if (!game_topology(role, error)) return false;
        if (role->vm) {
            if (!portable_executor(row->executor, error) ||
                !application_guest_q3_functions_restore(role, row->input, row->executor, error) ||
                !qa_qvm_restore_candidate(role->vm, row->executor, error) ||
                !q3g_role_catalog_refresh(role, error)) return false;
        } else if (row->flags & ROLE_COMMITTED) {
            if (!role->native || !qa_native_process_restore_pending(qa_native_host_instance(role->native)) ||
                !qa_native_process_restore_host(qa_native_host_instance(role->native), row->native_host, error)) return false;
            if ((qa_native_get_lifecycle(qa_native_host_instance(role->native)) == QA_NATIVE_INITIALIZED) !=
                ((row->flags & ROLE_INITIALIZED) != 0))
                return application_fail(error, QA_ERROR_FORMAT, "Native Q3 role Init state differs from its actual process lifecycle");
            if (role->catalog && (row->flags & ROLE_INITIALIZED) &&
                !application_q3_catalog_native_restore_validate(role->catalog, role, error)) return false;
        } else if (role->native || row->executor.size || row->native_host.size)
            return application_fail(error, QA_ERROR_FORMAT, "Uncommitted native Q3 role acquired an executor during import");
        qa_command_tokens_free(&role->arguments);
        role->arguments = row->arguments; row->arguments = (qa_command_tokens){0};
        role->retired = (row->flags & ROLE_RETIRED) != 0;
        role->committed = (row->flags & ROLE_COMMITTED) != 0;
        if (row->projection_count && (!role->projection || !role->projection->has_inventory))
            return application_fail(error, QA_ERROR_FORMAT, "Q3 source projections lack their actual immutable declaration");
        guest_projection_actor **tail = role->projection ? &role->projection->actors : NULL;
        for (size_t j = 0; j < row->projection_count; ++j) {
            saved_projection *record = row->projections + j;
            if (record->bound) {
                uint32_t slot;
                if (!qa_q3_host_actor_slot(role->host, record->actor, &slot, error) || slot != record->slot)
                    return application_fail(error, QA_ERROR_FORMAT, "Q3 source projection changes its selected actor or client slot");
            }
            guest_projection_actor *context = calloc(1, sizeof(*context));
            if (!context) return application_fail(error, QA_ERROR_MEMORY, "Restoring Q3 private inventory lease contexts");
            *context = (guest_projection_actor){.projection = role->projection, .actor = record->actor,
                .slot = record->slot, .inventory_bound = record->bound, .inventory_prepared = record->bound,
                .inventory_lease = {record->serial ? record->actor : (qa_actor_id){0}, record->serial}};
            *tail = context; tail = &context->next;
        }
    }
    saved->imported = true;
    return true;
}

bool application_guest_q3_save_finish(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_restore *saved = engine ? engine->restoration : NULL;
    if (engine && q3g_primary_role(provider->launch->selection.artifact) != QA_QVM_GAME)
        return !engine->restore_pending && owner(provider, error);
    if (!saved || !saved->imported || !engine->restore_pending || !owner(provider, error) ||
        !qa_world_idle(engine->world) || provider->application->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source finish requires its imported candidate continuation");
    if (!clients_agree(provider, error) || !application_portals_validate(provider->application, error)) return false;
    /* The application coordinator must have validated the complete WORLD,
     * collective portal contributors, bots and roster first.
     * Engine entry stays blocked throughout this final byte qualification. */
    for (size_t i = 0; i < saved->role_count; ++i) {
        saved_role *row = saved->roles + i; q3g_role *role = row->actual;
        if (!game_topology(role, error)) return false;
        if (role->projection)
            for (guest_projection_actor *context = role->projection->actors; context; context = context->next)
                if (context->inventory_prepared || (context->inventory_bound &&
                    (!qa_inventory_primary_current(provider->application->inventory, context->inventory_lease, context) ||
                     application_provider_for(provider->application, context->actor, QA_ROLE_INVENTORY, NULL) != provider)))
                    return application_fail(error, QA_ERROR_FORMAT, "Q3 source primary lease was not restored exactly");
        qa_buffer services = {0};
        bool ok = qa_q3_host_checkpoint_services(role->host, &services, error) &&
            equal_bytes(row->services, (qa_bytes){services.data, services.size});
        qa_buffer_free(&services);
        if (!ok) return application_fail(error, QA_ERROR_FORMAT, "Q3 host service inventory differs from its original source owner");
    }
    for (size_t i = 0; i < saved->role_count; ++i)
        if (!qa_q3_host_finish_restore(saved->roles[i].actual->host, error)) return false;
    for (size_t i = 0; i < saved->role_count; ++i) {
        q3g_role *role = saved->roles[i].actual;
        if (role->module && !qa_native_process_resources_options_read(role->process.resources,
            &role->process.process, error)) return false;
    }
    qa_buffer actual = {0};
    bool ok = capture_body(provider, provider->application->native_restore_resources, saved, &actual, error) &&
        equal_bytes((qa_bytes){actual.data, actual.size}, (qa_bytes){saved->storage.data, saved->storage.size});
    qa_buffer_free(&actual);
    if (!ok) return application_fail(error, QA_ERROR_FORMAT, "Q3 candidate source continuation differs after owner reconnection");
    for (size_t i = 0; i < saved->role_count; ++i) {
        saved->roles[i].actual->initialized = (saved->roles[i].flags & ROLE_INITIALIZED) != 0;
        saved->roles[i].actual->init_succeeded = (saved->roles[i].flags & ROLE_INIT_SUCCEEDED) != 0;
    }
    engine->restore_pending = false;
    application_guest_q3_save_clear(engine);
    return true;
}
