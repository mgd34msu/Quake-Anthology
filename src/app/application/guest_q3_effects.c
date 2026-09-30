#include "guest_q3_private.h"

bool q3g_client_effect(q3g_role *role, qa_application_q3_client_effect effect,
                        const char *text, qa_error *error)
{
    if (!role || !role->local_client || role->client >= 64 || !text)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client effect has no local seat owner");
    q3g_client *client = &role->engine->clients[role->client];
    switch (effect) {
    case QA_APPLICATION_Q3_SYSTEM_INFO: {
        char server[64];
        if (!qa_q3_info_value(text, "sv_serverid", server, sizeof(server), error)) return false;
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
        return application_fail(error, QA_ERROR_ARGUMENT, "unknown Q3 client effect");
    }
    application_provider *provider = role->engine->provider;
    qa_application *application = provider->application;
    if (!application->q3_client_effect)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 client prediction/frontend lifecycle owner is unbound");
    ++role->engine->calls;
    bool ok = application->q3_client_effect(application->guest_context, application,
        provider->owner, role->seat, effect, text, error);
    --role->engine->calls;
    return ok;
}
