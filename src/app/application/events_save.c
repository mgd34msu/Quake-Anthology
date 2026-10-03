#include "events_save.h"
#include "match_intents.h"
#include "rankings.h"
#include "qa/source_save.h"
#include "unified_output.h"
#include "save_content.h"
#include "qa/json.h"
#include "qa/application_network_q2.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct event_store {
    application_event_record *builtin;
    application_q2_map_event_record *q2_map;
    qa_application_q3_map_event *q3_map;
    qa_application_q2_player_event *q2_player;
    application_protocol_record *protocol;
    size_t counts[5], capacities[5];
    size_t arena_block_size;
    uint64_t protocol_generation;
    application_event_journal_record *journal;
    size_t journal_count, journal_capacity;
    uint64_t sequence;
    uint64_t simulation_sequence;
    application_unified_event_record *unified;
    size_t unified_count, unified_capacity;
    uint64_t unified_sequence, presentation_sequence;
    application_unified_persistent_event *persistent;
    size_t persistent_count, persistent_capacity;
    uint64_t persistent_revision;
    application_unified_event_owner *owners;
    size_t owner_count, owner_capacity;
    uint64_t owner_generation;
    application_unified_event_resource *resources;
    size_t resource_count, resource_capacity;
    application_unified_event_registration *registrations;
    size_t registration_count, registration_capacity;
    uint64_t registration_revision;
    application_unified_world_text *world_text;
    size_t world_text_count, world_text_capacity;
    uint64_t world_text_revision, world_text_map;
    qa_application *application;
    qa_arena arena;
} event_store;

static const uint8_t event_magic[8] = {'Q','A','E','V','T','S',0,0};
static const size_t event_widths[5] = {
    sizeof(application_event_record), sizeof(application_q2_map_event_record),
    sizeof(qa_application_q3_map_event), sizeof(qa_application_q2_player_event),
    sizeof(application_protocol_record)
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
    return qa_source_save_bytes(io, magic, sizeof(magic)) &&
        (!memcmp(magic, event_magic, sizeof(magic)) ||
         event_fail(io, QA_ERROR_FORMAT, "Invalid application event signature"));
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
    if (!qa_source_save_u64(io, &store->sequence) ||
        store->sequence > QA_UNIFIED_SAFE_INTEGER ||
        !qa_source_save_u64(io, &store->simulation_sequence) ||
        store->simulation_sequence > QA_UNIFIED_SAFE_INTEGER ||
        !qa_source_save_count(io, &store->journal_capacity, SIZE_MAX / sizeof(*store->journal)) ||
        !qa_source_save_count(io, &store->journal_count, store->journal_capacity))
        return event_fail(io, QA_ERROR_FORMAT, "Invalid Source event journal extent");
    if (!qa_source_save_u64(io, &store->unified_sequence) || store->unified_sequence > QA_UNIFIED_SAFE_INTEGER ||
        !qa_source_save_u64(io, &store->presentation_sequence) || store->presentation_sequence > QA_UNIFIED_SAFE_INTEGER ||
        !qa_source_save_count(io, &store->unified_capacity, SIZE_MAX / sizeof(*store->unified)) ||
        !qa_source_save_count(io, &store->unified_count, store->unified_capacity) ||
        store->unified_count > store->unified_sequence ||
        !qa_source_save_count(io, &store->persistent_capacity, SIZE_MAX / sizeof(*store->persistent)) ||
        !qa_source_save_count(io, &store->persistent_count, store->persistent_capacity) ||
        store->persistent_count > store->presentation_sequence ||
        !qa_source_save_u64(io, &store->persistent_revision) ||
        !qa_source_save_count(io, &store->owner_capacity, SIZE_MAX / sizeof(*store->owners)) ||
        !qa_source_save_count(io, &store->owner_count, store->owner_capacity) ||
        !qa_source_save_u64(io, &store->owner_generation) || store->owner_generation >= QA_UNIFIED_SAFE_INTEGER ||
        !qa_source_save_count(io, &store->resource_capacity, SIZE_MAX / sizeof(*store->resources)) ||
        !qa_source_save_count(io, &store->resource_count, store->resource_capacity) ||
        !qa_source_save_count(io, &store->registration_capacity, SIZE_MAX / sizeof(*store->registrations)) ||
        !qa_source_save_count(io, &store->registration_count, store->registration_capacity) ||
        !qa_source_save_u64(io, &store->registration_revision) ||
        !qa_source_save_count(io, &store->world_text_capacity, SIZE_MAX / sizeof(*store->world_text)) ||
        !qa_source_save_count(io, &store->world_text_count, store->world_text_capacity) ||
        !qa_source_save_u64(io, &store->world_text_revision) || !qa_source_save_u64(io, &store->world_text_map))
        return event_fail(io, QA_ERROR_FORMAT, "Invalid normalized Source continuation extent");
    size_t total = 0;
    for (size_t i = 0; i < 5; ++i) {
        if (store->counts[i] > SIZE_MAX - total)
            return event_fail(io, QA_ERROR_FORMAT, "Source event count overflows");
        total += store->counts[i];
    }
    if (total != store->journal_count || store->journal_count > store->sequence)
        return event_fail(io, QA_ERROR_FORMAT, "Source event journal differs from its payload owners");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        size_t remaining = io->input.size - io->offset;
        for (size_t i = 0; i < 5; ++i) {
            if (store->counts[i] > remaining / event_minimums[i])
                return event_fail(io, QA_ERROR_FORMAT, "Truncated application event rows");
            remaining -= store->counts[i] * event_minimums[i];
        }
        if (store->journal_count > remaining / 21)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Source event journal");
        remaining -= store->journal_count * 21;
        if (store->unified_count > remaining / 100)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated normalized Source events");
        remaining -= store->unified_count * 100;
        if (store->persistent_count > remaining / 100)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated persistent Source presentation");
        remaining -= store->persistent_count * 100;
        if (store->owner_count > remaining / 27) return event_fail(io, QA_ERROR_FORMAT, "Truncated Source activation owners");
        remaining -= store->owner_count * 27;
        if (store->resource_count > remaining / 100)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Source resource dictionary");
        remaining -= store->resource_count * 100;
        if (store->registration_count > remaining / 13)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Source registration owner");
        remaining -= store->registration_count * 13;
        if (store->world_text_count > remaining / 60)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated retained world text");
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
#if INT_MAX > INT32_MAX || INT_MIN < INT32_MIN
    if (io->direction == QA_SOURCE_SAVE_WRITE &&
        ((int64_t)*value < INT32_MIN || (int64_t)*value > INT32_MAX))
        return event_fail(io, QA_ERROR_FORMAT, "Application event integer exceeds its source domain");
#endif
    int32_t number = io->direction == QA_SOURCE_SAVE_WRITE ? (int32_t)*value : 0;
    if (!qa_source_save_i32(io, &number)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
#if INT_MAX < INT32_MAX || INT_MIN > INT32_MIN
        if ((int64_t)number < INT_MIN || (int64_t)number > INT_MAX)
            return event_fail(io, QA_ERROR_FORMAT, "Application event integer exceeds this host");
#endif
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

static bool audience_field(qa_source_save_io *io,event_store *store,
    qa_application_q2_audience *audience)
{
    if (!qa_source_save_bool(io,&audience->captured)) return false;
    if (!audience->captured) {
        if (io->direction==QA_SOURCE_SAVE_READ) *audience=(qa_application_q2_audience){0};
        else if (audience->count || audience->recipients || audience->source || audience->world_source)
            return event_fail(io,QA_ERROR_FORMAT,"Absent Q2 audience has retained provenance");
        return true;
    }
    uint32_t kind=audience->source_frame.kind,phase=audience->source_frame.phase;
    uint32_t delivery=audience->kind;
    if (!provider_field(io,&audience->source,true) || !provider_field(io,&audience->world_source,true) ||
        !provider_field(io,&audience->source_frame.provider,true) ||
        !enum_field(io,&kind,QA_CLOCK_Q3) || !enum_field(io,&phase,QA_FRAME_EXIT) ||
        !qa_source_save_u64(io,&audience->source_frame.number) ||
        !qa_source_save_u64(io,&audience->source_frame.start_ns) ||
        !qa_source_save_u64(io,&audience->source_frame.elapsed_ns) ||
        !qa_source_save_u64(io,&audience->source_frame.time_ns) ||
        !qa_source_save_u64(io,&audience->source_time_ns) ||
        !qa_source_save_u64(io,&audience->map_identity) ||
        !enum_field(io,&delivery,QA_APPLICATION_Q2_UNICAST) ||
        !qa_source_save_bool(io,&audience->positioned) ||
        !vector_field(io,&audience->multicast_origin) ||
        !qa_source_save_i32(io,&audience->area) || !qa_source_save_i32(io,&audience->cluster) ||
        !qa_source_save_count(io,&audience->count,SIZE_MAX/sizeof(*audience->recipients))) return false;
    if (audience->source!=audience->source_frame.provider ||
        (kind!=QA_CLOCK_Q2_CLASSIC && kind!=QA_CLOCK_Q2_RERELEASE) ||
        audience->source_frame.elapsed_ns>UINT64_MAX-audience->source_frame.start_ns ||
        audience->source_frame.time_ns<audience->source_frame.start_ns ||
        audience->source_frame.time_ns>audience->source_frame.start_ns+audience->source_frame.elapsed_ns ||
        audience->source_time_ns!=audience->source_frame.time_ns ||
        audience->cluster< -1 || audience->area< -1 ||
        ((delivery==QA_APPLICATION_Q2_PVS || delivery==QA_APPLICATION_Q2_PHS) &&
            (!audience->positioned || audience->area<0)) ||
        (!audience->positioned && (audience->area!=-1 || audience->cluster!=-1)) ||
        (delivery==QA_APPLICATION_Q2_UNICAST && audience->count>1))
        return event_fail(io,QA_ERROR_FORMAT,"Q2 audience lost its actual emission source");
    audience->source_frame.kind=(qa_clock_kind)kind;
    audience->source_frame.phase=(qa_frame_phase)phase;
    audience->kind=(qa_application_q2_delivery_kind)delivery;
    qa_application_q2_recipient *decoded=NULL;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (audience->count>(io->input.size-io->offset)/21)
            return event_fail(io,QA_ERROR_FORMAT,"Truncated Q2 recipient receipts");
        decoded=arena_array(io,store,audience->count,sizeof(*decoded),_Alignof(qa_application_q2_recipient));
        if (audience->count && !decoded) return false;
        audience->recipients=decoded;
    } else if (audience->count && !audience->recipients)
        return event_fail(io,QA_ERROR_FORMAT,"Q2 audience has no recipient storage");
    for (size_t i=0;i<audience->count;++i) {
        qa_application_q2_recipient value=decoded?(qa_application_q2_recipient){0}:audience->recipients[i];
        if (!actor_field(io,&value.actor) ||
            !vector_field(io,&value.origin) || !qa_source_save_i32(io,&value.area) ||
            !qa_source_save_i32(io,&value.cluster) ||
            !qa_source_save_bool(io,&value.has_connection) ||
            !qa_source_save_u64(io,&value.connection.owner) ||
            !qa_source_save_u64(io,&value.connection.generation) ||
            !qa_source_save_u32(io,&value.connection.slot) ||
            !qa_source_save_u64(io,&value.connection_seat.owner) ||
            !qa_source_save_u32(io,&value.connection_seat.index) ||
            !qa_source_save_u64(io,&value.connection_epoch) ||
            !qa_source_save_u8(io,&value.remote_index)) return false;
        if (!value.actor.registry || value.area<0 || value.cluster< -1)
            return event_fail(io,QA_ERROR_FORMAT,"Q2 recipient has invalid source leaf provenance");
        if (value.has_connection ? (!value.connection.owner || !value.connection.generation ||
                !value.connection_seat.owner || !value.connection_epoch) :
            (value.connection.owner || value.connection.generation || value.connection.slot ||
                value.connection_seat.owner || value.connection_seat.index ||
                value.connection_epoch || value.remote_index))
            return event_fail(io,QA_ERROR_FORMAT,"Q2 recipient lost its historical transport admission");
        for (size_t j=0;j<i;++j)
            if (qa_actor_id_equal(audience->recipients[j].actor,value.actor))
                return event_fail(io,QA_ERROR_FORMAT,"Q2 audience repeats a full client identity");
        if (decoded) decoded[i]=value;
    }
    return true;
}

static bool prompt_field(qa_source_save_io *io, event_store *store, qa_builtin_event *event)
{
    if (!qa_source_save_count(io, &event->prompt_choice_count,
            SIZE_MAX / sizeof(*event->prompt_choices))) return false;
    qa_builtin_prompt_choice *decoded = NULL;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (event->prompt_choice_count > (io->input.size - io->offset) / 5)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated source prompt choices");
        decoded = arena_array(io, store, event->prompt_choice_count, sizeof(*decoded),
            _Alignof(qa_builtin_prompt_choice));
        if (event->prompt_choice_count && !decoded) return false;
        event->prompt_choices = decoded;
    } else if (event->prompt_choice_count && !event->prompt_choices)
        return event_fail(io, QA_ERROR_FORMAT, "Source prompt choices have no retained storage");
    for (size_t i = 0; i < event->prompt_choice_count; ++i) {
        qa_builtin_prompt_choice choice = io->direction == QA_SOURCE_SAVE_WRITE ?
            event->prompt_choices[i] : (qa_builtin_prompt_choice){0};
        if (!qa_source_save_string(io, &choice.label) || !choice.label ||
            !qa_source_save_i32(io, &choice.impulse))
            return event_fail(io, QA_ERROR_FORMAT, "Source prompt choice lost its label or impulse");
        if (decoded) decoded[i] = choice;
    }
    return true;
}

static bool builtin_field(qa_source_save_io *io, event_store *store, application_event_record *record)
{
    qa_builtin_event *event=&record->event;
    uint32_t kind = event->kind, family = event->family;
    if (!enum_field(io, &kind, QA_BUILTIN_Q2_ENTITY_EVENT) || !enum_field(io, &family, QA_GAME_Q3) ||
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
    if (family == QA_GAME_Q2 && kind == QA_BUILTIN_MUZZLE) {
        if (!qa_source_save_bool(io, &event->has_muzzle_pose)) return false;
        if (event->has_muzzle_pose &&
            (!vector_field(io, &event->muzzle_angles) || !finite_field(io, &event->muzzle_scale) ||
             event->muzzle_scale <= 0 || !event->provider || !event->actor.registry))
            return event_fail(io, QA_ERROR_FORMAT, "Q2 muzzle lost its append-time Source pose");
    } else if (event->has_muzzle_pose)
        return event_fail(io, QA_ERROR_FORMAT, "Non-Q2 muzzle has a Source pose receipt");
    if (!event->has_muzzle_pose) {
        if (io->direction == QA_SOURCE_SAVE_WRITE &&
            (event->muzzle_scale != 0.0f || event->muzzle_angles.x != 0.0f || event->muzzle_angles.y != 0.0f || event->muzzle_angles.z != 0.0f))
            return event_fail(io, QA_ERROR_FORMAT, "Absent muzzle pose has retained Source fields");
        event->muzzle_angles = (qa_vec3){0}; event->muzzle_scale = 0;
    }
    if (family == QA_GAME_Q2 && kind == QA_BUILTIN_ITEM && event->code == 0) {
        if (!qa_source_save_string(io, &event->item) || !event->item)
            return event_fail(io, QA_ERROR_FORMAT, "Q2 pickup lost its authored canonical item");
    } else if (io->direction == QA_SOURCE_SAVE_READ) event->item = 0;
    else if (event->item)
        return event_fail(io, QA_ERROR_FORMAT, "Non-pickup event has a Q2 pickup item");
    if ((kind == QA_BUILTIN_Q2_PLAYER_ANIMATION || kind == QA_BUILTIN_Q2_ENTITY_EVENT) &&
        (family != QA_GAME_Q2 || !event->provider || !event->actor.registry ||
         (kind == QA_BUILTIN_Q2_PLAYER_ANIMATION && (event->code < 0 || event->code > 2))))
        return event_fail(io, QA_ERROR_FORMAT, "Invalid source-qualified Q2 animation receipt");
    if (kind == QA_BUILTIN_LOG &&
        (family != QA_GAME_Q3 || !event->provider || !event->text || event->argument_count ||
         event->actor.registry || event->other.registry))
        return event_fail(io, QA_ERROR_FORMAT, "Invalid provider-owned Q3 log event");
    if (kind == QA_BUILTIN_CTF_STATUS) {
        qa_builtin_ctf_status *status = &event->ctf_status;
        if (!qa_source_save_f64(io, &status->red) || !qa_source_save_f64(io, &status->blue) ||
            !qa_source_save_f64(io, &status->flags) || !qa_source_save_f64(io, &status->rune_items))
            return false;
        if (family != QA_GAME_Q1 || !event->provider || !event->actor.registry ||
            event->argument_count || !isfinite(status->red) || !isfinite(status->blue) ||
            !isfinite(status->flags) || !isfinite(status->rune_items))
            return event_fail(io, QA_ERROR_FORMAT, "Invalid source-addressed CTF status event");
    } else if (io->direction == QA_SOURCE_SAVE_READ) event->ctf_status = (qa_builtin_ctf_status){0};
    if (kind == QA_BUILTIN_SOURCE_LOG &&
        (family != QA_GAME_Q1 || !event->provider || !event->actor.registry ||
         event->other.registry || !event->text || event->resource || event->argument_count))
        return event_fail(io, QA_ERROR_FORMAT, "Invalid source-player action log");
    if (kind == QA_BUILTIN_CTF_CAPTURE) {
        qa_builtin_ctf_capture *capture = &event->ctf_capture;
        if (!qa_source_save_bool(io, &capture->blue) ||
            !qa_source_save_f64(io, &capture->total)) return false;
        if (family != QA_GAME_Q1 || !event->provider || event->actor.registry ||
            event->other.registry || event->text || event->resource || event->argument_count ||
            !isfinite(capture->total))
            return event_fail(io, QA_ERROR_FORMAT, "Invalid source-wide CTF capture total");
    } else if (io->direction == QA_SOURCE_SAVE_READ) event->ctf_capture = (qa_builtin_ctf_capture){0};
    if (kind == QA_BUILTIN_Q1_POWERUP) {
        if (!qa_source_save_u32(io, &event->q1_powerup.power) ||
            !qa_source_save_f64(io, &event->q1_powerup.expires) ||
            family != QA_GAME_Q1 || !event->provider || !event->actor.registry ||
            event->other.registry || event->text || event->resource || event->argument_count ||
            event->q1_powerup.power >= QA_Q1_POWER_COUNT || !isfinite(event->q1_powerup.expires))
            return event_fail(io, QA_ERROR_FORMAT, "Invalid typed Q1 powerup continuation");
    } else if (io->direction == QA_SOURCE_SAVE_READ) event->q1_powerup = (qa_builtin_q1_powerup){0};
    if (kind == QA_BUILTIN_SOURCE_PROMPT || kind == QA_BUILTIN_CLEAR_PROMPT) {
        if (family != QA_GAME_Q1 || !event->provider || !event->actor.registry ||
            event->other.registry || event->resource || event->argument_count ||
            (kind == QA_BUILTIN_SOURCE_PROMPT ? !event->text : event->text != 0))
            return event_fail(io, QA_ERROR_FORMAT, "Invalid source-player prompt continuation");
        if (kind == QA_BUILTIN_SOURCE_PROMPT && !prompt_field(io, store, event)) return false;
    }
    if (kind != QA_BUILTIN_SOURCE_PROMPT) {
        if (io->direction == QA_SOURCE_SAVE_WRITE && event->prompt_choice_count)
            return event_fail(io, QA_ERROR_FORMAT, "Non-prompt continuation has source prompt choices");
        if (io->direction == QA_SOURCE_SAVE_READ) {
            event->prompt_choices = NULL; event->prompt_choice_count = 0;
        }
    }
    if (!audience_field(io,store,&record->q2_audience)) return false;
    if (record->q2_audience.captured &&
        (kind!=QA_BUILTIN_PARTICLES || family!=QA_GAME_Q2 || event->provider!=record->q2_audience.source ||
         record->q2_audience.kind!=QA_APPLICATION_Q2_PVS || !record->q2_audience.positioned))
        return event_fail(io,QA_ERROR_FORMAT,"Q2 particle audience differs from its source event");
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
                         application_q2_map_event_record *retained)
{
    qa_application_q2_map_event *record=&retained->source;
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
    if (!qa_source_save_count(io,&event->level_count,QA_Q2_CAMPAIGN_LEVEL_LIMIT) ||
        !qa_source_save_u64(io,&event->button_time_ns)) return false;
    if (kind!=QA_Q2_MAP_END_UNIT && (event->level_count || event->button_time_ns))
        return event_fail(io,QA_ERROR_FORMAT,"Non-report Q2 event retains campaign levels");
    qa_q2_campaign_level *levels=NULL;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (event->level_count>(io->input.size-io->offset)/30)
            return event_fail(io,QA_ERROR_FORMAT,"Truncated Q2 campaign report");
        levels=arena_array(io,store,event->level_count,sizeof(*levels),_Alignof(qa_q2_campaign_level));
        if (event->level_count && !levels) return false;
        event->levels=levels;
    } else if (event->level_count && !event->levels)
        return event_fail(io,QA_ERROR_FORMAT,"Q2 campaign report has no retained rows");
    for (size_t i=0;i<event->level_count;++i) {
        qa_q2_campaign_level row=levels?(qa_q2_campaign_level){0}:event->levels[i];
        if (!qa_source_save_string(io,&row.map) || !qa_source_save_string(io,&row.name) ||
            !qa_source_save_u32(io,&row.visit_order) || !qa_source_save_u32(io,&row.total_secrets) ||
            !qa_source_save_u32(io,&row.found_secrets) || !qa_source_save_u32(io,&row.total_monsters) ||
            !qa_source_save_u32(io,&row.killed_monsters) || !qa_source_save_f64(io,&row.time_seconds)) return false;
        if (!row.map || !isfinite(row.time_seconds) || row.time_seconds<0)
            return event_fail(io,QA_ERROR_FORMAT,"Invalid Q2 campaign report row");
        for (size_t j=0;j<i;++j)
            if (event->levels[j].map==row.map)
                return event_fail(io,QA_ERROR_FORMAT,"Q2 campaign report repeats its actual map");
        if (levels) levels[i]=row;
    }
    if (!audience_field(io,store,&retained->audience)) return false;
    if (retained->audience.captured &&
        ((kind!=QA_Q2_MAP_STEAM && kind!=QA_Q2_MAP_FORCE_WALL) ||
         record->provider!=retained->audience.source ||
         retained->audience.kind!=QA_APPLICATION_Q2_PVS || !retained->audience.positioned))
        return event_fail(io,QA_ERROR_FORMAT,"Q2 map audience differs from its source effect");
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

static bool resource_key_field(qa_source_save_io *io,
                               char key[QA_APPLICATION_RESOURCE_KEY_CAPACITY])
{
    size_t size = QA_APPLICATION_RESOURCE_KEY_CAPACITY;
    return qa_source_save_bytes(io, key, size) &&
        (!key[size - 1] || event_fail(io, QA_ERROR_FORMAT, "Unterminated Source resource key"));
}

static bool view_field(qa_source_save_io *io, qa_q2_player_view *view)
{
    return qa_source_save_vec3(io, &view->angles) && qa_source_save_vec3(io, &view->offset) &&
        qa_source_save_vec3(io, &view->kick_angles) && qa_source_save_vec3(io, &view->gun_angles) &&
        qa_source_save_vec3(io, &view->gun_offset) && qa_source_save_f32(io, &view->blend.x) &&
        qa_source_save_f32(io, &view->blend.y) && qa_source_save_f32(io, &view->blend.z) &&
        qa_source_save_f32(io, &view->blend.w) && qa_source_save_f32(io, &view->fov) &&
        qa_source_save_f32(io, &view->health) && qa_source_save_f64(io, &view->armor) &&
        qa_source_save_f32(io, &view->ammo) &&
        qa_source_save_string(io, &view->ammo_icon) && qa_source_save_string(io, &view->armor_icon) &&
        qa_source_save_i32(io, &view->ammo_count) && int_field(io, &view->score) &&
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
        !enum_field(io, &policy, QA_COUNT_SOURCE_DOUBLE)) return false;
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
    if (!qa_source_save_count(io, &record->recipient_count,
        SIZE_MAX / sizeof(qa_application_network_q2_recipient_view))) return false;
    if (record->recipient_count && event->kind != QA_Q2_PLAYER_PRINT)
        return event_fail(io, QA_ERROR_FORMAT, "Non-print Q2 player event has transport recipients");
    if (event->actor.registry && record->recipient_count > 1)
        return event_fail(io, QA_ERROR_FORMAT, "Unicast Q2 print has multiple recipients");
    qa_application_network_q2_recipient_view *decoded = NULL;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (record->recipient_count > (io->input.size - io->offset) / 41)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Q2 print recipient receipts");
        decoded = arena_array(io, store, record->recipient_count, sizeof(*decoded), _Alignof(qa_application_network_q2_recipient_view));
        if (record->recipient_count && !decoded) return false;
        record->recipients = decoded;
    } else if (record->recipient_count && !record->recipients)
        return event_fail(io, QA_ERROR_FORMAT, "Q2 print has no retained recipient storage");
    for (size_t i = 0; i < record->recipient_count; ++i) {
        qa_application_network_q2_recipient_view value = decoded ?
            (qa_application_network_q2_recipient_view){0} : record->recipients[i];
        if (!actor_field(io, &value.actor) || !qa_source_save_u64(io, &value.client.owner) ||
            !qa_source_save_u64(io, &value.client.generation) || !qa_source_save_u32(io, &value.client.slot) ||
            !qa_source_save_u64(io, &value.seat.owner) || !qa_source_save_u32(io, &value.seat.index) ||
            !qa_source_save_u64(io, &value.connection_epoch) || !qa_source_save_u8(io, &value.remote_index)) return false;
        if (!value.actor.registry || !value.client.owner || !value.client.generation || !value.seat.owner ||
            !value.connection_epoch || value.remote_index >= QA_Q2_MAX_SEATS ||
            (event->actor.registry && !qa_actor_id_equal(event->actor, value.actor)))
            return event_fail(io, QA_ERROR_FORMAT, "Q2 print lost its historical transport admission");
        for (size_t j = 0; j < i; ++j)
            if (qa_actor_id_equal(record->recipients[j].actor, value.actor) ||
                (qa_net_client_id_equal(record->recipients[j].client, value.client) &&
                    (record->recipients[j].remote_index == value.remote_index ||
                        (record->recipients[j].seat.owner == value.seat.owner && record->recipients[j].seat.index == value.seat.index))))
                return event_fail(io, QA_ERROR_FORMAT, "Q2 print repeats its captured recipient");
        if (decoded) decoded[i] = value;
    }
    return true;
}

static bool protocol_field(qa_source_save_io *io, event_store *store,
                            application_protocol_record *record)
{
    qa_application_protocol_event *event=&record->event;
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
    if (!qa_source_save_count(io, &event->resource_count, SIZE_MAX / sizeof(*event->resources))) return false;
    qa_application_protocol_resource_reference *resources = NULL;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (event->resource_count > (io->input.size - io->offset) / 95)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Source protocol resource receipts");
        resources = arena_array(io, store, event->resource_count, sizeof(*resources),
            _Alignof(qa_application_protocol_resource_reference));
        if (event->resource_count && !resources) return false;
        event->resources = resources;
    } else if (event->resource_count && !event->resources)
        return event_fail(io, QA_ERROR_FORMAT, "Source protocol resource receipts have no storage");
    for (size_t i = 0; i < event->resource_count; ++i) {
        qa_application_protocol_resource_reference resource = io->direction == QA_SOURCE_SAVE_WRITE ?
            event->resources[i] : (qa_application_protocol_resource_reference){0};
        uint32_t kind = (uint32_t)resource.kind;
        if (!qa_source_save_count(io, &resource.record_ordinal, SIZE_MAX) ||
            !enum_field(io, &kind, QA_NATIVE_HOST_IMAGE) || !qa_source_save_u32(io, &resource.source_index) ||
            !text_field(io, store, &resource.name) ||
            !resource_key_field(io, resource.resource_key) ||
            !qa_source_save_u64(io, &resource.resource_custody)) return false;
        resource.kind = (qa_native_host_resource_kind)kind;
        if (!resource.name || resource.record_ordinal >= size || resource.resource_key[QA_APPLICATION_RESOURCE_KEY_CAPACITY - 1] ||
            (resource.resource_key[0] && strncmp(resource.resource_key, "resource:unified:", sizeof("resource:unified:") - 1)) ||
            (!resource.resource_key[0] && resource.resource_custody))
            return event_fail(io, QA_ERROR_FORMAT, "Source protocol resource receipt differs from its immutable registration");
        if (resources) resources[i] = resource;
    }
    if (!qa_source_save_i32(io, &event->destination) || !qa_source_save_bool(io, &event->reliable) ||
        !qa_source_save_bool(io, &event->multicast) || !qa_source_save_bool(io, &event->signon)) return false;
    event->dialect = (qa_clock_kind)dialect;
    if (!qa_source_save_bool(io,&record->q2.original)) return false;
    if (record->q2.original) {
        uint32_t profile=record->q2.profile;
        if (!enum_field(io,&profile,QA_NATIVE_Q2_GAME_API2023) ||
            !qa_source_save_u32(io,&record->q2.dupe_key) ||
            !audience_field(io,store,&record->q2.audience)) return false;
        record->q2.profile=(qa_native_profile)profile;
        if (dialect!=(profile==QA_NATIVE_Q2_GAME_API3?QA_CLOCK_Q2_CLASSIC:QA_CLOCK_Q2_RERELEASE) ||
            event->signon || (record->q2.dupe_key &&
                (profile!=QA_NATIVE_Q2_GAME_API2023 || event->multicast)) ||
            (event->multicast && (event->destination<0 || event->destination>2)) ||
            (record->q2.audience.captured &&
                (record->q2.audience.source!=event->provider ||
                 record->q2.audience.source_time_ns!=event->time_ns ||
                 record->q2.audience.source_frame.kind!=event->dialect ||
                 (event->multicast && record->q2.audience.kind==QA_APPLICATION_Q2_UNICAST) ||
                 (event->multicast && record->q2.audience.kind!=(event->destination==0 ?
                    QA_APPLICATION_Q2_ALL:event->destination==1?QA_APPLICATION_Q2_PHS:QA_APPLICATION_Q2_PVS)) ||
                 (!event->multicast && record->q2.audience.kind!=QA_APPLICATION_Q2_UNICAST) ||
                 (!event->multicast && record->q2.audience.count &&
                    !qa_actor_id_equal(record->q2.audience.recipients[0].actor,event->recipient)))))
            return event_fail(io,QA_ERROR_FORMAT,"Original Q2 protocol differs from its retained delivery");
    } else if (io->direction==QA_SOURCE_SAVE_READ) record->q2=(qa_application_q2_protocol_delivery){0};
    else if (record->q2.audience.captured)
        return event_fail(io,QA_ERROR_FORMAT,"Unqualified protocol has an original Q2 audience");
    return true;
}

static event_store borrow_store(qa_application *app)
{
    return (event_store){
        .builtin = app->events, .q2_map = app->q2_map_events, .q3_map = app->q3_map_events,
        .protocol_generation = app->protocol_events_generation,
        .journal = app->event_journal, .journal_count = app->event_journal_count,
        .journal_capacity = app->event_journal_capacity, .sequence = app->event_sequence,
        .simulation_sequence = app->simulation_event_sequence,
        .unified = app->unified_events, .unified_count = app->unified_event_count,
        .unified_capacity = app->unified_event_capacity, .unified_sequence = app->unified_event_sequence,
        .presentation_sequence = app->presentation_event_sequence,
        .persistent = app->unified_persistent, .persistent_count = app->unified_persistent_count,
        .persistent_capacity = app->unified_persistent_capacity, .persistent_revision = app->unified_persistent_revision,
        .owners = app->unified_event_owners, .owner_count = app->unified_event_owner_count,
        .owner_capacity = app->unified_event_owner_capacity, .owner_generation = app->unified_event_owner_generation,
        .resources = app->unified_event_resources, .resource_count = app->unified_event_resource_count,
        .resource_capacity = app->unified_event_resource_capacity,
        .registrations = app->unified_event_registrations,
        .registration_count = app->unified_event_registration_count,
        .registration_capacity = app->unified_event_registration_capacity,
        .registration_revision = app->unified_event_registration_revision,
        .world_text = app->unified_world_text, .world_text_count = app->unified_world_text_count,
        .world_text_capacity = app->unified_world_text_capacity,
        .world_text_revision = app->unified_world_text_revision, .world_text_map = app->unified_world_text_map,
        .application = app,
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
    free(store->journal);
    free(store->unified);
    for (size_t i = 0; store->persistent && i < store->persistent_count; ++i) {
        qa_buffer_free(&store->persistent[i].key); qa_buffer_free(&store->persistent[i].payload);
    }
    free(store->persistent);
    free(store->owners);
    for (size_t i = 0; store->resources && i < store->resource_count; ++i) {
        qa_buffer_free(&store->resources[i].key);
        qa_resource_release(store->resources[i].resource);
        qa_launch_instance_lease_release(store->resources[i].descriptor);
        qa_vfs_acquisition_dispose(&store->resources[i].opening);
        qa_vfs_destroy(store->resources[i].view);
        qa_resource_pool_destroy(store->resources[i].pool);
        for (size_t j = 0; store->resources[i].custodies && j < store->resources[i].custody_count; ++j) {
            application_unified_event_resource_custody *held = store->resources[i].custodies + j;
            qa_resource_release(held->resource); qa_vfs_acquisition_dispose(&held->opening);
            qa_vfs_destroy(held->view); qa_resource_pool_destroy(held->pool);
        }
        free(store->resources[i].custodies);
    }
    free(store->resources);
    free(store->registrations);
    free(store->world_text);
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
    if (store->journal_capacity) {
        store->journal = calloc(store->journal_capacity, sizeof(*store->journal));
        if (!store->journal)
            return event_fail(io, QA_ERROR_MEMORY, "Allocating restored Source event journal");
    }
    if (store->unified_capacity) store->unified = calloc(store->unified_capacity, sizeof(*store->unified));
    if (store->persistent_capacity) store->persistent = calloc(store->persistent_capacity, sizeof(*store->persistent));
    if (store->owner_capacity) store->owners = calloc(store->owner_capacity, sizeof(*store->owners));
    if (store->resource_capacity) store->resources = calloc(store->resource_capacity, sizeof(*store->resources));
    if (store->registration_capacity) store->registrations = calloc(store->registration_capacity, sizeof(*store->registrations));
    if (store->world_text_capacity) store->world_text = calloc(store->world_text_capacity, sizeof(*store->world_text));
    if ((store->unified_capacity && !store->unified) || (store->resource_capacity && !store->resources) ||
        (store->persistent_capacity && !store->persistent) ||
        (store->owner_capacity && !store->owners) ||
        (store->registration_capacity && !store->registrations) ||
        (store->world_text_capacity && !store->world_text))
        return event_fail(io, QA_ERROR_MEMORY, "Allocating genuine Source continuation owners");
    return true;
}

static bool journal_rows(qa_source_save_io *io, event_store *store)
{
    if ((store->journal_capacity != 0) != (store->journal != NULL))
        return event_fail(io, QA_ERROR_FORMAT, "Source event journal allocation differs");
    size_t next[5] = {0};
    for (size_t i = 0; i < store->journal_count; ++i) {
        application_event_journal_record row = io->direction == QA_SOURCE_SAVE_WRITE ?
            store->journal[i] : (application_event_journal_record){0};
        uint32_t queue = row.queue;
        if (!qa_source_save_u64(io, &row.sequence) ||
            !enum_field(io, &queue, APPLICATION_EVENT_PROTOCOL) ||
            !qa_source_save_count(io, &row.index, SIZE_MAX) ||
            !qa_source_save_bool(io, &row.has_frame)) return false;
        if (row.sequence != store->sequence - store->journal_count + i ||
            row.index != next[queue] || row.index >= store->counts[queue])
            return event_fail(io, QA_ERROR_FORMAT, "Source event journal has invalid append order");
        ++next[queue];
        row.queue = (application_event_queue)queue;
        if (row.has_frame) {
            uint32_t kind = row.frame.kind, phase = row.frame.phase;
            if (!provider_field(io, &row.frame.provider, true) ||
                !enum_field(io, &kind, QA_CLOCK_Q3) || !enum_field(io, &phase, QA_FRAME_EXIT) ||
                !qa_source_save_u64(io, &row.frame.number) ||
                !qa_source_save_u64(io, &row.frame.start_ns) ||
                !qa_source_save_u64(io, &row.frame.elapsed_ns) ||
                !qa_source_save_u64(io, &row.frame.time_ns)) return false;
            row.frame.kind = (qa_clock_kind)kind;
            row.frame.phase = (qa_frame_phase)phase;
            if (row.frame.elapsed_ns > UINT64_MAX - row.frame.start_ns ||
                row.frame.time_ns < row.frame.start_ns ||
                row.frame.time_ns > row.frame.start_ns + row.frame.elapsed_ns)
                return event_fail(io, QA_ERROR_FORMAT, "Source event clock exceeds its admitted frame");
        } else if (io->direction == QA_SOURCE_SAVE_WRITE &&
            (row.frame.provider || row.frame.number || row.frame.start_ns ||
             row.frame.elapsed_ns || row.frame.time_ns))
            return event_fail(io, QA_ERROR_FORMAT, "Absent Source event clock has provenance");
        if (io->direction == QA_SOURCE_SAVE_READ) store->journal[i] = row;
    }
    for (size_t i = 0; i < 5; ++i)
        if (next[i] != store->counts[i])
            return event_fail(io, QA_ERROR_FORMAT, "Source event journal omitted a payload");
    return true;
}

static bool payload_field(qa_source_save_io *io, event_store *store, qa_bytes *bytes)
{
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, 16u * 1024u * 1024u)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (size > io->input.size - io->offset)
            return event_fail(io, QA_ERROR_FORMAT, "Truncated Source payload");
        uint8_t *data = arena_array(io, store, size, 1, 1);
        if (size && !data) return false;
        *bytes = (qa_bytes){data, size};
    }
    return qa_source_save_bytes(io, (void *)bytes->data, size);
}

static bool normalized_actor_field(qa_source_save_io *io, application_unified_event_record *row, bool simulation)
{
    qa_actor_id *actor = simulation ? &row->simulation_recipient : &row->recipient;
    qa_saved_actor_id *receipt = simulation ? &row->simulation_recipient_saved : &row->recipient_saved;
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && actor->registry != 0;
    qa_saved_actor_id saved = {0};
    if (present) {
        if (row->payload_checkpoint) {
            qa_actor_id actual;
            saved = *receipt;
            if (!qa_actors_reference_saved(qa_session_actors(io->session), saved, true, &actual, io->error)) return false;
        } else if (!qa_actors_save_reference(qa_session_actors(io->session), *actor, &saved, io->error)) return false;
    }
    if (!qa_source_save_bool(io, &present) || !qa_source_save_u64(io, &saved.generation) ||
        !qa_source_save_u32(io, &saved.slot)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!present) {
            if (saved.generation || saved.slot) return event_fail(io, QA_ERROR_FORMAT, "Absent normalized recipient contains actor history");
            *actor = (qa_actor_id){0};
        } else if (!qa_actors_reference_saved(qa_session_actors(io->session), saved, true, actor, io->error)) return false;
        *receipt = saved;
    }
    return true;
}

static bool normalized_field(qa_source_save_io *io, event_store *store, application_unified_event_record *row)
{
    uint32_t clock = row->clock, presentation_clock = row->presentation_clock;
    if (!qa_source_save_u64(io, &row->order) ||
        !qa_source_save_u64(io, &row->presentation_sequence) || !qa_source_save_u64(io, &row->simulation_sequence) ||
        !qa_source_save_u64(io, &row->owner_generation) || row->owner_generation > QA_UNIFIED_SAFE_INTEGER ||
        !qa_source_save_u8(io,&row->q2_source_profile) || row->q2_source_profile>2 ||
        !qa_source_save_u64(io,&row->q2_source_interval_ns) ||
        ((row->q2_source_profile!=0)!=(row->q2_source_interval_ns!=0)) ||
        !qa_source_save_u64(io, &row->time_ns) || !qa_source_save_u64(io, &row->simulation_time_ns) ||
        !enum_field(io, &clock, QA_CLOCK_Q3) || !enum_field(io, &presentation_clock, QA_CLOCK_Q3) ||
        !normalized_actor_field(io, row, false) || !normalized_actor_field(io, row, true) ||
        !provider_field(io, &row->provider, true) || !qa_source_save_string(io, &row->content) ||
        !qa_source_save_i32(io, &row->source_entity) || !qa_source_save_bool(io, &row->has_source_entity) ||
        !qa_source_save_bool(io, &row->link_presentation) || !qa_source_save_u64(io, &row->client.owner) ||
        !qa_source_save_u32(io, &row->client.slot) || !qa_source_save_u64(io, &row->client.generation) ||
        !payload_field(io, store, &row->presentation) || !payload_field(io, store, &row->simulation)) return false;
    row->clock = clock; row->presentation_clock = presentation_clock;
    if (row->q2_source_profile && presentation_clock!=(row->q2_source_profile==1 ?
        QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE))
        return event_fail(io,QA_ERROR_FORMAT,"Q2 Source profile differs from its retained emission clock");
    if (io->direction == QA_SOURCE_SAVE_READ) row->payload_checkpoint = true;
    return row->content &&
        application_unified_event_payload_valid(row->presentation, true, false, io->error) &&
        application_unified_event_payload_valid(row->simulation, false, row->link_presentation, io->error) &&
        application_unified_event_actors_valid(store->application, row, io->error);
}

static bool persistent_rows(qa_source_save_io *io, event_store *store)
{
    for (size_t i = 0; i < store->persistent_count; ++i) {
        application_unified_persistent_event *row = store->persistent + i;
        if (!normalized_field(io, store, &row->event)) return false;
        qa_bytes key = {row->key.data, row->key.size};
        if (!payload_field(io, store, &key)) return false;
        if (!row->event.presentation.size || row->event.simulation.size || row->event.link_presentation ||
            row->event.simulation_sequence || row->event.order >= store->unified_sequence ||
            row->event.presentation_sequence >= store->presentation_sequence ||
            (i && row->event.presentation_sequence <= store->persistent[i - 1].event.presentation_sequence))
            return event_fail(io, QA_ERROR_FORMAT, "Persistent presentation lost its original emitted sequence");
        qa_buffer expected = {0}; bool remove = false;
        bool valid = application_unified_persistent_key(store->application, &row->event, &expected, &remove, io->error) &&
            expected.size && !remove && expected.size == key.size && !memcmp(expected.data, key.data, key.size);
        qa_buffer_free(&expected);
        if (!valid) return event_fail(io, QA_ERROR_FORMAT, "Persistent presentation differs from its actual source domain");
        for (size_t n = 0; n < i; ++n)
            if (store->persistent[n].key.size == key.size && !memcmp(store->persistent[n].key.data, key.data, key.size))
                return event_fail(io, QA_ERROR_FORMAT, "Persistent presentation repeats an owned domain");
        if (io->direction == QA_SOURCE_SAVE_READ) {
            row->key.data = malloc(key.size); row->key.size = key.size;
            row->payload.data = malloc(row->event.presentation.size); row->payload.size = row->event.presentation.size;
            if (!row->key.data || !row->payload.data) return event_fail(io, QA_ERROR_MEMORY, "Retaining decoded persistent presentation");
            memcpy(row->key.data, key.data, key.size);
            memcpy(row->payload.data, row->event.presentation.data, row->payload.size);
            row->event.presentation = (qa_bytes){row->payload.data, row->payload.size};
        }
    }
    for (size_t i = 0; i < store->owner_count; ++i) {
        application_unified_event_owner *owner = store->owners + i;
        if (!provider_field(io, &owner->provider, true) || !qa_source_save_string(io, &owner->content) || !owner->content ||
            !qa_source_save_u64(io, &owner->generation) || owner->generation > store->owner_generation ||
            !qa_source_save_bool(io, &owner->active)) return event_fail(io, QA_ERROR_FORMAT, "Invalid genuine Source activation token");
        for (size_t n = 0; n < i; ++n)
            if (store->owners[n].provider == owner->provider ||
                (owner->generation && store->owners[n].generation == owner->generation))
                return event_fail(io, QA_ERROR_FORMAT, "Source activation owner is duplicated");
    }
    for (size_t i = 0; i < store->persistent_count; ++i) {
        const application_unified_event_record *event = &store->persistent[i].event;
        bool found = false;
        for (size_t n = 0; n < store->owner_count; ++n) {
            const application_unified_event_owner *owner = store->owners + n;
            if (owner->active && owner->provider == event->provider &&
                owner->generation == event->owner_generation && owner->content == event->content)
                found = true;
        }
        if (!found) return event_fail(io, QA_ERROR_FORMAT, "Persistent presentation lost its genuine active Source token");
    }
    return true;
}

static bool custody_field(qa_source_save_io *io, const qa_application_content_graph *graph,
    application_unified_event_resource_custody *held)
{
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        held->saved_view = qa_application_content_view_id(graph, held->view);
        if (!held->saved_view || !qa_application_content_resource_id(graph, held->resource,
            &held->saved_pool, &held->saved_resource))
            return event_fail(io, QA_ERROR_FORMAT, "Source custody is outside its captured CONTENT graph");
    }
    if (!qa_source_save_u64(io, &held->saved_pool) || !qa_source_save_u64(io, &held->saved_resource) ||
        !qa_source_save_u64(io, &held->saved_view) || !held->saved_pool || !held->saved_resource || !held->saved_view)
        return event_fail(io, QA_ERROR_FORMAT, "Source custody lost its actual graph owner");
    const qa_vfs *files = io->direction == QA_SOURCE_SAVE_READ ?
        qa_application_content_view(graph, held->saved_view) : held->view;
    qa_vfs_acquisition *opening = &held->opening;
    return files && qa_source_save_u64(io, &opening->mount) && qa_source_save_u64(io, &opening->resource_id) &&
        qa_source_save_owned_text(io, &opening->path) && qa_source_save_owned_text(io, &opening->lookup_path) &&
        qa_source_save_owned_text(io, &opening->link_source) && qa_source_save_owned_text(io, &opening->link_target) &&
        qa_vfs_acquisition_opening_codec(io, files, opening) && opening->opening_present &&
        qa_vfs_acquisition_retained(files, opening, io->error);
}

static bool normalized_rows(qa_source_save_io *io, event_store *store)
{
    uint64_t presentation_count = 0, simulation_count = 0;
    for (size_t i = 0; i < store->unified_count; ++i) {
        application_unified_event_record row = store->unified[i];
        if (!normalized_field(io, store, &row)) return false;
        if (row.order != store->unified_sequence - store->unified_count + i || !row.content ||
            row.owner_generation > store->owner_generation ||
            (!row.presentation.size && !row.simulation.size) ||
            (row.link_presentation && (!row.presentation.size || !row.simulation.size)) ||
            (!row.presentation.size && row.presentation_sequence) ||
            (!row.simulation.size && row.simulation_sequence) ||
            (!row.has_source_entity && row.source_entity) ||
            (row.presentation.size && row.presentation_sequence >= store->presentation_sequence) ||
            (row.simulation.size && row.simulation_sequence >= store->simulation_sequence) ||
            !application_unified_event_payload_valid(row.presentation, true, false, io->error) ||
            !application_unified_event_payload_valid(row.simulation, false, row.link_presentation, io->error))
            return event_fail(io, QA_ERROR_FORMAT, "Source event continuation lost its actual payload or sequence");
        if (row.presentation.size) {
            if (presentation_count && row.presentation_sequence != presentation_count)
                return event_fail(io, QA_ERROR_FORMAT, "Source presentation sequence is not contiguous");
            presentation_count = row.presentation_sequence + 1;
        }
        if (row.simulation.size) {
            if (simulation_count && row.simulation_sequence != simulation_count)
                return event_fail(io, QA_ERROR_FORMAT, "Source simulation sequence is not contiguous");
            simulation_count = row.simulation_sequence + 1;
        }
        if (io->direction == QA_SOURCE_SAVE_READ) store->unified[i] = row;
    }
    if ((presentation_count && presentation_count != store->presentation_sequence) ||
        (simulation_count && simulation_count != store->simulation_sequence))
        return event_fail(io, QA_ERROR_FORMAT, "Source continuation omits its latest emitted sequence");
    const qa_application_content_graph *graph = store->application ?
        qa_application_content_graph_read(store->application) : NULL;
    for (size_t i = 0; i < store->resource_count; ++i) {
        application_unified_event_resource *row = store->resources + i;
        uint64_t pool = row->saved_pool, resource = row->saved_resource, view = row->saved_view;
        if (io->direction == QA_SOURCE_SAVE_WRITE) {
            view = qa_application_content_view_id(graph, row->view);
            if (!view || !qa_application_content_resource_id(graph, row->resource, &pool, &resource))
                return event_fail(io, QA_ERROR_FORMAT, "Source dictionary resource is outside its captured CONTENT graph");
        }
        if (!provider_field(io, &row->provider, true) || !qa_source_save_string(io, &row->content) ||
            !qa_source_save_string(io, &row->path) || !qa_source_save_u64(io, &pool) ||
            !qa_source_save_u64(io, &resource) || !qa_source_save_u64(io, &view) ||
            !pool || !resource || !view || !row->content || !row->path ||
            !resource_key_field(io, row->id) || row->id[QA_APPLICATION_RESOURCE_KEY_CAPACITY - 1] ||
            strncmp(row->id, "resource:unified:", sizeof("resource:unified:") - 1))
            return event_fail(io, QA_ERROR_FORMAT, "Source resource dictionary has invalid ownership");
        const qa_vfs *files = io->direction == QA_SOURCE_SAVE_READ ?
            qa_application_content_view(graph, view) : row->view;
        qa_vfs_acquisition *opening = &row->opening;
        if (!files || !qa_source_save_u64(io, &opening->mount) ||
            !qa_source_save_u64(io, &opening->resource_id) ||
            !qa_source_save_owned_text(io, &opening->path) || !qa_source_save_owned_text(io, &opening->lookup_path) ||
            !qa_source_save_owned_text(io, &opening->link_source) || !qa_source_save_owned_text(io, &opening->link_target) ||
            !qa_vfs_acquisition_opening_codec(io, files, opening) || !opening->opening_present ||
            !qa_vfs_acquisition_retained(files, opening, io->error))
            return event_fail(io, QA_ERROR_FORMAT, "Source dictionary lost its actual acquisition opening");
        size_t size = row->key.size;
        if (!qa_source_save_count(io, &size, 16u * 1024u * 1024u) || !size) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (size > io->input.size - io->offset)
                return event_fail(io, QA_ERROR_FORMAT, "Truncated Source resource key");
            row->key.data = malloc(size); row->key.size = size;
            if (!row->key.data) return event_fail(io, QA_ERROR_MEMORY, "Retaining restored Source dictionary key");
            row->saved_pool = pool; row->saved_resource = resource; row->saved_view = view;
        }
        if (!qa_source_save_bytes(io, row->key.data, size)) return false;
        qa_unified_document *key = NULL;
        bool valid = qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
            (qa_bytes){row->key.data, row->key.size}, &key, io->error);
        qa_unified_document_destroy(key);
        if (!valid) return event_fail(io, QA_ERROR_FORMAT, "Source resource key is malformed");
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(row->id, store->resources[j].id))
                return event_fail(io, QA_ERROR_FORMAT, "Source resource dictionary identity is duplicated");
        if (!qa_source_save_count(io, &row->custody_capacity, SIZE_MAX / sizeof(*row->custodies)) ||
            !qa_source_save_count(io, &row->custody_count, row->custody_capacity)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            if (row->custody_count > (io->input.size - io->offset) / 64 ||
                (row->custody_capacity && (row->custody_capacity < 4 ||
                    (row->custody_capacity & (row->custody_capacity - 1)) ||
                    row->custody_capacity > (row->custody_count < 2 ? 4 : row->custody_count * 2))))
                return event_fail(io, QA_ERROR_FORMAT, "Source custody allocation exceeds its actual growth or document");
            row->custodies = row->custody_capacity ? calloc(row->custody_capacity, sizeof(*row->custodies)) : NULL;
            if (row->custody_capacity && !row->custodies)
                return event_fail(io, QA_ERROR_MEMORY, "Retaining restored Source opening receipts");
        } else if ((row->custody_capacity != 0) != (row->custodies != NULL))
            return event_fail(io, QA_ERROR_FORMAT, "Source custody allocation lost its backing");
        for (size_t j = 0; j < row->custody_count; ++j)
            if (!custody_field(io, graph, row->custodies + j)) return false;
    }
    for (size_t i = 0; i < store->world_text_count; ++i) {
        application_unified_world_text *row = store->world_text + i;
        if (!provider_field(io, &row->provider, true) || !qa_source_save_string(io, &row->content) ||
            !qa_source_save_string(io, &row->text) || !vector_field(io, &row->origin) ||
            !vector_field(io, &row->angles) || !vector_field(io, &row->color) ||
            !finite_field(io, &row->alpha) || !finite_field(io, &row->cell_size) || row->cell_size <= 0 ||
            !qa_source_save_f64(io, &row->expires) || !isfinite(row->expires) ||
            !qa_source_save_u64(io, &row->first_frame) || !qa_source_save_bool(io, &row->timed) ||
            !qa_source_save_bool(io, &row->observed) || !qa_source_save_bool(io, &row->billboard) ||
            !qa_source_save_bool(io, &row->depth_test) || !row->content || !row->text ||
            (row->timed && row->observed) || (!row->observed && row->first_frame))
            return event_fail(io, QA_ERROR_FORMAT, "World text continuation lost its Source geometry or lifetime");
    }
    for (size_t i = 0; i < store->registration_count; ++i) {
        application_unified_event_registration *row = store->registrations + i;
        uint32_t kind = (uint32_t)row->kind;
        if (!provider_field(io, &row->provider, true) || !qa_source_save_string(io, &row->path) ||
            !row->path || !store->resource_count ||
            !qa_source_save_count(io, &row->resource, store->resource_count - 1) ||
            !qa_source_save_u32(io, &kind) || kind > QA_NATIVE_HOST_IMAGE ||
            !qa_source_save_u64(io, &row->custody) || row->custody > store->resources[row->resource].custody_count)
            return event_fail(io, QA_ERROR_FORMAT, "Source registration lost its actual retained resource");
        row->kind = (qa_native_host_resource_kind)kind;
        for (size_t j = 0; j < i; ++j)
            if (row->provider == store->registrations[j].provider && row->path == store->registrations[j].path &&
                row->kind == store->registrations[j].kind)
                return event_fail(io, QA_ERROR_FORMAT, "Source registration repeats a current path");
    }
    return true;
}

static bool custody_valid(const application_unified_event_resource *row, qa_application *app,
    uint64_t pool, uint64_t resource, uint64_t view_id, const qa_vfs_acquisition *opening, qa_error *error)
{
    const qa_resource *actual = qa_application_content_resource(app->content_graph, pool, resource);
    const qa_vfs *view = qa_application_content_view(app->content_graph, view_id);
    const char *content = qa_strings_cstr(qa_session_strings(app->session), row->content);
    const char *path = qa_strings_cstr(qa_session_strings(app->session), row->path);
    qa_bytes content_bytes = qa_strings_text(qa_session_strings(app->session), row->content);
    qa_bytes path_bytes = qa_strings_text(qa_session_strings(app->session), row->path);
    if (!actual || !view || !content || !path || strlen(content) != content_bytes.size ||
        strlen(path) != path_bytes.size || !opening->path || strcmp(opening->path, path) ||
        opening->resource_id != qa_resource_id(actual) ||
        qa_resource_pool_find(qa_vfs_resources(view), qa_resource_id(actual)) != actual ||
        !opening->opening_present || !qa_vfs_acquisition_retained(view, opening, error))
        return application_fail(error, QA_ERROR_FORMAT, "Source dictionary cannot bind its decoded immutable CONTENT opening");
    qa_product product = {.identity = content};
    qa_unified_document *key = NULL;
    char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    if (!application_unified_resource_key(&product, path, actual, &key, id, error)) return false;
    qa_bytes bytes = qa_json_source(qa_unified_document_json(key), qa_unified_document_root(key));
    bool same = !strcmp(id, row->id) && bytes.size == row->key.size && !memcmp(bytes.data, row->key.data, bytes.size);
    qa_unified_document_destroy(key);
    return same || application_fail(error, QA_ERROR_FORMAT, "Source dictionary key differs from its actual saved opening bytes");
}

static bool resource_bindings(event_store *store, qa_application *app, qa_error *error)
{
    for (size_t i = 0; i < store->owner_count; ++i) {
        const application_unified_event_owner *owner = store->owners + i;
        if (!owner->active) continue;
        application_unified_event_source source;
        const char *content = qa_strings_cstr(qa_session_strings(app->session), owner->content);
        if (!application_unified_event_source_read(app, owner->provider, &source, error)) return false;
        if (!content || !source.product || strcmp(source.product->identity, content) ||
            source.component != (owner->generation != 0))
            return application_fail(error, QA_ERROR_FORMAT, "Source activation differs from its actual restored content owner");
    }
    for (size_t i = 0; i < store->resource_count; ++i) {
        application_unified_event_resource *row = store->resources + i;
        if (!custody_valid(row, app, row->saved_pool, row->saved_resource, row->saved_view, &row->opening, error)) return false;
        for (size_t j = 0; j < row->custody_count; ++j) {
            application_unified_event_resource_custody *held = row->custodies + j;
            if (!custody_valid(row, app, held->saved_pool, held->saved_resource, held->saved_view, &held->opening, error)) return false;
        }
    }
    for (size_t i = 0; i < store->registration_count; ++i) {
        const application_unified_event_registration *row = store->registrations + i;
        const application_unified_event_resource *resource = store->resources + row->resource;
        application_unified_event_source source;
        if (!application_unified_event_source_read(app, row->provider, &source, error)) return false;
        const char *path = qa_strings_cstr(qa_session_strings(app->session), row->path);
        const char *content = qa_strings_cstr(qa_session_strings(app->session), resource->content);
        qa_bytes path_bytes = qa_strings_text(qa_session_strings(app->session), row->path);
        if (!source.product || !path || !*path || !content ||
            strlen(path) != path_bytes.size || strcmp(source.product->identity, content) ||
            (unsigned)row->kind > QA_NATIVE_HOST_IMAGE)
            return application_fail(error, QA_ERROR_FORMAT, "Source registration differs from its actual restored source and resource path");
    }
    /* Validate the entire dictionary before transferring any graph owner. */
    for (size_t i = 0; i < store->resource_count; ++i) {
        application_unified_event_resource *row = store->resources + i;
        if (!application_save_content_event_pool(app->content_graph, row->saved_pool, &row->pool, error)) return false;
        if (!application_save_content_event_view(app->content_graph, row->saved_view, &row->view, error)) return false;
        row->resource = (qa_resource *)qa_application_content_resource(app->content_graph,
            row->saved_pool, row->saved_resource);
        qa_resource_retain(row->resource);
        for (size_t j = 0; j < row->custody_count; ++j) {
            application_unified_event_resource_custody *held = row->custodies + j;
            if (!application_save_content_event_pool(app->content_graph, held->saved_pool, &held->pool, error) ||
                !application_save_content_event_view(app->content_graph, held->saved_view, &held->view, error)) return false;
            held->resource = (qa_resource *)qa_application_content_resource(app->content_graph,
                held->saved_pool, held->saved_resource);
            qa_resource_retain(held->resource);
        }
    }
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
        application_event_record event = store->builtin[i];
        if (!builtin_field(io, store, &event)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) store->builtin[i] = event;
    }
    for (size_t i = 0; i < store->counts[1]; ++i) {
        application_q2_map_event_record event = store->q2_map[i];
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
        application_protocol_record event = store->protocol[i];
        if (!protocol_field(io, store, &event)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) store->protocol[i] = event;
    }
    if (!journal_rows(io, store) || !normalized_rows(io, store) || !persistent_rows(io, store)) return false;
    for (size_t i = 0; i < store->counts[4]; ++i) {
        const qa_application_protocol_event *event = &store->protocol[i].event;
        for (size_t j = 0; j < event->resource_count; ++j) {
            const char *key = event->resources[j].resource_key;
            if (!*key) continue;
            bool found = false;
            for (size_t r = 0; r < store->resource_count; ++r)
                found = found || (!strcmp(key, store->resources[r].id) &&
                    event->resources[j].resource_custody <= store->resources[r].custody_count);
            if (!found) return event_fail(io, QA_ERROR_FORMAT, "Source protocol resource receipt is outside its retained dictionary");
        }
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
        !app->q2_player_event_count && !app->protocol_event_count && !app->event_journal_count && !app->unified_event_count;
}

static void install_store(qa_application *app, event_store *store)
{
    free(app->events);
    free(app->q2_map_events);
    free(app->q3_map_events);
    free(app->q2_player_events);
    free(app->protocol_events);
    free(app->event_journal);
    free(app->unified_events);
    application_unified_persistent_dispose(app);
    free(app->unified_event_owners);
    application_unified_events_resources_dispose(app);
    free(app->unified_world_text);
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
    app->event_journal = store->journal;
    app->event_journal_count = store->journal_count;
    app->event_journal_capacity = store->journal_capacity;
    app->event_sequence = store->sequence;
    app->simulation_event_sequence = store->simulation_sequence;
    app->unified_events = store->unified; app->unified_event_count = store->unified_count;
    app->unified_event_capacity = store->unified_capacity; app->unified_event_sequence = store->unified_sequence;
    app->presentation_event_sequence = store->presentation_sequence;
    app->unified_persistent = store->persistent; app->unified_persistent_count = store->persistent_count;
    app->unified_persistent_capacity = store->persistent_capacity; app->unified_persistent_revision = store->persistent_revision;
    app->unified_event_owners = store->owners; app->unified_event_owner_count = store->owner_count;
    app->unified_event_owner_capacity = store->owner_capacity; app->unified_event_owner_generation = store->owner_generation;
    app->unified_event_resources = store->resources; app->unified_event_resource_count = store->resource_count;
    app->unified_event_resource_capacity = store->resource_capacity;
    app->unified_event_registrations = store->registrations;
    app->unified_event_registration_count = store->registration_count;
    app->unified_event_registration_capacity = store->registration_capacity;
    app->unified_event_registration_revision = store->registration_revision;
    app->unified_world_text = store->world_text; app->unified_world_text_count = store->world_text_count;
    app->unified_world_text_capacity = store->world_text_capacity;
    app->unified_world_text_revision = store->world_text_revision; app->unified_world_text_map = store->world_text_map;
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
    event_store store = {.application = app};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, app->session, bytes, error) && prefix(&io, &store) &&
        allocate_store(&io, &store) && rows(&io, &store) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) ok = resource_bindings(&store, app, error);
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
    if (!app || app->operation != APPLICATION_IDLE || app->client_preparation || !app->session || !app->world ||
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
