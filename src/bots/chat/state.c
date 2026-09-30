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
    if (s == NULL || s->retired || s->restoring)
        return;
    s->retired = true;
    while (s->states != NULL)
        qa_bot_chat_destroy(s->states);
    chat_system_release(s);
}
bool qa_bot_chat_system_active(const qa_bot_chat_system *s) {
    if (!s) return false;
    if (s->restoring) return true;
    size_t references = 1;
    for (const qa_bot_chat *state = s->states; state; state = state->next) {
        if (state->references != 1) return true;
        ++references;
    }
    return s->references != references;
}
bool qa_bot_chat_system_configure(qa_bot_chat_system *s, const qa_bot_chat_options *o,
                                  qa_error *e) {
    if (!s || s->retired || s->restoring || s->revision == UINT64_MAX || !o || o->console_capacity >= UINT32_MAX ||
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
    if (system == NULL || system->retired || system->restoring || out == NULL) {
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
    s->initial_revision = 1;
    ++system->references;
    s->next = system->states;
    if (s->next != NULL)
        s->next->previous = s;
    system->states = s;
    *out = s;
    return true;
}
void qa_bot_chat_destroy(qa_bot_chat *s) {
    if (s == NULL || s->retired || s->system->restoring)
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
    if (s == NULL || s->retired || s->system->restoring || s->initial_revision == UINT64_MAX ||
        !kind(asset, QA_BOT_CHAT_INITIAL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid initial bot chat asset/state");
        return false;
    }
    qa_bot_chat_asset_retain(asset);
    qa_bot_chat_asset_release(s->initial);
    s->initial = asset;
    ++s->initial_revision;
    return true;
}
static bool load_report(qa_bot_chat *s, qa_script_severity severity, const char *prefix,
                        const char *name, const char *path, qa_error *e) {
    size_t a = strlen(prefix), b = strlen(name), c = strlen(path);
    if (b > SIZE_MAX - a - 7 || c > SIZE_MAX - a - b - 7) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Bot chat diagnostic size overflow");
        return false;
    }
    size_t size = a + b + c + 7;
    char *message = malloc(size);
    if (message == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, size, "Retaining bot chat load diagnostic");
        return false;
    }
    memcpy(message, prefix, a);
    memcpy(message + a, name, b);
    memcpy(message + a + b, " from ", 6);
    memcpy(message + a + b + 6, path, c + 1);
    chat_report(s->system, severity, message);
    free(message);
    return true;
}
bool qa_bot_chat_load_initial(qa_bot_chat *s, qa_bot_library *library, const char *path,
                              const char *name, bool developer, int32_t *result, qa_error *e) {
    if (result == NULL || library == NULL || path == NULL || name == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid initial chat load request");
        return false;
    }
    if (!qa_bot_chat_set_initial(s, NULL, e))
        return false;
    uint64_t revision = s->initial_revision;
    chat_retain(s);
    *result = 8;
    qa_bot_chat_asset *asset = NULL;
    qa_error local = {0};
    bool cached = false;
    bool ok = chat_asset_load(library, QA_BOT_CHAT_INITIAL, path, name, &asset, &cached, &local);
    if (s->retired || s->initial_revision != revision)
        ok = true;
    else if (!ok && (local.code == QA_ERROR_FORMAT || local.code == QA_ERROR_NOT_FOUND))
        ok = load_report(s, QA_SCRIPT_FATAL, "couldn't load chat ", name, path, &local);
    else if (ok) {
        if (!cached)
            ok = load_report(s, QA_SCRIPT_INFO, "loaded ", name, path, &local);
        if (ok && !cached && developer && !s->retired && s->initial_revision == revision)
            ok = qa_bot_chat_check_integrity(s->system, asset, &local);
        if (ok && !s->retired && s->initial_revision == revision) {
            ok = qa_bot_chat_set_initial(s, asset, &local);
            if (ok)
                *result = 0;
        }
    }
    qa_bot_chat_asset_release(asset);
    chat_release(s);
    if (!ok && e != NULL)
        *e = local;
    return ok;
}
void qa_bot_chat_set_name(qa_bot_chat *s, const char *name, int32_t client) {
    qa_bot_chat_set_identity(s, name, &client);
}
void qa_bot_chat_set_identity(qa_bot_chat *s, const char *name, const int32_t *client) {
    if (s != NULL && !s->retired && !s->system->restoring) {
        chat_copy(s->name, sizeof(s->name), name);
        if (client != NULL)
            s->client = *client;
    }
}
void qa_bot_chat_set_gender(qa_bot_chat *s, uint32_t gender) {
    if (s != NULL && !s->retired && !s->system->restoring)
        s->gender = gender == 1 || gender == 2 ? gender : 0;
}
const char *qa_bot_chat_message(const qa_bot_chat *s) { return s == NULL ? "" : s->message; }
bool qa_bot_chat_write_message(qa_bot_chat *s, void *context,
                               bool (*write)(void *, const char *, qa_error *), qa_error *e) {
    if (s == NULL || s->retired || s->system->restoring || write == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat output");
        return false;
    }
    chat_strip_tildes(s->message);
    chat_retain(s);
    bool ok = write(context, s->message, e);
    if (ok && !s->retired)
        s->message[0] = 0;
    chat_release(s);
    return ok;
}
typedef struct chat_output {
    char *text;
    size_t capacity;
} chat_output;
static bool write_text(void *context, const char *message, qa_error *e) {
    chat_output *output = context;
    if (output->text == NULL || output->capacity == 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot chat output");
        return false;
    }
    chat_copy(output->text, output->capacity, message);
    return true;
}
bool qa_bot_chat_take_message(qa_bot_chat *s, char *out, size_t capacity, qa_error *e) {
    chat_output output = {out, capacity};
    return qa_bot_chat_write_message(s, &output, write_text, e);
}
bool qa_bot_chat_enter(qa_bot_chat *s, int32_t recipient, qa_bot_chat_destination destination,
                       qa_error *e) {
    return qa_bot_chat_enter_from(s, NULL, recipient, destination, e);
}
bool qa_bot_chat_enter_from(qa_bot_chat *s, const int32_t *source_client, int32_t recipient,
                            qa_bot_chat_destination destination, qa_error *e) {
    if (s == NULL || s->retired || s->system->restoring) {
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
    chat_retain(s);
    bool test = services.test_initial != NULL && services.test_initial(services.context);
    if (s->retired) {
        chat_release(s);
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Bot chat retired during command preparation");
        return false;
    }
    if (!test && services.command == NULL) {
        chat_release(s);
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot chat command service");
        return false;
    }
    bool ok = true;
    if (test)
        chat_report(s->system, QA_SCRIPT_INFO, s->message);
    else
        ok = services.command(services.context, source_client != NULL ? *source_client : s->client,
                              command, e);
    if (ok && !s->retired)
        s->message[0] = 0;
    chat_release(s);
    return ok;
}
bool chat_reserve_console(qa_bot_chat_system *s, size_t count, qa_error *e) {
    size_t limit = s->options.console_unavailable     ? 0
                   : s->options.console_capacity == 0 ? UINT32_MAX - 1
                                                      : s->options.console_capacity;
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
    if (s == NULL || s->retired || s->system->restoring || text == NULL || !isfinite(time)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot console message");
        return false;
    }
    qa_bot_chat_system *system = s->system;
    if (system->options.console_unavailable ||
        (system->options.console_capacity != 0 &&
         system->console_count >= system->options.console_capacity)) {
        if (handle != NULL)
            *handle = 0;
        chat_report(system, QA_SCRIPT_ERROR, "empty console message heap");
        return true;
    }
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
    if (s == NULL || s->system->restoring)
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
