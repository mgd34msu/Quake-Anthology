#include "internal.h"

struct client_claim {
    uint64_t instance;
    uint32_t seat;
    char *name, *label;
    qa_input_client_handler handler;
    void *user;
};
struct installed_command {
    char *name;
};
struct qa_input_client_commands {
    qa_input_client_options options;
    uint64_t session;
    unsigned seats;
    bool active;
    struct client_claim *claims;
    size_t count, capacity;
    struct installed_command *installed;
    size_t installed_count, installed_capacity;
};
static char *fold_copy(const char *text, bool fold, qa_error *error) {
    size_t n = strlen(text);
    char *copy = malloc(n + 1);
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating client command name");
        return NULL;
    }
    for (size_t i = 0; i <= n; ++i) {
        unsigned c = (unsigned char)text[i];
        copy[i] = (char)(fold && c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    return copy;
}
static bool installed_dispatch(void *user, const qa_command_invocation *cmd, qa_error *error) {
    qa_input_client_commands *c = user;
    return qa_input_client_dispatch(c, cmd, 0, error) != QA_COMMAND_FAILED;
}
static bool install(qa_input_client_commands *c, const char *name, qa_error *error) {
    if (!c->active)
        return true;
    for (size_t i = 0; i < c->installed_count; ++i)
        if (strcmp(c->installed[i].name, name) == 0)
            return true;
    if (qa_console_find(c->options.console, NULL, name))
        return true;
    if (!qa_input_reserve((void **)&c->installed, &c->installed_capacity, c->installed_count + 1,
                          sizeof(*c->installed), error))
        return false;
    char *copy = fold_copy(name, false, error);
    if (!copy)
        return false;
    if (!qa_console_register(c->options.console, name, "Client module command", c->options.owner,
                             false, installed_dispatch, c, error)) {
        free(copy);
        return false;
    }
    c->installed[c->installed_count++] = (struct installed_command){copy};
    return true;
}
static void remove_unused(qa_input_client_commands *c, const char *name) {
    for (size_t i = 0; i < c->count; ++i)
        if (strcmp(c->claims[i].name, name) == 0)
            return;
    for (size_t i = 0; i < c->installed_count; ++i)
        if (strcmp(c->installed[i].name, name) == 0) {
            qa_console_unregister(c->options.console, name, c->options.owner);
            free(c->installed[i].name);
            memmove(c->installed + i, c->installed + i + 1,
                    (c->installed_count - i - 1) * sizeof(*c->installed));
            --c->installed_count;
            return;
        }
}
qa_input_client_commands *qa_input_client_commands_create(const qa_input_client_options *o,
                                                          qa_error *error) {
    if (!o || !o->console || !o->primary) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid client command owner");
        return NULL;
    }
    qa_input_client_commands *c = calloc(1, sizeof(*c));
    if (!c) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating client command registry");
        return NULL;
    }
    c->options = *o;
    return c;
}
void qa_input_client_commands_destroy(qa_input_client_commands *c) {
    if (!c)
        return;
    for (size_t i = 0; i < c->installed_count; ++i) {
        qa_console_unregister(c->options.console, c->installed[i].name, c->options.owner);
        free(c->installed[i].name);
    }
    for (size_t i = 0; i < c->count; ++i) {
        free(c->claims[i].name);
        free(c->claims[i].label);
    }
    free(c->installed);
    free(c->claims);
    free(c);
}
bool qa_input_client_seats(qa_input_client_commands *c, uint64_t session, unsigned mask,
                           qa_error *error) {
    if (!c || mask > 15) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid client command seat mask");
        return false;
    }
    for (size_t i = 0; i < c->count;) {
        if (session == c->session && (mask & (1u << c->claims[i].seat))) {
            ++i;
            continue;
        }
        struct client_claim old = c->claims[i];
        memmove(c->claims + i, c->claims + i + 1, (c->count - i - 1) * sizeof(*c->claims));
        --c->count;
        remove_unused(c, old.name);
        free(old.name);
        free(old.label);
    }
    c->session = session;
    c->seats = mask;
    return true;
}
bool qa_input_client_claim(qa_input_client_commands *c, uint64_t instance, uint32_t seat,
                           const char *name, qa_input_client_handler handler, void *user,
                           const char *label, qa_error *error) {
    if (!c || !instance || seat >= 4 || !(c->seats & (1u << seat)) || !name || !*name ||
        strpbrk(name, " \t\r\n;\"")) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid client command claim");
        return false;
    }
    for (size_t i = 0; i < c->count; ++i)
        if (c->claims[i].instance == instance) {
            if (c->claims[i].seat != seat || c->claims[i].handler != handler ||
                c->claims[i].user != user) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                             "Client command instance changed ownership");
                return false;
            }
            if (qa_input_ascii_equal(c->claims[i].name, name))
                return true;
        }
    if (!qa_input_reserve((void **)&c->claims, &c->capacity, c->count + 1, sizeof(*c->claims),
                          error))
        return false;
    struct client_claim claim = {
        .instance = instance, .seat = seat, .handler = handler, .user = user};
    claim.name = fold_copy(name, true, error);
    if (!claim.name)
        return false;
    claim.label = fold_copy(label ? label : "client module", false, error);
    if (!claim.label || !install(c, claim.name, error)) {
        free(claim.name);
        free(claim.label);
        return false;
    }
    c->claims[c->count++] = claim;
    return true;
}
bool qa_input_client_unclaim(qa_input_client_commands *c, uint64_t instance, const char *name) {
    for (size_t i = 0; i < c->count; ++i)
        if (c->claims[i].instance == instance && qa_input_ascii_equal(c->claims[i].name, name)) {
            struct client_claim old = c->claims[i];
            memmove(c->claims + i, c->claims + i + 1, (c->count - i - 1) * sizeof(*c->claims));
            --c->count;
            remove_unused(c, old.name);
            free(old.name);
            free(old.label);
            return true;
        }
    return false;
}
void qa_input_client_retire(qa_input_client_commands *c, uint64_t instance) {
    for (size_t i = 0; i < c->count;)
        if (c->claims[i].instance == instance)
            qa_input_client_unclaim(c, instance, c->claims[i].name);
        else
            ++i;
}
bool qa_input_client_activate(qa_input_client_commands *c, bool active, qa_error *error) {
    if (c->active == active)
        return true;
    c->active = active;
    if (active) {
        for (size_t i = 0; i < c->count; ++i)
            if (!install(c, c->claims[i].name, error)) {
                qa_input_client_activate(c, false, NULL);
                return false;
            }
    } else {
        for (size_t i = 0; i < c->installed_count; ++i) {
            qa_console_unregister(c->options.console, c->installed[i].name, c->options.owner);
            free(c->installed[i].name);
        }
        c->installed_count = 0;
    }
    return true;
}
qa_command_result qa_input_client_dispatch(qa_input_client_commands *c,
                                           const qa_command_invocation *cmd, uint64_t producer,
                                           qa_error *error) {
    if (!c->active || !cmd->argc || cmd->context.session != c->session)
        return QA_COMMAND_UNHANDLED;
    uint32_t seat = UINT32_MAX;
    if (cmd->context.origin == QA_COMMAND_SEAT)
        seat = cmd->context.seat;
    else if (cmd->context.origin == QA_COMMAND_LOCAL) {
        for (unsigned i = 0; i < 4; ++i)
            if (c->seats & (1u << i)) {
                bool claimed = false;
                for (size_t j = 0; j < c->count; ++j)
                    if (c->claims[j].seat == i) {
                        claimed = true;
                        break;
                    }
                if (claimed) {
                    seat = i;
                    break;
                }
            }
    }
    if (seat >= 4 || !(c->seats & (1u << seat)))
        return QA_COMMAND_UNHANDLED;
    struct client_claim *match = NULL;
    size_t count = 0;
    bool primary = false;
    for (size_t i = 0; i < c->count; ++i) {
        struct client_claim *claim = &c->claims[i];
        if (claim->seat != seat || !qa_input_ascii_equal(claim->name, cmd->argv[0]))
            continue;
        if (producer) {
            if (claim->instance == producer && claim->handler) {
                match = claim;
                count = 1;
                break;
            }
            continue;
        }
        match = claim;
        ++count;
        primary |= claim->handler == NULL;
    }
    if (!count)
        return QA_COMMAND_UNHANDLED;
    if (!producer && primary)
        return c->options.primary(c->options.user, cmd, seat, error) ? QA_COMMAND_HANDLED
                                                                     : QA_COMMAND_FAILED;
    if (count > 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Ambiguous component client command %s (%zu claims)", cmd->argv[0], count);
        return QA_COMMAND_FAILED;
    }
    /* Copy callback ownership before calling: a module may retire itself. */
    qa_input_client_handler handler = match->handler;
    void *user = match->user;
    return handler(user, cmd, seat, error) ? QA_COMMAND_HANDLED : QA_COMMAND_FAILED;
}
