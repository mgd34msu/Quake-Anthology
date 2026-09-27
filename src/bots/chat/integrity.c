#include "internal.h"

bool qa_bot_chat_check_integrity(qa_bot_chat_system *system, qa_bot_chat_asset *asset,
                                 qa_error *e) {
    if (!system || system->retired || !asset ||
        (asset->view.kind != QA_BOT_CHAT_INITIAL && asset->view.kind != QA_BOT_CHAT_REPLIES)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat integrity input");
        return false;
    }
    char (*missing)[256] = NULL;
    size_t count = 0, capacity = 0;
    uint64_t revision = system->revision;
    ++system->references;
    qa_bot_chat_asset_retain(asset);
    bool ok = true;
    for (size_t message = 0; ok && !system->retired && system->revision == revision &&
                             message < asset->view.message_count;
         ++message) {
        const char *text = asset->messages[message];
        for (size_t i = 0; ok && text[i] && !system->retired && system->revision == revision;) {
            if (text[i++] != 1)
                continue;
            char kind = text[i];
            if (kind != 'r' && kind != 'v') {
                chat_report(system, QA_SCRIPT_FATAL, "Invalid escape in bot chat message");
                continue;
            }
            size_t start = ++i;
            while (text[i] && text[i] != 1)
                ++i;
            char key[256];
            size_t size = i - start;
            memcpy(key, text + start, size);
            key[size] = 0;
            if (text[i] == 1)
                ++i;
            if (kind != 'r' || chat_random_string(system, key) != NULL)
                continue;
            bool seen = false;
            for (size_t j = 0; j < count; ++j)
                if (strcmp(missing[j], key) == 0) {
                    seen = true;
                    break;
                }
            if (seen)
                continue;
            if (!bot_grow((void **)&missing, &capacity, count + 1, sizeof(*missing), e)) {
                ok = false;
                break;
            }
            memcpy(missing[count++], key, size + 1);
            char diagnostic[288];
            memcpy(diagnostic, "Missing random bot chat string ", 31);
            memcpy(diagnostic + 31, key, size + 1);
            chat_report(system, QA_SCRIPT_WARNING, diagnostic);
        }
    }
    free(missing);
    qa_bot_chat_asset_release(asset);
    chat_system_release(system);
    return ok;
}
