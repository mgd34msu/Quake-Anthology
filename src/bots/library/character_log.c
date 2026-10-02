#include "internal.h"
#include "character_source.h"
#include "qa/bots_log_consumers.h"
#include "qa/text.h"
#include <stdio.h>

bool qa_bot_character_dump(qa_bot_library *library, qa_bot_log *log,
                            const qa_bot_character *character, qa_error *error) {
    if (!library || !character) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Character dump requires its actual resource owner");
        return false;
    }
    if (!log) return true;
    qa_bot_character *held = (qa_bot_character *)character;
    qa_bot_character_retain(held);
    const qa_bot_character_view *view = &held->view;
    bool okay = bot_character_header(held, error);
    if (okay) okay = qa_bot_log_write(log, view->path, error);
    if (okay && qa_bot_log_file_pointer(log)) {
        okay = bot_character_header(held, error);
        const qa_script_services *services = &library->options.scripts;
        if (okay && services->diagnostic) {
            qa_script_diagnostic warning = {
                .severity = QA_SCRIPT_WARNING, .location = {.path = view->path},
                .message = "BotDumpCharacter: omitted undefined skill log format (%d receives a promoted float)"};
            services->diagnostic(services->context, &warning);
        }
    }
    if (okay) okay = qa_bot_log_write(log, "{\n", error);
    for (uint32_t index = 0; okay && index < 80; ++index) {
        qa_bot_character_value current;
        okay = bot_character_value(held, index, &current, error);
        if (!okay) break;
        const qa_bot_character_value *value = &current;
        if (value->kind == QA_BOT_CHARACTER_UNSET) continue;
        char number[64];
        const char *text = number;
        if (value->kind == QA_BOT_CHARACTER_FLOAT)
            okay = qa_format_fixed(value->data.number, 6, number, sizeof(number), error);
        else if (value->kind == QA_BOT_CHARACTER_INTEGER)
            (void)snprintf(number, sizeof(number), "%d", value->data.integer);
        else text = value->data.string;
        if (!okay) break;
        size_t length = strlen(text);
        if (length > SIZE_MAX - 9) {
            qa_error_set(error, QA_ERROR_MEMORY, index, "Character log line length overflow");
            okay = false;
            break;
        }
        char *line = malloc(length + 9);
        if (!line) {
            qa_error_set(error, QA_ERROR_MEMORY, index, "Allocating character log line");
            okay = false;
            break;
        }
        (void)snprintf(line, length + 9, " %4u %s\n", index, text);
        okay = qa_bot_log_write(log, line, error);
        free(line);
    }
    if (okay) okay = qa_bot_log_write(log, "}\n", error);
    qa_bot_character_release(held);
    return okay;
}
