#include "guest_native_q2_private.h"
#include "guest_native_q2_continuation.h"
#include "guest_native_q2_combat.h"
#include "guest_native_q2_private_state.h"
#include "qa/source_save.h"
#include "qa/json.h"

enum { CONTINUATION_BYTES = 8u * 1024u * 1024u, CONTINUATION_FIELDS = 128 };
typedef struct scalar_field { uint64_t rva; uint32_t bytes; } scalar_field;
typedef struct continuation_profile {
    qa_json_document *document;
    scalar_field fields[CONTINUATION_FIELDS];
    size_t count;
    uint32_t client_pointer, connected, connected_bytes, spawned, entity_bytes;
    uint32_t size_offset, base_offset;
    bool classic;
} continuation_profile;
struct application_native_q2_continuation {
    qa_sha256_digest artifact, declaration, launch;
    qa_buffer fields[CONTINUATION_FIELDS];
    size_t count;
    qa_buffer private_bytes;
    struct application_q2_private_state *private_state;
    struct { bool present, connected, spawned; qa_actor_id actor; } clients[257];
};
static bool number(const qa_json_document *doc, qa_json_id object, const char *name,
    uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, qa_json_get(doc, object, name), &value, error)) return false;
    if (value > UINT32_MAX) return application_fail(error, QA_ERROR_FORMAT, "Native continuation field exceeds its word");
    *out = (uint32_t)value; return true;
}
static bool layout_field(const qa_json_document *doc, qa_json_id layout, const char *name,
    const char *storage, uint32_t bytes, uint32_t *offset, qa_error *error)
{
    uint32_t extent; qa_json_id fields = qa_json_get(doc, layout, "fields");
    if (!number(doc, layout, "byteLength", &extent, error) || qa_json_type(doc, fields) != QA_JSON_ARRAY)
        return application_fail(error, QA_ERROR_FORMAT, "Native continuation requires an explicit typed source layout");
    bool found = false;
    for (size_t i = 0; i < qa_json_size(doc, fields); ++i) {
        qa_json_id field = qa_json_at(doc, fields, i);
        if (!qa_json_string_equal(doc, qa_json_get(doc, field, "name"), name)) continue;
        uint32_t count;
        if (found || !qa_json_string_equal(doc, qa_json_get(doc, field, "storage"), storage) ||
            !number(doc, field, "count", &count, error) || count != 1 ||
            !number(doc, field, "offset", offset, error) || *offset > extent || bytes > extent - *offset)
            return application_fail(error, QA_ERROR_FORMAT, "Native continuation field changes its original scalar type or extent");
        found = true;
    }
    return found || application_fail(error, QA_ERROR_UNSUPPORTED, "Native continuation lacks its original typed field");
}
static bool profile_read(struct application_native_q2 *engine, continuation_profile *out, qa_error *error)
{
    if (!engine->declaration || engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native continuation has no qualified original game declaration");
    if (!qa_json_parse(qa_native_declaration_primary(engine->declaration), &out->document, error)) return false;
    const qa_json_document *doc = out->document; qa_json_id root = qa_json_root(doc);
    qa_json_id section = qa_json_get(doc, root, "continuation");
    if (qa_json_type(doc, section) != QA_JSON_OBJECT)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native artifact has no complete RNG/private-client continuation qualifier");
    uint32_t schema;
    if (!number(doc, section, "schema", &schema, error) || schema != 1)
        return application_fail(error, QA_ERROR_FORMAT, "Native continuation declaration schema differs");
    qa_json_id rng = qa_json_get(doc, section, "rng"), fields = qa_json_get(doc, rng, "fields");
    if (!qa_json_string_equal(doc, qa_json_get(doc, rng, "kind"), "image-scalars") ||
        !qa_json_string_equal(doc, qa_json_get(doc, rng, "coverage"), "original-generator") ||
        qa_json_type(doc, fields) != QA_JSON_ARRAY || !qa_json_size(doc, fields) || qa_json_size(doc, fields) > CONTINUATION_FIELDS)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native RNG requires complete declared pointer-free original image scalar state");
    out->count = qa_json_size(doc, fields); size_t total = 0;
    for (size_t i = 0; i < out->count; ++i) {
        qa_json_id field = qa_json_at(doc, fields, i), storage = qa_json_get(doc, field, "storage");
        uint32_t width = qa_json_string_equal(doc, storage, "uint32") ? 4 :
            qa_json_string_equal(doc, storage, "uint64") ? 8 : 0, count;
        if (!width || !qa_json_u64(doc, qa_json_get(doc, field, "rva"), &out->fields[i].rva, error) ||
            !number(doc, field, "count", &count, error) || !count || count > CONTINUATION_BYTES / width)
            return application_fail(error, QA_ERROR_FORMAT, "Native RNG field changes its pointer-free scalar representation");
        out->fields[i].bytes = count * width;
        if (!qa_native_module_mutable_range(engine->provider->state.native.module, out->fields[i].rva, out->fields[i].bytes, error)) return false;
        if (out->fields[i].rva > UINT64_MAX - out->fields[i].bytes || out->fields[i].bytes > CONTINUATION_BYTES - total)
            return application_fail(error, QA_ERROR_FORMAT, "Native RNG continuation extent overflows");
        total += out->fields[i].bytes;
        for (size_t j = 0; j < i; ++j)
            if (out->fields[i].rva < out->fields[j].rva + out->fields[j].bytes &&
                out->fields[j].rva < out->fields[i].rva + out->fields[i].bytes)
                return application_fail(error, QA_ERROR_FORMAT, "Native RNG scalar fields overlap");
    }
    qa_native_target target = qa_native_module_describe(engine->provider->state.native.module).image.target;
    out->classic = engine->profile == QA_NATIVE_Q2_GAME_API3;
    if ((out->classic && (target.pointer_bytes != 4 || target.arch != QA_NATIVE_ARCH_I386)) ||
        (!out->classic && (target.pointer_bytes != 8 || target.arch != QA_NATIVE_ARCH_X86_64)))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native continuation requires its original little-endian classic32 or KEX64 artifact");
    qa_json_id world = qa_json_get(doc, root, "world");
    uint32_t extent;
    if (out->classic) {
        /* API3/i386's original public edict prefix stores gclient* at byte 84;
         * private record extents still come from the artifact declaration. */
        out->client_pointer = 84;
        if (!number(doc, world, "entityBytes", &out->entity_bytes, error) || out->entity_bytes < 260 ||
            !number(doc, qa_json_get(doc, qa_json_get(doc, root, "weapons"), "client"), "byteLength", &extent, error))
            return application_fail(error, QA_ERROR_FORMAT, "Native classic continuation differs from its original public entity prefix");
    } else {
        qa_json_id edict = qa_json_get(doc, world, "edict");
        qa_json_id client = qa_json_get(doc, qa_json_get(doc, world, "client"), "layout");
        if (!number(doc, edict, "byteLength", &out->entity_bytes, error) ||
            !layout_field(doc, edict, "shared.client", "pointer", target.pointer_bytes, &out->client_pointer, error) ||
            !number(doc, client, "byteLength", &extent, error)) return false;
    }
    qa_json_id connected = qa_json_get(doc, qa_json_get(doc, section, "clients"), "connected");
    out->connected_bytes = qa_json_string_equal(doc, qa_json_get(doc, connected, "storage"), "bool8") ? 1 :
        qa_json_string_equal(doc, qa_json_get(doc, connected, "storage"), "qboolean32") ? 4 : 0;
    if (out->connected_bytes != (out->classic ? 4u : 1u) || !number(doc, connected, "offset", &out->connected, error) ||
        out->connected > extent || out->connected_bytes > extent - out->connected)
        return application_fail(error, QA_ERROR_FORMAT, "Native private connected state needs its exact original bool layout");
    if (!out->classic) {
        qa_json_id spawned = qa_json_get(doc, qa_json_get(doc, section, "clients"), "spawned");
        if (!qa_json_string_equal(doc, qa_json_get(doc, spawned, "storage"), "bool8") ||
            !number(doc, spawned, "offset", &out->spawned, error) || out->spawned >= extent ||
            out->spawned == out->connected)
            return application_fail(error, QA_ERROR_FORMAT, "Native private spawned state needs its distinct original bool layout");
    }
    qa_json_id level = qa_json_get(doc, section, "level");
    if (!out->classic) return qa_json_string_equal(doc, qa_json_get(doc, level, "kind"), "source-json") ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Native level continuation requires its original JSON save contract");
    return (target.os == QA_NATIVE_OS_WINDOWS && target.pointer_bytes == 4 &&
        qa_json_string_equal(doc, qa_json_get(doc, level, "kind"), "classic-init-base") &&
        number(doc, level, "sizeOffset", &out->size_offset, error) &&
        number(doc, level, "baseOffset", &out->base_offset, error) &&
        ((uint64_t)out->size_offset + 4 <= out->base_offset ||
            (uint64_t)out->base_offset + 4 <= out->size_offset)) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Native classic level save requires its exact original Windows base contract");
}
static bool identity(struct application_native_q2 *engine, const qa_native_checkpoint *snapshot,
    const continuation_profile *profile, qa_error *error)
{
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    const qa_sha256_digest *declaration = qa_resource_digest(engine->provider->launch->declaration);
    if (!snapshot || !snapshot->has_game || !snapshot->has_level || !snapshot->has_host ||
        snapshot->profile != engine->profile || snapshot->q3_role != QA_QVM_GAME ||
        snapshot->kind != (profile->classic ? QA_NATIVE_CHECKPOINT_Q2_CLASSIC : QA_NATIVE_CHECKPOINT_Q2_RERELEASE) ||
        snapshot->image.format != info.image.format || snapshot->image.target.os != info.image.target.os ||
        snapshot->image.target.arch != info.image.target.arch || snapshot->image.target.abi != info.image.target.abi ||
        snapshot->image.target.pointer_bytes != info.image.target.pointer_bytes ||
        !snapshot->has_declaration || !declaration ||
        !qa_sha256_equal(&snapshot->image.digest, &info.image.digest) ||
        !qa_sha256_equal(&snapshot->declaration, declaration))
        return application_fail(error, QA_ERROR_FORMAT, "Native continuation differs from its complete original module owner");
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    for (size_t i = 0; i < profile->count; ++i) {
        qa_native_address address;
        if (!qa_native_rva(instance, profile->fields[i].rva, profile->fields[i].bytes, &address, error)) return false;
    }
    if (!profile->classic) return true;
    if (profile->size_offset > snapshot->level.size || snapshot->level.size - profile->size_offset < 4 ||
        profile->base_offset > snapshot->level.size || snapshot->level.size - profile->base_offset < 4 ||
        qa_load_u32le(snapshot->level.data + profile->size_offset) != profile->entity_bytes)
        return application_fail(error, QA_ERROR_FORMAT, "Original native level save edict header differs");
    qa_native_address base;
    return qa_native_entry_address(instance, "Init", &base, error) &&
        (base == qa_load_u32le(snapshot->level.data + profile->base_offset) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Original Windows native level save requires its captured InitGame load base"));
}
static bool client_pointer(struct application_native_q2 *engine, const continuation_profile *profile,
    uint32_t slot, qa_native_address *out, qa_error *error)
{
    qa_native_instance *instance = qa_native_host_instance(engine->provider->state.native.host);
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(instance, &table, error)) return false;
    *out = 0;
    if (slot >= table.capacity) return true;
    if (table.stride != profile->entity_bytes)
        return application_fail(error, QA_ERROR_FORMAT, "Native private client table differs from its declared source extent");
    qa_native_address address; uint8_t bytes[8];
    size_t width = qa_native_module_describe(engine->provider->state.native.module).image.target.pointer_bytes;
    if (!qa_native_entity_address(instance, slot, &address, error) || address > UINT64_MAX - profile->client_pointer ||
        !qa_native_read(instance, address + profile->client_pointer, bytes, width, error)) return false;
    *out = width == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes); return true;
}
static struct application_native_q2 *owner(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE ? provider->state.native.q2_engine : NULL;
    if (!engine || !provider->launch || !provider->state.native.host || !application_native_q2_idle(provider)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native continuation requires its idle isolated original owner"); return NULL;
    }
    return engine;
}
bool application_native_q2_continuation_portable(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE ? provider->state.native.q2_engine : NULL;
    if (!engine || !provider->launch || !provider->state.native.module || !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native full private qualification requires its idle actual source provider");
    continuation_profile profile = {0};
    bool ok = profile_read(engine, &profile, error) && application_q2_private_qualified(engine, error);
    qa_json_destroy(profile.document); return ok;
}
void application_native_q2_continuation_abort(struct application_native_q2_continuation *state)
{
    if (!state) return;
    for (size_t i = 0; i < state->count; ++i) qa_buffer_free(&state->fields[i]);
    qa_buffer_free(&state->private_bytes); application_q2_private_free(state->private_state);
    free(state);
}
static bool fields_io(qa_source_save_io *io, struct application_native_q2_continuation *state)
{
    uint32_t version = 2; size_t count = state->count;
    if (!qa_source_save_u32(io, &version) || version != 2 ||
        !qa_source_save_bytes(io, state->artifact.bytes, 32) ||
        !qa_source_save_bytes(io, state->declaration.bytes, 32) ||
        !qa_source_save_bytes(io, state->launch.bytes, 32) ||
        !qa_source_save_count(io, &count, CONTINUATION_FIELDS) || count != state->count) return false;
    for (size_t i = 0; i < state->count; ++i) {
        size_t bytes = state->fields[i].size;
        if (!qa_source_save_count(io, &bytes, CONTINUATION_BYTES) || bytes != state->fields[i].size ||
            !qa_source_save_bytes(io, state->fields[i].data, bytes)) return false;
    }
    for (uint32_t slot = 1; slot < 257; ++slot)
        if (!qa_source_save_bool(io, &state->clients[slot].present) ||
            !qa_source_save_bool(io, &state->clients[slot].connected) ||
            !qa_source_save_bool(io, &state->clients[slot].spawned) ||
            !qa_source_save_actor(io, &state->clients[slot].actor)) return false;
    size_t private_size = state->private_bytes.size;
    if (!qa_source_save_count(io, &private_size, CONTINUATION_BYTES) || !private_size) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        state->private_bytes = (qa_buffer){.data = malloc(private_size), .size = private_size};
        if (!state->private_bytes.data) return application_fail(io->error, QA_ERROR_MEMORY, "Retaining native complete private supplement");
    }
    return qa_source_save_bytes(io, state->private_bytes.data, private_size);
}
static struct application_native_q2_continuation *allocate(const continuation_profile *profile, qa_error *error)
{
    struct application_native_q2_continuation *state = calloc(1, sizeof(*state));
    if (!state) { application_fail(error, QA_ERROR_MEMORY, "Preparing native private continuation"); return NULL; }
    state->count = profile->count;
    for (size_t i = 0; i < state->count; ++i) {
        state->fields[i] = (qa_buffer){.data = malloc(profile->fields[i].bytes), .size = profile->fields[i].bytes};
        if (!state->fields[i].data) { application_native_q2_continuation_abort(state);
            application_fail(error, QA_ERROR_MEMORY, "Retaining original native generator state"); return NULL; }
    }
    return state;
}
bool application_native_q2_continuation_capture(application_provider *provider, const qa_native_checkpoint *snapshot,
    qa_buffer *out, qa_error *error)
{
    struct application_native_q2 *engine = owner(provider, error); continuation_profile profile = {0};
    if (!engine || !out) return false;
    bool ok = application_native_q2_continuation_portable(provider, error) && profile_read(engine, &profile, error) && identity(engine, snapshot, &profile, error);
    struct application_native_q2_continuation *state = ok ? allocate(&profile, error) : NULL;
    ok = ok && state; qa_native_instance *instance = qa_native_host_instance(provider->state.native.host);
    if (ok) { state->artifact = snapshot->image.digest; state->declaration = snapshot->declaration; state->launch = provider->launch->identity; }
    qa_native_address clients[257] = {0};
    for (size_t i = 0; ok && i < profile.count; ++i) {
        qa_native_address address;
        ok = qa_native_rva(instance, profile.fields[i].rva, state->fields[i].size, &address, error) &&
            qa_native_read(instance, address, state->fields[i].data, state->fields[i].size, error);
    }
    for (uint32_t slot = 1; ok && slot < 257; ++slot) {
        qa_native_address address = 0; uint8_t bytes[4];
        ok = client_pointer(engine, &profile, slot, &address, error);
        clients[slot] = address;
        for (uint32_t previous = 1; ok && address && previous < slot; ++previous)
            if (address == clients[previous]) ok = false;
        state->clients[slot].present = address != 0;
        if (ok && address) {
            ok = address <= UINT64_MAX - profile.connected &&
                qa_native_read(instance, address + profile.connected, bytes, profile.connected_bytes, error);
            if (ok) {
                uint32_t value = profile.connected_bytes == 1 ? bytes[0] : qa_load_u32le(bytes);
                ok = value <= 1 && (value != 0) == engine->clients[slot].connected;
                state->clients[slot].connected = value != 0; state->clients[slot].actor = engine->clients[slot].actor;
            }
            if (ok && !profile.classic) {
                ok = address <= UINT64_MAX - profile.spawned && qa_native_read(instance, address + profile.spawned, bytes, 1, error);
                if (ok) { ok = bytes[0] <= 1; state->clients[slot].spawned = bytes[0] != 0; }
            }
        } else if (ok && engine->clients[slot].connected) ok = false;
    }
    qa_source_save_io io = {0};
    if (ok) ok = application_q2_private_capture(engine, &state->private_bytes, error) &&
        qa_source_save_writer(&io, provider->application->session, error) && fields_io(&io, state) && qa_source_save_finish(&io, out);
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Original native private client state differs from its source owner");
    qa_source_save_dispose(&io); application_native_q2_continuation_abort(state); qa_json_destroy(profile.document); return ok;
}
bool application_native_q2_continuation_prepare(application_provider *provider, const qa_native_checkpoint *snapshot,
    qa_bytes bytes, struct application_native_q2_continuation **out, qa_error *error)
{
    struct application_native_q2 *engine = owner(provider, error); continuation_profile profile = {0};
    if (!engine || !out) return false;
    *out = NULL;
    bool ok = application_native_q2_continuation_portable(provider, error) && profile_read(engine, &profile, error) && identity(engine, snapshot, &profile, error);
    struct application_native_q2_continuation *state = ok ? allocate(&profile, error) : NULL;
    ok = ok && state; qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_reader(&io, provider->application->session, bytes, error) && fields_io(&io, state) &&
        qa_source_save_finish(&io, NULL) && qa_sha256_equal(&state->artifact, &snapshot->image.digest) &&
        qa_sha256_equal(&state->declaration, &snapshot->declaration) &&
        qa_sha256_equal(&state->launch, &provider->launch->identity);
    for (uint32_t slot = 1; ok && slot < 257; ++slot)
        if ((!state->clients[slot].present && (state->clients[slot].connected || state->clients[slot].spawned || state->clients[slot].actor.registry)) ||
            (profile.classic && state->clients[slot].spawned) ||
            (state->clients[slot].connected && !state->clients[slot].actor.registry)) ok = false;
    if (ok) ok = application_q2_private_prepare(engine,
        (qa_bytes){state->private_bytes.data, state->private_bytes.size}, &state->private_state, error);
    for (uint32_t slot = 1; ok && slot < 257; ++slot)
        ok = application_q2_private_client_matches(state->private_state, slot, state->clients[slot].present,
            state->clients[slot].connected, state->clients[slot].spawned, error);
    qa_source_save_dispose(&io); qa_json_destroy(profile.document);
    if (!ok) {
        application_native_q2_continuation_abort(state);
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid native qualified private continuation");
        return false;
    }
    *out = state; return true;
}
bool application_native_q2_continuation_apply(application_provider *provider,
    const struct application_native_q2_continuation *state, qa_error *error)
{
    struct application_native_q2 *engine = owner(provider, error); continuation_profile profile = {0};
    if (!engine || !state) return false;
    bool ok = application_native_q2_continuation_portable(provider, error) && profile_read(engine, &profile, error);
    qa_native_module_info info = qa_native_module_describe(provider->state.native.module);
    const qa_sha256_digest *declaration = qa_resource_digest(provider->launch->declaration);
    if (ok && (state->count != profile.count || !qa_sha256_equal(&state->artifact, &info.image.digest) ||
        !declaration || !qa_sha256_equal(&state->declaration, declaration) ||
        !qa_sha256_equal(&state->launch, &provider->launch->identity))) ok = false;
    qa_native_instance *instance = qa_native_host_instance(provider->state.native.host);
    if (ok) ok = qa_native_restore_ready(instance, error) && application_native_q2_combat_restore_ready(engine, error);
    for (uint32_t slot = 1; ok && slot < 257; ++slot)
        if (engine->clients[slot].inventory_bound)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native private continuation requires detached primary inventory claims");
    qa_native_address fields[CONTINUATION_FIELDS], clients[257] = {0};
    for (size_t i = 0; ok && i < profile.count; ++i)
        ok = state->fields[i].size == profile.fields[i].bytes &&
            qa_native_rva(instance, profile.fields[i].rva, profile.fields[i].bytes, &fields[i], error);
    for (uint32_t slot = 1; ok && slot < 257; ++slot) {
        ok = client_pointer(engine, &profile, slot, &clients[slot], error) &&
            (clients[slot] != 0) == state->clients[slot].present &&
            (!clients[slot] || qa_actor_id_equal(engine->clients[slot].actor, state->clients[slot].actor)) &&
            engine->clients[slot].connected == state->clients[slot].connected &&
            clients[slot] <= UINT64_MAX - profile.connected &&
            (profile.classic || clients[slot] <= UINT64_MAX - profile.spawned);
        for (uint32_t previous = 1; ok && clients[slot] && previous < slot; ++previous)
            if (clients[slot] == clients[previous]) ok = false;
    }
    /* All scalar addresses and canonical identities qualify before the first
     * isolated private write. No source callback or observer runs in this step. */
    if (ok) ok = application_q2_private_apply(engine, state->private_state, error);
    for (size_t i = 0; ok && i < profile.count; ++i)
        ok = qa_native_write(instance, fields[i], (qa_bytes){state->fields[i].data, state->fields[i].size}, error);
    for (uint32_t slot = 1; ok && slot < 257; ++slot) if (clients[slot]) {
        uint8_t value[4]; qa_store_u32le(value, state->clients[slot].connected ? 1 : 0);
        ok = qa_native_write(instance, clients[slot] + profile.connected, (qa_bytes){value, profile.connected_bytes}, error);
        if (ok && !profile.classic) {
            value[0] = state->clients[slot].spawned ? 1 : 0;
            ok = qa_native_write(instance, clients[slot] + profile.spawned, (qa_bytes){value, 1}, error);
        }
    }
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Native private continuation differs from restored source clients");
    qa_json_destroy(profile.document); return ok;
}
