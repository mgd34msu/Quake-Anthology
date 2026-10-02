#include "guest_q3_video.h"
#include "guest_q3_private.h"

typedef struct video_engine {
    struct application_q3_guest *engine;
    application_provider *provider;
    q3g_role *game;
    const qa_launch_instance *launch, *client_descriptor;
    qa_world *world;
    uint64_t generation, epoch;
} video_engine;
typedef struct video_role {
    video_engine *parent;
    q3g_role *role;
    char *path;
    qa_qvm_role kind;
    uint32_t seat, client;
    application_provider *source;
    struct application_q3_guest *source_engine;
    qa_actor_owner source_owner;
    int32_t init_arguments[3];
    uint8_t init_argument_count;
    bool primary, local, source_cleared, closed, reopened, initialized, source_pinned, init_attempted;
} video_role;
struct application_guest_q3_video {
    qa_application *app;
    video_engine *engines;
    video_role *roles;
    size_t engine_count, role_count;
    uint64_t generation;
    bool busy;
};

static bool live_provider(const qa_application *app, const application_provider *provider)
{
    for (const application_provider *p = app->live_providers; p; p = p->next_live)
        if (p == provider) return true;
    return false;
}
static q3g_role **position(video_role *row)
{
    q3g_role **p = &row->parent->engine->roles;
    while (*p && *p != row->role) p = &(*p)->next;
    return p;
}
bool application_guest_q3_video_current(const application_guest_q3_video *ticket, qa_error *error)
{
    if (!ticket || !ticket->app || ticket->busy || !ticket->app->session ||
        (ticket->app->operation != APPLICATION_IDLE && ticket->app->operation != APPLICATION_ADVANCING) ||
        !qa_session_safe(ticket->app->session) ||
        qa_application_configuration_generation(ticket->app) != ticket->generation)
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video restart lost its returned application topology");
    for (size_t i = 0; i < ticket->engine_count; ++i) {
        const video_engine *p = &ticket->engines[i];
        if (!live_provider(ticket->app, p->provider) || p->provider->close_pending ||
            !p->provider->constructed || !p->provider->attached ||
            q3g_engine(p->provider) != p->engine || p->provider->launch != p->launch ||
            p->engine->game != p->game || p->engine->world != p->world ||
            p->engine->calls || p->engine->draining_clients || p->engine->restore_pending ||
            p->engine->round.phase != Q3G_ROUND_NONE || !qa_world_idle(p->world) ||
            p->engine->client_generation != p->generation || p->engine->connection_epoch != p->epoch ||
            (p->engine->client_descriptor ? qa_launch_instance_lease_view(p->engine->client_descriptor) : NULL) != p->client_descriptor)
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video restart changed its retained ENGINE or GAME owner");
    }
    for (size_t i = 0; i < ticket->role_count; ++i) {
        video_role *row = &ticket->roles[i];
        if (row->source && (!live_provider(ticket->app, row->source) || row->source->close_pending ||
            row->source->owner != row->source_owner || !row->source->constructed ||
            !row->source->attached || q3g_engine(row->source) != row->source_engine))
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video restart changed its physical source client");
        if (!row->role) continue;
        if (!*position(row))
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video restart lost its retained role");
        if (row->role->kind != row->kind || row->role->seat != row->seat ||
            strcmp(row->role->path, row->path))
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video restart changed its role recipe");
        if (row->reopened && (!row->role->ready || row->role->retired || !row->role->host ||
            row->role->local_client != row->local || row->role->client_source != row->source ||
            row->role->client_engine != row->source_engine || row->role->client != row->client ||
            row->role->source_owner != row->source_owner))
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video replacement changed its actual client association");
        if (row->reopened && row->initialized && row->role->init_succeeded &&
            (row->role->init_argument_count != row->init_argument_count ||
             memcmp(row->role->init_arguments, row->init_arguments, sizeof(row->init_arguments))))
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video replacement changed its real Init invocation");
    }
    return true;
}

static bool close_role(video_role *row, qa_error *error)
{
    if (!row->role) { row->closed = true; return true; }
    q3g_role **at = position(row);
    if (!*at) return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video cleanup lost its physical role");
    if (row->role->host && !qa_q3_host_destroy_ready(row->role->host))
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video role retains an entered callback or collision scene");
    if (!q3g_role_shutdown(row->role, false, error)) return false;
    q3g_role *next = row->role->next;
    if (!q3g_role_destroy(row->role, error)) return false;
    *at = next;
    row->role = NULL; row->closed = true; row->reopened = false;
    row->init_attempted = false;
    return true;
}
static void release(application_guest_q3_video *ticket)
{
    for (size_t i = 0; i < ticket->role_count; ++i) {
        video_role *row = &ticket->roles[i];
        if (row->source_pinned) --row->source->hosted_video_leases;
        free(row->path);
    }
    for (size_t i = 0; i < ticket->engine_count; ++i) {
        --ticket->engines[i].engine->client_leases;
        --ticket->engines[i].engine->video_leases;
        --ticket->engines[i].provider->hosted_video_leases;
    }
    free(ticket->roles); free(ticket->engines); free(ticket);
}
bool application_guest_q3_video_prepare(qa_application *app, application_guest_q3_video **out, qa_error *error)
{
    if (!app || !out || *out || !app->session || !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video preparation requires its returned application owner");
    size_t engines = 0, roles = 0;
    for (application_provider *p = app->live_providers; p; p = p->next_live) {
        struct application_q3_guest *engine = q3g_engine(p);
        if (!engine || !p->attached || p->close_pending) continue;
        bool selected = false;
        for (q3g_role *r = engine->roles; r; r = r->next) if (r->kind != QA_QVM_GAME) {
            if (!r->ready || r->retired || !r->host || engine->video_leases || !application_q3_guest_idle(p) ||
                !qa_q3_host_destroy_ready(r->host))
                return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video preparation requires completed idle client roles");
            selected = true; ++roles;
            if (r->initialized && (!r->init_succeeded ||
                r->init_argument_count != (r->kind == QA_QVM_UI ? 1 : 3)))
                return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video role lacks its actual successful Init receipt");
        }
        if (selected) {
            if (engine->client_leases == SIZE_MAX || p->hosted_video_leases == SIZE_MAX)
                return application_fail(error, QA_ERROR_MEMORY, "Hosted video source lifetime exhausted");
            ++engines;
        }
    }
    if (!roles) { *out = NULL; return true; }
    application_guest_q3_video *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return application_fail(error, QA_ERROR_MEMORY, "Retaining hosted video restart");
    ticket->engines = calloc(engines, sizeof(*ticket->engines));
    ticket->roles = calloc(roles, sizeof(*ticket->roles));
    if (!ticket->engines || !ticket->roles) {
        free(ticket->engines); free(ticket->roles); free(ticket);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining hosted video role recipes");
    }
    ticket->app = app; ticket->generation = qa_application_configuration_generation(app);
    for (application_provider *p = app->live_providers; p; p = p->next_live) {
        struct application_q3_guest *engine = q3g_engine(p);
        if (!engine || !p->attached || p->close_pending) continue;
        video_engine *parent = NULL;
        for (q3g_role *r = engine->roles; r; r = r->next) if (r->kind != QA_QVM_GAME) {
            if (!parent) {
                parent = &ticket->engines[ticket->engine_count++];
                *parent = (video_engine){.engine = engine, .provider = p, .game = engine->game,
                    .launch = p->launch, .world = engine->world,
                    .client_descriptor = engine->client_descriptor ? qa_launch_instance_lease_view(engine->client_descriptor) : NULL,
                    .generation = engine->client_generation, .epoch = engine->connection_epoch};
                ++engine->client_leases;
                ++engine->video_leases;
                ++p->hosted_video_leases;
            }
            video_role *row = &ticket->roles[ticket->role_count++];
            *row = (video_role){.parent = parent, .role = r, .kind = r->kind,
                .seat = r->seat, .client = r->client, .source = r->client_source,
                .source_engine = r->client_engine, .source_owner = r->source_owner,
                .primary = r->primary, .local = r->local_client, .source_cleared = r->source_cleared,
                .initialized = r->initialized, .init_argument_count = r->init_argument_count};
            memcpy(row->init_arguments, r->init_arguments, sizeof(row->init_arguments));
            if (row->initialized && row->kind == QA_QVM_CGAME) {
                if (!r->client_services.current_snapshot || !r->client_services.gamestate) {
                    application_fail(error, QA_ERROR_ARGUMENT, "Video CGAME lost its actual current source services");
                    release(ticket); return false;
                }
                int32_t message = 0, time = 0;
                ++engine->calls;
                bool reached = r->client_services.current_snapshot(r->client_services.context, &message, &time, error);
                const qa_q3_gamestate *state = reached ? r->client_services.gamestate(r->client_services.context) : NULL;
                if (reached && state)
                    memcpy(row->init_arguments, (int32_t[]){message, state->command_sequence, state->client_number},
                        sizeof(row->init_arguments));
                --engine->calls;
                if (!reached || !state) {
                    if (reached) application_fail(error, QA_ERROR_ARGUMENT,
                        "Video CGAME lost its real retained gamestate");
                    release(ticket); return false;
                }
            }
            if (row->source) {
                if (row->source->hosted_video_leases == SIZE_MAX) {
                    application_fail(error, QA_ERROR_MEMORY, "Hosted video physical source lifetime exhausted");
                    release(ticket); return false;
                }
                ++row->source->hosted_video_leases; row->source_pinned = true;
            }
            row->path = q3g_copy_text(r->path, error);
            if (!row->path) { release(ticket); return false; }
        }
    }
    *out = ticket;
    if (!application_guest_q3_video_current(ticket, error)) return false;
    ticket->busy = true;
    bool ok = true;
    for (int kind = QA_QVM_CGAME; kind <= QA_QVM_UI && ok; ++kind)
        for (size_t i = 0; i < ticket->role_count && ok; ++i)
            if ((int)ticket->roles[i].kind == kind) ok = close_role(&ticket->roles[i], error);
    ticket->busy = false;
    return ok;
}

bool application_guest_q3_video_reopen(application_guest_q3_video *ticket, qa_error *error)
{
    if (!application_guest_q3_video_current(ticket, error)) return false;
    ticket->busy = true;
    bool ok = true;
    for (size_t i = 0; i < ticket->role_count && ok; ++i) {
        video_role *row = &ticket->roles[i];
        if (row->reopened) continue;
        if (!close_role(row, error)) { ok = false; break; }
        struct application_q3_guest *engine = row->parent->engine;
        uint64_t sequence = engine->role_sequence;
        q3g_role *replacement = NULL;
        ok = q3g_role_create(engine, row->kind, row->seat, row->path, row->primary, &replacement, error);
        if (!ok) {
            /* The constructor retains a refused cleanup shell in its real list. */
            for (q3g_role *r = engine->roles; r; r = r->next)
                if (r->service_sequence > sequence && r->kind == row->kind &&
                    r->seat == row->seat && !strcmp(r->path, row->path)) { row->role = r; break; }
            break;
        }
        replacement->source_cleared = row->source_cleared;
        replacement->next = engine->roles; engine->roles = replacement;
        row->role = replacement; row->reopened = true;
    }
    ticket->busy = false;
    if (!ok || !application_guest_q3_video_current(ticket, error)) return false;
    /* UI precedes CG, as in the actual first client initialization. Arguments
     * come from the former successful entered calls, never renderer guesses. */
    for (int kind = QA_QVM_UI; kind >= QA_QVM_CGAME; --kind)
        for (size_t i = 0; i < ticket->role_count; ++i) {
            video_role *row = &ticket->roles[i];
            if ((int)row->kind != kind || !row->initialized) continue;
            if (row->role->initialized && row->role->init_succeeded) continue;
            if (row->init_attempted) {
                if (!close_role(row, error)) return false;
                return application_guest_q3_video_reopen(ticket, error);
            }
            row->init_attempted = true;
            if (!application_q3_guest_role_initialize(row->parent->provider, row->kind, row->seat,
                row->kind == QA_QVM_CGAME ? row->init_arguments[0] : 0,
                row->kind == QA_QVM_CGAME ? row->init_arguments[1] : 0,
                row->kind == QA_QVM_CGAME ? row->init_arguments[2] : 0,
                row->kind == QA_QVM_UI && row->init_arguments[0] != 0, error)) return false;
        }
    return application_guest_q3_video_current(ticket, error);
}
bool application_guest_q3_video_finish(application_guest_q3_video **slot, qa_error *error)
{
    if (!slot || !*slot) return true;
    application_guest_q3_video *ticket = *slot;
    if (!application_guest_q3_video_current(ticket, error)) return false;
    for (size_t i = 0; i < ticket->role_count; ++i)
        if (!ticket->roles[i].reopened || (ticket->roles[i].initialized &&
            (!ticket->roles[i].role->initialized || !ticket->roles[i].role->init_succeeded)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Hosted video replacement remains incomplete");
    release(ticket); *slot = NULL; return true;
}
bool application_guest_q3_video_abort(application_guest_q3_video **slot, qa_error *error)
{
    if (!slot || !*slot) return true;
    return application_guest_q3_video_reopen(*slot, error) && application_guest_q3_video_finish(slot, error);
}
