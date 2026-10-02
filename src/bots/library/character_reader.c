#include "character_reader.h"

static bool read_source(void *context, const qa_script_include *request,
                        qa_script_resource *resource, bool *found, qa_error *error) {
    bot_character_reader *reader = context;
    bool okay = reader->services.read(reader->services.context, request, resource, found, error);
    if (!okay) reader->callback_failed = true;
    return okay;
}
static void release_source(void *context, qa_script_resource *resource) {
    bot_character_reader *reader = context;
    reader->services.release(reader->services.context, resource);
}
static void diagnostic_source(void *context, const qa_script_diagnostic *diagnostic) {
    bot_character_reader *reader = context;
    if (reader->services.diagnostic) reader->services.diagnostic(reader->services.context, diagnostic);
}
bool bot_character_reader_create(const qa_script_services *services, bot_character_reader **out,
                                 qa_error *error) {
    if (!services || !services->read || !services->release || !out || *out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot parser requires its real source services"); return false;
    }
    bot_character_reader *reader = calloc(1, sizeof(*reader));
    if (!reader) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining bot parser source callbacks"); return false; }
    reader->services = *services; *out = reader; return true;
}
qa_script_services bot_character_reader_services(bot_character_reader *reader) {
    qa_script_services services = reader->services;
    services.context = reader; services.read = read_source; services.release = release_source;
    services.diagnostic = diagnostic_source; return services;
}
