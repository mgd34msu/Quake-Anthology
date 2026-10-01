#include "internal.h"
#include "qa/bots_log_consumers.h"
#include "qa/text.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct chat_log_output {
    qa_bot_log *log;
    qa_bot_log_file *file;
    bool raw;
} chat_log_output;

bool qa_bot_chat_system_log_bind(qa_bot_chat_system *system, qa_bot_log *log, qa_error *error) {
    if (!system || system->retired || qa_bot_chat_system_active(system)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Chat log binding requires its idle actual system");
        return false;
    }
    system->log = log;
    return true;
}

static bool write_text(chat_log_output *output, const char *text, qa_error *error) {
    if (!output->raw) return qa_bot_log_write(output->log, text, error);
    int64_t written;
    return qa_bot_log_file_write(output->file, text, &written, error);
}

static bool write_format(chat_log_output *output, qa_error *error, const char *format, ...) {
    va_list arguments, copy;
    va_start(arguments, format);
    va_copy(copy, arguments);
    int count = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (count < 0) {
        va_end(arguments);
        qa_error_set(error, QA_ERROR_IO, 0, "Formatting bot chat log text");
        return false;
    }
    char *text = malloc((size_t)count + 1);
    if (!text) {
        va_end(arguments);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating bot chat log text");
        return false;
    }
    (void)vsnprintf(text, (size_t)count + 1, format, arguments);
    va_end(arguments);
    bool okay = write_text(output, text, error);
    free(text);
    return okay;
}

static bool pieces(chat_log_output *output, const qa_bot_chat_asset_view *view,
                     qa_bot_chat_range range, bool first_only, qa_error *error) {
    for (uint32_t index = 0; index < range.count; ++index) {
        const qa_bot_chat_piece *piece = view->pieces + range.first + index;
        if (piece->kind == QA_BOT_CHAT_VARIABLE) {
            if (!write_format(output, error, "%u", piece->data.variable)) return false;
        } else {
            uint32_t count = first_only ? 1 : piece->data.alternatives.count;
            for (uint32_t alternative = 0; alternative < count; ++alternative) {
                const char *text = view->alternatives[piece->data.alternatives.first + alternative];
                if (!write_format(output, error, "\"%s\"", text)) return false;
                if (alternative + 1 < count && !write_text(output, "|", error)) return false;
            }
        }
        if (index + 1 < range.count && !write_text(output, ", ", error)) return false;
    }
    return true;
}

static bool reply_keys(chat_log_output *output, const qa_bot_chat_asset_view *view,
                         const qa_bot_chat_reply *reply, qa_error *error) {
    if (!write_text(output, "[", error)) return false;
    for (uint32_t index = 0; index < reply->keys.count; ++index) {
        const qa_bot_chat_key *key = view->keys + reply->keys.first + index;
        if (key->mode == QA_BOT_CHAT_AND && !write_text(output, "&", error)) return false;
        if (key->mode == QA_BOT_CHAT_NOT && !write_text(output, "!", error)) return false;
        if (key->kind == QA_BOT_CHAT_NAME) {
            if (!write_text(output, "name", error)) return false;
        } else if (key->kind == QA_BOT_CHAT_GENDER) {
            const char *gender = key->data.gender == 1 ? "female" : key->data.gender == 2 ? "male" : "it";
            if (!write_text(output, gender, error)) return false;
        } else if (key->kind == QA_BOT_CHAT_PATTERN) {
            if (!write_text(output, "(", error) ||
                !pieces(output, view, key->data.pieces, true, error) ||
                !write_text(output, ")", error)) return false;
        } else if (key->kind == QA_BOT_CHAT_WORD) {
            if (!write_format(output, error, "\"%s\"", key->data.text)) return false;
        }
        /* Source prints no text for its bot-name key bit. */
        if (index + 1 < reply->keys.count) {
            if (!write_text(output, ", ", error)) return false;
        } else {
            char priority[64];
            if (!qa_format_fixed(reply->priority, 0, priority, sizeof(priority), error) ||
                !write_format(output, error, "] = %s\n", priority)) return false;
        }
    }
    return write_text(output, "{\n", error);
}

static bool dump(chat_log_output *output, const qa_bot_chat_asset_view *view, qa_error *error) {
    if (view->kind == QA_BOT_CHAT_SYNONYMS) {
        for (size_t index = 0; index < view->group_count; ++index) {
            const qa_bot_chat_synonyms *group = view->groups + index;
            int32_t context;
            memcpy(&context, &group->context, sizeof(context));
            if (!write_format(output, error, "%d : [", context)) return false;
            for (uint32_t entry = 0; entry < group->entries.count; ++entry) {
                const qa_bot_chat_synonym *synonym = view->synonyms + group->entries.first + entry;
                char weight[64];
                if (!qa_format_fixed(synonym->weight, 2, weight, sizeof(weight), error) ||
                    !write_format(output, error, "(\"%s\", %s)", synonym->text, weight)) return false;
                if (entry + 1 < group->entries.count && !write_text(output, ", ", error)) return false;
            }
            if (!write_text(output, "]\n", error)) return false;
        }
    } else if (view->kind == QA_BOT_CHAT_RANDOMS) {
        for (size_t index = 0; index < view->list_count; ++index) {
            const qa_bot_chat_list *list = view->lists + index;
            if (!write_format(output, error, "%s = {", list->name)) return false;
            for (uint32_t message = 0; message < list->messages.count; ++message) {
                if (!write_format(output, error, "\"%s\"", view->messages[list->messages.first + message]) ||
                    !write_text(output, message + 1 < list->messages.count ? ", " : "}\n", error)) return false;
            }
        }
    } else if (view->kind == QA_BOT_CHAT_MATCHES) {
        for (size_t index = 0; index < view->template_count; ++index) {
            const qa_bot_chat_template *match = view->templates + index;
            if (!write_text(output, "{ ", error) || !pieces(output, view, match->pieces, false, error) ||
                !write_format(output, error, " = (%d, %d);}\n", match->type, match->subtype)) return false;
        }
    } else {
        if (!write_text(output, "BotDumpReplyChat:\n", error)) return false;
        for (size_t index = 0; index < view->reply_count; ++index) {
            const qa_bot_chat_reply *reply = view->replies + index;
            if (!reply_keys(output, view, reply, error)) return false;
            for (uint32_t message = 0; message < reply->messages.count; ++message)
                if (!write_format(output, error, "\t\"%s\";\n", view->messages[reply->messages.first + message])) return false;
            if (!write_text(output, "}\n", error)) return false;
        }
    }
    return true;
}

bool qa_bot_chat_dump_asset(qa_bot_log *log, qa_bot_chat_asset *asset, qa_error *error) {
    if (!asset || asset->view.kind == QA_BOT_CHAT_INITIAL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Chat dump requires an actual configuration asset");
        return false;
    }
    qa_bot_log_file *file = qa_bot_log_file_pointer(log);
    if (!file) return true;
    qa_bot_chat_asset_retain(asset);
    chat_log_output output = {.log = log, .file = file, .raw = true};
    bool okay = dump(&output, qa_bot_chat_asset_read(asset), error);
    qa_bot_chat_asset_release(asset);
    return okay;
}

bool qa_bot_chat_log_initial(qa_bot_log *log, qa_bot_chat_asset *asset, qa_error *error) {
    if (!asset || asset->view.kind != QA_BOT_CHAT_INITIAL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Initial chat log requires its actual source asset");
        return false;
    }
    if (!log) return true;
    qa_bot_chat_asset_retain(asset);
    const qa_bot_chat_asset_view *view = qa_bot_chat_asset_read(asset);
    chat_log_output output = {.log = log};
    bool okay = write_text(&output, "{", error);
    for (size_t index = 0; okay && index < view->list_count; ++index) {
        const qa_bot_chat_list *list = view->lists + index;
        okay = write_format(&output, error, " type \"%s\"", list->name) &&
            write_text(&output, " {", error) &&
            write_format(&output, error, "  numchatmessages = %u", list->messages.count);
        for (uint32_t message = 0; okay && message < list->messages.count; ++message)
            okay = write_format(&output, error, "  \"%s\"", view->messages[list->messages.first + message]);
        if (okay) okay = write_text(&output, " }", error);
    }
    if (okay) okay = write_text(&output, "}", error);
    qa_bot_chat_asset_release(asset);
    return okay;
}
