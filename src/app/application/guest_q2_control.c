#include "guest_native_q2_private.h"
#include "guest_q2_control.h"
#include "guest_native_q2_input.h"
#include "qa/native_observe.h"
#include "qa/native_host_q2_wire.h"
#include <limits.h>

/* API2023 x64 public pmove_t and trace_t, from rerelease/game.h and
 * compat/q2/rerelease/layouts.ts. These are public ABI fields, not mod offsets. */
enum {
    RR_PM_ORIGIN = 4, RR_PM_FLAGS = 28, RR_PM_HEIGHT = 48,
    RR_PM_BUTTONS = 53, RR_PM_MINS = 3180, RR_PM_PLAYER = 3248,
    RR_TRACE_BYTES = 96
};

typedef struct control_client {
    struct application_native_q2 *engine;
    qa_native_host *host;
    qa_native_instance *native;
    qa_actor_id actor;
    qa_native_address player;
    uint32_t slot;
} control_client;

typedef struct control_frame {
    struct control_frame *previous;
    control_client client;
    qa_native_address address;
    qa_bounds accepted, requested;
    uint16_t previous_duck;
    uint8_t previous_height;
    bool has_requested;
} control_frame;

struct application_q2_control {
    struct application_native_q2 *engine;
    qa_native_module *module;
    qa_native_declaration *declaration;
    qa_native_host *host;
    qa_native_instance *native;
    uint32_t game_api_rva, pmove_rva, dimensions_rva, trace_rva, global_rva;
    qa_native_address trace, global;
    qa_native_entry_observer *pmove_hook, *dimensions_hook;
    control_frame *current;
    qa_native_abi abi;
    bool active, raw_only;
};

static bool client_live(const control_client *client, qa_error *error)
{
    struct application_native_q2 *engine = client->engine;
    application_provider *provider = engine->provider;
    qa_application *app = provider->application;
    if (provider->state.native.q2_engine != engine || !engine->initialized ||
        engine->baseline || engine->shutting_down || provider->close_pending ||
        provider->state.native.host != client->host ||
        qa_native_host_instance(client->host) != client->native ||
        !qa_actors_get(qa_session_actors(app->session), client->actor) ||
        !engine->clients[client->slot].connected || !engine->clients[client->slot].begun ||
        engine->clients[client->slot].disconnect_started ||
        !qa_actor_id_equal(engine->clients[client->slot].actor, client->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native movement lost its original client generation");
    qa_native_slot_binding binding; qa_native_address address;
    if (!qa_native_slot(client->native, client->slot, &binding, error) ||
        !qa_native_entity_address(client->native, client->slot, &address, error)) return false;
    if (binding.kind == QA_NATIVE_SLOT_FREE || binding.slot != client->slot || binding.owner != provider->owner ||
        binding.source_slot != client->slot || !qa_actor_id_equal(binding.actor, client->actor) ||
        address != client->player)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native movement changed its physical source client binding");
    return true;
}

static bool client_for(struct application_native_q2 *engine, qa_actor_id actor,
    control_client *out, qa_error *error)
{
    if (!engine || !out || !engine->provider->state.native.host ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native client outputs require their original game host");
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i)
        if (qa_actor_id_equal(engine->clients[i].actor, actor)) { slot = i; break; }
    if (!slot) return application_fail(error, QA_ERROR_NOT_FOUND, "Native client outputs have no actual source slot");
    qa_native_host *host = engine->provider->state.native.host;
    control_client client = {.engine = engine, .host = host,
        .native = qa_native_host_instance(host), .actor = actor, .slot = slot};
    if (!qa_native_entity_address(client.native, slot, &client.player, error) ||
        !client_live(&client, error)) return false;
    *out = client; return true;
}

static bool active_outputs(const control_client *client, application_client_outputs *out,
    qa_error *error)
{
    qa_application *app = client->engine->provider->application;
    application_client_outputs values; qa_combat_state combat;
    if (!client_live(client, error) ||
        !application_qc_control_outputs(app, client->actor, &values, error) ||
        !client_live(client, error)) return false;
    if (!values.has_view_offset && !values.has_mode && !values.has_stance && !values.has_body_bounds) {
        *out = values; return true;
    }
    if (!qa_combat_read_traits(app->combat, client->actor, &combat, error) ||
        !client_live(client, error)) return false;
    if (combat.health <= 0) { *out = (application_client_outputs){0}; return true; }
    qa_q2_player player;
    bool ok = qa_native_host_q2_player(client->host, client->slot, &player, error) &&
        client_live(client, error);
    if (ok) {
        int32_t intermission = client->engine->profile == QA_NATIVE_Q2_GAME_API3 ? 4 : 6;
        *out = player.pmove.type == intermission ? (application_client_outputs){0} : values;
    }
    return ok;
}

bool application_q2_control_outputs(struct application_native_q2 *engine, qa_actor_id actor,
    application_client_outputs *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Native client output destination is absent");
    control_client client;
    if (!client_for(engine, actor, &client, error)) return false;
    ++engine->calls;
    application_client_outputs values;
    bool ok = active_outputs(&client, &values, error);
    --engine->calls;
    if (ok) *out = values;
    return ok;
}

bool application_q2_control_crouched(struct application_native_q2 *engine, qa_actor_id actor,
    bool *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Native crouch destination is absent");
    control_client client;
    if (!client_for(engine, actor, &client, error)) return false;
    ++engine->calls;
    qa_q2_player player;
    bool ok = qa_native_host_q2_player(client.host, client.slot, &player, error) &&
        client_live(&client, error);
    if (ok) *out = ((uint32_t)player.pmove.flags & 1u) != 0;
    --engine->calls;
    return ok;
}


static qa_vec3 load_vector(const uint8_t *bytes)
{ return qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8)); }

static void store_vector(uint8_t *bytes, qa_vec3 value)
{
    float components[] = {value.x, value.y, value.z};
    for (size_t i = 0; i < 3; ++i) {
        uint32_t bits; memcpy(&bits, &components[i], sizeof(bits)); qa_store_u32le(bytes + i * 4, bits);
    }
}

static bool read_pointer(qa_native_instance *native, qa_native_address address,
    qa_native_address *out, qa_error *error)
{
    uint8_t bytes[8];
    if (!qa_native_read(native, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_u64le(bytes); return true;
}

static bool frame_live(struct application_q2_control *p, const control_frame *frame, qa_error *error)
{
    struct application_native_q2 *engine = p->engine;
    bool raw=p->raw_only && engine->input_stage &&
        qa_actor_id_equal(engine->input_stage->actor,frame->client.actor) &&
        engine->input_stage->current(engine->input_stage->context,frame->client.actor) &&
        application_native_q2_declared_raw_capable(engine->provider);
    if (!p->active || (!application_q2_control_body_admitted(engine) && !raw) || engine->source_control != p || p->host != frame->client.host ||
        p->native != frame->client.native || !engine->calls || engine->current_client != frame->client.slot ||
        (application_provider_for(engine->provider->application, frame->client.actor,
            QA_ROLE_MOVEMENT, NULL) != engine->provider &&
         (!engine->input_stage || !qa_actor_id_equal(engine->input_stage->actor,frame->client.actor) ||
          !engine->input_stage->current(engine->input_stage->context,frame->client.actor))))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Pmove lost its actual movement invocation");
    if (!client_live(&frame->client, error)) return false;
    qa_native_address player;
    if (!read_pointer(p->native, frame->address + RR_PM_PLAYER, &player, error)) return false;
    return player == frame->client.player ||
        application_fail(error, QA_ERROR_NOT_FOUND, "Native Pmove replaced its public player pointer");
}

static const qa_native_type plane_fields[] = {
    {.kind = QA_NATIVE_F32, .count = 3}, {.kind = QA_NATIVE_F32, .count = 1},
    {.kind = QA_NATIVE_U8, .count = 1}, {.kind = QA_NATIVE_U8, .count = 1}, {.kind = QA_NATIVE_U8, .count = 2}
};
static const qa_native_type trace_fields[] = {
    {.kind = QA_NATIVE_U8, .count = 1}, {.kind = QA_NATIVE_U8, .count = 1},
    {.kind = QA_NATIVE_F32, .count = 1}, {.kind = QA_NATIVE_F32, .count = 3},
    {.kind = QA_NATIVE_BYTES, .fields = plane_fields, .field_count = 5, .count = 1},
    {.kind = QA_NATIVE_ADDRESS, .count = 1}, {.kind = QA_NATIVE_U32, .count = 1},
    {.kind = QA_NATIVE_ADDRESS, .count = 1},
    {.kind = QA_NATIVE_BYTES, .fields = plane_fields, .field_count = 5, .count = 1},
    {.kind = QA_NATIVE_ADDRESS, .count = 1}
};
static const qa_native_type trace_parameters[] = {
    {.kind = QA_NATIVE_ADDRESS, .count = 1}, {.kind = QA_NATIVE_ADDRESS, .count = 1},
    {.kind = QA_NATIVE_ADDRESS, .count = 1}, {.kind = QA_NATIVE_ADDRESS, .count = 1},
    {.kind = QA_NATIVE_U32, .count = 1}
};

static bool expansion_clear(struct application_q2_control *p, control_frame *frame,
    qa_bounds bounds, bool *out, qa_error *error)
{
    uint8_t vectors[36], trace_bytes[RR_TRACE_BYTES]; qa_native_address scratch;
    if (!qa_native_read(p->native, frame->address + RR_PM_ORIGIN, vectors, 12, error)) return false;
    if (!qa_vec_finite(load_vector(vectors)))
        return application_fail(error, QA_ERROR_FORMAT, "Native body expansion has a nonfinite original origin");
    store_vector(vectors + 12, bounds.mins); store_vector(vectors + 24, bounds.maxs);
    if (!qa_native_allocate(p->native, sizeof(vectors), INT32_MIN + 13, &scratch, error)) return false;
    qa_native_signature signature = {.abi = p->abi, .parameters = trace_parameters, .parameter_count = 5,
        .result = {.kind = QA_NATIVE_BYTES, .fields = trace_fields, .field_count = 10, .count = 1}};
    qa_native_value arguments[] = {
        {.type = QA_NATIVE_ADDRESS, .as.address = scratch},
        {.type = QA_NATIVE_ADDRESS, .as.address = scratch + 12},
        {.type = QA_NATIVE_ADDRESS, .as.address = scratch + 24},
        {.type = QA_NATIVE_ADDRESS, .as.address = scratch}, {.type = QA_NATIVE_U32, .as.u32 = 0}
    };
    qa_native_value result = {.type = QA_NATIVE_BYTES, .as.bytes = {trace_bytes, sizeof(trace_bytes)}};
    bool ok = qa_native_write(p->native, scratch, (qa_bytes){vectors, sizeof(vectors)}, error) &&
        frame_live(p, frame, error) && qa_native_invoke(p->native, p->trace, &signature, arguments, 5, &result, error) &&
        frame_live(p, frame, error);
    if (ok && (result.type != QA_NATIVE_BYTES || result.as.bytes.size != sizeof(trace_bytes)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Original body expansion returned an invalid trace aggregate");
    bool clear = ok && trace_bytes[0] == 0;
    qa_error cleanup = {0};
    if (!qa_native_free(p->native, scratch, &cleanup) && ok) { ok = false; if (error) *error = cleanup; }
    if (ok) *out = clear;
    return ok;
}

static bool apply_dimensions(struct application_q2_control *p, control_frame *frame, qa_error *error)
{
    if (!frame_live(p, frame, error)) return false;
    uint8_t native_bounds[24], flags[2], height;
    qa_bounds desired = frame->requested;
    if (!frame->has_requested) {
        if (!qa_native_read(p->native, frame->address + RR_PM_MINS, native_bounds, sizeof(native_bounds), error)) return false;
        desired = (qa_bounds){load_vector(native_bounds), load_vector(native_bounds + 12)};
    }
    if (!qa_bounds_valid(desired)) return application_fail(error, QA_ERROR_FORMAT, "Native dimensions produced invalid public bounds");
    qa_bounds previous = frame->accepted;
    bool expands = qa_bounds_expands(previous, desired);
    bool clear = true;
    if (expands && !expansion_clear(p, frame, desired, &clear, error)) return false;
    if (!frame_live(p, frame, error) ||
        !qa_native_read(p->native, frame->address + RR_PM_FLAGS, flags, sizeof(flags), error) ||
        !qa_native_read(p->native, frame->address + RR_PM_HEIGHT, &height, 1, error)) return false;
    if (!clear) {
        qa_store_u16le(flags, (uint16_t)((qa_load_u16le(flags) & ~1u) | frame->previous_duck));
        height = frame->previous_height;
        if (!qa_native_write(p->native, frame->address + RR_PM_FLAGS, (qa_bytes){flags, sizeof(flags)}, error) ||
            !qa_native_write(p->native, frame->address + RR_PM_HEIGHT, (qa_bytes){&height, 1}, error)) return false;
        desired = previous;
    }
    store_vector(native_bounds, desired.mins); store_vector(native_bounds + 12, desired.maxs);
    if (!qa_native_write(p->native, frame->address + RR_PM_MINS,
            (qa_bytes){native_bounds, sizeof(native_bounds)}, error)) return false;
    frame->accepted = desired; frame->previous_duck = qa_load_u16le(flags) & 1u; frame->previous_height = height;
    return true;
}

static bool dimensions_entry(void *context, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    struct application_q2_control *p = context;
    if (native != p->native || count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original dimensions observer has a different source boundary");
    ++p->engine->calls;
    bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
    control_frame *frame = p->current;
    if (ok && p->active && frame) {
        qa_native_address current;
        ok = read_pointer(native, p->global, &current, error);
        if (ok && current == frame->address) ok = apply_dimensions(p, frame, error);
    }
    --p->engine->calls; return ok;
}

#include "guest_q2_control_input.h"

static bool pmove_entry(void *context, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    struct application_q2_control *p = context;
    struct application_native_q2 *engine = p->engine;
    if (native != p->native || count != 1 || arguments[0].type != QA_NATIVE_ADDRESS || !arguments[0].as.address)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Pmove observer has a different public invocation");
    ++engine->calls;
    uint32_t slot = engine->current_client;
    if (!p->active || !slot || slot >= 257 || (p->raw_only && !engine->input_stage)) {
        bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
        --engine->calls; return ok;
    }
    control_frame frame = {.previous = p->current, .address = arguments[0].as.address};
    bool ok = client_for(engine, engine->clients[slot].actor, &frame.client, error);
    qa_native_address player = 0;
    if (ok) ok = read_pointer(native, frame.address + RR_PM_PLAYER, &player, error);
    if (ok && player != frame.client.player) {
        ok = qa_native_invoke_original(binding, arguments, count, result, error);
        --engine->calls; return ok;
    }
    if(p->raw_only) {
        qa_body_state source;
        if(ok) ok=frame_live(p,&frame,error)&&
            qa_world_body_read(engine->world,frame.client.actor,&source,error);
        if(ok&&!qa_bounds_valid(source.bounds))
            ok=application_fail(error,QA_ERROR_FORMAT,"Declared Pmove lost its actual Source body bounds");
        if(ok) frame.accepted=source.bounds;
        p->current=&frame;
        bool selected=false;
        if(ok) ok=raw_move(&frame,&selected,error);
        if(ok&&selected) *result=(qa_native_value){.type=QA_NATIVE_VOID};
        if(ok&&!selected) ok=frame_live(p,&frame,error)&&
            qa_native_invoke_original(binding,arguments,count,result,error)&&
            frame_live(p,&frame,error)&&raw_native_commit(&frame,error);
        p->current=frame.previous;
        --engine->calls; return ok;
    }
    application_client_outputs outputs = {0}; qa_body_state body;
    uint8_t flags[2], height = 0, before_type[4], projected_type[4];
    bool projected = false;
    if (ok) ok = frame_live(p, &frame, error) && active_outputs(&frame.client, &outputs, error) &&
        frame_live(p, &frame, error) && qa_world_body_read(engine->world, frame.client.actor, &body, error) &&
        frame_live(p, &frame, error);
    if (ok && !qa_bounds_valid(body.bounds)) ok = application_fail(error, QA_ERROR_FORMAT, "Native Pmove lost its actual current body bounds");
    if (ok) {
        frame.accepted = body.bounds; frame.has_requested = outputs.has_body_bounds; frame.requested = outputs.body_bounds;
        ok = qa_native_read(native, frame.address + RR_PM_FLAGS, flags, sizeof(flags), error) &&
            qa_native_read(native, frame.address + RR_PM_HEIGHT, &height, 1, error);
        frame.previous_duck = ok ? qa_load_u16le(flags) & 1u : 0; frame.previous_height = height;
    }
    if (ok && outputs.has_stance) {
        uint8_t buttons;
        ok = qa_native_read(native, frame.address + RR_PM_BUTTONS, &buttons, 1, error);
        if (ok) {
            buttons = outputs.crouched ? (uint8_t)(buttons | 16u) : (uint8_t)(buttons & ~16u);
            ok = qa_native_write(native, frame.address + RR_PM_BUTTONS, (qa_bytes){&buttons, 1}, error);
        }
    }
    if (ok && outputs.has_mode) {
        if ((unsigned)outputs.mode > QA_MOVEMENT_MODE_FREEZE)
            ok = application_fail(error, QA_ERROR_FORMAT, "Native movement output has an invalid mode");
        else {
            qa_store_u32le(projected_type, outputs.mode == QA_MOVEMENT_MODE_NORMAL ? 0u :
                outputs.mode == QA_MOVEMENT_MODE_NOCLIP ? 2u : 6u);
            ok = qa_native_read(native, frame.address, before_type, sizeof(before_type), error);
            projected = ok;
            if (ok) ok = qa_native_write(native, frame.address, (qa_bytes){projected_type, sizeof(projected_type)}, error);
        }
    }
    p->current = &frame;
    bool selected=false;
    if(ok) ok=raw_move(&frame,&selected,error);
    if(ok&&selected) *result=(qa_native_value){.type=QA_NATIVE_VOID};
    if (ok&&!selected) ok = frame_live(p, &frame, error) && qa_native_invoke_original(binding, arguments, count, result, error) &&
        frame_live(p, &frame, error) && raw_native_commit(&frame,error);
    if (projected) {
        qa_error cleanup = {0}; uint8_t current[4];
        bool retained = frame_live(p, &frame, &cleanup);
        bool restored = !retained || (qa_native_read(native, frame.address, current, sizeof(current), &cleanup) &&
            (memcmp(current, projected_type, sizeof(current)) != 0 ||
             qa_native_write(native, frame.address, (qa_bytes){before_type, sizeof(before_type)}, &cleanup)));
        if (!restored && ok) { ok = false; if (error) *error = cleanup; }
    }
    p->current = frame.previous;
    --engine->calls; return ok;
}

static bool declared_rva(struct application_native_q2 *engine, const char *path,
    size_t bytes, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_native_declaration_u64(engine->declaration, path, &value, error)) return false;
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    if (!value || value > UINT32_MAX || value > info.image.image_bytes || bytes > info.image.image_bytes - value)
        return application_fail(error, QA_ERROR_FORMAT, "Declared native movement address exceeds its actual artifact");
    *out = (uint32_t)value; return true;
}

bool application_q2_control_prepare(struct application_native_q2 *engine, qa_error *error)
{
    if (!engine) return application_fail(error, QA_ERROR_ARGUMENT, "Native movement producer has no source owner");
    if (engine->profile != QA_NATIVE_Q2_GAME_API2023 || !engine->declaration) return true;
    qa_json_kind kind = qa_native_declaration_kind(engine->declaration, "/world/movement/body");
    bool raw_only=kind==QA_JSON_INVALID && application_native_q2_declared_raw_capable(engine->provider);
    if (kind == QA_JSON_INVALID && !raw_only) return true;
    if (!raw_only && kind != QA_JSON_OBJECT) return application_fail(error, QA_ERROR_FORMAT, "Native body movement declaration is not an object");
    if (engine->source_control) return application_fail(error, QA_ERROR_ARGUMENT, "Native movement producer is already prepared");
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    /* declaration_load already checked the selected artifact path and API. */
    if (info.profile != QA_NATIVE_Q2_GAME_API2023 || info.image.target.pointer_bytes != 8 ||
        info.image.target.arch != QA_NATIVE_ARCH_X86_64)
        return application_fail(error, QA_ERROR_FORMAT, "Native body movement declaration differs from its original API2023 artifact");
    struct application_q2_control *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(error, QA_ERROR_MEMORY, "Retaining original native movement producer");
    if(raw_only) {
        p->engine=engine; p->module=engine->provider->state.native.module;
        p->declaration=engine->declaration; p->abi=info.image.target.abi; p->raw_only=true;
        engine->source_control=p; return true;
    }
    bool ok = declared_rva(engine, "/world/movement/gameApi", 1, &p->game_api_rva, error) &&
        declared_rva(engine, "/world/movement/pmove", 1, &p->pmove_rva, error) &&
        declared_rva(engine, "/world/movement/body/dimensions", 1, &p->dimensions_rva, error) &&
        declared_rva(engine, "/world/movement/body/trace", 1, &p->trace_rva, error) &&
        declared_rva(engine, "/world/movement/body/movementGlobal", 8, &p->global_rva, error);
    if (ok) ok = qa_native_module_mutable_range(engine->provider->state.native.module, p->global_rva, 8, error);
    if (ok && (p->pmove_rva == p->dimensions_rva || p->pmove_rva == p->trace_rva ||
        p->dimensions_rva == p->trace_rva || p->game_api_rva == p->pmove_rva ||
        p->game_api_rva == p->dimensions_rva || p->game_api_rva == p->trace_rva))
        ok = application_fail(error, QA_ERROR_FORMAT, "Declared native movement entries alias incompatible signatures");
    if (!ok) { free(p); return false; }
    p->engine = engine; p->module = engine->provider->state.native.module;
    p->declaration = engine->declaration; p->abi = info.image.target.abi; engine->source_control = p;
    return true;
}

bool application_q2_control_body_admitted(const struct application_native_q2 *engine)
{
    return engine && engine->profile == QA_NATIVE_Q2_GAME_API2023 && engine->source_control &&
        !engine->source_control->raw_only && engine->source_control->engine == engine && engine->source_control->declaration == engine->declaration &&
        engine->source_control->module == engine->provider->state.native.module;
}

bool application_q2_control_activate(struct application_native_q2 *engine, qa_error *error)
{
    struct application_q2_control *p = engine ? engine->source_control : NULL;
    if (!p) return true;
    qa_native_host *host = engine->provider->state.native.host;
    bool raw=p->raw_only && p->engine==engine && p->module==engine->provider->state.native.module &&
        p->declaration==engine->declaration && application_native_q2_declared_raw_capable(engine->provider);
    if ((!application_q2_control_body_admitted(engine) && !raw) || !engine->initialized || !host || p->current || engine->shutting_down ||
        qa_native_host_profile(host) != QA_NATIVE_Q2_GAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native movement activation requires its initialized actual game host");
    qa_native_instance *native = qa_native_host_instance(host);
    if ((p->pmove_hook || p->dimensions_hook) && (p->host != host || p->native != native))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native movement observers still own another source instance");
    if (p->active) return true;
    p->host = host; p->native = native;
    qa_native_address expected, game_api, pmove, dimensions;
    if (!qa_native_entry_address(native,"Pmove",&pmove,error)) return false;
    if(!p->raw_only) {
        if (!qa_native_export(native, "GetGameAPI", &game_api, error) ||
            !qa_native_rva(native, p->game_api_rva, 1, &expected, error)) return false;
        if (game_api != expected) return application_fail(error, QA_ERROR_FORMAT, "Native movement metadata differs from the actual GetGameAPI export");
        if (!qa_native_rva(native, p->pmove_rva, 1, &expected, error)) return false;
        if (pmove != expected) return application_fail(error, QA_ERROR_FORMAT, "Native movement metadata differs from the actual Pmove API callback");
        if (!qa_native_rva(native, p->dimensions_rva, 1, &dimensions, error) ||
            !qa_native_rva(native, p->trace_rva, 1, &p->trace, error) ||
            !qa_native_rva(native, p->global_rva, 8, &p->global, error)) return false;
    }
    const qa_native_signature *pmove_signature = qa_native_entry_signature(native, "Pmove");
    if (!pmove_signature || pmove_signature->abi != p->abi || pmove_signature->variadic ||
        pmove_signature->parameter_count != 1 || pmove_signature->parameters[0].kind != QA_NATIVE_ADDRESS ||
        pmove_signature->parameters[0].count != 1 || pmove_signature->result.kind != QA_NATIVE_VOID)
        return application_fail(error, QA_ERROR_FORMAT, "Native movement lost its public void Pmove(pointer) ABI");
    qa_native_signature dimensions_signature = {.abi = p->abi, .result = {.kind = QA_NATIVE_VOID, .count = 1}};
    if (!p->raw_only && !p->dimensions_hook && !qa_native_observe_entry(native, dimensions, &dimensions_signature,
            dimensions_entry, p, &p->dimensions_hook, error)) return false;
    if (!p->pmove_hook && !qa_native_observe_entry(native, pmove, pmove_signature,
            pmove_entry, p, &p->pmove_hook, error)) return false;
    p->active = true; return true;
}

bool application_q2_control_suspend(struct application_native_q2 *engine, qa_error *error)
{
    struct application_q2_control *p = engine ? engine->source_control : NULL;
    if (!p) return true;
    if (p->current || engine->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native movement suspension requires drained original calls");
    p->active = false;
    if (p->pmove_hook && !qa_native_unobserve_entry(p->pmove_hook, error)) return false;
    p->pmove_hook = NULL;
    if (p->dimensions_hook && !qa_native_unobserve_entry(p->dimensions_hook, error)) return false;
    p->dimensions_hook = NULL; p->native = NULL; p->host = NULL; p->trace = p->global = 0;
    return true;
}

bool application_q2_control_close(struct application_native_q2 *engine, qa_error *error)
{
    struct application_q2_control *p = engine ? engine->source_control : NULL;
    if (!p) return true;
    if (!application_q2_control_suspend(engine, error)) return false;
    free(p); engine->source_control = NULL;
    return true;
}
