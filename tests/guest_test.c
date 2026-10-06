#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "gameplay_fixture.h"
#include "qa/frontend.h"
#include "qa/application_startup_prepare.h"
#include "qa/qc.h"

#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct guest_fixture {
    gameplay_map map;
    qa_session *session;
    qa_world *world;
    qa_qc_program *program;
    qa_qc_instance *instance;
    double source_time;
} guest_fixture;

static bool guest_released(void *context, qa_session *session, qa_actor_record actor,
    qa_error *error)
{
    (void)session;
    guest_fixture *fixture = context;
    return qa_world_actor_released(fixture->world, actor, error);
}

static void guest_component_released(void *state, qa_session *session, qa_actor_record actor)
{
    (void)session;
    qa_qc_actor_released(state, actor);
}

/* The original progs.dat ABI, with these self-contained QuakeC functions:
 * float(float x) increment = { return x+1; };
 * float(float x) nested = { return increment(x)+x; };
 * bump stores a result while its temporary locals are restored on return.
 * Builtin declarations use the original pr_cmds.c numbers. */
static qa_qc_program *guest_program(qa_qc_api api)
{
    qa_error error = {0};
    static const char strings[] = "\0bump\0increment\0nested\0normalize\0vlen\0rint\0"
        "spawn\0setorigin\0remove\0origin\0mins\0maxs\0size\0fixture.qc\0projected\0random\0";
    static const qa_qc_statement statements[] = {
        {QA_QC_DONE, 0, 0, 0},
        {QA_QC_ADD_F, 32, 28, 32}, {QA_QC_STORE_F, 32, 29, 0}, {QA_QC_RETURN, 32, 0, 0},
        {QA_QC_ADD_F, 32, 28, 32}, {QA_QC_RETURN, 32, 0, 0},
        {QA_QC_STORE_F, 35, 4, 0}, {QA_QC_CALL1, 30, 0, 0},
        {QA_QC_ADD_F, 1, 35, 35}, {QA_QC_RETURN, 35, 0, 0},
        {QA_QC_LOAD_F, 40, 41, 43}, {QA_QC_LOAD_F, 40, 41, 44},
        {QA_QC_CALL0, 42, 0, 0}, {QA_QC_LOAD_F, 40, 41, 45},
        {QA_QC_ADDRESS, 40, 41, 46}, {QA_QC_STOREP_F, 28, 46, 0},
        {QA_QC_LOAD_F, 40, 41, 47}, {QA_QC_RETURN, 47, 0, 0}
    };
    static const struct { const char *name; int32_t first; uint32_t locals;
        uint8_t parameters; } functions[] = {
        {"", 0, 0, 0}, {"bump", 1, 32, 0}, {"increment", 4, 32, 1},
        {"nested", 6, 35, 1}, {"normalize", -9, 0, 0}, {"vlen", -12, 0, 0},
        {"rint", -36, 0, 0}, {"spawn", -14, 0, 0},
        {"setorigin", -2, 0, 0}, {"remove", -15, 0, 0},
        {"projected", 10, 0, 0}, {"random", -7, 0, 0}
    };
    const uint32_t statement_count = (uint32_t)(sizeof(statements) / sizeof(*statements));
    const uint32_t function_count = (uint32_t)(sizeof(functions) / sizeof(*functions));
    const uint32_t statements_at = 60, fields_at = statements_at + statement_count * 8;
    const uint32_t functions_at = fields_at + 4 * 8;
    const uint32_t strings_at = functions_at + function_count * 36;
    const uint32_t globals_at = strings_at + (uint32_t)sizeof(strings);
    uint8_t bytes[1024] = {0};
    qa_store_u32le(bytes, 6);
    qa_store_u32le(bytes + 4, api == QA_QC_API_QUAKEWORLD ? 54730u : 5927u);
    const uint32_t sections[][3] = {
        {8, statements_at, statement_count}, {16, fields_at, 0},
        {24, fields_at, 4}, {32, functions_at, function_count},
        {40, strings_at, (uint32_t)sizeof(strings)}, {48, globals_at, 50}
    };
    for (size_t i = 0; i < sizeof(sections) / sizeof(*sections); ++i) {
        qa_store_u32le(bytes + sections[i][0], sections[i][1]);
        qa_store_u32le(bytes + sections[i][0] + 4, sections[i][2]);
    }
    qa_store_u32le(bytes + 56, 12);
    memcpy(bytes + strings_at, strings, sizeof(strings));
    for (uint32_t i = 0; i < statement_count; ++i) {
        uint8_t *record = bytes + statements_at + i * 8;
        qa_store_u16le(record, (uint16_t)statements[i].opcode);
        qa_store_u16le(record + 2, statements[i].a);
        qa_store_u16le(record + 4, statements[i].b);
        qa_store_u16le(record + 6, statements[i].c);
    }
    const char *fields[] = {"origin", "mins", "maxs", "size"};
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t *record = bytes + fields_at + i * 8;
        qa_store_u16le(record, 3);
        qa_store_u16le(record + 2, (uint16_t)(i * 3));
        for (size_t offset = 0; offset < sizeof(strings); offset += strlen(strings + offset) + 1)
            if (!strcmp(strings + offset, fields[i]))
                qa_store_u32le(record + 4, (uint32_t)offset);
    }
    for (uint32_t i = 0; i < function_count; ++i) {
        uint8_t *record = bytes + functions_at + i * 36;
        qa_store_u32le(record, (uint32_t)functions[i].first);
        qa_store_u32le(record + 4, functions[i].locals);
        qa_store_u32le(record + 8, functions[i].locals ? 3 : 0);
        qa_store_u32le(record + 24, functions[i].parameters);
        record[28] = 1;
        for (size_t offset = 0; offset < sizeof(strings); offset += strlen(strings + offset) + 1) {
            if (!strcmp(strings + offset, functions[i].name))
                qa_store_u32le(record + 16, (uint32_t)offset);
            if (!strcmp(strings + offset, "fixture.qc"))
                qa_store_u32le(record + 20, (uint32_t)offset);
        }
    }
    gameplay_float(bytes + globals_at + 28 * 4, 1);
    qa_store_u32le(bytes + globals_at + 30 * 4, 2);
    qa_store_u32le(bytes + globals_at + 42 * 4, 8);
    gameplay_float(bytes + globals_at + 32 * 4, 7);
    gameplay_float(bytes + globals_at + 35 * 4, -1);
    qa_qc_program *program = NULL;
    GAME_CHECK(qa_qc_program_load((qa_bytes){bytes, globals_at + 50 * 4},
        "fixture.qc", &program, &error));
    return program;
}

typedef struct projection_fixture {
    qa_world *world;
    qa_actor_id actor;
    unsigned binds, reads, writes, stores;
} projection_fixture;

static bool projected_access(void *opaque, qa_qc_instance *instance,
    const qa_qc_entity_access *access, qa_error *error)
{
    projection_fixture *fixture = opaque;
    if (access->kind == QA_QC_ENTITY_BIND) { ++fixture->binds; return true; }
    if (access->kind == QA_QC_ENTITY_READ) ++fixture->reads;
    else ++fixture->writes;
    qa_body_state body;
    return qa_actor_id_equal(access->binding.actor, fixture->actor) &&
        qa_world_body_read(fixture->world, fixture->actor, &body, error) &&
        qa_qc_project_entity_float(instance, access->reference, access->word,
            body.origin.x, error);
}

static bool projected_builtin(void *opaque, qa_qc_instance *instance,
    qa_qc_builtin builtin, const char *name, qa_error *error)
{
    (void)instance; (void)builtin; (void)name;
    projection_fixture *fixture = opaque;
    qa_body_state body;
    if (!qa_world_body_read(fixture->world, fixture->actor, &body, error)) return false;
    body.origin.x += 5;
    return qa_world_body_write(fixture->world, fixture->actor, &body, error);
}

static bool projected_store(void *opaque, qa_qc_instance *instance,
    const qa_qc_store_event *event, qa_error *error)
{
    (void)instance;
    projection_fixture *fixture = opaque;
    if (event->kind != QA_QC_STORE_ENTITY || event->word != 0) return false;
    ++fixture->stores;
    qa_body_state body;
    if (!qa_world_body_read(fixture->world, fixture->actor, &body, error)) return false;
    memcpy(&body.origin.x, event->after, sizeof(body.origin.x));
    return qa_world_body_write(fixture->world, fixture->actor, &body, error);
}

static void projection_loads(guest_fixture *shared, qa_actor_owner owner)
{
    qa_error error = {0};
    projection_fixture fixture = {.world = shared->world};
    qa_qc_builtin_binding builtin = {QA_QC_BUILTIN_SETORIGIN, NULL, &fixture, projected_builtin};
    qa_qc_instance *instance = NULL;
    qa_qc_options options = {.profile = QA_QC_NETQUAKE, .entity_capacity = 8,
        .host = {.session = shared->session, .world = shared->world, .owner = owner,
            .default_definition = owner, .context = &fixture, .declared_projection = true,
            .prepare_entity = projected_access, .builtins = &builtin, .builtin_count = 1},
        .observers = {.context = &fixture, .stored = projected_store, .entity_stores_only = true}};
    GAME_CHECK(qa_qc_instance_create(shared->program, &options, &instance, &error));
    for (unsigned run = 0; run < 3; ++run) {
        qa_qc_slot_kind kind = run == 1 ? QA_QC_SLOT_OWNED : QA_QC_SLOT_BORROWED;
        uint32_t slot = run == 1 ? 2u : 1u;
        GAME_CHECK(qa_session_allocate(shared->session, owner, owner, true, slot,
            &fixture.actor, &error));
        qa_body_state body = {.origin = {10 + (float)run, 0, 0}};
        if (kind == QA_QC_SLOT_BORROWED)
            GAME_CHECK(qa_world_body_create(fixture.world, fixture.actor, &body, &error));
        unsigned binds = fixture.binds;
        GAME_CHECK(qa_qc_bind_actor(instance, slot, fixture.actor, kind, &error));
        GAME_CHECK(fixture.binds == binds + 1);
        int32_t reference;
        GAME_CHECK(qa_qc_slot_reference(instance, slot, &reference, &error));
        if (kind == QA_QC_SLOT_OWNED)
            GAME_CHECK(qa_qc_set_entity_float(instance, reference, 0, body.origin.x, &error));
        GAME_CHECK(qa_qc_set_global_int(instance, 40, reference, &error));
        GAME_CHECK(qa_qc_set_global_int(instance, 41, 0, &error));
        fixture.reads = fixture.writes = fixture.stores = 0;
        GAME_CHECK(qa_qc_execute_named(instance, "projected", 0, &error));
        float first, repeated, changed, result;
        GAME_CHECK(qa_qc_global_float(instance, 43, &first, &error));
        GAME_CHECK(qa_qc_global_float(instance, 44, &repeated, &error));
        GAME_CHECK(qa_qc_global_float(instance, 45, &changed, &error));
        GAME_CHECK(qa_qc_global_float(instance, 1, &result, &error));
        GAME_CHECK(first == body.origin.x && repeated == first && changed == first + 5 && result == 1);
        GAME_CHECK(fixture.reads == (kind == QA_QC_SLOT_BORROWED ? 3u : 0u));
        GAME_CHECK(fixture.writes == (kind == QA_QC_SLOT_BORROWED ? 1u : 0u) && fixture.stores == 1);
        GAME_CHECK(qa_world_body_read(fixture.world, fixture.actor, &body, &error) && body.origin.x == 1);
        /* Public host reads must see mutations between guest invocations. */
        body.origin.x = 23;
        GAME_CHECK(qa_world_body_write(fixture.world, fixture.actor, &body, &error));
        GAME_CHECK(qa_qc_entity_float(instance, reference, 0, &result, &error) && result == 23);
        qa_actor_id previous = fixture.actor;
        if (kind == QA_QC_SLOT_OWNED)
            GAME_CHECK(qa_qc_remove_entity(instance, reference, &error));
        else {
            GAME_CHECK(qa_qc_unbind_actor(instance, slot, &error));
            GAME_CHECK(qa_session_release(shared->session, previous, &error));
        }
        qa_error stale = {0};
        GAME_CHECK(!qa_qc_actor_reference(instance, previous, false, &reference, &stale));
        GAME_CHECK(stale.code == QA_ERROR_NOT_FOUND);
    }
    GAME_CHECK(qa_qc_instance_destroy(instance, &error));
}

static void execution(qa_qc_instance *instance, const qa_qc_program *program)
{
    qa_error error = {0};
    float value;
    GAME_CHECK(qa_qc_execute_named(instance, "bump", 0, &error));
    GAME_CHECK(qa_qc_global_float(instance, 1, &value, &error) && value == 8);
    GAME_CHECK(qa_qc_global_float(instance, 32, &value, &error) && value == 7);
    GAME_CHECK(qa_qc_global_float(instance, 29, &value, &error) && value == 8);
    GAME_CHECK(qa_qc_set_global_float(instance, 4, 4, &error));
    GAME_CHECK(qa_qc_execute_named(instance, "nested", 1, &error));
    GAME_CHECK(qa_qc_global_float(instance, 1, &value, &error) && value == 9);
    GAME_CHECK(qa_qc_global_float(instance, 32, &value, &error) && value == 7);
    GAME_CHECK(qa_qc_global_float(instance, 35, &value, &error) && value == -1);
    qa_qc_instance *second = NULL;
    GAME_CHECK(qa_qc_instance_create(program,
        &(qa_qc_options){.profile = QA_QC_NETQUAKE, .entity_capacity = 4}, &second, &error));
    GAME_CHECK(qa_qc_global_float(second, 29, &value, &error) && value == 0);
    GAME_CHECK(qa_qc_instance_destroy(second, &error));
    GAME_CHECK(qa_qc_set_global_vector(instance, 4, qa_v3(3, 4, 0), &error));
    GAME_CHECK(qa_qc_execute_named(instance, "vlen", 1, &error));
    GAME_CHECK(qa_qc_global_float(instance, 1, &value, &error) && value == 5);
    GAME_CHECK(qa_qc_set_global_vector(instance, 4, qa_v3(0, 0, 0), &error));
    GAME_CHECK(qa_qc_execute_named(instance, "normalize", 1, &error));
    qa_vec3 normalized;
    GAME_CHECK(qa_qc_global_vector(instance, 1, &normalized, &error));
    GAME_CHECK(normalized.x == 0 && normalized.y == 0 && normalized.z == 0);
    GAME_CHECK(qa_qc_set_global_float(instance, 4, -1.5f, &error));
    GAME_CHECK(qa_qc_execute_named(instance, "rint", 1, &error));
    GAME_CHECK(qa_qc_global_float(instance, 1, &value, &error) && value == -2);
}

static void shared_entities(guest_fixture *fixture, qa_actor_owner owner)
{
    qa_error error = {0};
    GAME_CHECK(qa_actors_count(qa_session_actors(fixture->session)) == 0);
    GAME_CHECK(qa_qc_execute_named(fixture->instance, "spawn", 0, &error));
    int32_t reference;
    qa_actor_id actor;
    GAME_CHECK(qa_qc_global_int(fixture->instance, 1, &reference, &error));
    GAME_CHECK(reference != 0);
    GAME_CHECK(qa_qc_reference_actor(fixture->instance, reference, &actor, &error));
    const qa_actor_record *record = qa_actors_get(qa_session_actors(fixture->session), actor);
    GAME_CHECK(record && record->owner == owner && record->has_source);
    GAME_CHECK(qa_actors_count(qa_session_actors(fixture->session)) == 1);
    GAME_CHECK(qa_qc_set_global_int(fixture->instance, 4, reference, &error));
    GAME_CHECK(qa_qc_set_global_vector(fixture->instance, 7, qa_v3(12, -8, 24), &error));
    GAME_CHECK(qa_qc_execute_named(fixture->instance, "setorigin", 2, &error));
    qa_body_state body;
    GAME_CHECK(qa_world_body_read(fixture->world, actor, &body, &error));
    GAME_CHECK(body.origin.x == 12 && body.origin.y == -8 && body.origin.z == 24);
    qa_vec3 guest_origin;
    GAME_CHECK(qa_qc_entity_vector(fixture->instance, reference, 0, &guest_origin, &error));
    GAME_CHECK(guest_origin.x == body.origin.x && guest_origin.y == body.origin.y &&
        guest_origin.z == body.origin.z);
    GAME_CHECK(qa_qc_execute_named(fixture->instance, "remove", 1, &error));
    GAME_CHECK(!qa_actors_get(qa_session_actors(fixture->session), actor));
    GAME_CHECK(qa_actors_count(qa_session_actors(fixture->session)) == 0);
    GAME_CHECK(!qa_world_body_read(fixture->world, actor, &body, &error));
    GAME_CHECK(error.code == QA_ERROR_NOT_FOUND);
}

static double guest_source_time(void *opaque)
{
    return ((guest_fixture *)opaque)->source_time;
}

static void checkpoint_strings(const qa_qc_program *program, qa_qc_profile profile)
{
    qa_error error = {0};
    qa_qc_instance *instance = NULL, *fresh = NULL;
    GAME_CHECK(qa_qc_instance_create(program,
        &(qa_qc_options){.profile = profile, .entity_capacity = 4}, &instance, &error));
    GAME_CHECK(qa_qc_instance_create(program,
        &(qa_qc_options){.profile = profile, .entity_capacity = 8192}, &fresh, &error));
    qa_qc_checkpoint *checkpoint = NULL, *decoded = NULL;
    qa_buffer small = {0}, large = {0};
    GAME_CHECK(qa_qc_checkpoint_capture(instance, &checkpoint, &error));
    GAME_CHECK(qa_qc_checkpoint_encode(checkpoint, &small, &error));
    qa_qc_checkpoint_destroy(checkpoint);
    GAME_CHECK(qa_qc_checkpoint_capture(fresh, &checkpoint, &error));
    GAME_CHECK(qa_qc_checkpoint_encode(checkpoint, &large, &error));
    GAME_CHECK(small.size < 256 && small.size == large.size && !memcmp(small.data, large.data, small.size));
    qa_buffer_free(&small); qa_buffer_free(&large);
    qa_qc_checkpoint_destroy(checkpoint);
    /* Rint preserves its signed-zero result; only the extra call's profile differs. */
    GAME_CHECK(qa_qc_execute_named(fresh, "rint", 1, &error));
    GAME_CHECK(qa_qc_execute_named(instance, "rint", 1, &error));
    GAME_CHECK(qa_qc_execute_named(instance, "rint", 1, &error));
    GAME_CHECK(qa_qc_checkpoint_capture(instance, &checkpoint, &error));
    GAME_CHECK(qa_qc_checkpoint_encode(checkpoint, &small, &error));
    qa_qc_checkpoint_destroy(checkpoint);
    GAME_CHECK(qa_qc_checkpoint_capture(fresh, &checkpoint, &error));
    GAME_CHECK(qa_qc_checkpoint_encode(checkpoint, &large, &error));
    GAME_CHECK(small.size == large.size && !memcmp(small.data, large.data, small.size));
    qa_buffer_free(&small); qa_buffer_free(&large);
    qa_qc_checkpoint_destroy(checkpoint);
    int32_t dynamic, engine;
    GAME_CHECK(qa_qc_string_allocate(instance, "runtime-only", &dynamic, &error));
    GAME_CHECK(qa_qc_engine_string(instance, "mutable", "abcdefgh", 65536, &engine, &error));
    int32_t same;
    GAME_CHECK(qa_qc_engine_string(instance, "mutable", "x", 65536, &same, &error) && same == engine);
    GAME_CHECK(qa_qc_set_global_int(instance, 31, dynamic, &error));
    GAME_CHECK(qa_qc_set_global_int(instance, 33, engine, &error));
    GAME_CHECK(qa_qc_set_global_float(instance, 32, 0, &error));
    GAME_CHECK(qa_qc_execute_named(instance, "random", 0, &error));
    GAME_CHECK(qa_qc_checkpoint_capture(instance, &checkpoint, &error));
    GAME_CHECK(qa_qc_checkpoint_encode(checkpoint, &small, &error));
    GAME_CHECK(small.size < 512);
    GAME_CHECK(qa_qc_checkpoint_decode((qa_bytes){small.data, small.size}, &decoded, &error));
    GAME_CHECK(qa_qc_checkpoint_restore(fresh, decoded, &error));
    const char *text;
    int32_t restored;
    float value;
    GAME_CHECK(qa_qc_global_int(fresh, 31, &restored, &error) && restored == dynamic);
    GAME_CHECK(qa_qc_string(fresh, restored, &text, &error) && !strcmp(text, "runtime-only"));
    GAME_CHECK(qa_qc_global_int(fresh, 33, &restored, &error) && restored == engine);
    GAME_CHECK(qa_qc_string(fresh, restored, &text, &error) && !strcmp(text, "x") && !memcmp(text + 2, "cdefgh", 6));
    GAME_CHECK(qa_qc_global_float(fresh, 32, &value, &error) && value == 0);
    GAME_CHECK(qa_qc_global_float(fresh, 28, &value, &error) && value == 1);
    GAME_CHECK(qa_qc_string(fresh, 1, &text, &error) && !strcmp(text, "bump"));
    float next;
    GAME_CHECK(qa_qc_execute_named(instance, "random", 0, &error));
    GAME_CHECK(qa_qc_global_float(instance, 1, &next, &error));
    GAME_CHECK(qa_qc_execute_named(fresh, "random", 0, &error));
    GAME_CHECK(qa_qc_global_float(fresh, 1, &value, &error) && value == next);
    GAME_CHECK(qa_qc_engine_string(fresh, "mutable", "replacement", 65536, &same, &error) && same == engine);
    GAME_CHECK(qa_qc_string(fresh, same, &text, &error) && !strcmp(text, "replacement"));
    qa_error truncated = {0};
    qa_qc_checkpoint *invalid = NULL;
    GAME_CHECK(!qa_qc_checkpoint_decode((qa_bytes){small.data, small.size - 1}, &invalid, &truncated));
    GAME_CHECK(truncated.code == QA_ERROR_FORMAT && invalid == NULL);
    qa_buffer_free(&small);
    qa_qc_checkpoint_destroy(decoded); qa_qc_checkpoint_destroy(checkpoint);
    GAME_CHECK(qa_qc_instance_destroy(fresh, &error));
    GAME_CHECK(qa_qc_instance_destroy(instance, &error));
}

static void checkpoint_entities(guest_fixture *fixture, qa_actor_owner owner)
{
    qa_error error = {0};
    fixture->source_time = 10;
    GAME_CHECK(qa_qc_execute_named(fixture->instance, "spawn", 0, &error));
    int32_t freed, reference;
    GAME_CHECK(qa_qc_global_int(fixture->instance, 1, &freed, &error));
    GAME_CHECK(qa_qc_set_global_int(fixture->instance, 4, freed, &error));
    GAME_CHECK(qa_qc_execute_named(fixture->instance, "remove", 1, &error));
    GAME_CHECK(qa_qc_execute_named(fixture->instance, "spawn", 0, &error));
    GAME_CHECK(qa_qc_global_int(fixture->instance, 1, &reference, &error) && reference != freed);
    qa_actor_id actor, borrowed;
    GAME_CHECK(qa_qc_reference_actor(fixture->instance, reference, &actor, &error));
    GAME_CHECK(qa_qc_set_entity_vector(fixture->instance, reference, 0, qa_v3(12, 13, 14), &error));
    GAME_CHECK(qa_session_allocate(fixture->session, owner, owner, false, 0, &borrowed, &error));
    GAME_CHECK(qa_world_body_create(fixture->world, borrowed, &(qa_body_state){.origin = {21, 22, 23}}, &error));
    GAME_CHECK(qa_qc_bind_actor(fixture->instance, 3, borrowed, QA_QC_SLOT_BORROWED, &error));
    qa_qc_checkpoint *checkpoint = NULL, *decoded = NULL;
    qa_buffer bytes = {0};
    GAME_CHECK(qa_qc_checkpoint_capture(fixture->instance, &checkpoint, &error));
    GAME_CHECK(qa_qc_checkpoint_encode(checkpoint, &bytes, &error) && bytes.size < 1024);
    GAME_CHECK(qa_qc_checkpoint_decode((qa_bytes){bytes.data, bytes.size}, &decoded, &error));
    GAME_CHECK(qa_qc_set_entity_vector(fixture->instance, reference, 0, qa_v3(90, 91, 92), &error));
    GAME_CHECK(qa_qc_checkpoint_restore(fixture->instance, decoded, &error));
    qa_actor_id restored;
    GAME_CHECK(qa_qc_reference_actor(fixture->instance, reference, &restored, &error) && qa_actor_id_equal(restored, actor));
    qa_body_state body;
    GAME_CHECK(qa_world_body_read(fixture->world, actor, &body, &error) && body.origin.x == 12 && body.origin.y == 13 && body.origin.z == 14);
    int32_t borrowed_reference;
    GAME_CHECK(qa_qc_slot_reference(fixture->instance, 3, &borrowed_reference, &error));
    GAME_CHECK(qa_qc_reference_actor(fixture->instance, borrowed_reference, &restored, &error) && qa_actor_id_equal(restored, borrowed));
    /* The restored free timestamp still prevents premature source-slot reuse. */
    fixture->source_time = 10.1;
    GAME_CHECK(qa_qc_execute_named(fixture->instance, "spawn", 0, &error));
    int32_t later;
    GAME_CHECK(qa_qc_global_int(fixture->instance, 1, &later, &error) && later != freed);
    GAME_CHECK(qa_qc_remove_entity(fixture->instance, later, &error));
    fixture->source_time = 11;
    GAME_CHECK(qa_qc_execute_named(fixture->instance, "spawn", 0, &error));
    GAME_CHECK(qa_qc_global_int(fixture->instance, 1, &later, &error) && later == freed);
    GAME_CHECK(qa_qc_remove_entity(fixture->instance, later, &error));
    GAME_CHECK(qa_qc_remove_entity(fixture->instance, reference, &error));
    GAME_CHECK(qa_qc_unbind_actor(fixture->instance, 3, &error));
    GAME_CHECK(qa_session_release(fixture->session, borrowed, &error));
    qa_buffer_free(&bytes);
    qa_qc_checkpoint_destroy(decoded); qa_qc_checkpoint_destroy(checkpoint);
    fixture->source_time = 0;
}

typedef struct live_guest_case {
    const char *name, *product, *map, *path, *artifact;
    qa_program_kind runtime;
    qa_bsp_family family;
} live_guest_case;

static const live_guest_case live_guests[] = {
    {"retail", "q1-classic-id1", "e1m1", "maps/e1m1.bsp", "progs.dat", QA_PROGRAM_QUAKEC, QA_BSP_Q1},
    {"qc", "q1-classic-ctf", "ctfstart", "maps/ctfstart.bsp", "progs.dat", QA_PROGRAM_QUAKEC, QA_BSP_Q1},
    {"qvm", "q3-classic-lrctf", "lrctf01", "maps/lrctf01.bsp", "vm/qagame.qvm", QA_PROGRAM_QVM, QA_BSP_Q3},
    {"dll", "q2-classic-lmctf", "lmctf09", "maps/lmctf09.bsp", "gamex86.dll", QA_PROGRAM_NATIVE, QA_BSP_Q2}
};

static bool live_time(uint64_t *out, qa_error *error)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "Reading live guest deadline: %s", strerror(errno));
        return false;
    }
    *out = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    return true;
}

static bool live_guest_run(const live_guest_case *test, const char *root,
    const char *binary, const char *user_root)
{
    qa_error error = {0};
    qa_frontend_options options = {0};
    qa_frontend *frontend = NULL;
    qa_clock_state initial = {0}, current = {0};
    qa_actor_owner primary = 0;
    bool ready = false, passed = false;
    uint64_t began = 0, now = 0;
    const char *stage = "options";
    char port[16];
    (void)snprintf(port, sizeof(port), "%u", 40000u + (unsigned)getpid() % 20000u);
    char *argv[] = {(char *)binary, "--content-root", (char *)root,
        "--user-content-root", (char *)user_root, "--game", (char *)test->product,
        "--map", (char *)test->path, "--original", "--dedicated", "--no-audio",
        "--host", "127.0.0.1", "--port", port, "--protocol", "unified-1",
        "--native-backend", "emulated"};
    printf("LIVE_GUEST_START case=%s product=%s user_root=%s\n",
        test->name, test->product, user_root);
    fflush(stdout);
    if (!live_time(&began, &error) ||
        !qa_frontend_options_parse((int)(sizeof(argv) / sizeof(*argv)), argv, &options, &error))
        goto cleanup;
    options.native_bootstrap = malloc(strlen(binary) + 1);
    if (!options.native_bootstrap) {
        qa_error_set(&error, QA_ERROR_MEMORY, 0, "Retaining actual live guest executable");
        goto cleanup;
    }
    memcpy(options.native_bootstrap, binary, strlen(binary) + 1);
    options.application.native_bootstrap = options.native_bootstrap;
    stage = "locations";
    if (!qa_frontend_options_resolve_locations(&options, &error)) goto cleanup;
    stage = "create";
    if (!qa_frontend_create(&options, &frontend, &error)) goto cleanup;
    stage = "startup";
    for (;;) {
        if (!live_time(&now, &error)) goto cleanup;
        /* Leave shutdown time inside the parent's strict 100-second bound. */
        if (now - began >= UINT64_C(95000000000)) {
            qa_error_set(&error, QA_ERROR_IO, 0, "Live guest deadline reached during %s", stage);
            goto cleanup;
        }
        if (!qa_frontend_step(frontend, UINT64_C(20000000), &error)) goto cleanup;
        qa_application *app = qa_frontend_application(frontend);
        if (!app || qa_application_get_state(app) != QA_APPLICATION_RUNNING ||
            qa_application_startup_pending(app)) continue;
        if (qa_application_should_stop(app)) {
            qa_error_set(&error, QA_ERROR_FORMAT, 0, "Live guest stopped before completing its Source frames");
            goto cleanup;
        }
        if (!ready) {
            stage = "source admission";
            const qa_launch_instance *instance = qa_launch_snapshot_find(
                qa_application_launch(app), "native:primary");
            const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(app));
            qa_application_map_view map;
            if (!instance || instance->selection.runtime != test->runtime ||
                !instance->selection.artifact || strcmp(instance->selection.artifact, test->artifact) ||
                !instance->artifact || !qa_resource_bytes(instance->artifact).size ||
                !choices || strcmp(choices->world.map, test->path) ||
                !qa_application_provider_owner(app, "native:primary", &primary) ||
                !qa_application_map_read(app, &map) || !map.resource || strcmp(map.name, test->map)) {
                qa_error_set(&error, QA_ERROR_FORMAT, 0, "Live guest lost its selected original program or real map");
                goto cleanup;
            }
            qa_bsp_view bsp;
            if (!qa_bsp_open(qa_resource_bytes(map.resource), &bsp, &error)) goto cleanup;
            if (bsp.family != test->family || !qa_world_geometry(qa_application_world(app)) ||
                !qa_bsp_record_count(&bsp, QA_BSP_MODELS) ||
                !bsp.lumps[QA_BSP_ENTITIES].bytes.size ||
                !qa_session_clock(qa_application_session(app), primary, &initial)) {
                qa_error_set(&error, QA_ERROR_FORMAT, 0, "Live guest lacks real BSP entities or primary Source clock");
                goto cleanup;
            }
            printf("LIVE_GUEST_READY case=%s artifact=%s resource=%llu bytes=%zu map=%s bsp=%s primary=%u frame=%llu\n",
                test->name, test->artifact, (unsigned long long)qa_resource_id(instance->artifact),
                qa_resource_bytes(instance->artifact).size, map.name, qa_bsp_format_name(bsp.format),
                primary, (unsigned long long)initial.frame_number);
            fflush(stdout);
            ready = true;
            stage = "Source frames";
        }
        if (!qa_session_clock(qa_application_session(app), primary, &current) ||
            current.frame_number < initial.frame_number || current.elapsed_ns < initial.elapsed_ns ||
            qa_session_faulted(qa_application_session(app))) {
            qa_error_set(&error, QA_ERROR_FORMAT, 0, "Live guest lost its advancing primary Source clock");
            goto cleanup;
        }
        if (current.frame_number - initial.frame_number >= 20 && current.elapsed_ns > initial.elapsed_ns &&
            current.frame.phase == QA_FRAME_EXIT && current.frame.number == current.frame_number) {
            passed = true;
            break;
        }
    }
cleanup:
    if (!passed)
        fprintf(stderr, "LIVE_GUEST_FAILURE case=%s stage=%s code=%d offset=%zu message=%s\n",
            test->name, stage, (int)error.code, error.offset, error.message);
    qa_error cleanup_error = {0};
    if (frontend && !qa_frontend_shutdown(&frontend, &cleanup_error)) {
        fprintf(stderr, "LIVE_GUEST_CLEANUP_FAILURE case=%s code=%d message=%s retained=%d\n",
            test->name, (int)cleanup_error.code, cleanup_error.message, frontend != NULL);
        passed = false;
    }
    /* A rejected owner still borrows options until this isolated process exits. */
    if (!frontend) qa_frontend_options_destroy(&options);
    if (passed)
        printf("LIVE_GUEST_PASS case=%s ticks=%llu elapsed_ns=%llu shutdown=complete\n", test->name,
            (unsigned long long)(current.frame_number - initial.frame_number),
            (unsigned long long)(current.elapsed_ns - initial.elapsed_ns));
    fflush(stdout);
    fflush(stderr);
    return passed;
}

static void live_guest_case_run(const live_guest_case *test, const char *root, const char *binary)
{
    qa_error error = {0};
    const char *cache = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    char user_root[4096];
    int length = cache && *cache ? snprintf(user_root, sizeof(user_root), "%s/qa-live-%s-XXXXXX", cache, test->name) :
        home && *home ? snprintf(user_root, sizeof(user_root), "%s/.cache/qa-live-%s-XXXXXX", home, test->name) : -1;
    if (length < 0 || (size_t)length >= sizeof(user_root) || !mkdtemp(user_root)) {
        fprintf(stderr, "LIVE_GUEST_FAILURE case=%s creating isolated user root: %s\n", test->name, strerror(errno));
        exit(EXIT_FAILURE);
    }
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "LIVE_GUEST_FAILURE case=%s fork: %s\n", test->name, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (!child) {
        if (setpgid(0, 0) != 0) {
            fprintf(stderr, "LIVE_GUEST_FAILURE case=%s process group: %s\n", test->name, strerror(errno));
            _exit(EXIT_FAILURE);
        }
        _exit(live_guest_run(test, root, binary, user_root) ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    /* The child also establishes this group before any native Source begins. */
    (void)setpgid(child, child);
    uint64_t began = 0, now = 0;
    bool timed = live_time(&began, &error);
    int status = 0;
    for (;;) {
        pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if ((waited < 0 && errno != EINTR) || !timed || !live_time(&now, &error) ||
            now - began >= UINT64_C(100000000000)) {
            if (error.code == QA_OK)
                qa_error_set(&error, QA_ERROR_IO, 0, "%s", waited < 0 ?
                    "Waiting for live guest child failed" : "Live guest exceeded its 100-second deadline");
            (void)kill(-child, SIGKILL);
            (void)kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
            fprintf(stderr, "LIVE_GUEST_FAILURE case=%s supervisor deadline/wait code=%d message=%s\n",
                test->name, (int)error.code, error.message);
            exit(EXIT_FAILURE);
        }
        struct timespec delay = {.tv_nsec = 10000000};
        (void)nanosleep(&delay, NULL);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        fprintf(stderr, "LIVE_GUEST_FAILURE case=%s child_status=%d user_root=%s\n", test->name, status, user_root);
        exit(EXIT_FAILURE);
    }
}

static void live_guest_tests(void)
{
    const char *gate = getenv("QA_REQUIRE_LIVE");
    if (!gate || strcmp(gate, "1")) return;
    const char *root = getenv("QA_LIVE_CONTENT_ROOT"), *binary = getenv("QA_LIVE_BINARY");
    const char *selected = getenv("QA_LIVE_CASE");
    if (selected && !strcmp(selected, "recovery")) return;
    struct stat root_info;
    if (!root || !*root || stat(root, &root_info) != 0 || !S_ISDIR(root_info.st_mode) ||
        !binary || binary[0] != '/' || access(binary, X_OK) != 0) {
        fputs("QA_REQUIRE_LIVE=1 requires QA_LIVE_CONTENT_ROOT and an absolute executable QA_LIVE_BINARY\n", stderr);
        exit(EXIT_FAILURE);
    }
    bool found = false;
    for (size_t i = 0; i < sizeof(live_guests) / sizeof(*live_guests); ++i) {
        if (selected && *selected && strcmp(selected, live_guests[i].name)) continue;
        found = true;
        live_guest_case_run(live_guests + i, root, binary);
    }
    if (!found) {
        fputs("QA_LIVE_CASE must be retail, qc, qvm, or dll\n", stderr);
        exit(EXIT_FAILURE);
    }
}

void test_guest(void);
void test_guest(void)
{
    qa_error error = {0};
    guest_fixture fixture = {0};
    gameplay_map_create(&fixture.map);
    GAME_CHECK(qa_session_create(&(qa_session_options){.actor_capacity = 8,
        .component_capacity = 1, .actor_released = guest_released, .release_context = &fixture},
        &fixture.session, &error));
    GAME_CHECK(qa_world_create(qa_session_actor_registry(fixture.session),
        fixture.map.geometry, NULL, &fixture.world, &error));
    fixture.program = guest_program(QA_QC_API_NETQUAKE);
    qa_actor_owner owner;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session),
        "test:guest", &owner, &error));
    qa_qc_options options = {.profile = QA_QC_NETQUAKE, .entity_capacity = 8,
        .host = {.session = fixture.session, .world = fixture.world, .owner = owner,
            .default_definition = owner, .context = &fixture, .source_time_seconds = guest_source_time}};
    GAME_CHECK(qa_qc_instance_create(fixture.program, &options, &fixture.instance, &error));
    qa_component component = {.owner = owner, .clock = qa_clock_defaults(QA_CLOCK_NETQUAKE),
        .state = fixture.instance, .actor_released = guest_component_released};
    GAME_CHECK(qa_session_add(fixture.session, &component, &error));
    execution(fixture.instance, fixture.program);
    shared_entities(&fixture, owner);
    checkpoint_strings(fixture.program, QA_QC_NETQUAKE);
    qa_qc_program *qw = guest_program(QA_QC_API_QUAKEWORLD);
    checkpoint_strings(qw, QA_QC_QUAKEWORLD);
    qa_qc_program_destroy(qw);
    checkpoint_entities(&fixture, owner);
    projection_loads(&fixture, owner);
    GAME_CHECK(qa_session_remove(fixture.session, owner, &error));
    GAME_CHECK(qa_qc_instance_destroy(fixture.instance, &error));
    qa_qc_program_destroy(fixture.program);
    GAME_CHECK(qa_world_destroy(fixture.world, &error));
    GAME_CHECK(qa_session_destroy(fixture.session, &error));
    qa_collision_destroy(fixture.map.geometry);
    live_guest_tests();
}
