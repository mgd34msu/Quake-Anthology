#include "internal.h"
#include "q3_restart.h"
#include "guest_q3_restart.h"
#include "q3_round.h"
#include "q3_world_restart.h"
#include "native_q3_clients.h"
#include "qa/launch_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct application_q3_restart {
    qa_mode_id mode;
    qa_actor_owner owner;
    qa_string_id source_command, product_identity;
    uint64_t source_serial;
    qa_buffer selection_identity;
    size_t selection_index;
    application_next_map_plan plan;
    int32_t delay_seconds, requested_ms, due_ms;
    uint64_t last_frame;
    bool pending, warmup_enabled, scheduled, announced, has_last_frame, busy, restoring;
};

static const qa_launch_snapshot *snapshot(qa_application *app) {
    return app->routing_snapshot ? app->routing_snapshot :
        app->configuration ? qa_configuration_current(app->configuration) : NULL;
}
static int32_t deadline(int32_t now, int32_t delay) {
    uint32_t bits = (uint32_t)now + (uint32_t)delay * UINT32_C(1000);
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
void application_q3_restart_cancel(application_q3_restart *state) {
    if (!state) return;
    uint64_t last_frame = state->last_frame;
    bool has_last_frame = state->has_last_frame, busy = state->busy;
    application_next_map_plan_free(&state->plan);
    qa_buffer_free(&state->selection_identity);
    memset(state, 0, sizeof(*state));
    state->last_frame = last_frame;
    state->has_last_frame = has_last_frame;
    state->busy = busy;
}
application_q3_restart *application_q3_restart_create(qa_error *error) {
    application_q3_restart *state = calloc(1, sizeof(*state));
    if (!state) application_fail(error, QA_ERROR_MEMORY, "cannot retain Q3 round continuation");
    return state;
}
void application_q3_restart_destroy(application_q3_restart *state) {
    if (state) { application_q3_restart_cancel(state); free(state); }
}
bool application_q3_restart_idle(const application_q3_restart *state) {
    return !state || !state->busy;
}
bool application_q3_restart_pending(const application_q3_restart *state) {
    return state && state->pending;
}
void application_q3_restart_mutated(application_q3_restart *state, const qa_application *app) {
    if (state && app) {
        state->has_last_frame = true;
        state->last_frame = application_frame_revision(app);
    }
}
bool application_q3_restart_clock(application_provider *provider, int32_t *out, qa_error *error) {
    qa_application *app = provider ? provider->application : NULL;
    qa_clock_state clock;
    if (!app || !out || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->product || provider->product->family != QA_GAME_Q3)
        return application_fail(error, QA_ERROR_NOT_FOUND, "restart source clock is absent");
    if (provider->kind != APPLICATION_PROVIDER_Q3)
        return application_q3_guest_round_clock(provider, out, error);
    if (!provider->state.q3 || provider->component.clock.kind != QA_CLOCK_Q3 ||
        !qa_session_clock(app->session, provider->owner, &clock) ||
        clock.frame.kind != QA_CLOCK_Q3 || clock.frame.provider != provider->owner)
        return application_fail(error, QA_ERROR_NOT_FOUND, "restart has no actual native Q3 clock");
    uint32_t bits = (uint32_t)(clock.frame.time_ns / UINT64_C(1000000));
    memcpy(out, &bits, sizeof(bits));
    return true;
}
static bool qualify(application_q3_restart *state, qa_application *app, qa_error *error) {
    application_provider *provider = app ? application_native_q3_mode_source_provider(app, state->mode) : NULL;
    const qa_launch_snapshot *current = app ? snapshot(app) : NULL;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(current);
    const char *identity = app && app->session ?
        qa_strings_cstr(qa_session_strings(app->session), state->product_identity) : NULL;
    if (!provider || !provider->launch || provider->owner != state->owner ||
        !provider->product || provider->product->family != QA_GAME_Q3 ||
        !identity || !provider->product->identity || strcmp(identity, provider->product->identity) ||
        !choices || state->selection_index >= choices->mode_count ||
        state->selection_index >= app->mode_count ||
        app->mode_ids[state->selection_index].slot != state->mode.slot ||
        app->mode_ids[state->selection_index].generation != state->mode.generation ||
        strcmp(choices->modes[state->selection_index].instance, provider->launch->selection.instance) ||
        !qa_application_command_context_active(app, &state->plan.context))
        return application_fail(error, QA_ERROR_ARGUMENT, "pending Q3 restart source retired");
    qa_buffer encoded = {0};
    if (!qa_launch_mode_identity_encode(current, state->selection_index, &encoded, error)) return false;
    bool equal = encoded.size == state->selection_identity.size &&
        !memcmp(encoded.data, state->selection_identity.data, encoded.size);
    qa_buffer_free(&encoded);
    if (!equal) return application_fail(error, QA_ERROR_ARGUMENT, "pending Q3 restart selection changed");
    if (state->restoring) state->source_serial = provider->launch->identity;
    if (!state->source_serial || state->source_serial != provider->launch->identity)
        return application_fail(error, QA_ERROR_ARGUMENT, "pending Q3 restart source retired");
    state->restoring = false;
    return true;
}
static bool boundary(qa_application *app, qa_error *error) {
    if (!app || app->operation != APPLICATION_IDLE || !app->session ||
        !qa_session_safe(app->session) || !qa_session_destroy_ready(app->session) ||
        !app->world || !qa_world_idle(app->world) || !qa_combat_idle(app->combat) ||
        !qa_console_idle(app->console) || !application_guests_idle(app) ||
        !application_bots_can_destroy(app) || (app->modes && !qa_modes_idle(app->modes)) ||
        (app->equipment && !qa_equipment_idle(app->equipment)) || app->publication_started ||
        app->destroy_requested || app->finalizing || app->pending_close || app->routing_snapshot ||
        app->routing_providers || app->routing_provider_count ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 restart requires drained idle source owners");
    return true;
}
bool application_q3_restart_enqueue(application_q3_restart *state, qa_application *app,
    const qa_match_intent *intent, qa_error *error) {
    if (!state || !app || !app->session || !intent || intent->kind != QA_MATCH_RESTART_MAP)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 restart needs its actual source intent");
    if (state->pending || (state->has_last_frame && state->last_frame == application_frame_revision(app)))
        return true;
    if (state->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round continuation cannot reenter");
    application_provider *provider = application_native_q3_mode_source_provider(app, intent->mode);
    if (!provider || !provider->launch || !provider->product ||
        provider->product->family != QA_GAME_Q3 || !provider->product->identity)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q3 restart has no selected GAME provider");
    application_q3_restart next = {.mode = intent->mode, .owner = provider->owner,
        .source_command = intent->source_command, .source_serial = provider->launch->identity,
        .last_frame = state->last_frame, .has_last_frame = state->has_last_frame};
    while (next.selection_index < app->mode_count &&
        (app->mode_ids[next.selection_index].slot != intent->mode.slot ||
         app->mode_ids[next.selection_index].generation != intent->mode.generation)) ++next.selection_index;
    bool okay = application_native_restart_plan(app, intent->mode, intent->source_command,
        &next.plan, &next.delay_seconds, &next.warmup_enabled, error) &&
        application_q3_restart_clock(provider, &next.requested_ms, error) &&
        qa_strings_intern_cstr(qa_session_strings(app->session), provider->product->identity,
            &next.product_identity, error) &&
        qa_launch_mode_identity_encode(snapshot(app), next.selection_index, &next.selection_identity, error);
    if (!okay) { application_q3_restart_cancel(&next); return false; }
    next.scheduled = next.delay_seconds != 0 && !next.warmup_enabled;
    next.due_ms = next.scheduled ? deadline(next.requested_ms, next.delay_seconds) : next.requested_ms;
    next.pending = true;
    application_q3_restart_cancel(state);
    *state = next;
    return true;
}
bool application_q3_restart_request(application_q3_restart *state, qa_application *app,
    qa_mode_id mode, const qa_command_invocation *command, qa_error *error) {
    application_provider *provider = app ? application_native_q3_mode_source_provider(app, mode) : NULL;
    if (!provider || !command || !command->raw || !command->argc || !command->argv ||
        command->context.owner != provider->owner || command->context.dialect != QA_CONSOLE_Q3 ||
        !qa_application_command_context_active(app, &command->context))
        return application_fail(error, QA_ERROR_ARGUMENT, "restart invocation lacks its actual GAME context");
    qa_match_intent intent = {.kind = QA_MATCH_RESTART_MAP, .mode = mode, .actor = command->context.actor};
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), command->raw, &intent.source_command, error))
        return false;
    return application_q3_restart_enqueue(state, app, &intent, error);
}
bool application_q3_restart_reconnect(application_q3_restart *state, qa_application *app, qa_error *error) {
    if (!state || !app || state->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 restart reconnect requires idle continuation");
    if (state->has_last_frame && state->last_frame > application_frame_revision(app))
        return application_fail(error, QA_ERROR_FORMAT, "saved Q3 restart frame exceeds its actual application frame");
    if (!state->pending) return true;
    if (state->has_last_frame && state->last_frame == application_frame_revision(app))
        return application_fail(error, QA_ERROR_FORMAT, "saved pending Q3 restart duplicates a completed frame");
    if (!qualify(state, app, error)) return false;
    application_next_map_plan actual = {0}; int32_t delay; bool warmup;
    if (!application_native_restart_plan(app, state->mode, state->source_command, &actual, &delay, &warmup, error))
        return false;
    bool equal = actual.owner == state->plan.owner && actual.scope.kind == state->plan.scope.kind &&
        actual.scope.provider == state->plan.scope.provider && actual.scope.seat == state->plan.scope.seat &&
        delay == state->delay_seconds;
    application_next_map_plan_free(&actual);
    if (!equal) return application_fail(error, QA_ERROR_FORMAT, "saved Q3 restart command or source scope differs");
    return true;
}
bool application_q3_restart_prepare(application_q3_restart *state, qa_application *app,
    bool *consumed, qa_error *error) {
    if (!state || !app || !consumed || state->busy)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 restart prepare cannot reenter");
    *consumed = false;
    if (!state->pending) return true;
    qa_application_travel_view travel;
    if (qa_application_travel_read(app, &travel)) { application_q3_restart_cancel(state); return true; }
    if (!boundary(app, error) || !application_q3_restart_reconnect(state, app, error)) return false;
    application_provider *provider = application_native_q3_mode_source_provider(app, state->mode);
    if (state->scheduled && !state->announced) {
        char text[32]; snprintf(text, sizeof(text), "%d", state->due_ms);
        app->operation = APPLICATION_CONFIGURING;
        bool okay = provider->kind == APPLICATION_PROVIDER_Q3 ?
            application_native_q3_restart_configstring(provider, 5, text, error) :
            application_q3_guest_round_configstring(provider, 5, text, error);
        app->operation = APPLICATION_IDLE;
        if (!okay) {
            /* The real source setter can commit before its notification fails.
             * Repeating equal text would skip that notification. */
            application_fault(app, error);
            return false;
        }
        state->announced = true; *consumed = true;
        return true;
    }
    int32_t now;
    if (!application_q3_restart_clock(provider, &now, error)) return false;
    if (now < state->due_ms) return true;
    bool mutated = false;
    state->busy = true;
    bool compatible = false;
    bool okay = application_q3_round_compatible(app, provider, state->mode,
                                               &compatible, error);
    if (okay)
        okay = compatible
            ? application_q3_round_restart(app, provider, state->mode, &mutated, error)
            : application_q3_world_restart(app, provider, state->mode, &mutated, error);
    if (!okay && !mutated && error && error->code == QA_ERROR_UNSUPPORTED) {
        *error = (qa_error){0};
        okay = application_q3_world_restart(app, provider, state->mode, &mutated, error);
    }
    state->busy = false;
    if (mutated) application_q3_restart_mutated(state, app);
    if (okay && !mutated)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round executor completed without an actual restart");
    if (mutated) application_q3_restart_cancel(state);
    if (okay) *consumed = true;
    return okay;
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
        !qa_source_save_u64(io, &context->generation) || !qa_source_save_actor(io, &context->actor)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        context->registry = qa_actors_identity(qa_session_actors(io->session));
    context->owner = owner; context->dialect = (qa_console_dialect)dialect; context->origin = (qa_command_origin)origin;
    return owner && dialect == QA_CONSOLE_Q3 && origin == QA_COMMAND_SERVER &&
        !context->client && !context->actor.registry && !context->direct && !context->console_text &&
        context->generation;
}
bool application_q3_restart_stream(qa_source_save_io *io, application_q3_restart *state) {
    char magic[4] = {'Q','A','R','S'};
    if (!state || state->busy || !qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QARS", 4) ||
        !qa_source_save_bool(io, &state->has_last_frame) || !qa_source_save_u64(io, &state->last_frame) ||
        (!state->has_last_frame && state->last_frame) || !qa_source_save_bool(io, &state->pending)) return false;
    if (!state->pending) return true;
    if (io->direction == QA_SOURCE_SAVE_READ) state->restoring = true;
    size_t maximum = io->direction == QA_SOURCE_SAVE_READ ? io->input.size : SIZE_MAX;
    uint32_t scope_kind = state->plan.scope.kind;
    if (!qa_source_save_u32(io, &state->mode.slot) || !qa_source_save_u64(io, &state->mode.generation) ||
        !qa_source_save_string(io, &state->owner) || !qa_source_save_string(io, &state->source_command) ||
        !qa_source_save_string(io, &state->product_identity) ||
        !qa_source_save_count(io, &state->selection_index, SIZE_MAX) ||
        !qa_source_save_count(io, &state->selection_identity.size, maximum)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && state->selection_identity.size) {
        state->selection_identity.data = malloc(state->selection_identity.size);
        if (!state->selection_identity.data)
            return application_fail(io->error, QA_ERROR_MEMORY, "cannot restore Q3 restart selection identity");
    }
    if (!qa_source_save_bytes(io, state->selection_identity.data, state->selection_identity.size) ||
        !qa_source_save_string(io, &state->plan.owner) || !qa_source_save_string(io, &state->plan.scope.provider) ||
        !qa_source_save_u32(io, &scope_kind) || !qa_source_save_u32(io, &state->plan.scope.seat) ||
        !context_fields(io, &state->plan.context) || !qa_source_save_i32(io, &state->delay_seconds) ||
        !qa_source_save_i32(io, &state->requested_ms) || !qa_source_save_i32(io, &state->due_ms) ||
        !qa_source_save_bool(io, &state->warmup_enabled) || !qa_source_save_bool(io, &state->scheduled) ||
        !qa_source_save_bool(io, &state->announced)) return false;
    state->plan.mode = state->mode; state->plan.scope.kind = (qa_application_console_kind)scope_kind;
    qa_bytes command = qa_strings_text(qa_session_strings(io->session), state->source_command);
    return state->owner && state->mode.generation && state->product_identity && state->selection_identity.size &&
        command.data && command.size && command.size < 1024 && !memchr(command.data, 0, command.size) &&
        state->plan.owner == state->owner && state->plan.context.owner == state->owner &&
        scope_kind == QA_APPLICATION_CONSOLE_Q3_GAME && state->plan.scope.provider == state->owner &&
        !state->plan.scope.seat && state->scheduled == (state->delay_seconds != 0 && !state->warmup_enabled) &&
        state->due_ms == (state->scheduled ? deadline(state->requested_ms, state->delay_seconds) : state->requested_ms) &&
        (state->scheduled || !state->announced);
}
