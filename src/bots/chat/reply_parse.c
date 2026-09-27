#include "internal.h"

static bool read_key(qa_bot_chat_asset *a, qa_script *s, qa_bot_chat_key *key, qa_error *e) {
    qa_script_token t;
    if (!bot_token(s, &t, e))
        return false;
    if (qa_script_token_is(&t, "&") || qa_script_token_is(&t, "!")) {
        key->mode = qa_script_token_is(&t, "&") ? QA_BOT_CHAT_AND : QA_BOT_CHAT_NOT;
        if (!bot_token(s, &t, e))
            return false;
    }
    if (qa_script_token_is(&t, "name"))
        key->kind = QA_BOT_CHAT_NAME;
    else if (qa_script_token_is(&t, "female") || qa_script_token_is(&t, "male") ||
             qa_script_token_is(&t, "it")) {
        key->kind = QA_BOT_CHAT_GENDER;
        key->data.gender = qa_script_token_is(&t, "female") ? 1
                           : qa_script_token_is(&t, "male") ? 2
                                                            : 0;
    } else if (qa_script_token_is(&t, "(")) {
        key->kind = QA_BOT_CHAT_PATTERN;
        if (!chat_pieces_parse(a, s, ")", &key->data.pieces, e))
            return false;
    } else if (qa_script_token_is(&t, "<")) {
        key->kind = QA_BOT_CHAT_BOT_NAMES;
        char names[256];
        size_t length = 0;
        for (;;) {
            const char *name;
            if (!chat_string(a, s, &name, e))
                return false;
            size_t size = strlen(name), separator = length != 0 ? 1 : 0;
            if (size >= sizeof(names) - length || separator >= sizeof(names) - length - size)
                return bot_fail(s, "Bot name key exceeds 255 bytes", e);
            if (separator != 0)
                names[length++] = '\\';
            memcpy(names + length, name, size);
            length += size;
            bool more;
            if (!qa_script_check(s, ",", &more, e))
                return false;
            if (!more)
                break;
        }
        if (!qa_script_expect(s, ">", e))
            return false;
        key->data.text = bot_string(&a->arena, (qa_bytes){(const uint8_t *)names, length}, e);
        if (key->data.text == NULL)
            return false;
    } else {
        key->kind = QA_BOT_CHAT_WORD;
        if (!qa_script_unread(s, &t, e) || !chat_string(a, s, &key->data.text, e))
            return false;
    }
    return true;
}
static bool pattern_space(const qa_bot_chat_asset *a, qa_bot_chat_range pieces, const char *word) {
    for (uint32_t i = 0; i < pieces.count; ++i) {
        const qa_bot_chat_piece *p = a->pieces + pieces.first + i;
        if (p->kind == QA_BOT_CHAT_VARIABLE)
            return true;
        for (uint32_t j = 0; j < p->data.alternatives.count; ++j)
            if (qa_bot_chat_contains(a->alternatives[p->data.alternatives.first + j], word,
                                     false) >= 0)
                return true;
    }
    return false;
}
static void key_warnings(qa_bot_library *library, qa_bot_chat_asset *a, qa_script *s,
                         qa_bot_chat_range range) {
    bool all_prefixed = true, has_variables = false, has_word = false;
    for (uint32_t i = 0; i < range.count; ++i) {
        const qa_bot_chat_key *key = a->keys + range.first + i;
        if (key->mode == QA_BOT_CHAT_ANY) {
            all_prefixed = false;
            if (key->kind == QA_BOT_CHAT_WORD)
                has_word = true;
            if (key->kind == QA_BOT_CHAT_PATTERN)
                for (uint32_t j = 0; j < key->data.pieces.count; ++j)
                    has_variables |=
                        a->pieces[key->data.pieces.first + j].kind == QA_BOT_CHAT_VARIABLE;
        }
        if (key->kind != QA_BOT_CHAT_WORD || key->mode == QA_BOT_CHAT_ANY)
            continue;
        for (uint32_t j = 0; j < range.count; ++j) {
            const qa_bot_chat_key *other = a->keys + range.first + j;
            if (i == j || other->mode == QA_BOT_CHAT_NOT)
                continue;
            if (key->mode == QA_BOT_CHAT_AND && other->kind == QA_BOT_CHAT_PATTERN &&
                !pattern_space(a, other->data.pieces, key->data.text))
                bot_warning(library, s, "Bot reply pattern leaves no space for its required word");
            if (key->mode != QA_BOT_CHAT_NOT)
                continue;
            if (other->kind == QA_BOT_CHAT_WORD &&
                qa_bot_chat_contains(other->data.text, key->data.text, false) >= 0)
                bot_warning(library, s, "Bot reply excludes text present in another key");
            if (other->kind == QA_BOT_CHAT_PATTERN)
                for (uint32_t k = 0; k < other->data.pieces.count; ++k) {
                    const qa_bot_chat_piece *piece = a->pieces + other->data.pieces.first + k;
                    if (piece->kind != QA_BOT_CHAT_ALTERNATIVES)
                        continue;
                    for (uint32_t n = 0; n < piece->data.alternatives.count; ++n)
                        if (qa_bot_chat_contains(
                                a->alternatives[piece->data.alternatives.first + n], key->data.text,
                                false) >= 0)
                            bot_warning(library, s,
                                        "Bot reply excludes text present in a match alternative");
                }
        }
    }
    if (all_prefixed)
        bot_warning(library, s, "All bot reply keys have an AND or NOT prefix");
    if (has_variables && has_word)
        bot_warning(library, s,
                    "Bot reply word keys can select a message without matched variables");
}
bool chat_parse_replies(qa_bot_library *library, qa_bot_chat_asset *a, qa_script *s, qa_error *e) {
    for (;;) {
        qa_script_token t;
        bool found;
        if (!qa_script_next(s, &t, &found, e))
            return false;
        if (!found)
            break;
        if (!qa_script_token_is(&t, "["))
            return bot_fail(s, "Expected bot reply key list", e);
        qa_bot_chat_reply reply = {.keys.first = (uint32_t)a->view.key_count};
        for (;;) {
            qa_bot_chat_key key = {0};
            if (!read_key(a, s, &key, e) || !chat_append((void **)&a->keys, &a->key_capacity,
                                                         &a->view.key_count, sizeof(key), &key, e))
                return false;
            bool separator, end;
            if (!qa_script_check(s, ",", &separator, e) || !qa_script_check(s, "]", &end, e))
                return false;
            if (end)
                break;
        }
        reply.keys.count = (uint32_t)a->view.key_count - reply.keys.first;
        for (uint32_t i = 0; i < reply.keys.count / 2; ++i) {
            qa_bot_chat_key swap = a->keys[reply.keys.first + i];
            a->keys[reply.keys.first + i] = a->keys[reply.keys.first + reply.keys.count - 1 - i];
            a->keys[reply.keys.first + reply.keys.count - 1 - i] = swap;
        }
        key_warnings(library, a, s, reply.keys);
        if (!qa_script_expect(s, "=", e) || !bot_number(s, &reply.priority, true, e) ||
            !qa_script_expect(s, "{", e) || !chat_messages_parse(a, s, &reply.messages, e))
            return false;
        if (!chat_append((void **)&a->replies, &a->reply_capacity, &a->view.reply_count,
                         sizeof(reply), &reply, e))
            return false;
    }
    for (size_t i = 0; i < a->view.reply_count / 2; ++i) {
        qa_bot_chat_reply swap = a->replies[i];
        a->replies[i] = a->replies[a->view.reply_count - 1 - i];
        a->replies[a->view.reply_count - 1 - i] = swap;
    }
    return true;
}
