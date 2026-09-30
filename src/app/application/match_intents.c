#include "internal.h"
#include "match_intents.h"
#include "q3_restart.h"
#include "qa/launch_identity.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>

typedef enum match_map_stage {
    MATCH_MAP_EMPTY, MATCH_MAP_UNRESOLVED, MATCH_MAP_BEFORE,
    MATCH_MAP_WAIT, MATCH_MAP_AFTER
} match_map_stage;
struct application_match_intents {
    application_q3_restart *restart;
    match_map_stage stage;
    qa_match_intent_kind kind;
    qa_string_id source_command;
    qa_mode_id mode;
    qa_actor_id cause;
    qa_actor_owner owner;
    qa_product_id product;
    char *product_identity;
    qa_sha256_digest source_identity;
    qa_buffer selection_identity;
    size_t selection_index, cursor;
    uint64_t revision;
    application_next_map_plan plan;
    bool busy;
};

static char *copy_text(const char *text, qa_error *error) {
    if (!text) return NULL;
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) { application_fail(error, QA_ERROR_MEMORY, "cannot retain match map text"); return NULL; }
    memcpy(copy, text, length + 1);
    return copy;
}
static void clear(application_match_intents *state) {
    application_q3_restart *restart = state->restart;
    if (!state->plan.assignments) state->plan.assignment_count = 0;
    application_next_map_plan_free(&state->plan);
    qa_buffer_free(&state->selection_identity);
    free(state->product_identity);
    memset(state, 0, sizeof(*state));
    state->restart = restart;
}
application_match_intents *application_match_intents_create(qa_error *error) {
    application_match_intents *state = calloc(1, sizeof(*state));
    if (!state) application_fail(error, QA_ERROR_MEMORY, "cannot retain match map continuation");
    else if (!(state->restart = application_q3_restart_create(error))) { free(state); return NULL; }
    return state;
}
void application_match_intents_destroy(application_match_intents *state) {
    if (state) { clear(state); application_q3_restart_destroy(state->restart); free(state); }
}
bool application_match_intents_idle(const application_match_intents *state) {
    return !state || (!state->busy && application_q3_restart_idle(state->restart));
}
static bool restart_owned_by_travel(application_match_intents *state, qa_application *app) {
    qa_application_travel_view travel;
    if ((state->stage == MATCH_MAP_WAIT && state->revision) ||
        (app && qa_application_travel_read(app, &travel))) {
        application_q3_restart_cancel(state->restart);
        return true;
    }
    return false;
}
bool application_match_intents_request_restart(application_match_intents *state, qa_application *app,
    qa_mode_id mode, const qa_command_invocation *command, qa_error *error) {
    if (!state) return application_fail(error, QA_ERROR_ARGUMENT, "restart requires match continuation owner");
    if (restart_owned_by_travel(state, app)) return true;
    return application_q3_restart_request(state->restart, app, mode, command, error);
}
void application_match_intents_cancel_restart(application_match_intents *state) {
    if (state) application_q3_restart_cancel(state->restart);
}
void application_match_intents_restart_mutated(application_match_intents *state, const qa_application *app) {
    if (state) application_q3_restart_mutated(state->restart, app);
}
static const qa_launch_snapshot *snapshot(qa_application *app) {
    return app->routing_snapshot ? app->routing_snapshot :
        app->configuration ? qa_configuration_current(app->configuration) : NULL;
}
static application_provider *source(qa_application *app, qa_actor_owner owner) {
    for (application_provider *p = app->live_providers; p; p = p->next_live)
        if (p->owner == owner && p->constructed && p->attached && !p->close_pending &&
            p->launch && p->product) return p;
    return NULL;
}
static bool qualify(application_match_intents *state, qa_application *app,
                    bool original_mode, qa_error *error) {
    application_provider *p = app ? source(app, state->owner) : NULL;
    const qa_launch_snapshot *current = app ? snapshot(app) : NULL;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(current);
    if (!p || !p->product->identity || !state->product_identity || !choices ||
        state->selection_index >= choices->mode_count ||
        strcmp(p->product->identity, state->product_identity) ||
        strcmp(choices->modes[state->selection_index].instance, p->launch->selection.instance) ||
        !qa_sha256_equal(&p->launch->identity, &state->source_identity))
        return application_fail(error, QA_ERROR_ARGUMENT, "match map source selection changed");
    if (original_mode && (state->selection_index >= app->mode_count ||
        app->mode_ids[state->selection_index].slot != state->mode.slot ||
        app->mode_ids[state->selection_index].generation != state->mode.generation ||
        application_mode_provider(app, state->mode) != p))
        return application_fail(error, QA_ERROR_ARGUMENT, "match map mode generation retired");
    qa_buffer identity = {0};
    if (!qa_launch_mode_identity_encode(current, state->selection_index, &identity, error)) return false;
    bool equal = identity.size == state->selection_identity.size &&
        !memcmp(identity.data, state->selection_identity.data, identity.size);
    qa_buffer_free(&identity);
    if (!equal) return application_fail(error, QA_ERROR_ARGUMENT, "match map mode configuration changed");
    state->product = p->product->id;
    return true;
}
static bool boundary(qa_application *app, qa_error *error) {
    if (!app || app->operation != APPLICATION_IDLE || !app->session ||
        !qa_session_safe(app->session) || !qa_session_destroy_ready(app->session) ||
        !app->world || !qa_world_idle(app->world) || !qa_combat_idle(app->combat) ||
        !qa_console_idle(app->console) || !application_guests_idle(app) ||
        !application_bots_can_destroy(app) ||
        (app->modes && !qa_modes_idle(app->modes)) ||
        (app->equipment && !qa_equipment_idle(app->equipment)) ||
        app->publication_started || app->destroy_requested || app->finalizing ||
        app->pending_close || app->routing_snapshot || app->routing_providers ||
        app->routing_provider_count ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING))
        return application_fail(error, QA_ERROR_ARGUMENT, "match map execution requires idle source owners");
    return true;
}
static bool destination(application_match_intents *state, qa_application *app, qa_error *error) {
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot(app));
    if (!choices || !choices->world.map || strcmp(choices->world.map, state->plan.map) ||
        !qualify(state, app, false, error) || choices->world.geometry != state->product)
        return application_fail(error, QA_ERROR_ARGUMENT, "match map destination was not published");
    return true;
}
static qa_console *plan_console(qa_application *app, const application_next_map_plan *plan) {
    qa_console *found = NULL;
    for (size_t i = 0, count = qa_application_console_count(app); i < count; ++i) {
        qa_console *console = qa_application_console_at(app, i, NULL);
        qa_application_console_scope scope;
        if (!qa_application_console_scope_read(app, console, &scope) ||
            scope.provider != plan->scope.provider || scope.kind != plan->scope.kind ||
            scope.seat != plan->scope.seat) continue;
        if (found && found != console) return NULL;
        found = console;
    }
    return found;
}
static bool context_read(application_match_intents *state, qa_application *app,
                         bool after, qa_error *error) {
    if (!qualify(state, app, !after, error)) return false;
    application_next_map_plan *plan = &state->plan;
    if (!plan_console(app, plan))
        return application_fail(error, QA_ERROR_ARGUMENT, "match map console scope retired");
    if (after) {
        qa_command_context context = plan->context;
        context.registry = context.generation = 0;
        if (!qa_application_capture_command_context(app, &context, &context, error)) return false;
        plan->context = context;
    } else if (!qa_application_command_context_active(app, &plan->context))
        return application_fail(error, QA_ERROR_ARGUMENT, "match map command publication retired");
    return true;
}
static bool config_intent(qa_match_intent_kind kind) {
    return kind == QA_MATCH_GAME_TYPE || kind == QA_MATCH_WARMUP ||
           kind == QA_MATCH_TIME_LIMIT || kind == QA_MATCH_FRAG_LIMIT;
}
bool application_match_intents_enqueue(application_match_intents *state, qa_application *app,
    const qa_match_intent *intent, qa_error *error) {
    if (state && intent && intent->kind == QA_MATCH_RESTART_MAP) {
        if (restart_owned_by_travel(state, app)) return true;
        return application_q3_restart_enqueue(state->restart, app, intent, error);
    }
    if (!state || state->busy || !app || !intent || !app->session)
        return application_fail(error, QA_ERROR_ARGUMENT, "match map enqueue requires live owners");
    if (intent->kind != QA_MATCH_NEXT_MAP && intent->kind != QA_MATCH_SELECTED_MAP && !config_intent(intent->kind))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "match map intent lacks its source continuation");
    qa_bytes command = qa_strings_text(qa_session_strings(app->session), intent->source_command);
    if ((intent->kind == QA_MATCH_NEXT_MAP && intent->source_command) ||
        (intent->kind != QA_MATCH_NEXT_MAP && (!command.data || !command.size ||
         command.size >= 1024 || memchr(command.data, 0, command.size))))
        return application_fail(error, QA_ERROR_ARGUMENT, "match map intent has no valid source snapshot");
    if (state->stage != MATCH_MAP_EMPTY)
        return application_fail(error, QA_ERROR_ARGUMENT, "another match map transition is pending");
    application_provider *p = application_mode_provider(app, intent->mode);
    if (!p || !p->product || !p->product->identity)
        return application_fail(error, QA_ERROR_NOT_FOUND, "match map has no actual source provider");
    application_match_intents next = {.restart = state->restart, .stage = MATCH_MAP_UNRESOLVED,
        .kind = intent->kind, .source_command = intent->source_command,
        .mode = intent->mode, .cause = intent->actor, .owner = p->owner,
        .product = p->product->id, .source_identity = p->launch->identity};
    while (next.selection_index < app->mode_count &&
        (app->mode_ids[next.selection_index].slot != intent->mode.slot ||
         app->mode_ids[next.selection_index].generation != intent->mode.generation)) ++next.selection_index;
    next.product_identity = copy_text(p->product->identity, error);
    if (!next.product_identity || !qa_launch_mode_identity_encode(snapshot(app),
        next.selection_index, &next.selection_identity, error)) { clear(&next); return false; }
    *state = next;
    return true;
}
static bool assignments(application_match_intents *state, qa_application *app,
                        size_t end, bool after, qa_error *error) {
    while (state->cursor < end) {
        if (!boundary(app, error) || !context_read(state, app, after, error)) return false;
        qa_console *console = plan_console(app, &state->plan);
        application_map_assignment *value = &state->plan.assignments[state->cursor];
        if (config_intent(state->kind) &&
            !application_native_config_command_allowed(app, &state->plan, value->name, error)) return false;
        app->operation = APPLICATION_CONFIGURING;
        qa_cvars *cvars = qa_console_cvar_owner(console, &state->plan.context, value->name);
        if (!cvars || qa_cvars_dialect(cvars) != QA_CONSOLE_Q3) {
            app->operation = APPLICATION_IDLE;
            return application_fail(error, QA_ERROR_NOT_FOUND, "match map assignment has no source cvar owner");
        }
        bool okay = value->set_flags ?
            qa_cvars_set_flags(cvars, value->name, value->value, value->set_flags, error) :
            qa_cvars_set(cvars, value->name, value->value, false, error);
        app->operation = APPLICATION_IDLE;
        if (!okay) return false;
        ++state->cursor;
        if (!context_read(state, app, after, error)) return false;
    }
    return true;
}
static bool prepare_inner(application_match_intents *state, qa_application *app,
                           bool *consumed, qa_error *error) {
    if (state->stage == MATCH_MAP_EMPTY) return true;
    if (!boundary(app, error)) return false;
    if (state->stage == MATCH_MAP_AFTER) {
        if (!destination(state, app, error) || !context_read(state, app, true, error) ||
            !assignments(state, app, state->plan.assignment_count, true, error)) return false;
        clear(state); return true;
    }
    if (state->stage == MATCH_MAP_UNRESOLVED) {
        if (!qualify(state, app, true, error)) return false;
        qa_application_travel_view travel;
        if (qa_application_travel_read(app, &travel))
            return application_fail(error, QA_ERROR_ARGUMENT, "ordinary travel is already pending");
        application_next_map_plan next = {0};
        bool okay = config_intent(state->kind) ?
            application_native_config_plan(app, state->mode, state->kind, state->source_command, &next, error) :
            state->kind == QA_MATCH_NEXT_MAP ?
            application_native_next_map_plan(app, state->mode, &next, error) :
            application_native_selected_map_plan(app, state->mode, state->source_command, &next, error);
        if (!okay) return false;
        state->plan = next; state->stage = MATCH_MAP_BEFORE;
    }
    if (!context_read(state, app, false, error)) return false;
    if (state->stage == MATCH_MAP_BEFORE) {
        if (!assignments(state, app, state->plan.assignments_before_map, false, error)) return false;
        if (config_intent(state->kind)) { clear(state); *consumed = true; return true; }
        state->stage = MATCH_MAP_WAIT;
    }
    return true;
}
bool application_match_intents_prepare(application_match_intents *state, qa_application *app,
    bool *consumed, qa_error *error) {
    if (!state || state->busy || !consumed) return application_fail(error, QA_ERROR_ARGUMENT, "match intent execution cannot reenter");
    *consumed = false;
    state->busy = true;
    bool okay = prepare_inner(state, app, consumed, error);
    state->busy = false;
    if (okay && !*consumed && state->stage == MATCH_MAP_EMPTY)
        okay = application_q3_restart_prepare(state->restart, app, consumed, error);
    return okay;
}
bool application_match_intents_travel_read(const application_match_intents *state,
    const application_next_map_plan **plan, qa_application_travel_request *request) {
    if (!state || state->busy || state->stage != MATCH_MAP_WAIT || state->revision || !plan || !request) return false;
    *plan = &state->plan;
    *request = (qa_application_travel_request){.provider = state->owner, .cause = state->cause,
        .geometry = state->product, .expression = state->plan.map};
    return true;
}
static bool travel_matches(application_match_intents *state, qa_application *app,
                            uint64_t revision, qa_error *error) {
    qa_application_travel_view travel;
    if (!revision || !qa_application_travel_read(app, &travel) || travel.revision != revision ||
        travel.provider != state->owner || travel.geometry != state->product ||
        travel.target.kind != QA_TRAVEL_MAP || !travel.target.name ||
        strcmp(travel.target.name, state->plan.map) || travel.target.new_unit ||
        (travel.target.spawn_point && travel.target.spawn_point[0]) ||
        travel.carry_players || travel.complete_campaign || travel.has_landmark ||
        !qa_actor_id_equal(travel.cause, state->cause))
        return application_fail(error, QA_ERROR_ARGUMENT, "match map travel continuation differs");
    return true;
}
bool application_match_intents_waiting(const application_match_intents *state, uint64_t *revision) {
    if (!state || state->busy || state->stage != MATCH_MAP_WAIT || !revision) return false;
    *revision = state->revision;
    return true;
}
bool application_match_intents_queued(application_match_intents *state, qa_application *app,
    uint64_t revision, qa_error *error) {
    if (!state || state->busy || state->stage != MATCH_MAP_WAIT ||
        (state->revision && state->revision != revision) || !context_read(state, app, false, error) ||
        !travel_matches(state, app, revision, error)) return false;
    state->revision = revision;
    application_q3_restart_cancel(state->restart);
    return true;
}
bool application_match_intents_completed(application_match_intents *state, qa_application *app,
    uint64_t revision, qa_error *error) {
    if (!state || state->busy || state->stage != MATCH_MAP_WAIT || !revision || state->revision != revision)
        return application_fail(error, QA_ERROR_ARGUMENT, "match map completion has no current travel");
    if (!boundary(app, error)) return false;
    if (!destination(state, app, error)) return false;
    state->stage = MATCH_MAP_AFTER;
    bool consumed;
    return application_match_intents_prepare(state, app, &consumed, error);
}

static bool text_field(qa_source_save_io *io, char **text) {
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *text != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) {
        if (io->direction == QA_SOURCE_SAVE_READ) *text = NULL;
        return true;
    }
    uint64_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) : 0;
    if (!qa_source_save_u64(io, &length) || length >= SIZE_MAX) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, *text, (size_t)length);
    if (io->offset > io->input.size || length > io->input.size - io->offset) return false;
    char *value = malloc((size_t)length + 1);
    if (!value) return application_fail(io->error, QA_ERROR_MEMORY, "cannot restore private match text");
    if (!qa_source_save_bytes(io, value, (size_t)length) ||
        (length && memchr(value, 0, (size_t)length))) { free(value); return false; }
    value[length] = 0;
    *text = value;
    return true;
}
static bool context_fields(qa_source_save_io *io, qa_command_context *context) {
    uint32_t dialect = context->dialect, origin = context->origin;
    qa_string_id owner = (qa_string_id)context->owner;
    uint64_t registry = 1;
    if (io->direction == QA_SOURCE_SAVE_WRITE &&
        context->registry != qa_actors_identity(qa_session_actors(io->session))) return false;
    if (!qa_source_save_u64(io, &context->session) || !qa_source_save_string(io, &owner) ||
        !qa_source_save_u64(io, &context->client) || !qa_source_save_u32(io, &context->seat) ||
        !qa_source_save_u32(io, &dialect) || !qa_source_save_u32(io, &origin) ||
        !qa_source_save_bool(io, &context->direct) || !qa_source_save_bool(io, &context->console_text) ||
        !qa_source_save_u64(io, &registry) || registry != 1 ||
        !qa_source_save_u64(io, &context->generation) ||
        !qa_source_save_actor(io, &context->actor)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        context->registry = qa_actors_identity(qa_session_actors(io->session));
    context->owner = owner; context->dialect = (qa_console_dialect)dialect;
    context->origin = (qa_command_origin)origin;
    return owner && dialect == QA_CONSOLE_Q3 && origin == QA_COMMAND_SERVER &&
        !context->client && !context->actor.registry && !context->direct && !context->console_text &&
        context->registry && context->generation;
}
static bool stream(qa_source_save_io *io, application_match_intents *state) {
    char magic[4] = {'Q','A','M','I'}; uint32_t version = 5, stage = state->stage;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QAMI", 4) ||
        !qa_source_save_u32(io, &version) || version != 5 ||
        !application_q3_restart_stream(io, state->restart) ||
        !qa_source_save_u32(io, &stage) || stage > MATCH_MAP_AFTER) return false;
    state->stage = (match_map_stage)stage;
    if (state->stage == MATCH_MAP_EMPTY) return true;
    uint32_t kind = state->kind;
    if (!qa_source_save_u32(io, &kind) ||
        (kind != QA_MATCH_NEXT_MAP && kind != QA_MATCH_SELECTED_MAP && !config_intent((qa_match_intent_kind)kind)) ||
        !qa_source_save_string(io, &state->source_command)) return false;
    state->kind = (qa_match_intent_kind)kind;
    qa_bytes command = qa_strings_text(qa_session_strings(io->session), state->source_command);
    if ((kind == QA_MATCH_NEXT_MAP && state->source_command) ||
        (kind != QA_MATCH_NEXT_MAP && (!command.data || !command.size || command.size >= 1024 ||
         memchr(command.data, 0, command.size)))) return false;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size : SIZE_MAX;
    if (!qa_source_save_u32(io, &state->mode.slot) || !qa_source_save_u64(io, &state->mode.generation) ||
        !qa_source_save_actor(io, &state->cause) || !qa_source_save_string(io, &state->owner) ||
        !text_field(io, &state->product_identity) ||
        !qa_source_save_bytes(io, state->source_identity.bytes, sizeof(state->source_identity.bytes)) ||
        !qa_source_save_count(io, &state->selection_index, SIZE_MAX) ||
        !qa_source_save_count(io, &state->selection_identity.size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && state->selection_identity.size) {
        state->selection_identity.data = malloc(state->selection_identity.size);
        if (!state->selection_identity.data) return application_fail(io->error, QA_ERROR_MEMORY, "cannot restore mode identity");
    }
    if (!state->owner || !state->mode.generation || !state->product_identity ||
        !state->product_identity[0] || !state->selection_identity.size ||
        !qa_source_save_bytes(io, state->selection_identity.data, state->selection_identity.size)) return false;
    if (state->stage == MATCH_MAP_UNRESOLVED) return true;
    application_next_map_plan *plan = &state->plan;
    uint32_t scope_kind = plan->scope.kind;
    if (!qa_source_save_string(io, &plan->owner) || !qa_source_save_string(io, &plan->scope.provider) ||
        !qa_source_save_u32(io, &scope_kind) || !qa_source_save_u32(io, &plan->scope.seat) ||
        !context_fields(io, &plan->context) || !text_field(io, &plan->map) ||
        !qa_source_save_count(io, &plan->assignment_count, maximum) ||
        !qa_source_save_count(io, &plan->assignments_before_map, plan->assignment_count) ||
        !qa_source_save_count(io, &state->cursor, plan->assignment_count) ||
        !qa_source_save_u64(io, &state->revision) ||
        (state->revision && application_q3_restart_pending(state->restart))) return false;
    plan->mode = state->mode; plan->scope.kind = (qa_application_console_kind)scope_kind;
    bool config = config_intent(state->kind);
    if (plan->owner != state->owner || plan->context.owner != state->owner ||
        (config ? plan->map || state->stage != MATCH_MAP_BEFORE || state->revision ||
                  !plan->assignment_count || plan->assignments_before_map != plan->assignment_count
                : !plan->map || !plan->map[0]) ||
        !((scope_kind == QA_APPLICATION_CONSOLE_ENGINE && !plan->scope.provider && !plan->scope.seat) ||
          (scope_kind == QA_APPLICATION_CONSOLE_Q3_GAME && plan->scope.provider && !plan->scope.seat)) ||
        (state->stage == MATCH_MAP_BEFORE && (state->cursor > plan->assignments_before_map || state->revision)) ||
        (state->stage == MATCH_MAP_WAIT && state->cursor != plan->assignments_before_map) ||
        (state->stage == MATCH_MAP_AFTER && (!state->revision || state->cursor < plan->assignments_before_map))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && plan->assignment_count) {
        if (plan->assignment_count > SIZE_MAX / sizeof(*plan->assignments)) return false;
        plan->assignments = calloc(plan->assignment_count, sizeof(*plan->assignments));
        if (!plan->assignments) return application_fail(io->error, QA_ERROR_MEMORY, "cannot restore map assignments");
    }
    for (size_t i = 0; i < plan->assignment_count; ++i) {
        application_map_assignment *value = plan->assignments + i;
        if (!text_field(io, &value->name) || !text_field(io, &value->value) ||
            !qa_source_save_u32(io, &value->set_flags) || !value->name || !value->name[0] || !value->value ||
            (value->set_flags && value->set_flags != QA_CVAR_ARCHIVE &&
             value->set_flags != QA_CVAR_USERINFO && value->set_flags != QA_CVAR_SERVERINFO)) return false;
    }
    return true;
}
bool application_match_intents_reconnect(application_match_intents *state, qa_application *app, qa_error *error) {
    if (!state || state->busy) return application_fail(error, QA_ERROR_ARGUMENT, "match map reconnect cannot reenter");
    if (!application_q3_restart_reconnect(state->restart, app, error)) return false;
    if (state->stage == MATCH_MAP_EMPTY) return true;
    bool after = state->stage == MATCH_MAP_AFTER;
    if (!qualify(state, app, !after, error)) return false;
    if (after && !destination(state, app, error)) return false;
    if (state->stage == MATCH_MAP_UNRESOLVED) return true;
    if (!plan_console(app, &state->plan)) return application_fail(error, QA_ERROR_FORMAT, "saved match console scope is absent");
    if (config_intent(state->kind)) {
        application_next_map_plan actual = {0};
        if (!application_native_config_plan(app, state->mode, state->kind,
            state->source_command, &actual, error)) return false;
        bool equal = actual.assignment_count == state->plan.assignment_count &&
                    actual.owner == state->plan.owner && actual.scope.kind == state->plan.scope.kind &&
                    actual.scope.provider == state->plan.scope.provider && actual.scope.seat == state->plan.scope.seat;
        for (size_t i = 0; equal && i < actual.assignment_count; ++i)
                equal = !strcmp(actual.assignments[i].name, state->plan.assignments[i].name) &&
                    !strcmp(actual.assignments[i].value, state->plan.assignments[i].value) &&
                    actual.assignments[i].set_flags == state->plan.assignments[i].set_flags;
        application_next_map_plan_free(&actual);
        if (!equal) return application_fail(error, QA_ERROR_FORMAT, "saved config assignments differ from their actual source command");
    } else {
        char *map = NULL;
        bool valid_map = application_source_map_path(source(app, state->owner), state->plan.map, &map, error);
        bool exact_map = valid_map && !strcmp(map, state->plan.map);
        free(map);
        if (!exact_map) return application_fail(error, QA_ERROR_FORMAT, "saved match map is not qualified by its actual source");
    }
    if (!after && !qa_application_command_context_active(app, &state->plan.context))
        return application_fail(error, QA_ERROR_FORMAT, "saved match command publication differs");
    if (state->stage == MATCH_MAP_WAIT && state->revision &&
        !travel_matches(state, app, state->revision, error)) return false;
    return true;
}
bool application_match_intents_capture(application_match_intents *state, qa_application *app,
    qa_buffer *out, qa_error *error) {
    if (!state || !out || state->busy || !application_match_intents_reconnect(state, app, error)) return false;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, app->session, error) && stream(&io, state) &&
        qa_source_save_finish(&io, out);
    if (!okay && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "invalid match map continuation");
    qa_source_save_dispose(&io); return okay;
}
bool application_match_intents_restore(application_match_intents *state, qa_application *app,
    qa_bytes bytes, qa_error *error) {
    if (!state || state->busy || !app || !app->session) return application_fail(error, QA_ERROR_ARGUMENT, "match map restore requires candidate owners");
    application_match_intents next = {.restart = application_q3_restart_create(error)};
    if (!next.restart) return false;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, app->session, bytes, error) && stream(&io, &next) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) {
        clear(&next);
        application_q3_restart_destroy(next.restart);
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "invalid saved match map continuation");
        return false;
    }
    clear(state); application_q3_restart_destroy(state->restart); *state = next; return true;
}
