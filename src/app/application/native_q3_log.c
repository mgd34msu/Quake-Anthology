#include "native_q3_log.h"

bool application_native_q3_source_log(void *context, const char *text,
                                     qa_error *error)
{
    return application_native_q3_log(context, text, error);
}

bool application_native_q3_log(application_provider *provider, const char *text,
                               qa_error *error)
{
    return application_source_log(provider, text, false, error);
}
