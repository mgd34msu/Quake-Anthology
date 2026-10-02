#include "character_reader.h"
#include "../save_fields.h"

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
bool bot_character_reader_copy(qa_script *source, const bot_character_reader *host,
                               qa_script **out, bot_character_reader **out_host, qa_error *error) {
    if (!source) return true;
    if (!host || !out || *out || !out_host || *out_host) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Parser history requires its actual service owner"); return false;
    }
    qa_script_checkpoint image = {0};
    bool okay = qa_script_capture(source, &image, error) &&
        bot_character_reader_create(&host->services, out_host, error);
    if (okay) {
        (*out_host)->callback_failed = host->callback_failed;
        qa_script_services services = bot_character_reader_services(*out_host);
        okay = qa_script_restore(&services, &image, out, error);
    }
    qa_script_checkpoint_free(&image);
    if (!okay) { free(*out_host); *out_host = NULL; }
    return okay;
}
bool bot_character_reader_fields(qa_source_save_io *io, qa_script **reader,
                                 bot_character_reader **host, const qa_script_services *services) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = !reading && *reader;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    bool failed = !reading && *host && (*host)->callback_failed;
    qa_script_checkpoint saved = {0}; qa_buffer encoded = {0}; size_t length = 0;
    bool okay = qa_source_save_bool(io, &failed) && (reading ||
        (qa_script_capture(*reader, &saved, io->error) &&
         qa_script_checkpoint_encode(&saved, &encoded, io->error)));
    if (!reading) length = encoded.size;
    if (okay) okay = qa_source_save_count(io, &length, SIZE_MAX);
    if (okay && reading) {
        if (!services || length > io->input.size - io->offset)
            okay = bot_save_fail(io, QA_ERROR_FORMAT, "Bot PC requires actual retained script services");
        else {
            okay = qa_script_checkpoint_decode((qa_bytes){io->input.data + io->offset, length}, &saved, io->error) &&
                bot_character_reader_create(services, host, io->error);
            if (okay) {
                (*host)->callback_failed = failed;
                qa_script_services wrapped = bot_character_reader_services(*host);
                okay = qa_script_restore(&wrapped, &saved, reader, io->error);
            }
            if (okay) io->offset += length;
        }
    } else if (okay) okay = qa_source_save_bytes(io, encoded.data, length);
    qa_script_checkpoint_free(&saved); qa_buffer_free(&encoded);
    if (!okay) io->failed = true;
    return okay;
}
