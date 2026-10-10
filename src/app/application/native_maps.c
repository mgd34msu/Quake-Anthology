#include "internal.h"
#include "native_maps.h"
#include "native_q3_console.h"
#include "native_q3_clients.h"
#include "startup_flow.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct map_registry {
    qa_cvars *cvars;
    bool callback_cheats, fallback_cheats;
    struct map_registry *next;
} map_registry;
typedef struct map_variable {
    qa_cvars *registry;
    char *name, *value;
    uint32_t flags;
    bool present;
    struct map_variable *next;
} map_variable;
typedef struct map_script {
    qa_cvars *registry;
    char *name, *text;
    size_t length, offset;
    uint64_t overlay_revision;
    struct map_script *next;
} map_script;
typedef struct map_parser {
    qa_application *application;
    application_provider *source;
    qa_console *console;
    application_next_map_plan plan;
    map_registry *registries;
    map_variable *variables;
    map_script *scripts;
    size_t command_limit, buffer_limit, pending, assignment_capacity;
    uint64_t overlay_revision;
} map_parser;

static char *copy_text(const char *text, qa_error *error) {
    size_t length = strlen(text);
    if (length == SIZE_MAX) {
        application_fail(error, QA_ERROR_MEMORY, "map command exceeds address space");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        application_fail(error, QA_ERROR_MEMORY, "cannot retain map command text");
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}
static bool equal_name(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
void application_next_map_plan_free(application_next_map_plan *plan) {
    if (!plan) return;
    free(plan->map);
    for (size_t i = 0; i < plan->assignment_count; ++i) {
        free(plan->assignments[i].name);
        free(plan->assignments[i].value);
    }
    free(plan->assignments);
    *plan = (application_next_map_plan){0};
}
bool application_source_map_path(application_provider *source, const char *name,
                                  char **out, qa_error *error) {
    if (!out || !source || !source->product || !source->launch ||
        !source->launch->content || !name || !*name)
        return application_fail(error, QA_ERROR_ARGUMENT, "map has no actual source content");
    *out = NULL;
    char *normalized = qa_vfs_normalize_path(name, error);
    if (!normalized) return false;
    size_t length = strlen(normalized);
    bool prefix = !strncmp(normalized, "maps/", 5);
    bool suffix = length >= 4 && !strcmp(normalized + length - 4, ".bsp");
    if (length > SIZE_MAX - 10) {
        free(normalized);
        return application_fail(error, QA_ERROR_MEMORY, "map path exceeds address space");
    }
    char *path = malloc(length + (prefix ? 0 : 5) + (suffix ? 0 : 4) + 1);
    if (!path) {
        free(normalized);
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain source map path");
    }
    size_t used = 0;
    if (!prefix) { memcpy(path, "maps/", 5); used = 5; }
    memcpy(path + used, normalized, length);
    used += length;
    if (!suffix) { memcpy(path + used, ".bsp", 4); used += 4; }
    path[used] = 0;
    free(normalized);
    qa_resource *resource = NULL;
    bool allowed = qa_vfs_acquire(source->launch->content, path, &resource, NULL, error);
    qa_bsp_view map;
    if (allowed) allowed = qa_bsp_open(qa_resource_bytes(resource), &map, error);
    if (allowed && !((source->product->family == QA_GAME_Q1 && map.family == QA_BSP_Q1) ||
                     (source->product->family == QA_GAME_Q2 && map.family == QA_BSP_Q2) ||
                     (source->product->family == QA_GAME_Q3 && map.family == QA_BSP_Q3)))
        allowed = application_fail(error, QA_ERROR_FORMAT, "map BSP does not match its source product");
    if (allowed) allowed = qa_bsp_validate(&map, error);
    qa_resource_release(resource);
    if (!allowed) { free(path); return false; }
    *out = path;
    return true;
}
static void parser_free(map_parser *parser) {
    while (parser->scripts) {
        map_script *script = parser->scripts;
        parser->scripts = script->next;
        free(script->name); free(script->text); free(script);
    }
    while (parser->variables) {
        map_variable *variable = parser->variables;
        parser->variables = variable->next;
        free(variable->name); free(variable->value); free(variable);
    }
    while (parser->registries) {
        map_registry *registry = parser->registries;
        parser->registries = registry->next;
        free(registry);
    }
    application_next_map_plan_free(&parser->plan);
}
static bool source_console(map_parser *parser, qa_mode_id mode, qa_error *error) {
    qa_application *app = parser->application;
    application_provider *source = application_native_q3_mode_source_provider(app, mode);
    if (!source) return application_fail(error, QA_ERROR_NOT_FOUND, "nextmap source has retired");
    qa_application_startup_source selected = {0};
    bool has_selected = false;
    for (size_t i = 0;; ++i) {
        qa_application_startup_source candidate;
        bool present;
        if (!application_provider_startup_source_at(source, i, &candidate, &present, error)) return false;
        if (!present) break;
        if (candidate.scope.kind != QA_APPLICATION_CONSOLE_Q3_GAME) continue;
        if (has_selected)
            return application_fail(error, QA_ERROR_ARGUMENT, "nextmap has ambiguous Source GAME views");
        selected = candidate;
        has_selected = true;
    }
    qa_command_context context = selected.command;
    if (!has_selected || !selected.console || selected.scope.provider != source->owner ||
        context.owner != source->owner || context.dialect != QA_RULESET_Q3)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "nextmap needs an actual Q3 source console");
    context.origin = QA_COMMAND_SERVER;
    context.client = 0;
    context.actor = (qa_actor_id){0};
    context.direct = false;
    context.console_text = false;
    context.script = NULL;
    if (!qa_application_capture_command_context(app, &context, &context, error)) return false;
    if (!qa_console_limits(selected.console, &context, &parser->command_limit, &parser->buffer_limit, error))
        return false;
    parser->source = source;
    parser->console = selected.console;
    parser->plan.scope = selected.scope;
    parser->plan.mode = mode;
    parser->plan.owner = source->owner;
    parser->plan.context = context;
    return true;
}
static map_registry *registry_read(map_parser *parser, qa_cvars *cvars, qa_error *error) {
    for (map_registry *r = parser->registries; r; r = r->next)
        if (r->cvars == cvars) return r;
    if (!cvars || qa_cvars_dialect(cvars) != QA_RULESET_Q3) {
        application_fail(error, QA_ERROR_UNSUPPORTED, "nextmap variable has no Q3 registry");
        return NULL;
    }
    map_registry *r = calloc(1, sizeof(*r));
    if (!r) {
        application_fail(error, QA_ERROR_MEMORY, "cannot inspect nextmap registry");
        return NULL;
    }
    if (!qa_cvars_cheats_policy(cvars, &r->callback_cheats, &r->fallback_cheats)) {
        free(r);
        application_fail(error, QA_ERROR_ARGUMENT, "nextmap has no actual cheats policy");
        return NULL;
    }
    r->cvars = cvars;
    r->next = parser->registries;
    parser->registries = r;
    return r;
}
static map_variable *variable_read(map_parser *parser, qa_cvars *registry,
                                     const char *name, bool create, qa_error *error) {
    for (map_variable *v = parser->variables; v; v = v->next)
        if (v->registry == registry && equal_name(v->name, name)) return v;
    const qa_cvar_view *view = qa_cvars_find(registry, name);
    if (!view && !create) {
        application_fail(error, QA_ERROR_NOT_FOUND, "nextmap refers to an absent source variable");
        return NULL;
    }
    map_variable *v = calloc(1, sizeof(*v));
    if (!v) {
        application_fail(error, QA_ERROR_MEMORY, "cannot inspect nextmap variable");
        return NULL;
    }
    v->name = copy_text(view ? view->name : name, error);
    v->value = copy_text(view ? view->value : "", error);
    if (!v->name || !v->value) { free(v->name); free(v->value); free(v); return NULL; }
    v->registry = registry;
    v->flags = view ? view->flags : QA_CVAR_USER_CREATED;
    v->present = view != NULL;
    v->next = parser->variables;
    parser->variables = v;
    return v;
}
static bool push_variable(map_parser *parser, const char *name, qa_error *error) {
    qa_cvars *registry = qa_console_cvar_owner(parser->console, &parser->plan.context, name);
    if (!registry_read(parser, registry, error)) return false;
    map_variable *v = variable_read(parser, registry, name, false, error);
    if (!v || !*v->value) return v ?
        application_fail(error, QA_ERROR_NOT_FOUND, "nextmap source variable is empty") : false;
    size_t length = strlen(v->value);
    if (length > SIZE_MAX - 3 || parser->pending > parser->buffer_limit ||
        length + 2 > parser->buffer_limit - parser->pending)
        return application_fail(error, QA_ERROR_FORMAT, "nextmap exceeds its source command buffer");
    for (map_script *s = parser->scripts; s; s = s->next)
        if (s->overlay_revision == parser->overlay_revision &&
            s->registry == registry && equal_name(s->name, v->name) &&
            s->length == length + 2 && !memcmp(s->text, v->value, length))
            return application_fail(error, QA_ERROR_FORMAT, "nextmap source vstr cycle");
    map_script *script = calloc(1, sizeof(*script));
    if (!script) return application_fail(error, QA_ERROR_MEMORY, "cannot retain nextmap vstr");
    script->name = copy_text(v->name, error);
    script->text = malloc(length + 3);
    if (!script->name || !script->text) {
        free(script->name); free(script->text); free(script);
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain nextmap vstr text");
    }
    memcpy(script->text, v->value, length);
    script->text[length] = '\n'; script->text[length + 1] = '\n'; script->text[length + 2] = 0;
    script->registry = registry;
    script->overlay_revision = parser->overlay_revision;
    script->length = length + 2;
    script->next = parser->scripts;
    parser->scripts = script;
    parser->pending += script->length;
    return true;
}
static bool source_integer_nonzero(const char *text) {
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    if (*text == '-' || *text == '+') ++text;
    while (*text >= '0' && *text <= '9')
        if (*text++ != '0') return true;
    return false;
}
static bool effective_cheats(map_parser *parser, const map_registry *registry,
                               bool *out, qa_error *error) {
    if (registry->callback_cheats) {
        application_fail(error, QA_ERROR_UNSUPPORTED,
            "nextmap protected assignment needs an actual prospective callback cheats policy");
        return false;
    }
    for (map_variable *v = parser->variables; v; v = v->next)
        if (v->registry == registry->cvars && v->present && equal_name(v->name, "sv_cheats")) {
            *out = source_integer_nonzero(v->value);
            return true;
        }
    const qa_cvar_view *view = qa_cvars_find(registry->cvars, "sv_cheats");
    *out = view ? view->integer != 0 : registry->fallback_cheats;
    return true;
}
static bool assignment(map_parser *parser, const qa_command_tokens *tokens,
                          uint32_t flags, qa_error *error) {
    if (tokens->count < 3 || (flags && tokens->count != 3))
        return application_fail(error, QA_ERROR_FORMAT, "invalid nextmap source assignment");
    const char *name = tokens->values[1];
    qa_cvars *registry = qa_console_cvar_owner(parser->console, &parser->plan.context, name);
    map_registry *r = registry_read(parser, registry, error);
    if (!r) return false;
    /* Q3's setter maps forbidden info-name bytes to the actual BADNAME cvar. */
    const char *key = strpbrk(name, "\\\";") ? "BADNAME" : name;
    bool valid = *key != 0;
    for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
        if (*p <= 32) valid = false;
    if (!valid)
        return application_fail(error, QA_ERROR_FORMAT, "invalid nextmap variable name");
    map_variable *v = variable_read(parser, registry, key, true, error);
    if (!v) return false;
    size_t length = 0;
    for (size_t i = 2; i < tokens->count; ++i) {
        size_t part = strlen(tokens->values[i]);
        if (part > SIZE_MAX - length - 1)
            return application_fail(error, QA_ERROR_MEMORY, "nextmap assignment exceeds address space");
        length += part + (i > 2);
    }
    application_map_assignment item = {.set_flags = flags};
    item.name = copy_text(name, error);
    item.value = malloc(length + 1);
    if (!item.name || !item.value) {
        free(item.name); free(item.value);
        return application_fail(error, QA_ERROR_MEMORY, "cannot retain nextmap assignment");
    }
    size_t used = 0;
    for (size_t i = 2; i < tokens->count; ++i) {
        if (i > 2) item.value[used++] = ' ';
        size_t part = strlen(tokens->values[i]);
        memcpy(item.value + used, tokens->values[i], part); used += part;
    }
    item.value[used] = 0;
    size_t count = parser->plan.assignment_count;
    if (count >= SIZE_MAX / sizeof(item)) {
        free(item.name); free(item.value);
        return application_fail(error, QA_ERROR_MEMORY, "nextmap assignments exceed address space");
    }
    if (count == parser->assignment_capacity) {
        size_t limit = SIZE_MAX / sizeof(item);
        size_t capacity = parser->assignment_capacity ? parser->assignment_capacity : 8;
        capacity = capacity > limit / 2 ? limit : capacity * 2;
        application_map_assignment *items = realloc(parser->plan.assignments, capacity * sizeof(item));
        if (!items) {
            free(item.name); free(item.value);
            return application_fail(error, QA_ERROR_MEMORY, "cannot retain nextmap assignments");
        }
        parser->plan.assignments = items;
        parser->assignment_capacity = capacity;
    }
    parser->plan.assignments[count] = item;
    parser->plan.assignment_count = count + 1;
    bool changed = strcmp(v->value, item.value) != 0;
    bool blocked = (v->flags & (QA_CVAR_READONLY | QA_CVAR_INIT | QA_CVAR_LATCH)) != 0;
    if (!blocked && changed && (v->flags & QA_CVAR_CHEAT)) {
        bool allowed;
        if (!effective_cheats(parser, r, &allowed, error)) return false;
        blocked = !allowed;
    }
    bool overlay_changed = !v->present || (!blocked && changed) || (flags & ~v->flags) != 0;
    if (overlay_changed && parser->overlay_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "nextmap overlay identity exhausted");
    if (!blocked) {
        char *value = copy_text(item.value, error);
        if (!value) return false;
        free(v->value); v->value = value;
    }
    v->flags |= flags;
    v->present = true;
    if (overlay_changed) ++parser->overlay_revision;
    return true;
}
static bool parse_command(map_parser *parser, const qa_command_tokens *tokens, qa_error *error) {
    if (!tokens->count) return true;
    const char *command = tokens->values[0];
    if (equal_name(command, "vstr")) {
        if (tokens->count != 2)
            return application_fail(error, QA_ERROR_FORMAT, "invalid nextmap vstr command");
        return push_variable(parser, tokens->values[1], error);
    }
    if (equal_name(command, "set") || equal_name(command, "seta") ||
        equal_name(command, "sets") || equal_name(command, "setu")) {
        uint32_t flag = equal_name(command, "seta") ? QA_CVAR_ARCHIVE
            : equal_name(command, "sets") ? QA_CVAR_SERVERINFO
            : equal_name(command, "setu") ? QA_CVAR_USERINFO : 0;
        return assignment(parser, tokens, flag, error);
    }
    if (!equal_name(command, "map"))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "nextmap contains an unsupported source command");
    if (tokens->count != 2 || parser->plan.map)
        return application_fail(error, QA_ERROR_FORMAT, "nextmap must contain exactly one map destination");
    if (!application_source_map_path(parser->source, tokens->values[1], &parser->plan.map, error))
        return false;
    parser->plan.assignments_before_map = parser->plan.assignment_count;
    return true;
}
static bool map_plan(qa_application *app, qa_mode_id mode, const char *command,
                       application_next_map_plan *out, qa_error *error) {
    if (!app || !out || out->map || out->assignments || out->assignment_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "nextmap plan requires an empty output");
    map_parser parser = {.application = app};
    bool ok = source_console(&parser, mode, error);
    if (ok && command) {
        size_t length = strlen(command);
        map_script *script = calloc(1, sizeof(*script));
        if (!script || length >= parser.buffer_limit) {
            free(script);
            ok = application_fail(error, QA_ERROR_MEMORY, "selected-map source command exceeds its buffer");
        } else {
            script->name = copy_text("", error);
            script->text = malloc(length + 2);
            if (!script->name || !script->text) {
                free(script->name); free(script->text); free(script);
                ok = application_fail(error, QA_ERROR_MEMORY, "cannot retain selected-map source command");
            } else {
                memcpy(script->text, command, length);
                script->text[length] = '\n'; script->text[length + 1] = 0;
                script->length = length + 1;
                parser.scripts = script; parser.pending = script->length;
            }
        }
    } else if (ok) ok = push_variable(&parser, "nextmap", error);
    while (ok && parser.scripts) {
        map_script *script = parser.scripts;
        if (script->offset == script->length) {
            parser.scripts = script->next;
            free(script->name); free(script->text); free(script);
            continue;
        }
        const char *text = script->text + script->offset;
        size_t remaining = script->length - script->offset;
        size_t length = qa_command_separator(text, remaining, parser.plan.context.dialect);
        if (length >= parser.command_limit) length = parser.command_limit - 1;
        size_t consumed = length < remaining ? length + 1 : length;
        char *line = malloc(length + 1);
        if (!line) { ok = application_fail(error, QA_ERROR_MEMORY, "cannot inspect nextmap command"); break; }
        memcpy(line, text, length); line[length] = 0;
        script->offset += consumed;
        parser.pending -= consumed;
        qa_command_tokens tokens = {0};
        ok = qa_command_tokenize(line, parser.plan.context.dialect,
                                 parser.plan.context.console_text, &tokens, NULL, NULL, error);
        free(line);
        if (ok) ok = parse_command(&parser, &tokens, error);
        qa_command_tokens_free(&tokens);
    }
    if (ok && !parser.plan.map)
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "nextmap has no admitted map destination");
    if (ok && !qa_application_command_context_active(app, &parser.plan.context))
        ok = application_fail(error, QA_ERROR_NOT_FOUND, "nextmap source retired during admission");
    if (ok) { *out = parser.plan; parser.plan = (application_next_map_plan){0}; }
    parser_free(&parser);
    return ok;
}
bool application_native_next_map_plan(qa_application *app, qa_mode_id mode,
    application_next_map_plan *out, qa_error *error) {
    return map_plan(app, mode, NULL, out, error);
}
bool application_native_selected_map_plan(qa_application *app, qa_mode_id mode,
    qa_string_id command, application_next_map_plan *out, qa_error *error) {
    qa_bytes bytes = app && app->session ? qa_strings_text(qa_session_strings(app->session), command) : (qa_bytes){0};
    if (!bytes.data || !bytes.size || bytes.size >= 1024 || memchr(bytes.data, 0, bytes.size))
        return application_fail(error, QA_ERROR_ARGUMENT, "selected-map intent lacks its actual source command");
    return map_plan(app, mode, qa_strings_cstr(qa_session_strings(app->session), command), out, error);
}

static const char *config_name(qa_match_intent_kind kind) {
    switch (kind) {
    case QA_MATCH_GAME_TYPE: return "g_gametype";
    case QA_MATCH_WARMUP: return "g_doWarmup";
    case QA_MATCH_TIME_LIMIT: return "timelimit";
    case QA_MATCH_FRAG_LIMIT: return "fraglimit";
    default: return NULL;
    }
}
bool application_native_config_command_allowed(qa_application *app,
    const application_next_map_plan *plan, const char *name, qa_error *error) {
    application_provider *source = app && plan ? application_mode_provider(app, plan->mode) : NULL;
    qa_console *console = NULL;
    qa_command_context actual;
    if (!source || source->kind != APPLICATION_PROVIDER_Q3 || source->owner != plan->owner ||
        plan->scope.kind != QA_APPLICATION_CONSOLE_Q3_GAME || plan->scope.provider != source->owner ||
        plan->scope.seat || plan->context.owner != source->owner || plan->context.actor.registry ||
        plan->context.dialect != QA_RULESET_Q3 || plan->context.origin != QA_COMMAND_SERVER ||
        !application_native_q3_console_at(source, &console, NULL, &actual) ||
        actual.session != plan->context.session || actual.cvar_view != plan->context.cvar_view ||
        !qa_application_command_context_active(app, &plan->context))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "config command has no qualified native source fallback");
    /* This actual source callback routes the retained zero-actor context to
     * qa_q3_game_console_command, whose native player gate returns UNHANDLED.
     * Guest callbacks require their own readonly command-policy capability. */
    if (!name || qa_console_find(console, &plan->context, name))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "config command is overridden by a real source command");
    return true;
}
bool application_native_config_plan(qa_application *app, qa_mode_id mode,
    qa_match_intent_kind kind, qa_string_id command, application_next_map_plan *out, qa_error *error) {
    const char *expected = config_name(kind);
    qa_bytes bytes = app && app->session ? qa_strings_text(qa_session_strings(app->session), command) : (qa_bytes){0};
    if (!expected || !out || out->map || out->assignments || !bytes.data || !bytes.size ||
        bytes.size >= 1024 || memchr(bytes.data, 0, bytes.size))
        return application_fail(error, QA_ERROR_ARGUMENT, "config command has no actual source text");
    map_parser parser = {.application = app};
    bool okay = source_console(&parser, mode, error);
    size_t offset = 0;
    while (okay && offset < bytes.size) {
        const char *text = (const char *)bytes.data + offset;
        size_t length = qa_command_separator(text, bytes.size - offset, parser.plan.context.dialect);
        size_t consumed = length < bytes.size - offset ? length + 1 : length;
        size_t copied = length < parser.command_limit ? length : parser.command_limit - 1;
        char *line = malloc(copied + 1);
        if (!line) { okay = application_fail(error, QA_ERROR_MEMORY, "cannot inspect source config command"); break; }
        memcpy(line, text, copied); line[copied] = 0;
        offset += consumed;
        qa_command_tokens tokens = {0};
        okay = qa_command_tokenize(line, parser.plan.context.dialect, parser.plan.context.console_text, &tokens, NULL, NULL, error);
        free(line);
        if (okay && tokens.count) {
            qa_cvars *cvars = qa_console_cvar_owner(parser.console, &parser.plan.context, tokens.values[0]);
            const qa_cvar_view *variable = qa_cvars_find(cvars, tokens.values[0]);
            if (!application_native_config_command_allowed(app, &parser.plan, tokens.values[0], error))
                okay = false;
            else if ((!parser.plan.assignment_count && !equal_name(tokens.values[0], expected)) ||
                !cvars || qa_cvars_dialect(cvars) != QA_RULESET_Q3 || !variable || tokens.count < 2)
                okay = application_fail(error, QA_ERROR_UNSUPPORTED, "config continuation lacks an actual direct source cvar command");
            else {
                application_map_assignment value = {.name = copy_text(tokens.values[0], error),
                    .value = copy_text(tokens.values[1], error)};
                size_t count = parser.plan.assignment_count;
                application_map_assignment *items = value.name && value.value && count < SIZE_MAX / sizeof(*items)
                    ? realloc(parser.plan.assignments, (count + 1) * sizeof(*items)) : NULL;
                if (!items) {
                    free(value.name); free(value.value);
                    okay = application_fail(error, QA_ERROR_MEMORY, "cannot retain source config assignment");
                } else {
                    parser.plan.assignments = items;
                    items[count] = value;
                    parser.plan.assignment_count = count + 1;
                }
            }
        }
        qa_command_tokens_free(&tokens);
    }
    if (okay && !parser.plan.assignment_count)
        okay = application_fail(error, QA_ERROR_FORMAT, "config continuation has no source assignment");
    if (okay && !qa_application_command_context_active(app, &parser.plan.context))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "config source retired during admission");
    if (okay) {
        parser.plan.assignments_before_map = parser.plan.assignment_count;
        *out = parser.plan;
        parser.plan = (application_next_map_plan){0};
    }
    parser_free(&parser);
    return okay;
}
bool application_native_mode_selected_map_command(void *opaque, qa_mode_id mode,
    qa_string_id map, qa_string_id *command, qa_error *error) {
    qa_application *app = opaque;
    if (!app || !app->session || !command)
        return application_fail(error, QA_ERROR_ARGUMENT, "selected-map snapshot needs actual source owners");
    const char *name = qa_strings_cstr(qa_session_strings(app->session), map);
    if (!name || !name[0]) return application_fail(error, QA_ERROR_ARGUMENT, "selected-map snapshot lacks its destination");
    map_parser parser = {.application = app};
    bool okay = source_console(&parser, mode, error);
    if (okay) {
        qa_cvars *cvars = qa_console_cvar_owner(parser.console, &parser.plan.context, "nextmap");
        if (!cvars || qa_cvars_dialect(cvars) != QA_RULESET_Q3)
            okay = application_fail(error, QA_ERROR_UNSUPPORTED, "selected-map snapshot has no actual Q3 cvar scope");
        else {
            const qa_cvar_view *nextmap = qa_cvars_find(cvars, "nextmap");
            size_t name_size = strlen(name);
            size_t next_size = nextmap && nextmap->value[0] ? strlen(nextmap->value) : 0;
            if (next_size > 1023) next_size = 1023;
            if (name_size > SIZE_MAX - next_size - 21) {
                okay = application_fail(error, QA_ERROR_MEMORY, "selected-map command extent overflow");
            } else {
                size_t size = name_size + next_size + 21;
                char *text = malloc(size);
                if (!text) okay = application_fail(error, QA_ERROR_MEMORY,
                                                   "retaining selected-map command snapshot");
                else {
                    if (next_size)
                        snprintf(text, size, "map %s; set nextmap \"%.*s\"", name, (int)next_size, nextmap->value);
                    else snprintf(text, size, "map %s", name);
                    if (!qa_application_command_context_active(app, &parser.plan.context))
                        okay = application_fail(error, QA_ERROR_ARGUMENT, "selected-map source retired during snapshot");
                    else okay = qa_strings_intern_cstr(qa_session_strings(app->session), text, command, error);
                    free(text);
                }
            }
        }
    }
    parser_free(&parser);
    return okay;
}
bool application_native_mode_next_map_allowed(void *opaque, qa_mode_id mode) {
    application_next_map_plan plan = {0};
    bool allowed = application_native_next_map_plan(opaque, mode, &plan, NULL);
    application_next_map_plan_free(&plan);
    return allowed;
}

static int32_t restart_integer(const char *text) {
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t bits = 0, limit = negative ? UINT32_C(2147483648) : UINT32_C(2147483647);
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (unsigned char)*text++ - '0';
        bits = bits > (limit - digit) / 10 ? limit : bits * 10 + digit;
    }
    if (negative) bits = 0 - bits;
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool application_native_restart_plan(qa_application *app, qa_mode_id mode,
    qa_string_id command, application_next_map_plan *out, int32_t *delay_seconds,
    bool *warmup_enabled, qa_error *error) {
    qa_bytes bytes = app && app->session ?
        qa_strings_text(qa_session_strings(app->session), command) : (qa_bytes){0};
    if (!out || out->map || out->assignments || !delay_seconds || !warmup_enabled ||
        !bytes.data || !bytes.size || bytes.size >= 1024 || memchr(bytes.data, 0, bytes.size))
        return application_fail(error, QA_ERROR_ARGUMENT, "restart has no actual source command");
    map_parser parser = {.application = app};
    bool okay = source_console(&parser, mode, error);
    qa_command_tokens tokens = {0};
    if (okay) {
        size_t length = qa_command_separator((const char *)bytes.data, bytes.size, QA_RULESET_Q3);
        size_t copied = length < parser.command_limit ? length : parser.command_limit - 1;
        char *line = malloc(copied + 1);
        if (!line) okay = application_fail(error, QA_ERROR_MEMORY, "cannot inspect source restart command");
        else {
            memcpy(line, bytes.data, copied); line[copied] = 0;
            okay = qa_command_tokenize(line, QA_RULESET_Q3, false, &tokens, NULL, NULL, error);
            free(line);
        }
        /* Enqueued source intents retain one actual engine command. Subsequent
         * script commands belong to their original console buffer. */
        if (okay && length < bytes.size)
            for (size_t i = length + 1; i < bytes.size; ++i)
                if (((const unsigned char *)bytes.data)[i] > 32) {
                    okay = application_fail(error, QA_ERROR_ARGUMENT, "restart continuation contains another command");
                    break;
                }
    }
    if (okay && (!tokens.count || !equal_name(tokens.values[0], "map_restart")))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "restart continuation differs from its source command");
    qa_cvars *cvars = okay ? qa_console_cvar_owner(parser.console, &parser.plan.context, "g_doWarmup") : NULL;
    const qa_cvar_view *warmup = qa_cvars_find(cvars, "g_doWarmup");
    if (okay && (!cvars || qa_cvars_dialect(cvars) != QA_RULESET_Q3 || !warmup))
        okay = application_fail(error, QA_ERROR_NOT_FOUND, "restart has no actual source warmup cvar");
    if (okay && !qa_application_command_context_active(app, &parser.plan.context))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "restart source retired during admission");
    if (okay) {
        *delay_seconds = tokens.count > 1 ? restart_integer(tokens.values[1]) : 5;
        *warmup_enabled = warmup->number != 0;
        *out = parser.plan;
        parser.plan = (application_next_map_plan){0};
    }
    qa_command_tokens_free(&tokens);
    parser_free(&parser);
    return okay;
}
