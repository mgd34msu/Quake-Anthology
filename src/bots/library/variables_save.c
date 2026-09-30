#include "variables_private.h"
#include "../save_fields.h"
#include "qa/bot_library_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'V', 'A', 'R', 'S', 0};

static bool variable_fields(qa_source_save_io *io, const bot_variable *source,
                            qa_bot_library *scratch, bot_variable ***tail)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_bot_variable value = reading ? (qa_bot_variable){0} : source->view;
    size_t capacity = reading ? 0 : source->capacity;
    bool ok = bot_save_text(io, &value.name) && value.name &&
        bot_save_text(io, &value.string) && value.string &&
        qa_source_save_i32(io, &value.flags) && qa_source_save_bool(io, &value.modified) &&
        qa_source_save_f32(io, &value.value) && qa_source_save_count(io, &capacity, SIZE_MAX);
    if (ok && (capacity <= strlen(value.string) ||
        strlen(value.name) > SIZE_MAX - sizeof(bot_variable) - 1))
        ok = bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot variable storage extent");
    if (ok && reading) {
        uint32_t hash = bot_variable_name_hash(value.name);
        for (bot_variable *entry = scratch->variable_buckets[hash & 127]; entry; entry = entry->bucket_next)
            if (entry->hash == hash && bot_variable_name_equal(entry->name, value.name)) {
                ok = bot_save_fail(io, QA_ERROR_FORMAT, "Duplicate saved bot variable name");
                break;
            }
        bot_variable *entry = ok ? calloc(1, sizeof(*entry) + strlen(value.name) + 1) : NULL;
        if (ok && !entry)
            ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring bot variable record");
        if (ok) {
            entry->text = malloc(capacity);
            if (!entry->text) {
                free(entry);
                ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring bot variable text storage");
            } else {
                memcpy(entry->name, value.name, strlen(value.name) + 1);
                memcpy(entry->text, value.string, strlen(value.string) + 1);
                entry->view = value;
                entry->view.name = entry->name;
                entry->view.string = entry->text;
                entry->capacity = capacity;
                entry->hash = hash;
                **tail = entry;
                *tail = &entry->next;
                /* Both producer chains insert new variables at their head.
                 * Appending decoded records retains their captured order. */
                bot_variable **bucket = &scratch->variable_buckets[hash & 127];
                while (*bucket)
                    bucket = &(*bucket)->bucket_next;
                *bucket = entry;
            }
        }
    }
    if (reading) {
        free((void *)value.name);
        free((void *)value.string);
    }
    if (!ok && !io->failed)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot variable record");
    return ok;
}

bool qa_bot_library_variables_capture(const qa_bot_library *library, qa_buffer *out, qa_error *error)
{
    if (!library || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot variable table/output");
        return false;
    }
    size_t count = 0;
    for (const bot_variable *entry = library->variables; entry; entry = entry->next) {
        if (count == SIZE_MAX) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Bot variable count overflow");
            return false;
        }
        ++count;
    }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) &&
        qa_source_save_count(&io, &count, SIZE_MAX);
    for (const bot_variable *entry = library->variables; ok && entry; entry = entry->next)
        ok = variable_fields(&io, entry, NULL, NULL);
    if (ok)
        ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bot_library_variables_restore(qa_bot_library *library, qa_bytes bytes, qa_error *error)
{
    if (!library) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing detached bot variable library");
        return false;
    }
    qa_bot_library scratch = {0};
    bot_variable **tail = &scratch.variables;
    qa_source_save_io io = {0};
    size_t count = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) &&
        qa_source_save_count(&io, &count, bytes.size / 35);
    for (size_t i = 0; ok && i < count; ++i)
        ok = variable_fields(&io, NULL, &scratch, &tail);
    if (ok)
        ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        qa_bot_library_variables_clear(library);
        library->variables = scratch.variables;
        memcpy(library->variable_buckets, scratch.variable_buckets, sizeof(library->variable_buckets));
        scratch.variables = NULL;
    }
    qa_bot_library_variables_clear(&scratch);
    return ok;
}
