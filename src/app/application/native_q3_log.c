#include "native_q3_log.h"
#include "native_q3_console.h"

bool application_native_q3_source_log(void *context, const char *text,
                                     qa_error *error)
{
    return application_native_q3_log(context, text, error);
}

bool application_native_q3_log(application_provider *provider, const char *text,
                               qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || !application->session || !text || !provider->owner ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 source log requires its actual provider");
    if (!application_native_q3_console_borrow(provider, error)) return false;

    int32_t source_time;
    bool ok = qa_q3_source_clock(provider->state.q3, &source_time, error);
    qa_builtin_event event = {.kind = QA_BUILTIN_LOG, .family = QA_GAME_Q3,
        .provider = provider->owner};
    if (ok) {
        event.time_ns = (uint64_t)(uint32_t)source_time * UINT64_C(1000000);
        ok = qa_strings_intern_cstr(qa_session_strings(application->session),
                                   text, &event.text, error) &&
             application_emit(application, &event, error);
    }
    application_native_q3_console_release(provider);
    return ok;
}
