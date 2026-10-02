#include "guest_q3_private.h"
#include "native_q3_wire_state.h"
#include "qa/application_q3_client.h"

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
    struct application_q3_guest *source = role->client_engine;
    application_provider *source_provider = role->client_source;
    qa_application_q3_client_context admission;
    if (!source || !source_provider || source->provider != source_provider || q3g_engine(source_provider) != source ||
        source_provider->application != application || !source_provider->constructed || !source_provider->attached ||
        source_provider->close_pending || source->world != engine->world || role->source_owner != source_provider->owner ||
        source->seats[role->client] != role->seat || engine->calls == UINT_MAX || source->calls == UINT_MAX ||
        !qa_application_q3_client_context_read(application, provider->owner, role->seat, &admission, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client effect lost its retained original GAME source");
    q3g_client *client = &source->clients[role->client];
    if (!role->host || !role->ready || role->retired || !client->allocated ||
        !client->begun || client->pending_retirement || !client->actor.registry ||
        admission.source_owner != role->source_owner || admission.source_client != role->client ||
        admission.service_owner != role->service_owner || admission.native_source ||
        !qa_actor_id_equal(admission.source_actor, client->actor) ||
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
    if (source != engine) ++source->calls;
    bool ok = application->q3_client_effect(application->guest_context, application,
        provider->owner, role->seat, effect, text, error);
    if (ok && (q3g_engine(provider) != engine || !provider->constructed || !provider->attached ||
        provider->close_pending || role->host != host || role->retired ||
        role->client_engine != source || role->client_source != source_provider ||
        q3g_engine(source_provider) != source || !source_provider->constructed || !source_provider->attached ||
        source_provider->close_pending || !qa_application_q3_client_context_current(application, &admission) ||
        !client->allocated || !client->begun || client->pending_retirement ||
        !qa_actor_id_equal(client->actor, actor) ||
        !qa_actors_get(qa_session_actors(application->session), actor)))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Q3 client effect replaced or retired its original source admission");
    if (source != engine) --source->calls;
    --engine->calls;
    free(retained);
    return ok;
}
