#include "internal.h"
#include "qa/text.h"

void chat_report(qa_bot_chat_system *s, qa_script_severity severity, const char *message) {
    if (s->services.diagnostic != NULL)
        s->services.diagnostic(s->services.context, severity, message);
}
static bool kind(const qa_bot_chat_asset *a, qa_bot_chat_asset_kind expected) {
    return a == NULL || a->view.kind == expected;
}
bool qa_bot_chat_system_create(const qa_bot_chat_services *services,
                               const qa_bot_chat_options *options, qa_bot_chat_system **out,
                               qa_error *e) {
    if (services == NULL || services->random.next == NULL || options == NULL || out == NULL ||
        options->console_capacity >= UINT32_MAX || !kind(options->synonyms, QA_BOT_CHAT_SYNONYMS) ||
        !kind(options->randoms, QA_BOT_CHAT_RANDOMS) ||
        !kind(options->matches, QA_BOT_CHAT_MATCHES) ||
        !kind(options->replies, QA_BOT_CHAT_REPLIES)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid native bot chat services/assets");
        return false;
    }
    qa_bot_chat_system *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating shared bot chat system");
        return false;
    }
    s->services = *services;
    s->options = *options;
    s->free_console = QA_BOT_NO_INDEX;
    s->references = 1;
    s->revision = 1;
    qa_bot_chat_asset_retain(options->synonyms);
    qa_bot_chat_asset_retain(options->randoms);
    qa_bot_chat_asset_retain(options->matches);
    qa_bot_chat_asset_retain(options->replies);
    *out = s;
    return true;
}
void chat_system_release(qa_bot_chat_system *s) {
    if (--s->references != 0)
        return;
    qa_bot_chat_asset_release(s->options.synonyms);
    qa_bot_chat_asset_release(s->options.randoms);
    qa_bot_chat_asset_release(s->options.matches);
    qa_bot_chat_asset_release(s->options.replies);
    free(s->console);
    free(s);
}
void qa_bot_chat_system_destroy(qa_bot_chat_system *s) {
    if (s == NULL || s->retired)
        return;
    s->retired = true;
    while (s->states != NULL)
        qa_bot_chat_destroy(s->states);
    chat_system_release(s);
}
bool qa_bot_chat_system_configure(qa_bot_chat_system *s, const qa_bot_chat_options *o,
                                  qa_error *e) {
    if (!s || s->retired || s->revision == UINT64_MAX || !o || o->console_capacity >= UINT32_MAX ||
        !kind(o->synonyms, QA_BOT_CHAT_SYNONYMS) || !kind(o->randoms, QA_BOT_CHAT_RANDOMS) ||
        !kind(o->matches, QA_BOT_CHAT_MATCHES) || !kind(o->replies, QA_BOT_CHAT_REPLIES)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat configuration");
        return false;
    }
    qa_bot_chat_asset_retain(o->synonyms);
    qa_bot_chat_asset_retain(o->randoms);
    qa_bot_chat_asset_retain(o->matches);
    qa_bot_chat_asset_retain(o->replies);
    qa_bot_chat_options previous = s->options;
    s->options = *o;
    ++s->revision;
    qa_bot_chat_asset_release(previous.synonyms);
    qa_bot_chat_asset_release(previous.randoms);
    qa_bot_chat_asset_release(previous.matches);
    qa_bot_chat_asset_release(previous.replies);
    return true;
}
bool qa_bot_chat_create(qa_bot_chat_system *system, int32_t client, qa_bot_chat **out,
                        qa_error *e) {
    if (system == NULL || system->retired || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot chat system/output");
        return false;
    }
    qa_bot_chat *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating bot chat state");
        return false;
    }
    s->system = system;
    s->client = client;
    s->first_console = s->last_console = QA_BOT_NO_INDEX;
    s->references = 1;
    ++system->references;
    s->next = system->states;
    if (s->next != NULL)
        s->next->previous = s;
    system->states = s;
    *out = s;
    return true;
}
void qa_bot_chat_destroy(qa_bot_chat *s) {
    if (s == NULL || s->retired)
        return;
    while (s->first_console != QA_BOT_NO_INDEX)
        qa_bot_chat_console_remove(s, s->system->console[s->first_console].message.handle);
    if (s->next != NULL)
        s->next->previous = s->previous;
    if (s->previous != NULL)
        s->previous->next = s->next;
    else
        s->system->states = s->next;
    s->retired = true;
    chat_release(s);
}
void chat_retain(qa_bot_chat *s) { ++s->references; }
void chat_release(qa_bot_chat *s) {
    if (--s->references != 0)
        return;
    qa_bot_chat_system *system = s->system;
    qa_bot_chat_asset_release(s->initial);
    free(s);
    chat_system_release(system);
}
bool qa_bot_chat_set_initial(qa_bot_chat *s, qa_bot_chat_asset *asset, qa_error *e) {
    if (s == NULL || !kind(asset, QA_BOT_CHAT_INITIAL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid initial bot chat asset/state");
        return false;
    }
    qa_bot_chat_asset_retain(asset);
    qa_bot_chat_asset_release(s->initial);
    s->initial = asset;
    return true;
}
void qa_bot_chat_set_name(qa_bot_chat *s, const char *name, int32_t client) {
    if (s != NULL) {
        chat_copy(s->name, sizeof(s->name), name);
        s->client = client;
    }
}
void qa_bot_chat_set_gender(qa_bot_chat *s, uint32_t gender) {
    if (s != NULL)
        s->gender = gender == 1 || gender == 2 ? gender : 0;
}
const char *qa_bot_chat_message(const qa_bot_chat *s) { return s == NULL ? "" : s->message; }
bool qa_bot_chat_take_message(qa_bot_chat *s, char *out, size_t capacity, qa_error *e) {
    if (s == NULL || out == NULL || capacity == 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat output");
        return false;
    }
    chat_strip_tildes(s->message);
    chat_copy(out, capacity, s->message);
    s->message[0] = 0;
    return true;
}
bool qa_bot_chat_enter(qa_bot_chat *s, int32_t recipient, qa_bot_chat_destination destination,
                       qa_error *e) {
    if (s == NULL || s->retired || destination < QA_BOT_CHAT_ALL ||
        destination > QA_BOT_CHAT_TELL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Missing bot chat command service or invalid destination");
        return false;
    }
    if (s->message[0] == 0)
        return true;
    chat_strip_tildes(s->message);
    char command[304];
    const char *prefix = destination == QA_BOT_CHAT_TEAM   ? "say_team "
                         : destination == QA_BOT_CHAT_TELL ? "tell "
                                                           : "say ";
    size_t length = strlen(prefix);
    memcpy(command, prefix, length);
    if (destination == QA_BOT_CHAT_TELL) {
        char number[32];
        if (!qa_format_number(recipient, number, e))
            return false;
        size_t size = strlen(number);
        memcpy(command + length, number, size);
        length += size;
        command[length++] = ' ';
    }
    memcpy(command + length, s->message, strlen(s->message) + 1);
    qa_bot_chat_services services = s->system->services;
    bool test = services.test_initial != NULL && services.test_initial(services.context);
    if (!test && services.command == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot chat command service");
        return false;
    }
    chat_retain(s);
    bool ok = true;
    if (test)
        chat_report(s->system, QA_SCRIPT_INFO, s->message);
    else
        ok = services.command(services.context, s->client, command, e);
    if (ok && !s->retired)
        s->message[0] = 0;
    chat_release(s);
    return ok;
}
bool chat_reserve_console(qa_bot_chat_system *s, size_t count, qa_error *e) {
    size_t limit = s->options.console_capacity == 0 ? UINT32_MAX - 1 : s->options.console_capacity;
    if (count > limit) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "Bot console message pool is full");
        return false;
    }
    if (count <= s->console_capacity)
        return true;
    size_t capacity = s->console_capacity == 0 ? 16 : s->console_capacity;
    while (capacity < count && capacity <= SIZE_MAX / 2)
        capacity *= 2;
    if (capacity > limit)
        capacity = limit;
    if (capacity < count || capacity > SIZE_MAX / sizeof(*s->console)) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "Bot console capacity overflow");
        return false;
    }
    chat_console_cell *cells = realloc(s->console, capacity * sizeof(*cells));
    if (cells == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "Growing retained bot console pool");
        return false;
    }
    s->console = cells;
    for (size_t i = capacity; i > s->console_capacity;) {
        --i;
        cells[i] = (chat_console_cell){.next = s->free_console, .previous = QA_BOT_NO_INDEX};
        s->free_console = (uint32_t)i;
    }
    s->console_capacity = capacity;
    return true;
}
bool qa_bot_chat_console_queue(qa_bot_chat *s, int32_t type, const char *text, float time,
                               uint32_t *handle, qa_error *e) {
    if (s == NULL || text == NULL || !isfinite(time)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot console message");
        return false;
    }
    qa_bot_chat_system *system = s->system;
    if (!chat_reserve_console(system, system->console_count + 1, e))
        return false;
    uint32_t index = system->free_console;
    chat_console_cell *cell = system->console + index;
    system->free_console = cell->next;
    if (++s->last_handle > 8192)
        s->last_handle = 1;
    cell->message = (qa_bot_console_message){.handle = s->last_handle, .time = time, .type = type};
    chat_copy(cell->message.text, sizeof(cell->message.text), text);
    cell->next = QA_BOT_NO_INDEX;
    cell->previous = s->last_console;
    if (s->last_console != QA_BOT_NO_INDEX)
        system->console[s->last_console].next = index;
    else
        s->first_console = index;
    s->last_console = index;
    ++s->console_count;
    ++system->console_count;
    if (handle != NULL)
        *handle = s->last_handle;
    return true;
}
bool qa_bot_chat_console_first(const qa_bot_chat *s, qa_bot_console_message *out) {
    if (s == NULL || out == NULL || s->first_console == QA_BOT_NO_INDEX)
        return false;
    *out = s->system->console[s->first_console].message;
    return true;
}
bool qa_bot_chat_console_remove(qa_bot_chat *s, uint32_t handle) {
    if (s == NULL)
        return false;
    qa_bot_chat_system *system = s->system;
    for (uint32_t i = s->first_console; i != QA_BOT_NO_INDEX; i = system->console[i].next) {
        chat_console_cell *cell = system->console + i;
        if (cell->message.handle != handle)
            continue;
        if (cell->next != QA_BOT_NO_INDEX)
            system->console[cell->next].previous = cell->previous;
        else
            s->last_console = cell->previous;
        if (cell->previous != QA_BOT_NO_INDEX)
            system->console[cell->previous].next = cell->next;
        else
            s->first_console = cell->next;
        cell->next = system->free_console;
        cell->previous = QA_BOT_NO_INDEX;
        system->free_console = i;
        --s->console_count;
        --system->console_count;
        return true;
    }
    return false;
}
size_t qa_bot_chat_console_count(const qa_bot_chat *s) { return s == NULL ? 0 : s->console_count; }
