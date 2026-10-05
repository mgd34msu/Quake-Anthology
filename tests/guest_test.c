#include "gameplay_fixture.h"
#include "qa/qc.h"

typedef struct guest_fixture {
    gameplay_map map;
    qa_session *session;
    qa_world *world;
    qa_qc_program *program;
    qa_qc_instance *instance;
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
static qa_qc_program *guest_program(void)
{
    qa_error error = {0};
    static const char strings[] = "\0bump\0increment\0nested\0normalize\0vlen\0rint\0"
        "spawn\0setorigin\0remove\0origin\0mins\0maxs\0size\0fixture.qc\0";
    static const qa_qc_statement statements[] = {
        {QA_QC_DONE, 0, 0, 0},
        {QA_QC_ADD_F, 32, 28, 32}, {QA_QC_STORE_F, 32, 29, 0}, {QA_QC_RETURN, 32, 0, 0},
        {QA_QC_ADD_F, 32, 28, 32}, {QA_QC_RETURN, 32, 0, 0},
        {QA_QC_STORE_F, 35, 4, 0}, {QA_QC_CALL1, 30, 0, 0},
        {QA_QC_ADD_F, 1, 35, 35}, {QA_QC_RETURN, 35, 0, 0}
    };
    static const struct { const char *name; int32_t first; uint32_t locals;
        uint8_t parameters; } functions[] = {
        {"", 0, 0, 0}, {"bump", 1, 32, 0}, {"increment", 4, 32, 1},
        {"nested", 6, 35, 1}, {"normalize", -9, 0, 0}, {"vlen", -12, 0, 0},
        {"rint", -36, 0, 0}, {"spawn", -14, 0, 0},
        {"setorigin", -2, 0, 0}, {"remove", -15, 0, 0}
    };
    const uint32_t statement_count = (uint32_t)(sizeof(statements) / sizeof(*statements));
    const uint32_t function_count = (uint32_t)(sizeof(functions) / sizeof(*functions));
    const uint32_t statements_at = 60, fields_at = statements_at + statement_count * 8;
    const uint32_t functions_at = fields_at + 4 * 8;
    const uint32_t strings_at = functions_at + function_count * 36;
    const uint32_t globals_at = strings_at + (uint32_t)sizeof(strings);
    uint8_t bytes[1024] = {0};
    qa_store_u32le(bytes, 6);
    qa_store_u32le(bytes + 4, 5927);
    const uint32_t sections[][3] = {
        {8, statements_at, statement_count}, {16, fields_at, 0},
        {24, fields_at, 4}, {32, functions_at, function_count},
        {40, strings_at, (uint32_t)sizeof(strings)}, {48, globals_at, 48}
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
    gameplay_float(bytes + globals_at + 32 * 4, 7);
    gameplay_float(bytes + globals_at + 35 * 4, -1);
    qa_qc_program *program = NULL;
    GAME_CHECK(qa_qc_program_load((qa_bytes){bytes, globals_at + 48 * 4},
        "fixture.qc", &program, &error));
    return program;
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
    fixture.program = guest_program();
    qa_actor_owner owner;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session),
        "test:guest", &owner, &error));
    qa_qc_options options = {.profile = QA_QC_NETQUAKE, .entity_capacity = 8,
        .host = {.session = fixture.session, .world = fixture.world, .owner = owner,
            .default_definition = owner}};
    GAME_CHECK(qa_qc_instance_create(fixture.program, &options, &fixture.instance, &error));
    qa_component component = {.owner = owner, .clock = qa_clock_defaults(QA_CLOCK_NETQUAKE),
        .state = fixture.instance, .actor_released = guest_component_released};
    GAME_CHECK(qa_session_add(fixture.session, &component, &error));
    execution(fixture.instance, fixture.program);
    shared_entities(&fixture, owner);
    GAME_CHECK(qa_session_remove(fixture.session, owner, &error));
    GAME_CHECK(qa_qc_instance_destroy(fixture.instance, &error));
    qa_qc_program_destroy(fixture.program);
    GAME_CHECK(qa_world_destroy(fixture.world, &error));
    GAME_CHECK(qa_session_destroy(fixture.session, &error));
    qa_collision_destroy(fixture.map.geometry);
}
