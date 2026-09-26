/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cvar {
    qa_cvar_view view;
    qa_cvar_binding binding;
    bool bound;
    struct cvar *next;
} cvar;

struct qa_cvars {
    qa_cvar_options options;
    cvar *first;
    size_t count;
    size_t next_handle;
    uint32_t changed_flags;
    bool userinfo_modified;
    bool server_active;
    bool high_characters;
    bool cheats;
};

static const uint32_t q2_no_archive = QA_Q2_CVAR_NOSET | QA_Q2_CVAR_CHEAT |
    QA_Q2_CVAR_PRIVATE | QA_Q2_CVAR_READONLY | QA_Q2_CVAR_NO_ARCHIVE;

static bool name_equal(const qa_cvars *registry, const char *a, const char *b)
{
    return registry->options.dialect == QA_CONSOLE_Q3 ? qac_equal(a, b) : strcmp(a, b) == 0;
}

static cvar *find_variable(const qa_cvars *registry, const char *name)
{
    if (registry == NULL || name == NULL) return NULL;
    for (cvar *entry = registry->first; entry != NULL; entry = entry->next)
        if (name_equal(registry, name, entry->view.name)) return entry;
    return NULL;
}

static void print_message(const qa_cvars *registry, const char *name, const char *message)
{
    if (registry->options.print == NULL) return;
    if (name != NULL) registry->options.print(registry->options.user, name);
    registry->options.print(registry->options.user, message);
}

static bool valid_info(const char *text)
{
    return strpbrk(text, "\\\";") == NULL;
}

static bool valid_name(const char *text)
{
    if (*text == '\0') return false;
    for (; *text != '\0'; ++text)
        if ((unsigned char)*text <= 32 || *text == '"' || *text == ';') return false;
    return true;
}

static const char *source_name(const qa_cvars *registry, const char *name)
{
    return registry->options.dialect == QA_CONSOLE_Q3 && !valid_info(name) ? "BADNAME" : name;
}

static void free_variable(cvar *entry)
{
    free((char *)entry->view.name);
    free((char *)entry->view.value);
    free((char *)entry->view.reset_value);
    free((char *)entry->view.latched_value);
    free((char *)entry->view.description);
    free(entry);
}

static void numbers(qa_cvars *registry, cvar *entry)
{
    entry->view.number = qac_number(entry->view.value, registry->options.dialect);
    entry->view.integer = qac_integer(entry->view.value);
}

static bool replace_text(const char **target, const char *value, qa_error *error)
{
    char *copy = qac_copy(value, error);
    if (copy == NULL) return false;
    free((char *)*target);
    *target = copy;
    return true;
}

static void effect(qa_cvars *registry, qa_cvar_effect_kind kind, const cvar *entry)
{
    if (registry->options.effect != NULL)
        registry->options.effect(registry->options.user, kind, &entry->view);
}

static void propagate(qa_cvars *registry, const cvar *entry, bool changed)
{
    if (registry->options.dialect == QA_CONSOLE_Q1 && changed && registry->server_active &&
        (entry->view.flags & QA_CVAR_SERVERINFO) != 0)
        effect(registry, QA_CVAR_EFFECT_BROADCAST, entry);
    else if (registry->options.dialect == QA_CONSOLE_QW) {
        if ((entry->view.flags & QA_CVAR_USERINFO) != 0) effect(registry, QA_CVAR_EFFECT_USERINFO, entry);
        if ((entry->view.flags & QA_CVAR_SERVERINFO) != 0) effect(registry, QA_CVAR_EFFECT_SERVERINFO, entry);
    }
}

static bool apply_value(qa_cvars *registry, cvar *entry, const char *value,
                        bool mark, qa_error *error)
{
    if (!replace_text(&entry->view.value, value, error)) return false;
    numbers(registry, entry);
    if (mark) {
        entry->view.modified = true;
        ++entry->view.modification_count;
    }
    if (entry->bound && entry->binding.changed != NULL)
        entry->binding.changed(entry->binding.user, entry->view.value);
    return true;
}

static bool cheats_allowed(const qa_cvars *registry)
{
    if (registry->options.cheats_allowed != NULL)
        return registry->options.cheats_allowed(registry->options.user);
    const cvar *cheats = find_variable(registry, "sv_cheats");
    return cheats == NULL ? registry->cheats : cheats->view.integer != 0;
}

qa_cvars *qa_cvars_create(const qa_cvar_options *options, qa_error *error)
{
    if (options == NULL || !qac_dialect_valid(options->dialect)) {
        qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar registry options");
        return NULL;
    }
    qa_cvars *registry = calloc(1, sizeof(*registry));
    if (registry == NULL) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating cvar registry");
        return NULL;
    }
    registry->options = *options;
    registry->cheats = true;
    return registry;
}

void qa_cvars_destroy(qa_cvars *registry)
{
    if (registry == NULL) return;
    cvar *entry = registry->first;
    while (entry != NULL) {
        cvar *next = entry->next;
        free_variable(entry);
        entry = next;
    }
    free(registry);
}

qa_console_dialect qa_cvars_dialect(const qa_cvars *registry)
{
    return registry->options.dialect;
}

const qa_cvar_view *qa_cvars_find(const qa_cvars *registry, const char *name)
{
    const cvar *entry = find_variable(registry, name);
    return entry == NULL ? NULL : &entry->view;
}

const qa_cvar_view *qa_cvars_at(const qa_cvars *registry, size_t ordinal)
{
    if (registry == NULL) return NULL;
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next)
        if (ordinal-- == 0) return &entry->view;
    return NULL;
}

const qa_cvar_view *qa_cvars_handle(const qa_cvars *registry, size_t handle)
{
    if (registry == NULL) return NULL;
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next)
        if (entry->view.handle == handle) return &entry->view;
    return NULL;
}

size_t qa_cvars_count(const qa_cvars *registry)
{
    return registry == NULL ? 0 : registry->count;
}

size_t qa_cvars_handle_count(const qa_cvars *registry)
{
    return registry == NULL ? 0 : registry->next_handle;
}

bool qa_cvars_bind(qa_cvars *registry, const char *name, const qa_cvar_binding *binding,
                    qa_error *error)
{
    cvar *entry = find_variable(registry, name);
    if (entry == NULL || binding == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "binding requires a registered cvar");
    if (entry->bound) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar already has a value binding");
    if (binding->validate != NULL &&
        (!binding->validate(binding->user, entry->view.value, error) ||
         !binding->validate(binding->user, entry->view.reset_value, error) ||
         (entry->view.latched_value != NULL && !binding->validate(binding->user, entry->view.latched_value, error)))) return false;
    entry->binding = *binding;
    entry->bound = true;
    return true;
}

void qa_cvars_unbind(qa_cvars *registry, const char *name, uint64_t owner)
{
    cvar *entry = find_variable(registry, name);
    if (entry != NULL && entry->bound && entry->binding.owner == owner) {
        entry->binding = (qa_cvar_binding){0};
        entry->bound = false;
    }
}

bool qa_cvars_register(qa_cvars *registry, const char *name, const char *default_value,
                        uint32_t flags, uint64_t owner, const char *description,
                        qa_error *error)
{
    if (registry == NULL || name == NULL || default_value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar registration requires name and default");
    name = source_name(registry, name);
    if (!valid_name(name)) return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar name");
    const qa_console_dialect dialect = registry->options.dialect;
    bool q2 = qac_q2(dialect);
    if (q2 && (flags & (QA_CVAR_USERINFO | QA_CVAR_SERVERINFO)) != 0 &&
        (!valid_info(name) || !valid_info(default_value)))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid info cvar name or default");
    cvar *entry = find_variable(registry, name);
    if (entry != NULL) {
        if (entry->bound && entry->binding.validate != NULL &&
            !entry->binding.validate(entry->binding.user, default_value, error)) return false;
        if (qac_q1(dialect)) {
            if (!entry->view.console_created) {
                print_message(registry, name, " is already registered\n");
                return true;
            }
            if (!replace_text(&entry->view.reset_value, default_value, error)) return false;
            entry->view.console_created = false;
            entry->view.flags |= flags;
            entry->view.owner = owner;
            propagate(registry, entry, true);
            return true;
        }
        uint32_t created = q2 ? QA_Q2_CVAR_CUSTOM : QA_CVAR_USER_CREATED;
        bool promoted = q2 && (entry->view.flags & created) != 0 && (flags & created) == 0;
        if (((entry->view.flags & created) != 0 && (flags & created) == 0 &&
             (q2 || default_value[0] != '\0')) || entry->view.reset_value[0] == '\0') {
            if (!replace_text(&entry->view.reset_value, default_value, error)) return false;
            entry->view.flags &= ~created;
            entry->view.console_created = false;
            entry->view.owner = owner;
            if (!q2) registry->changed_flags |= flags;
        }
        if (promoted && (((flags & (QA_Q2_CVAR_READONLY | QA_Q2_CVAR_NOSET)) != 0) ||
                   ((flags & QA_Q2_CVAR_CHEAT) != 0 && !cheats_allowed(registry)) ||
                   ((flags & 6u) != 0 && !valid_info(entry->view.value)))) {
            if (!qa_cvars_set(registry, name, default_value, true, error)) return false;
        }
        entry->view.flags |= flags;
        if (q2 && (entry->view.flags & q2_no_archive) != 0) entry->view.flags &= ~UINT32_C(1);
        if (!q2 && entry->view.latched_value != NULL)
            return qa_cvars_apply_latched(registry, name, error);
        return true;
    }
    if (qac_q1(dialect) && registry->options.command_exists != NULL &&
        registry->options.command_exists(registry->options.user, name))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar name is already a command");
    if (registry->next_handle == SIZE_MAX)
        return qac_fail(error, QA_ERROR_MEMORY, "cvar handles exhausted");
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) return qac_fail(error, QA_ERROR_MEMORY, "allocating cvar");
    entry->view.name = qac_copy(name, error);
    entry->view.value = qac_copy(default_value, error);
    entry->view.reset_value = qac_copy(default_value, error);
    entry->view.description = qac_copy(description == NULL ? "" : description, error);
    if (entry->view.name == NULL || entry->view.value == NULL ||
        entry->view.reset_value == NULL || entry->view.description == NULL) {
        free_variable(entry);
        return false;
    }
    entry->view.flags = q2 && (flags & q2_no_archive) != 0 ? flags & ~UINT32_C(1) : flags;
    entry->view.owner = owner;
    entry->view.modified = true;
    entry->view.modification_count = 1;
    entry->view.handle = registry->next_handle++;
    numbers(registry, entry);
    entry->next = registry->first;
    registry->first = entry;
    ++registry->count;
    if (dialect == QA_CONSOLE_QW) propagate(registry, entry, true);
    return true;
}

bool qa_cvars_set(qa_cvars *registry, const char *name, const char *value,
                   bool force, qa_error *error)
{
    if (registry == NULL || name == NULL || value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar set requires name and value");
    name = source_name(registry, name);
    cvar *entry = find_variable(registry, name);
    const qa_console_dialect dialect = registry->options.dialect;
    bool q2 = qac_q2(dialect);
    if (entry == NULL) {
        if (qac_q1(dialect)) return qac_fail(error, QA_ERROR_NOT_FOUND, "Q1 cvar is not registered");
        uint32_t flags = dialect == QA_CONSOLE_Q3 && !force ? QA_CVAR_USER_CREATED : 0;
        return qa_cvars_register(registry, name, value, flags, 0, NULL, error);
    }
    bool changed = strcmp(entry->view.value, value) != 0;
    if (entry->bound && entry->binding.validate != NULL &&
        !entry->binding.validate(entry->binding.user, value, error)) return false;
    if (qac_q1(dialect)) {
        if (!apply_value(registry, entry, value, false, error)) return false;
        propagate(registry, entry, changed);
        return true;
    }
    if (q2 && (entry->view.flags & 6u) != 0 && !valid_info(value))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid info cvar value");
    if (!q2 && !changed) {
        if (entry->bound && entry->binding.changed != NULL)
            entry->binding.changed(entry->binding.user, value);
        return true;
    }
    if (!q2) registry->changed_flags |= entry->view.flags;
    if (!force) {
        uint32_t readonly = q2 ? QA_Q2_CVAR_READONLY : QA_CVAR_READONLY;
        uint32_t init = q2 ? QA_Q2_CVAR_NOSET : QA_CVAR_INIT;
        uint32_t cheat = q2 ? QA_Q2_CVAR_CHEAT : QA_CVAR_CHEAT;
        uint32_t latch = q2 ? QA_Q2_CVAR_LATCH : QA_CVAR_LATCH;
        if ((entry->view.flags & readonly) != 0) {
            print_message(registry, name, " is read only.\n");
            return true;
        }
        if (q2 && (entry->view.flags & cheat) != 0 && !cheats_allowed(registry)) {
            print_message(registry, name, " is cheat protected.\n");
            return true;
        }
        if ((entry->view.flags & init) != 0) {
            print_message(registry, name, " is write protected.\n");
            return true;
        }
        if ((entry->view.flags & latch) != 0) {
            const char *pending = entry->view.latched_value;
            if (pending != NULL && strcmp(pending, value) == 0) return true;
            if (q2 && pending == NULL && !changed) return true;
            if (q2 && !registry->server_active) {
                const char *previous = entry->view.latched_value;
                entry->view.latched_value = NULL;
                if (!apply_value(registry, entry, value, false, error)) {
                    entry->view.latched_value = previous;
                    return false;
                }
                free((char *)previous);
                if (strcmp(entry->view.name, "game") == 0) effect(registry, QA_CVAR_EFFECT_GAME_DIRECTORY, entry);
                return true;
            }
            if (!replace_text(&entry->view.latched_value, value, error)) return false;
            if (!q2) { entry->view.modified = true; ++entry->view.modification_count; }
            print_message(registry, name, q2 ? " will be changed for next game.\n" : " will be changed upon restarting.\n");
            return true;
        }
        if (!q2 && (entry->view.flags & cheat) != 0 && !cheats_allowed(registry)) {
            print_message(registry, name, " is cheat protected.\n");
            return true;
        }
    }
    /* Detach pending storage until the value has been copied successfully. */
    const char *previous = force ? entry->view.latched_value : NULL;
    if (force) entry->view.latched_value = NULL;
    if (changed && !apply_value(registry, entry, value, true, error)) {
        if (force) entry->view.latched_value = previous;
        return false;
    }
    free((char *)previous);
    if (!changed && entry->bound && entry->binding.changed != NULL)
        entry->binding.changed(entry->binding.user, entry->view.value);
    if (changed && q2 && (entry->view.flags & QA_CVAR_USERINFO) != 0) registry->userinfo_modified = true;
    return true;
}

bool qa_cvars_set_console(qa_cvars *registry, const char *name, const char *value,
                           qa_error *error)
{
    if (registry == NULL || name == NULL || value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid console cvar arguments");
    cvar *entry = find_variable(registry, name);
    if (qac_q2(registry->options.dialect) && entry != NULL && strcmp(entry->view.value, value) == 0) {
        free((char *)entry->view.latched_value);
        entry->view.latched_value = NULL;
        if (entry->bound && entry->binding.changed != NULL)
            entry->binding.changed(entry->binding.user, entry->view.value);
        return true;
    }
    return qa_cvars_set(registry, name, value, false, error);
}

bool qa_cvars_set_number(qa_cvars *registry, const char *name, float value, qa_error *error)
{
    if (registry == NULL || !isfinite(value))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar numeric set requires a finite float");
    char text[64];
    if (!qac_q1(registry->options.dialect) && value >= -2147483648.0f &&
        value < 2147483648.0f && truncf(value) == value)
        (void)snprintf(text, sizeof(text), "%d", (int32_t)value);
    else (void)snprintf(text, sizeof(text), "%f", (double)value);
    if (strlen(text) >= 32) {
        if (qac_q1(registry->options.dialect))
            return qac_fail(error, QA_ERROR_FORMAT, "numeric cvar exceeds source value buffer");
        print_message(registry, NULL, "numeric cvar truncated to source value buffer\n");
        text[31] = '\0';
    }
    return qa_cvars_set(registry, name, text, registry->options.dialect == QA_CONSOLE_Q3, error);
}

bool qa_cvars_full_set(qa_cvars *registry, const char *name, const char *value,
                        uint32_t flags, qa_error *error)
{
    if (registry == NULL || !qac_q2(registry->options.dialect) || name == NULL || value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "full cvar set requires a Q2 registry");
    cvar *entry = find_variable(registry, name);
    if (entry == NULL) return qa_cvars_register(registry, name, value, flags, 0, NULL, error);
    if (entry->bound && entry->binding.validate != NULL &&
        !entry->binding.validate(entry->binding.user, value, error)) return false;
    if (!apply_value(registry, entry, value, true, error)) return false;
    if ((entry->view.flags & QA_CVAR_USERINFO) != 0) registry->userinfo_modified = true;
    entry->view.flags = flags;
    return true;
}

bool qa_cvars_set_flags(qa_cvars *registry, const char *name, const char *value,
                         uint32_t flag, qa_error *error)
{
    if (registry == NULL || name == NULL || value == NULL ||
        (flag != QA_CVAR_ARCHIVE && flag != QA_CVAR_USERINFO && flag != QA_CVAR_SERVERINFO))
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar command flag");
    bool q2 = qac_q2(registry->options.dialect);
    bool q3 = registry->options.dialect == QA_CONSOLE_Q3;
    name = source_name(registry, name);
    if (!q3 && flag != QA_CVAR_ARCHIVE && (!valid_info(name) || !valid_info(value) ||
        (q2 && (strlen(name) >= 64 || strlen(value) >= 64))))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid info cvar name or value");
    cvar *entry = find_variable(registry, name);
    uint32_t old_flags = entry == NULL ? 0 : entry->view.flags;
    if (entry == NULL) {
        uint32_t created = q2 ? QA_Q2_CVAR_CUSTOM : registry->options.dialect == QA_CONSOLE_Q3 ? QA_CVAR_USER_CREATED : 0;
        if (!qa_cvars_register(registry, name, value, flag | created, 0, NULL, error)) return false;
        entry = find_variable(registry, source_name(registry, name));
        entry->view.console_created = true;
    } else {
        if (!qa_cvars_set_console(registry, name, value, error)) return false;
        if (!q3 && flag != QA_CVAR_ARCHIVE && (!valid_info(entry->view.value) || (q2 && strlen(entry->view.value) >= 64)))
            return qac_fail(error, QA_ERROR_FORMAT, "invalid retained info cvar value");
        if (q2 && flag != QA_CVAR_ARCHIVE) entry->view.flags &= ~UINT32_C(6);
        entry->view.flags |= flag;
    }
    if (q2 && (entry->view.flags & q2_no_archive) != 0) entry->view.flags &= ~UINT32_C(1);
    if (flag != QA_CVAR_ARCHIVE) {
        if (q2) registry->userinfo_modified |= ((old_flags | entry->view.flags) & QA_CVAR_USERINFO) != 0;
        else propagate(registry, entry, true);
    }
    return true;
}

bool qa_cvars_stage(qa_cvars *registry, const char *name, const char *value, qa_error *error)
{
    if (registry == NULL || name == NULL || value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid staged cvar arguments");
    cvar *entry = find_variable(registry, name);
    if (entry == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "cannot stage unregistered cvar");
    if (entry->bound && entry->binding.validate != NULL &&
        !entry->binding.validate(entry->binding.user, value, error)) return false;
    qa_console_dialect dialect = registry->options.dialect;
    if ((dialect == QA_CONSOLE_Q3 && (entry->view.flags & (QA_CVAR_READONLY | QA_CVAR_INIT)) != 0) ||
        (qac_q2(dialect) && (entry->view.flags & QA_Q2_CVAR_NOSET) != 0))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cannot stage protected cvar");
    if (qac_q2(dialect) && (entry->view.flags & 6u) != 0 && !valid_info(value))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid staged info cvar");
    bool same = strcmp(value, entry->view.value) == 0;
    if ((same && entry->view.latched_value == NULL) ||
        (!same && entry->view.latched_value != NULL && strcmp(value, entry->view.latched_value) == 0)) return true;
    if (same) { free((char *)entry->view.latched_value); entry->view.latched_value = NULL; }
    else if (!replace_text(&entry->view.latched_value, value, error)) return false;
    entry->view.modified = true;
    ++entry->view.modification_count;
    if (dialect == QA_CONSOLE_Q3) registry->changed_flags |= entry->view.flags;
    return true;
}

bool qa_cvars_apply_latched(qa_cvars *registry, const char *name, qa_error *error)
{
    if (registry == NULL) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar registry is NULL");
    for (cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        if ((name != NULL && !name_equal(registry, name, entry->view.name)) || entry->view.latched_value == NULL) continue;
        const char *pending = entry->view.latched_value;
        entry->view.latched_value = NULL;
        if (!apply_value(registry, entry, pending, registry->options.dialect == QA_CONSOLE_Q3, error)) {
            entry->view.latched_value = pending;
            return false;
        }
        free((char *)pending);
        if (qac_q2(registry->options.dialect) && strcmp(entry->view.name, "game") == 0)
            effect(registry, QA_CVAR_EFFECT_GAME_DIRECTORY, entry);
    }
    return true;
}

bool qa_cvars_reset(qa_cvars *registry, const char *name, bool force, qa_error *error)
{
    cvar *entry = find_variable(registry, name);
    if (entry == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "cannot reset unregistered cvar");
    return qa_cvars_set(registry, name, entry->view.reset_value, force, error);
}

bool qa_cvars_restart(qa_cvars *registry, qa_error *error)
{
    if (registry == NULL || registry->options.dialect != QA_CONSOLE_Q3)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar restart requires a Q3 registry");
    cvar **link = &registry->first;
    while (*link != NULL) {
        cvar *entry = *link;
        if ((entry->view.flags & (QA_CVAR_READONLY | QA_CVAR_INIT | QA_CVAR_NO_RESTART)) != 0) {
            link = &entry->next;
        } else if ((entry->view.flags & QA_CVAR_USER_CREATED) != 0) {
            *link = entry->next;
            free_variable(entry);
            --registry->count;
        } else {
            if (!qa_cvars_set(registry, entry->view.name, entry->view.reset_value, true, error)) return false;
            link = &entry->next;
        }
    }
    return true;
}

bool qa_cvars_set_cheats(qa_cvars *registry, bool allowed, qa_error *error)
{
    if (registry == NULL) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar registry is NULL");
    registry->cheats = allowed;
    if (allowed || registry->options.dialect != QA_CONSOLE_Q3) return true;
    for (cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        if ((entry->view.flags & QA_CVAR_CHEAT) == 0) continue;
        free((char *)entry->view.latched_value);
        entry->view.latched_value = NULL;
        if (!qa_cvars_set(registry, entry->view.name, entry->view.reset_value, true, error)) return false;
    }
    return true;
}

void qa_cvars_set_server_active(qa_cvars *registry, bool active) { registry->server_active = active; }
void qa_cvars_set_high_characters(qa_cvars *registry, bool enabled) { registry->high_characters = enabled; }

void qa_cvars_remove_owner(qa_cvars *registry, uint64_t owner)
{
    if (registry == NULL) return;
    cvar **link = &registry->first;
    while (*link != NULL) {
        cvar *entry = *link;
        if (entry->bound && entry->binding.owner == owner) {
            entry->binding = (qa_cvar_binding){0};
            entry->bound = false;
        }
        if (entry->view.owner != owner) { link = &entry->next; continue; }
        *link = entry->next;
        registry->changed_flags |= entry->view.flags;
        if ((entry->view.flags & QA_CVAR_USERINFO) != 0) registry->userinfo_modified = true;
        --registry->count;
        free_variable(entry);
    }
}

uint32_t qa_cvars_take_modified_flags(qa_cvars *registry)
{
    uint32_t flags = registry->changed_flags;
    registry->changed_flags = 0;
    return flags;
}

void qa_cvars_clear_modified(qa_cvars *registry, const char *name)
{
    cvar *entry = find_variable(registry, name);
    if (entry != NULL) entry->view.modified = false;
}

bool qa_cvars_take_userinfo_modified(qa_cvars *registry)
{
    bool modified = registry->userinfo_modified;
    registry->userinfo_modified = false;
    return modified;
}

bool qa_cvars_info(const qa_cvars *registry, uint32_t flags, size_t maximum_length,
                    qa_buffer *out, qa_error *error)
{
    if (registry == NULL || out == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar info arguments");
    qa_console_dialect dialect = registry->options.dialect;
    if (maximum_length == 0) maximum_length = dialect == QA_CONSOLE_Q3 ? 1024 : 512;
    qac_text result = {0};
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        const qa_cvar_view *value = &entry->view;
        if ((value->flags & flags) == 0 || (qac_q2(dialect) && (value->flags & QA_Q2_CVAR_PRIVATE) != 0)) continue;
        size_t key_length = strlen(value->name);
        size_t value_length = strlen(value->value);
        if (*value->value == '\0' || strchr(value->name, '\\') != NULL || strchr(value->value, '\\') != NULL ||
            strchr(value->name, '"') != NULL || strchr(value->value, '"') != NULL ||
            (dialect != QA_CONSOLE_QW && strchr(value->name, ';') != NULL) ||
            (dialect == QA_CONSOLE_Q3 && strchr(value->value, ';') != NULL) ||
            (dialect == QA_CONSOLE_QW && *value->name == '*') ||
            (dialect != QA_CONSOLE_Q3 && (key_length >= 64 || value_length >= 64))) continue;
        qac_text pair = {0};
        if (!qac_text_add(&pair, "\\", 1, error) || !qac_text_string(&pair, value->name, error) ||
            !qac_text_add(&pair, "\\", 1, error) || !qac_text_string(&pair, value->value, error)) {
            free(pair.data);
            free(result.data);
            return false;
        }
        if (dialect != QA_CONSOLE_Q3) {
            size_t count = 0;
            bool userinfo = (flags & QA_CVAR_USERINFO) != 0;
            bool strip = dialect != QA_CONSOLE_QW || (userinfo ? !qac_equal(value->name, "name") : !registry->high_characters);
            for (size_t i = 0; i < pair.size; ++i) {
                unsigned char c = (unsigned char)pair.data[i];
                if (strip) {
                    c &= 127;
                    if (c < 32 || (dialect != QA_CONSOLE_QW && c == 127)) continue;
                    if (dialect == QA_CONSOLE_QW && userinfo && qac_equal(value->name, "team") && c >= 'A' && c <= 'Z') c += 32;
                }
                if (dialect == QA_CONSOLE_QW && c <= 13) continue;
                pair.data[count++] = (char)c;
            }
            pair.size = count;
            pair.data[count] = '\0';
        }
        if (pair.size >= maximum_length || result.size >= maximum_length - pair.size) {
            print_message(registry, NULL, "Info string length exceeded\n");
            free(pair.data);
            continue;
        }
        if (dialect == QA_CONSOLE_Q3 && maximum_length != 8192) {
            if (!qac_text_add(&pair, result.data, result.size, error)) {
                free(pair.data); free(result.data); return false;
            }
            free(result.data);
            result = pair;
        } else {
            bool ok = qac_text_add(&result, pair.data, pair.size, error);
            free(pair.data);
            if (!ok) { free(result.data); return false; }
        }
    }
    if (!qac_text_finish(&result, out, error)) { free(result.data); return false; }
    return true;
}

bool qa_cvars_config(const qa_cvars *registry, qa_buffer *out, qa_error *error)
{
    if (registry == NULL || out == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar config arguments");
    qac_text result = {0};
    qa_console_dialect dialect = registry->options.dialect;
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        const qa_cvar_view *variable = &entry->view;
        if ((variable->flags & QA_CVAR_ARCHIVE) == 0 ||
            (qac_q2(dialect) && (variable->flags & q2_no_archive) != 0) ||
            (dialect == QA_CONSOLE_Q3 && qac_equal(variable->name, "cl_cdkey"))) continue;
        const char *value = dialect == QA_CONSOLE_Q3 && variable->latched_value != NULL ? variable->latched_value : variable->value;
        if (strpbrk(value, "\"\r\n") != NULL) {
            free(result.data);
            return qac_fail(error, QA_ERROR_FORMAT, "cvar value cannot be represented by source config quoting");
        }
        const char *prefix = dialect == QA_CONSOLE_Q3 || variable->console_created ||
            (qac_q2(dialect) && (variable->flags & QA_Q2_CVAR_CUSTOM) != 0) ? "seta " : qac_q2(dialect) ? "set " : "";
        if (!qac_text_string(&result, prefix, error) || !qac_text_string(&result, variable->name, error) ||
            !qac_text_add(&result, " \"", 2, error) || !qac_text_string(&result, value, error) ||
            !qac_text_add(&result, "\"\n", 2, error)) {
            free(result.data);
            return false;
        }
    }
    if (!qac_text_finish(&result, out, error)) { free(result.data); return false; }
    return true;
}
