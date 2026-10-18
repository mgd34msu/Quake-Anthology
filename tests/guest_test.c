#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "gameplay_fixture.h"
#include "qa/frontend.h"
#include "qa/application_startup_prepare.h"
#include "qa/qc.h"
#include "../src/app/frontend/internal.h"
#include "../src/app/application/guest_native_q2_calls.h"
#include "../src/app/application/guest_qc_targets.h"

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
        "spawn\0setorigin\0remove\0origin\0mins\0maxs\0size\0fixture.qc\0projected\0random\0tag\0find\0"
        "use\0targetname\0target\0killtarget\0classname\0message\0delay\0wait\0"
        "self\0other\0activator\0time\0target_use\0";
    static const qa_qc_statement statements[] = {
        {QA_QC_DONE, 0, 0, 0},
        {QA_QC_ADD_F, 32, 28, 32}, {QA_QC_STORE_F, 32, 29, 0}, {QA_QC_RETURN, 32, 0, 0},
        {QA_QC_ADD_F, 32, 28, 32}, {QA_QC_RETURN, 32, 0, 0},
        {QA_QC_STORE_F, 35, 4, 0}, {QA_QC_CALL1, 30, 0, 0},
        {QA_QC_ADD_F, 1, 35, 35}, {QA_QC_RETURN, 35, 0, 0},
        {QA_QC_LOAD_F, 40, 41, 43}, {QA_QC_LOAD_F, 40, 41, 44},
        {QA_QC_CALL0, 42, 0, 0}, {QA_QC_LOAD_F, 40, 41, 45},
        {QA_QC_ADDRESS, 40, 41, 46}, {QA_QC_STOREP_F, 28, 46, 0},
        {QA_QC_LOAD_F, 40, 41, 47}, {QA_QC_RETURN, 47, 0, 0},
        {QA_QC_STORE_ENT, 50, 54, 0}, {QA_QC_STORE_ENT, 51, 55, 0},
        {QA_QC_STORE_ENT, 52, 56, 0}, {QA_QC_STORE_F, 53, 57, 0},
        {QA_QC_ADD_F, 58, 28, 58}, {QA_QC_DONE, 0, 0, 0}
    };
    static const struct { const char *name; int32_t first; uint32_t locals;
        uint8_t parameters; } functions[] = {
        {"", 0, 0, 0}, {"bump", 1, 32, 0}, {"increment", 4, 32, 1},
        {"nested", 6, 35, 1}, {"normalize", -9, 0, 0}, {"vlen", -12, 0, 0},
        {"rint", -36, 0, 0}, {"spawn", -14, 0, 0},
        {"setorigin", -2, 0, 0}, {"remove", -15, 0, 0},
        {"projected", 10, 0, 0}, {"random", -7, 0, 0}, {"find", -18, 0, 0}, {"target_use", 18, 0, 0}
    };
    const uint32_t statement_count = (uint32_t)(sizeof(statements) / sizeof(*statements));
    const uint32_t function_count = (uint32_t)(sizeof(functions) / sizeof(*functions));
    const uint32_t statements_at = 60, definitions_at = statements_at + statement_count * 8;
    const uint32_t fields_at = definitions_at + 4 * 8;
    const uint32_t functions_at = fields_at + 13 * 8;
    const uint32_t strings_at = functions_at + function_count * 36;
    const uint32_t globals_at = strings_at + (uint32_t)sizeof(strings);
    uint8_t bytes[2048] = {0};
    qa_store_u32le(bytes, 6);
    qa_store_u32le(bytes + 4, api == QA_QC_API_QUAKEWORLD ? 54730u : 5927u);
    const uint32_t sections[][3] = {
        {8, statements_at, statement_count}, {16, definitions_at, 4},
        {24, fields_at, 13}, {32, functions_at, function_count},
        {40, strings_at, (uint32_t)sizeof(strings)}, {48, globals_at, 64}
    };
    for (size_t i = 0; i < sizeof(sections) / sizeof(*sections); ++i) {
        qa_store_u32le(bytes + sections[i][0], sections[i][1]);
        qa_store_u32le(bytes + sections[i][0] + 4, sections[i][2]);
    }
    qa_store_u32le(bytes + 56, 21);
    memcpy(bytes + strings_at, strings, sizeof(strings));
    for (uint32_t i = 0; i < statement_count; ++i) {
        uint8_t *record = bytes + statements_at + i * 8;
        qa_store_u16le(record, (uint16_t)statements[i].opcode);
        qa_store_u16le(record + 2, statements[i].a);
        qa_store_u16le(record + 4, statements[i].b);
        qa_store_u16le(record + 6, statements[i].c);
    }
    const char *fields[] = {"origin", "mins", "maxs", "size", "tag",
        "targetname", "target", "killtarget", "classname", "message", "delay", "wait", "use"};
    for (uint32_t i = 0; i < 13; ++i) {
        uint8_t *record = bytes + fields_at + i * 8;
        qa_store_u16le(record, i < 4 ? QA_QC_VECTOR : i < 10 ? QA_QC_STRING :
            i < 12 ? QA_QC_FLOAT : QA_QC_FUNCTION);
        qa_store_u16le(record + 2, (uint16_t)(i < 5 ? i * 3 : i + 8));
        for (size_t offset = 0; offset < sizeof(strings); offset += strlen(strings + offset) + 1)
            if (!strcmp(strings + offset, fields[i]))
                qa_store_u32le(record + 4, (uint32_t)offset);
    }
    const char *globals[] = {"self", "other", "activator", "time"};
    for (uint32_t i = 0; i < 4; ++i) {
        uint8_t *record = bytes + definitions_at + i * 8;
        qa_store_u16le(record, i == 3 ? QA_QC_FLOAT : QA_QC_ENTITY);
        qa_store_u16le(record + 2, (uint16_t)(50 + i));
        for (size_t offset = 0; offset < sizeof(strings); offset += strlen(strings + offset) + 1)
            if (!strcmp(strings + offset, globals[i]))
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
    GAME_CHECK(qa_qc_program_load((qa_bytes){bytes, globals_at + 64 * 4},
        "fixture.qc", &program, &error));
    return program;
}

static bool target_resource(void *context, qa_qc_resource_kind kind, const char *name,
    bool precache, qa_qc_game_resource *out, qa_error *error)
{
    (void)context; (void)kind; (void)name; (void)precache; (void)out;
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Target fixture has no media");
    return false;
}
static bool target_emit(void *context, const qa_builtin_event *event, qa_error *error)
{
    (void)context; (void)event;
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Target fixture has no effects");
    return false;
}
static bool target_builtin(void *context, qa_qc_instance *vm,
    qa_qc_builtin builtin, const char *name, qa_error *error)
{
    (void)context; (void)vm; (void)builtin; (void)name;
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Target fixture does not call imports");
    return false;
}
static bool target_checkpoint(void *context, qa_buffer *out, qa_error *error)
{ (void)context; (void)error; *out = (qa_buffer){0}; return true; }
static bool target_restore(void *context, qa_bytes state, qa_error *error)
{ (void)context; (void)error; return state.size == 0; }
static bool target_stored(void *context, qa_qc_instance *vm,
    const qa_qc_store_event *event, qa_error *error)
{ return application_qc_targets_stored(context, vm, event, error); }
typedef struct target_native_fixture {
    qa_session *session;
    qa_string_id name;
    unsigned removals;
    bool protected;
} target_native_fixture;
static bool target_native_read(void *context, qa_actor_id actor, qa_authored_target *out)
{ (void)actor; *out = (qa_authored_target){.targetname = ((target_native_fixture *)context)->name}; return true; }
static bool target_native_remove(void *context, qa_actor_id actor, qa_error *error)
{
    target_native_fixture *native = context;
    ++native->removals;
    return native->protected || qa_session_release(native->session, actor, error);
}
static void target_released(void *context, qa_session *session, qa_actor_record record)
{
    (void)session;
    struct application_qc_state *engine = context;
    qa_targets_unbind(engine->provider->application->targets, record.id);
    qa_qc_game_actor_released(engine->provider->state.qc.game, record);
}
static void shared_targets(guest_fixture *fixture, qa_actor_owner owner)
{
    qa_error error = {0};
    qa_application app = {.session = fixture->session};
    app.targets = qa_targets_create(&(qa_target_options){.session = fixture->session}, &error);
    GAME_CHECK(app.targets);
    application_provider provider = {.kind = APPLICATION_PROVIDER_QC, .application = &app,
        .owner = owner, .component.clock.kind = QA_RULESET_NETQUAKE};
    application_qc_actor actors[8] = {0};
    struct application_qc_state engine = {.provider = &provider, .actors = actors,
        .actor_capacity = 8, .world = fixture->world, .profile = QA_QC_NETQUAKE,
        .field_bindings = qa_qc_program_resolved_fields(fixture->program),
        .global_bindings = qa_qc_program_resolved_globals(fixture->program),
        .services = {.session = fixture->session, .world = fixture->world, .emit = target_emit}};
    provider.state.qc.engine = &engine; provider.state.qc.program = fixture->program;
    qa_cvars *cvars = qa_cvars_create(&(qa_cvar_options){.dialect = QA_RULESET_NETQUAKE}, &error);
    GAME_CHECK(cvars);
    qa_console *console = qa_console_create(&(qa_console_options){.cvars = cvars,
        .context = {.dialect = QA_RULESET_NETQUAKE, .owner = owner}, .disable_builtins = true}, &error);
    GAME_CHECK(console);
    qa_qc_game_options options = {.vm = {.profile = QA_QC_NETQUAKE, .entity_capacity = 8,
            .host = {.owner = owner, .default_definition = owner},
            .observers = {.context = &engine, .stored = target_stored, .entity_stores_only = true}},
        .services = engine.services, .cvars = cvars, .console = console,
        .resource = target_resource, .checkpoint = target_checkpoint, .restore = target_restore};
    size_t requirement_count;
    const qa_qc_builtin_requirement *requirements =
        qa_qc_builtin_requirements(QA_QC_NETQUAKE, &requirement_count);
    qa_qc_builtin_binding *bindings = calloc(requirement_count, sizeof(*bindings));
    GAME_CHECK(bindings);
    size_t binding_count = 0;
    for (size_t i = 0; i < requirement_count; ++i) {
        bool found = false;
        for (size_t j = 0; j < binding_count; ++j)
            if (bindings[j].builtin == requirements[i].builtin) found = true;
        if (!found) bindings[binding_count++] = (qa_qc_builtin_binding){
            .builtin = requirements[i].builtin, .name = requirements[i].name,
            .call = target_builtin};
    }
    options.vm.host.builtins = bindings;
    options.vm.host.builtin_count = binding_count;
    GAME_CHECK(qa_qc_game_create(fixture->program, &options, &provider.state.qc.game, &error));
    free(bindings);
    qa_qc_instance *vm = qa_qc_game_instance(provider.state.qc.game);
    provider.state.qc.instance = vm;
    qa_component component = {.owner = owner, .clock = qa_clock_defaults(QA_RULESET_NETQUAKE),
        .state = &engine, .actor_released = target_released};
    GAME_CHECK(qa_session_add(fixture->session, &component, &error));
    qa_actor_owner foreign_owner;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture->session), "test:native-target", &foreign_owner, &error));
    qa_component foreign_component = {.owner = foreign_owner, .clock = qa_clock_defaults(QA_RULESET_Q3)};
    GAME_CHECK(qa_session_add(fixture->session, &foreign_component, &error));
    qa_actor_id self, activator, foreign;
    GAME_CHECK(qa_session_allocate(fixture->session, owner, owner, true, 1, &self, &error));
    GAME_CHECK(qa_session_allocate(fixture->session, owner, owner, true, 2, &activator, &error));
    GAME_CHECK(qa_session_allocate(fixture->session, foreign_owner, foreign_owner, false, 0, &foreign, &error));
    GAME_CHECK(qa_qc_bind_actor(vm, 1, self, QA_QC_SLOT_OWNED, &error));
    GAME_CHECK(qa_qc_bind_actor(vm, 2, activator, QA_QC_SLOT_OWNED, &error));
    qa_string_id native_name;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture->session), "native-target", &native_name, &error));
    target_native_fixture native_state = {.session = fixture->session,
        .name = native_name, .protected = true};
    qa_target_binding native = {.actor = foreign, .source = QA_RULESET_Q3,
        .context = &native_state, .read = target_native_read, .remove = target_native_remove};
    GAME_CHECK(qa_targets_bind(app.targets, &native, &error));
    qa_body_state body = {0};
    GAME_CHECK(qa_world_body_create(fixture->world, foreign, &body, &error));
    GAME_CHECK(qa_qc_bind_actor(vm, 3, foreign, QA_QC_SLOT_BORROWED, &error));
    int32_t references[4];
    for (uint32_t i = 1; i < 4; ++i)
        GAME_CHECK(qa_qc_slot_reference(vm, i, references + i, &error));
    actors[self.slot] = (application_qc_actor){.engine = &engine, .actor = self, .reference = references[1]};
    qa_qc_entity_access access = {.kind = QA_QC_ENTITY_BIND,
        .binding = {.kind = QA_QC_SLOT_OWNED, .slot = 1, .actor = self, .owner = owner, .source_slot = 1},
        .reference = references[1]};
    GAME_CHECK(application_qc_targets_bind(&engine, vm, &access, &error));
    access.binding = (qa_qc_slot_binding){.kind = QA_QC_SLOT_BORROWED, .slot = 3,
        .actor = foreign, .owner = owner}; access.reference = references[3];
    GAME_CHECK(application_qc_targets_bind(&engine, vm, &access, &error));
    qa_authored_target fields;
    GAME_CHECK(qa_targets_read(app.targets, foreign, &fields) && fields.targetname == native_name);
    int32_t first, second;
    GAME_CHECK(qa_qc_string_allocate(vm, "first-target", &first, &error));
    GAME_CHECK(qa_qc_string_allocate(vm, "second-target", &second, &error));
    GAME_CHECK(qa_qc_set_entity_int(vm, references[1], engine.field_bindings->targetname->offset, first, &error));
    qa_string_id first_id, second_id;
    GAME_CHECK(qa_qc_string_identity(vm, first, &first_id, &error));
    GAME_CHECK(qa_qc_string_identity(vm, second, &second_id, &error));
    qa_target_cursor cursor = {0}; qa_actor_id selected;
    GAME_CHECK(qa_targets_next(app.targets, first_id, &cursor, &selected) && qa_actor_id_equal(selected, self));
    GAME_CHECK(qa_qc_set_entity_int(vm, references[1], engine.field_bindings->targetname->offset, second, &error));
    cursor = (qa_target_cursor){0}; GAME_CHECK(!qa_targets_next(app.targets, first_id, &cursor, &selected));
    cursor = (qa_target_cursor){0};
    GAME_CHECK(qa_targets_next(app.targets, second_id, &cursor, &selected) && qa_actor_id_equal(selected, self));
    GAME_CHECK(qa_targets_set_targetname(app.targets, self, first_id, &error));
    cursor = (qa_target_cursor){0};
    GAME_CHECK(qa_targets_next(app.targets, first_id, &cursor, &selected) && qa_actor_id_equal(selected, self));
    cursor = (qa_target_cursor){0}; GAME_CHECK(!qa_targets_next(app.targets, second_id, &cursor, &selected));
    GAME_CHECK(qa_targets_set_target(app.targets, self, second_id, &error));
    GAME_CHECK(qa_targets_set_delay(app.targets, self, .2f, &error));
    GAME_CHECK(qa_targets_read(app.targets, self, &fields) &&
        fields.target == second_id && fields.delay_seconds == .2f);
    GAME_CHECK(qa_targets_set_targetname(app.targets, self, QA_STRING_NONE, &error));
    cursor = (qa_target_cursor){0}; GAME_CHECK(!qa_targets_next(app.targets, first_id, &cursor, &selected));
    uint32_t function_index;
    GAME_CHECK(qa_qc_program_find_function(fixture->program, "target_use", &function_index));
    GAME_CHECK(qa_qc_set_entity_int(vm, references[1], engine.field_bindings->use->offset, (int32_t)function_index, &error));
    const int32_t saved[] = {12345, 23456, 34567};
    for (uint32_t i = 0; i < 3; ++i) GAME_CHECK(qa_qc_set_global_int(vm, 50 + i, saved[i], &error));
    GAME_CHECK(qa_qc_set_global_float(vm, 53, 3.25f, &error));
    GAME_CHECK(qa_targets_invoke(app.targets, self, foreign, activator, &error));
    int32_t observed;
    GAME_CHECK(qa_qc_global_int(vm, 54, &observed, &error) && observed == references[1]);
    GAME_CHECK(qa_qc_global_int(vm, 55, &observed, &error) && observed == references[3]);
    GAME_CHECK(qa_qc_global_int(vm, 56, &observed, &error) && observed == references[2]);
    float number;
    GAME_CHECK(qa_qc_global_float(vm, 57, &number, &error) && number == 3.25f);
    GAME_CHECK(qa_qc_global_float(vm, 58, &number, &error) && number == 1);
    for (uint32_t i = 0; i < 3; ++i)
        GAME_CHECK(qa_qc_global_int(vm, 50 + i, &observed, &error) && observed == saved[i]);
    GAME_CHECK(qa_qc_global_float(vm, 53, &number, &error) && number == 3.25f);
    qa_targets_unbind(app.targets, self);
    qa_target_binding restored;
    GAME_CHECK(application_qc_target_binding(&provider, self, &restored, &error));
    GAME_CHECK(qa_targets_bind(app.targets, &restored, &error));
    GAME_CHECK(qa_qc_set_entity_int(vm, references[1], engine.field_bindings->targetname->offset, second, &error));
    cursor = (qa_target_cursor){0};
    GAME_CHECK(qa_targets_next(app.targets, second_id, &cursor, &selected) && qa_actor_id_equal(selected, self));
    application_qc_targets_close(&engine);
    GAME_CHECK(!qa_targets_read(app.targets, self, &fields));
    GAME_CHECK(qa_targets_read(app.targets, foreign, &fields) && fields.targetname == native_name);
    GAME_CHECK(qa_targets_invoke(app.targets, self, foreign, activator, &error));
    GAME_CHECK(qa_qc_global_float(vm, 58, &number, &error) && number == 1);
    qa_target_use remove = {.source = self, .activator = activator,
        .dialect = QA_RULESET_NETQUAKE, .fields.killtarget = native_name};
    GAME_CHECK(qa_targets_use_now(app.targets, &remove, &error));
    GAME_CHECK(native_state.removals == 1 &&
        qa_actors_get(qa_session_actors(fixture->session), foreign));
    GAME_CHECK(qa_targets_remove(app.targets, foreign, &error));
    GAME_CHECK(native_state.removals == 2 &&
        qa_actors_get(qa_session_actors(fixture->session), foreign));
    native_state.protected = false;
    GAME_CHECK(qa_targets_use_now(app.targets, &remove, &error));
    GAME_CHECK(native_state.removals == 3 &&
        !qa_actors_get(qa_session_actors(fixture->session), foreign));
    GAME_CHECK(qa_targets_remove(app.targets, foreign, &error) && native_state.removals == 3);
    GAME_CHECK(qa_targets_remove(app.targets, activator, &error));
    GAME_CHECK(!qa_actors_get(qa_session_actors(fixture->session), activator));
    GAME_CHECK(qa_session_remove(fixture->session, owner, &error));
    GAME_CHECK(qa_qc_game_destroy(provider.state.qc.game, &error));
    GAME_CHECK(qa_session_remove(fixture->session, foreign_owner, &error));
    qa_console_destroy(console); qa_cvars_destroy(cvars);
    qa_targets_destroy(app.targets);
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

static int32_t find_tag(qa_qc_instance *instance, int32_t start, int32_t text)
{
    qa_error error = {0};
    int32_t result;
    GAME_CHECK(qa_qc_set_global_int(instance, 4, start, &error));
    GAME_CHECK(qa_qc_set_global_int(instance, 7, 12, &error));
    GAME_CHECK(qa_qc_set_global_int(instance, 10, text, &error));
    GAME_CHECK(qa_qc_execute_named(instance, "find", 3, &error));
    GAME_CHECK(qa_qc_global_int(instance, 1, &result, &error));
    return result;
}

static void find_strings(guest_fixture *fixture)
{
    qa_error error = {0};
    qa_qc_instance *standalone = NULL;
    GAME_CHECK(qa_qc_instance_create(fixture->program,
        &(qa_qc_options){.profile = QA_QC_NETQUAKE, .entity_capacity = 4},
        &standalone, &error));
    int32_t needle;
    GAME_CHECK(qa_qc_string_allocate(standalone, "needle", &needle, &error));
    GAME_CHECK(find_tag(standalone, 0, needle) == 0);
    GAME_CHECK(find_tag(standalone, 0, 0) == 0);
    GAME_CHECK(qa_qc_set_global_int(standalone, 7, 0, &error));
    GAME_CHECK(!qa_qc_execute_named(standalone, "find", 3, &error));
    GAME_CHECK(error.code == QA_ERROR_FORMAT);
    GAME_CHECK(qa_qc_set_global_int(standalone, 7, 12, &error));
    GAME_CHECK(qa_qc_set_global_int(standalone, 10, INT32_MAX, &error));
    GAME_CHECK(!qa_qc_execute_named(standalone, "find", 3, &error));
    GAME_CHECK(qa_qc_instance_destroy(standalone, &error));

    qa_qc_instance *instance = fixture->instance;
    int32_t rows[4], same, empty;
    GAME_CHECK(qa_qc_string_allocate(instance, "needle", &needle, &error));
    GAME_CHECK(qa_qc_string_allocate(instance, "needle", &same, &error));
    GAME_CHECK(needle != same);
    GAME_CHECK(qa_qc_string_allocate(instance, "", &empty, &error) && empty != 0);
    for (size_t i = 0; i < 4; ++i)
        GAME_CHECK(qa_qc_spawn_entity(instance, rows + i, &error));
    GAME_CHECK(qa_qc_set_entity_int(instance, rows[1], 12, needle, &error));
    GAME_CHECK(qa_qc_set_entity_int(instance, rows[2], 12, same, &error));
    GAME_CHECK(qa_qc_set_entity_int(instance, rows[3], 12, empty, &error));
    GAME_CHECK(find_tag(instance, 0, needle) == rows[1]);
    GAME_CHECK(find_tag(instance, rows[1], needle) == rows[2]);
    GAME_CHECK(find_tag(instance, rows[2], needle) == 0);
    GAME_CHECK(find_tag(instance, 0, 0) == rows[3]);
    GAME_CHECK(qa_qc_remove_entity(instance, rows[1], &error));
    GAME_CHECK(find_tag(instance, 0, needle) == rows[2]);
    for (size_t i = 0; i < 4; ++i)
        if (i != 1) GAME_CHECK(qa_qc_remove_entity(instance, rows[i], &error));
}

static double guest_source_time(void *opaque)
{
    return ((guest_fixture *)opaque)->source_time;
}

static void string_buffer_growth(guest_fixture *shared, qa_actor_owner owner,
    const qa_qc_program *program, qa_qc_profile profile)
{
    qa_error error = {0};
    qa_qc_instance *instance = NULL;
    GAME_CHECK(qa_qc_instance_create(program,
        &(qa_qc_options){.profile = profile, .entity_capacity = 4,
            .host = {.session = shared->session, .world = shared->world,
                .owner = owner, .default_definition = owner}}, &instance, &error));
    char payload[4096];
    memset(payload, 'x', sizeof(payload) - 1); payload[sizeof(payload) - 1] = 0;
    int32_t dynamic, first, same;
    const char *text;
    GAME_CHECK(qa_qc_string_allocate(instance, payload, &dynamic, &error));
    GAME_CHECK(qa_qc_string(instance, dynamic, &text, &error));
    int32_t first_offset = (int32_t)((size_t)INT32_MAX - qa_qc_string_available(instance));
    GAME_CHECK(qa_qc_engine_string(instance, "first", text + 7, 16384, &first, &error));
    GAME_CHECK(qa_qc_string(instance, first, &text, &error));
    GAME_CHECK(strlen(text) == sizeof(payload) - 8 && !strcmp(text, payload + 7));
    int32_t rows[48];
    for (size_t i = 0; i < 48; ++i) {
        char name[32]; snprintf(name, sizeof(name), "buffer-%zu", i);
        GAME_CHECK(qa_qc_engine_string(instance, name, name, 64, rows + i, &error));
    }
    for (size_t i = 0; i < 48; ++i) {
        char name[32]; snprintf(name, sizeof(name), "buffer-%zu", i);
        GAME_CHECK(qa_qc_engine_string(instance, name, "changed", 64, &same, &error));
        GAME_CHECK(same == rows[i]);
        GAME_CHECK(qa_qc_string(instance, rows[i], &text, &error) && !strcmp(text, "changed"));
    }
    GAME_CHECK(qa_qc_engine_string(instance, "first", "final", 16384, &same, &error));
    GAME_CHECK(same == first);
    GAME_CHECK(qa_qc_string(instance, dynamic, &text, &error) && !strcmp(text, payload));
    size_t available = qa_qc_string_available(instance);
    GAME_CHECK(qa_qc_engine_string(instance, "first", "abcdefghij", 0, &same, &error));
    qa_string_id identity;
    GAME_CHECK(qa_qc_string_identity(instance, first_offset + 3, &identity, &error));
    GAME_CHECK(!strcmp(qa_strings_cstr(qa_session_strings(shared->session), identity), "defghij"));
    GAME_CHECK(qa_qc_engine_string(instance, "first", "wxyz", 0, &same, &error));
    GAME_CHECK(qa_qc_string_identity(instance, first_offset + 3, &identity, &error));
    GAME_CHECK(!strcmp(qa_strings_cstr(qa_session_strings(shared->session), identity), "z"));
    const char *lazy = profile == QA_QC_QUAKEWORLD ? "lazy-qw-command-identity" : "lazy-nq-command-identity";
    GAME_CHECK(qa_qc_engine_string(instance, "first", lazy, 0, &same, &error));
    qa_strings *strings = qa_session_strings(shared->session);
    GAME_CHECK(qa_strings_find(strings, (qa_bytes){(const uint8_t *)lazy, strlen(lazy)}) == QA_STRING_NONE);
    GAME_CHECK(qa_qc_string_identity(instance, same, &identity, &error));
    GAME_CHECK(!strcmp(qa_strings_cstr(strings, identity), lazy));
    GAME_CHECK(qa_qc_string_available(instance) == available);
    GAME_CHECK(!qa_qc_engine_string(instance, "missing-update-only", "x", 0, &same, &error));
    GAME_CHECK(error.code == QA_ERROR_NOT_FOUND && qa_qc_string_available(instance) == available);
    GAME_CHECK(qa_qc_instance_destroy(instance, &error));
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
        if (qa_platform_events_frame(frontend->platform_events,frontend->wall_time_ns+UINT64_C(20000000))!=QA_PLATFORM_EVENT_ACCEPTED ||
            !qa_frontend_step(frontend,&error)) goto cleanup;
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

typedef struct tree_observation {
    int32_t children[4];
    qa_collision_tree_stage stages[4];
    uint32_t payloads[4][2];
    unsigned count, first, mask, stop_after;
} tree_observation;

static qa_leaf_visit tree_observe(void *context, int32_t child,
    qa_collision_tree_stage stage, const uint32_t payload[2],
    qa_collision_tree_branch *branch, qa_error *error)
{
    tree_observation *observed = context;
    (void)error;
    unsigned slot = observed->count++;
    observed->children[slot] = child;
    observed->stages[slot] = stage;
    memcpy(observed->payloads[slot], payload, sizeof(observed->payloads[slot]));
    if (child >= 0 && stage == QA_COLLISION_TREE_ENTER) {
        branch->first = observed->first;
        branch->children = observed->mask;
        branch->between = true;
        branch->payload[0][0] = 13; branch->payload[0][1] = 17;
        branch->payload[1][0] = 19; branch->payload[1][1] = 23;
    }
    return observed->count == observed->stop_after ? QA_LEAF_STOP : QA_LEAF_CONTINUE;
}

static void tree_traversal(const qa_collision_geometry *geometry)
{
    qa_error error = {0};
    qa_collision_tree_frame scratch[2];
    const uint32_t payload[2] = {7, 11};
    for (unsigned first = 0; first < 2; ++first) {
        tree_observation observed = {.first = first, .mask = 3};
        GAME_CHECK(qa_collision_walk_tree(geometry, 0, scratch, payload,
            tree_observe, &observed, &error));
        GAME_CHECK(observed.count == 4 && observed.children[0] == 0 &&
            observed.children[1] == (first ? -1 : -2) &&
            observed.children[2] == 0 && observed.children[3] == (first ? -2 : -1));
        GAME_CHECK(observed.stages[2] == QA_COLLISION_TREE_BETWEEN &&
            observed.payloads[2][0] == 7 && observed.payloads[2][1] == 11 &&
            observed.payloads[1][0] == (first ? 19u : 13u) &&
            observed.payloads[3][1] == (first ? 17u : 23u));
    }
    tree_observation pruned = {0};
    GAME_CHECK(qa_collision_walk_tree(geometry, 0, scratch, payload,
        tree_observe, &pruned, &error));
    GAME_CHECK(pruned.count == 2 && pruned.stages[1] == QA_COLLISION_TREE_BETWEEN);
    tree_observation stopped = {.mask = 3, .stop_after = 2};
    GAME_CHECK(qa_collision_walk_tree(geometry, 0, scratch, payload,
        tree_observe, &stopped, &error));
    GAME_CHECK(stopped.count == 2 && stopped.children[1] == -2);
}

static void native_combat_calls(void)
{
    static const char damage[] =
        "[{\"kind\":\"field\",\"field\":\"target\"},{\"kind\":\"field\",\"field\":\"inflictor\"},"
        "{\"kind\":\"field\",\"field\":\"attacker\"},{\"kind\":\"field\",\"field\":\"direction\"},"
        "{\"kind\":\"field\",\"field\":\"point\"},{\"kind\":\"field\",\"field\":\"normal\"},"
        "{\"kind\":\"field\",\"field\":\"amount\"},{\"kind\":\"field\",\"field\":\"knockback\"},"
        "{\"kind\":\"field\",\"field\":\"flags\"},{\"kind\":\"field\",\"field\":\"cause\"}]";
    static const char pain[] =
        "{\"kind\":\"field\",\"field\":\"target\"},{\"kind\":\"field\",\"field\":\"attacker\"},"
        "{\"kind\":\"field\",\"field\":\"kick\"},{\"kind\":\"field\",\"field\":\"amount\"}";
    static const char death[] =
        "{\"kind\":\"field\",\"field\":\"target\"},{\"kind\":\"field\",\"field\":\"inflictor\"},"
        "{\"kind\":\"field\",\"field\":\"attacker\"},{\"kind\":\"field\",\"field\":\"amount\"},"
        "{\"kind\":\"field\",\"field\":\"point\"}";
    static const struct {
        qa_native_target target;
        const char *convention;
        qa_native_profile profile;
    } sources[] = {
        {{QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_I386, QA_NATIVE_ABI_CDECL_I386, 4}, "cdecl", QA_NATIVE_Q2_GAME_API3},
        {{QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_X86_64, QA_NATIVE_ABI_MICROSOFT_X64, 8}, "microsoft-x64", QA_NATIVE_Q2_GAME_API3},
        {{QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_I386, QA_NATIVE_ABI_SYSTEM_V_I386, 4}, "system-v-i386", QA_NATIVE_Q2_GAME_API3},
        {{QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_X86_64, QA_NATIVE_ABI_SYSTEM_V_X64, 8}, "system-v-x86-64", QA_NATIVE_Q2_GAME_API3},
        {{QA_NATIVE_OS_WINDOWS, QA_NATIVE_ARCH_X86_64, QA_NATIVE_ABI_MICROSOFT_X64, 8}, "microsoft-x64", QA_NATIVE_Q2_GAME_API2023},
        {{QA_NATIVE_OS_LINUX, QA_NATIVE_ARCH_X86_64, QA_NATIVE_ABI_SYSTEM_V_X64, 8}, "system-v-x86-64", QA_NATIVE_Q2_GAME_API2023}
    };
    static const application_q2_call_operation operations[] = {
        APPLICATION_Q2_DAMAGE, APPLICATION_Q2_PAIN, APPLICATION_Q2_DEATH
    };
    for (size_t source = 0; source < sizeof(sources) / sizeof(*sources); ++source) {
        bool rerelease = sources[source].profile == QA_NATIVE_Q2_GAME_API2023;
        for (size_t operation = 0; operation < sizeof(operations) / sizeof(*operations); ++operation) {
            qa_error error = {0};
            char arguments[1024], text[1200];
            int length = operation == 0 ? snprintf(arguments, sizeof(arguments), "%s", damage) :
                snprintf(arguments, sizeof(arguments), "[%s%s]", operation == 1 ? pain : death,
                    rerelease ? ",{\"kind\":\"field\",\"field\":\"cause\"}" : "");
            GAME_CHECK(length > 0 && (size_t)length < sizeof(arguments));
            length = snprintf(text, sizeof(text), "{\"convention\":\"%s\",\"arguments\":%s}",
                sources[source].convention, arguments);
            GAME_CHECK(length > 0 && (size_t)length < sizeof(text));
            qa_json_document *document = NULL;
            GAME_CHECK(qa_json_parse(QA_JSON_STRICT, (qa_bytes){(const uint8_t *)text, (size_t)length}, &document, &error));
            application_q2_call call = {0};
            GAME_CHECK(application_q2_call_read(document, qa_json_root(document), operations[operation],
                sources[source].profile, sources[source].target, &call, &error));
            GAME_CHECK(call.signature.abi == sources[source].target.abi &&
                call.signature.convention == QA_NATIVE_CALL_DEFAULT &&
                call.pointer_bytes == sources[source].target.pointer_bytes);
            GAME_CHECK(call.signature.parameter_count == (operation == 0 ? 10u :
                (operation == 1 ? 4u : 5u) + (rerelease ? 1u : 0u)));
            size_t cause = call.field_index[APPLICATION_Q2_CAUSE];
            if (operation && !rerelease) GAME_CHECK(cause == SIZE_MAX);
            else {
                GAME_CHECK(cause < call.signature.parameter_count);
                const qa_native_type *type = call.signature.parameters + cause;
                GAME_CHECK(type->kind == (!rerelease ? QA_NATIVE_I32 :
                    operation ? QA_NATIVE_ADDRESS : QA_NATIVE_BYTES));
                if (rerelease && !operation) {
                    GAME_CHECK(type->field_count == 3 && type->count == 1);
                    for (size_t i = 0; i < 3; ++i)
                        GAME_CHECK(type->fields[i].kind == QA_NATIVE_U8 && type->fields[i].count == 1);
                }
            }
            qa_native_value values[10] = {0}, fields[APPLICATION_Q2_FIELD_COUNT];
            uint8_t mod[] = {1, 0, 0};
            qa_native_address address = call.pointer_bytes == 8 ? UINT64_C(0x100000020) : 32;
            for (size_t i = 0; i < call.signature.parameter_count; ++i) {
                values[i].type = call.signature.parameters[i].kind;
                if (values[i].type == QA_NATIVE_ADDRESS) values[i].as.address = address;
                else if (values[i].type == QA_NATIVE_BYTES) values[i].as.bytes = (qa_native_memory){mod, sizeof(mod)};
            }
            GAME_CHECK(application_q2_call_project(&call, values, call.signature.parameter_count, fields, &error));
            GAME_CHECK(fields[APPLICATION_Q2_TARGET].as.address == address);
            if (rerelease && operation) GAME_CHECK(fields[APPLICATION_Q2_CAUSE].as.address == address);
            application_q2_call_free(&call);
            qa_json_destroy(document);
        }
    }
    qa_error error = {0};
    char text[1200];
    int length = snprintf(text, sizeof(text), "{\"convention\":\"microsoft-x64\",\"arguments\":%s}", damage);
    GAME_CHECK(length > 0 && (size_t)length < sizeof(text));
    qa_json_document *document = NULL;
    GAME_CHECK(qa_json_parse(QA_JSON_STRICT, (qa_bytes){(const uint8_t *)text, (size_t)length}, &document, &error));
    application_q2_call call = {0};
    GAME_CHECK(!application_q2_call_read(document, qa_json_root(document), APPLICATION_Q2_DAMAGE,
        QA_NATIVE_Q2_GAME_API3, sources[3].target, &call, &error));
    application_q2_call_free(&call);
    qa_json_destroy(document);
}

void test_guest(void);
void test_guest(void)
{
    qa_error error = {0};
    native_combat_calls();
    guest_fixture fixture = {0};
    gameplay_map_create(&fixture.map);
    tree_traversal(fixture.map.geometry);
    GAME_CHECK(qa_session_create(&(qa_session_options){.actor_capacity = 8,
        .component_capacity = 2},
        &fixture.session, &error));
    GAME_CHECK(qa_world_create(qa_session_actor_registry(fixture.session),
        fixture.map.geometry, NULL, 0, &fixture.world, &error));
    fixture.program = guest_program(QA_QC_API_NETQUAKE);
    qa_actor_owner owner;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session),
        "test:guest", &owner, &error));
    qa_qc_options options = {.profile = QA_QC_NETQUAKE, .entity_capacity = 8,
        .host = {.session = fixture.session, .world = fixture.world, .owner = owner,
            .default_definition = owner, .context = &fixture, .source_time_seconds = guest_source_time}};
    GAME_CHECK(qa_qc_instance_create(fixture.program, &options, &fixture.instance, &error));
    qa_component component = {.owner = owner, .clock = qa_clock_defaults(QA_RULESET_NETQUAKE),
        .state = fixture.instance, .actor_released = guest_component_released};
    GAME_CHECK(qa_session_add(fixture.session, &component, &error));
    execution(fixture.instance, fixture.program);
    shared_entities(&fixture, owner);
    find_strings(&fixture);
    string_buffer_growth(&fixture, owner, fixture.program, QA_QC_NETQUAKE);
    checkpoint_strings(fixture.program, QA_QC_NETQUAKE);
    qa_qc_program *qw = guest_program(QA_QC_API_QUAKEWORLD);
    string_buffer_growth(&fixture, owner, qw, QA_QC_QUAKEWORLD);
    checkpoint_strings(qw, QA_QC_QUAKEWORLD);
    qa_qc_program_destroy(qw);
    checkpoint_entities(&fixture, owner);
    projection_loads(&fixture, owner);
    GAME_CHECK(qa_session_remove(fixture.session, owner, &error));
    GAME_CHECK(qa_qc_instance_destroy(fixture.instance, &error));
    shared_targets(&fixture, owner);
    qa_qc_program_destroy(fixture.program);
    GAME_CHECK(qa_world_destroy(fixture.world, &error));
    GAME_CHECK(qa_session_destroy(fixture.session, &error));
    qa_collision_destroy(fixture.map.geometry);
    live_guest_tests();
}
