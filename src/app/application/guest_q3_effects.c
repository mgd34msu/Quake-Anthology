#include "guest_q3_private.h"
#include "native_q3_wire_state.h"

bool q3g_client_effect(q3g_role *role, qa_application_q3_client_effect effect,
                        const char *text, qa_error *error)
{
    if (!role || !role->local_client || role->client >= 64 || !text)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client effect has no local seat owner");
    if (role->native_client) {
        ++role->engine->calls;
        bool ok = application_native_q3_wire_client_effect(role->native_client, effect, text, error);
        --role->engine->calls;
        return ok;
    }
    struct application_q3_guest *engine = role->engine;
    application_provider *provider = engine->provider;
    qa_application *application = provider->application;
    q3g_client *client = &engine->clients[role->client];
    if (!role->host || !role->ready || role->retired || !client->allocated ||
        !client->begun || client->pending_retirement || !client->actor.registry ||
        !qa_actors_get(qa_session_actors(application->session), client->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client effect lost its actual original source binding");
    if (!application->q3_client_effect)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 client prediction/frontend lifecycle owner is unbound");
    char *retained = q3g_copy_text(text, error);
    if (!retained) return false;
    text = retained;
    qa_actor_id actor = client->actor;
    qa_q3_host *host = role->host;
    switch (effect) {
    case QA_APPLICATION_Q3_SYSTEM_INFO: {
        char server[64];
        if (!qa_q3_info_value(text, "sv_serverid", server, sizeof(server), error)) {
            free(retained);
            return false;
        }
        char *end;
        long long value = strtoll(server, &end, 10);
        client->server_id = end == server ? 0 : value > INT32_MAX ? INT32_MAX :
            value < INT32_MIN ? INT32_MIN : (int32_t)value;
        break;
    }
    case QA_APPLICATION_Q3_MAP_RESTART:
        memset(client->commands, 0, sizeof(client->commands));
        break;
    case QA_APPLICATION_Q3_DISCONNECT:
        client->connected = false;
        break;
    case QA_APPLICATION_Q3_LEVEL_SHOT:
        break;
    default:
        free(retained);
        return application_fail(error, QA_ERROR_ARGUMENT, "unknown Q3 client effect");
    }
    ++engine->calls;
    bool ok = application->q3_client_effect(application->guest_context, application,
        provider->owner, role->seat, effect, text, error);
    if (ok && (q3g_engine(provider) != engine || !provider->constructed || !provider->attached ||
        provider->close_pending || role->host != host || role->retired ||
        !client->allocated || !client->begun || client->pending_retirement ||
        !qa_actor_id_equal(client->actor, actor) ||
        !qa_actors_get(qa_session_actors(application->session), actor)))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q3 client effect replaced or retired its original source admission");
    --engine->calls;
    free(retained);
    return ok;
}
