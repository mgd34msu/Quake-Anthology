#include "internal.h"
#include "qa/console_discovery.h"
#include "qa/json.h"
#include "qa/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct entry_list {
    qa_console_discovery view;
    size_t capacity;
} entry_list;
static unsigned fold(unsigned c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; }
static int compare_names(const char *a, const char *b) {
    while (*a && fold((unsigned char)*a) == fold((unsigned char)*b)) {
        ++a;
        ++b;
    }
    unsigned x = fold((unsigned char)*a), y = fold((unsigned char)*b);
    return x < y ? -1 : x > y;
}
static int compare_entries(const void *a, const void *b) {
    const qa_console_discovery_entry *left = a, *right = b;
    int order = compare_names(left->name, right->name);
    return order ? order : strcmp(left->name, right->name);
}
static bool add(entry_list *list, qa_console_discovery_entry entry, qa_error *error) {
    for (size_t i = 0; i < list->view.count; ++i) {
        qa_console_discovery_entry *current = &list->view.entries[i];
        if ((current->kind != QA_CONSOLE_CVAR && !compare_names(current->name, entry.name)) ||
            (current->kind == QA_CONSOLE_CVAR && entry.kind == QA_CONSOLE_CVAR &&
             !strcmp(current->name, entry.name)))
            return true;
    }
    if (list->view.count == list->capacity) {
        size_t count = list->capacity ? list->capacity * 2 : 64;
        if (count < list->capacity || count > SIZE_MAX / sizeof(entry))
            return qac_fail(error, QA_ERROR_MEMORY, "console discovery overflow");
        void *data = realloc(list->view.entries, count * sizeof(entry));
        if (!data)
            return qac_fail(error, QA_ERROR_MEMORY, "allocating console discovery");
        list->view.entries = data;
        list->capacity = count;
    }
    list->view.entries[list->view.count++] = entry;
    return true;
}
void qa_console_discovery_free(qa_console_discovery *view) {
    if (!view)
        return;
    free(view->entries);
    *view = (qa_console_discovery){0};
}
bool qa_console_discovery_find(qa_console *console, const qa_command_context *context,
                               const char *name, qa_console_discovery_entry *out) {
    qa_command_context captured;
    if (!context || !qa_console_cvar_context(console, context, &captured, NULL)) return false;
    context = &captured;
    const qa_console_entry *command = qa_console_find(console, context, name);
    if (command) {
        *out = (qa_console_discovery_entry){.kind = QA_CONSOLE_COMMAND,
                                            .name = command->name,
                                            .summary = command->description,
                                            .documentation = command->documentation};
        return true;
    }
    if (context->dialect != QA_RULESET_Q3) {
        const qa_console_entry *alias;
        for (size_t i = 0; (alias = qa_console_alias_at(console, context->owner, i)); ++i)
            if (qac_equal(alias->name, name)) {
                *out = (qa_console_discovery_entry){
                    .kind = QA_CONSOLE_ALIAS, .name = alias->name, .value = alias->alias_text};
                return true;
            }
    }
    const qa_cvar_view *found = NULL;
    if (!qa_console_cvar_read(console,context,name,&found,NULL)) return false;
    if (!found)
        for (size_t r = 0;; ++r) {
            qa_cvars *vars = qa_console_visible_cvars(console, context, r);
            if (!vars)
                break;
            for (size_t i = 0;; ++i) {
                const qa_cvar_view *var=NULL;
                if (!qa_console_cvar_snapshot_at(console,context,vars,i,&var,NULL)) return false;
                if (!var) break;
                if (qac_equal(var->name, name) &&
                    qa_console_cvar_owner(console, context, var->name) == vars) {
                    found = var;
                    break;
                }
            }
            if (found)
                break;
        }
    if (!found)
        return false;
    *out = (qa_console_discovery_entry){.kind = QA_CONSOLE_CVAR,
                                        .name = found->name,
                                        .summary = found->description,
                                        .value = found->value,
                                        .reset_value = found->reset_value,
                                        .latched_value = found->latched_value,
                                        .documentation = found->documentation};
    return true;
}
bool qa_console_discover(qa_console *console, const qa_command_context *context,
                         qa_console_discovery *out, qa_error *error) {
    if (!console || !context || !out)
        return qac_fail(error, QA_ERROR_ARGUMENT, "missing console discovery context");
    qa_command_context captured;
    if (!qa_console_cvar_context(console, context, &captured, error)) return false;
    context = &captured;
    entry_list list = {0};
    const qa_console_entry *entry;
    for (size_t i = 0; (entry = qa_console_context_entry_at(console, context, i)); ++i) {
        const qa_console_entry *visible = qa_console_find(console, context, entry->name);
        if (visible != entry)
            continue;
        if (!add(&list,
                 (qa_console_discovery_entry){.kind = QA_CONSOLE_COMMAND,
                                              .name = entry->name,
                                              .summary = entry->description,
                                              .documentation = entry->documentation},
                 error))
            goto fail;
    }
    if (context->dialect != QA_RULESET_Q3) {
        for (size_t i = 0; (entry = qa_console_alias_at(console, context->owner, i)); ++i)
            if (!add(&list,
                     (qa_console_discovery_entry){
                         .kind = QA_CONSOLE_ALIAS, .name = entry->name, .value = entry->alias_text},
                     error))
                goto fail;
    }
    for (size_t registry = 0;; ++registry) {
        qa_cvars *vars = qa_console_visible_cvars(console, context, registry);
        if (!vars)
            break;
        for (size_t i = 0;; ++i) {
            const qa_cvar_view *var=NULL;
            if (!qa_console_cvar_snapshot_at(console,context,vars,i,&var,error)) goto fail;
            if (!var) break;
            if (qa_console_cvar_owner(console, context, var->name) != vars)
                continue;
            if (!add(&list,
                     (qa_console_discovery_entry){.kind = QA_CONSOLE_CVAR,
                                                  .name = var->name,
                                                  .summary = var->description,
                                                  .value = var->value,
                                                  .reset_value = var->reset_value,
                                                  .latched_value = var->latched_value,
                                                  .documentation = var->documentation},
                     error))
                goto fail;
        }
    }
    if (list.view.count > 1)
        qsort(list.view.entries, list.view.count, sizeof(*list.view.entries), compare_entries);
    *out = list.view;
    return true;
fail:
    qa_console_discovery_free(&list.view);
    return false;
}
static bool contains(const char *text, const char *query) {
    if (!*query)
        return true;
    if (!text)
        return false;
    for (; *text; ++text) {
        size_t i = 0;
        while (query[i] && text[i] && fold((unsigned char)text[i]) == fold((unsigned char)query[i]))
            ++i;
        if (!query[i])
            return true;
    }
    return false;
}
bool qa_console_entry_matches(const qa_console_discovery_entry *entry, const char *query) {
    const char *usage = entry->documentation ? entry->documentation->usage : NULL;
    const char *parts[] = {entry->name,
                           " ",
                           entry->summary ? entry->summary : "",
                           " ",
                           usage                               ? usage
                           : entry->kind == QA_CONSOLE_COMMAND ? ""
                                                               : entry->name,
                           !usage && entry->kind == QA_CONSOLE_CVAR ? " [value]" : ""};
    if (!*query)
        return true;
    for (size_t start = 0; start < sizeof(parts) / sizeof(*parts); ++start) {
        if (contains(parts[start], query))
            return true;
        for (size_t offset = 0; parts[start][offset]; ++offset) {
            size_t part = start, at = offset, matched = 0;
            while (query[matched]) {
                while (part < sizeof(parts) / sizeof(*parts) && !parts[part][at]) {
                    ++part;
                    at = 0;
                }
                if (part == sizeof(parts) / sizeof(*parts) ||
                    fold((unsigned char)parts[part][at]) != fold((unsigned char)query[matched]))
                    break;
                ++at;
                ++matched;
            }
            if (!query[matched])
                return true;
        }
    }
    return false;
}
static const char *kind_name(qa_console_entry_kind kind) {
    return kind == QA_CONSOLE_COMMAND ? "command" : kind == QA_CONSOLE_ALIAS ? "alias" : "cvar";
}
static bool line(qac_text *out, const char *label, const char *value, qa_error *error) {
    return qac_text_string(out, label, error) && qac_text_string(out, value, error) &&
           qac_text_string(out, "\n", error);
}
static bool quoted(qac_text *out, const char *text, qa_error *error) {
    qa_buffer encoded = {0};
    if (!qa_json_quote((qa_bytes){(const uint8_t *)text, strlen(text)}, &encoded, error))
        return false;
    bool ok = qac_text_add(out, (const char *)encoded.data, encoded.size, error);
    qa_buffer_free(&encoded);
    return ok;
}
bool qa_console_entry_help(const qa_console_discovery_entry *entry, qa_buffer *result,
                           qa_error *error) {
    qac_text out = {0};
    const qa_console_documentation *doc = entry->documentation;
    if (!qac_text_string(&out, entry->name, error) || !qac_text_string(&out, " (", error) ||
        !qac_text_string(&out, kind_name(entry->kind), error) ||
        !qac_text_string(&out, ")\n", error) ||
        !line(&out, "",
              entry->summary && *entry->summary ? entry->summary : "No description registered.",
              error) ||
        !qac_text_string(&out, "Usage: ", error))
        goto fail;
    if (doc && doc->usage) {
        if (!qac_text_string(&out, doc->usage, error))
            goto fail;
    } else if (entry->kind == QA_CONSOLE_CVAR) {
        if (!qac_text_string(&out, entry->name, error) || !qac_text_string(&out, " [value]", error))
            goto fail;
    } else if (!qac_text_string(
                   &out, entry->kind == QA_CONSOLE_ALIAS ? entry->name : "not documented", error))
        goto fail;
    if (!qac_text_string(&out, "\n", error))
        goto fail;
    if (entry->kind == QA_CONSOLE_CVAR) {
        const char *labels[] = {"Current: ", "Default: ", "Pending: "};
        const char *values[] = {entry->value, entry->reset_value, entry->latched_value};
        for (size_t i = 0; i < 3; ++i)
            if (values[i])
                if (!qac_text_string(&out, labels[i], error) || !quoted(&out, values[i], error) ||
                    !qac_text_string(&out, "\n", error))
                    goto fail;
    } else if (entry->kind == QA_CONSOLE_ALIAS && !line(&out, "Expands to: ", entry->value, error))
        goto fail;
    if (doc) {
        if (doc->has_allowed_values) {
            if (!qac_text_string(&out, "Allowed values: ", error))
                goto fail;
            for (size_t i = 0; i < doc->allowed_count; ++i)
                if ((i && !qac_text_string(&out, ", ", error)) ||
                    !qac_text_string(&out, doc->allowed_values[i], error))
                    goto fail;
            if (!qac_text_string(&out, "\n", error))
                goto fail;
        }
        for (size_t i = 0; i < doc->example_count; ++i)
            if (!line(&out, "Example: ", doc->examples[i], error))
                goto fail;
    }
    if (qac_text_finish(&out, result, error))
        return true;
fail:
    free(out.data);
    return false;
}
struct qa_console_discovery_commands {
    qa_console_discovery_options options;
    bool find, help;
};
static bool search_command(void *user, const qa_command_invocation *command, qa_error *error) {
    qa_console_discovery_commands *owner = user;
    qac_text out = {0};
    qa_console_discovery entries = {0};
    bool help = qac_equal(command->argv[0], "help");
    size_t page = 1;
    if (help && command->argc != 2) {
        owner->options.print(
            owner->options.context, &command->context,
            "Usage: help <name>\nUse find <text> to search commands and settings.\n");
        return true;
    }
    if (!help) {
        bool valid = command->argc == 2 || command->argc == 3;
        if (valid && command->argc == 3) {
            const char *text = command->argv[2];
            valid = *text >= '1' && *text <= '9';
            page = 0;
            for (; valid && *text; ++text) {
                unsigned digit = (unsigned char)*text - (unsigned)'0';
                if (digit > 9 || page > (SIZE_MAX - digit) / 10)
                    valid = false;
                else
                    page = page * 10 + digit;
            }
        }
        if (!valid) {
            owner->options.print(owner->options.context, &command->context,
                                 "Usage: find <text> [page]\nExample: find mouse\n");
            return true;
        }
    }
    if (!qa_console_discover(command->console, &command->context, &entries, error))
        return false;
    const char *query = command->argv[1];
    if (help) {
        const qa_console_discovery_entry *found = NULL;
        for (size_t i = 0; i < entries.count; ++i)
            if (!strcmp(entries.entries[i].name, query)) {
                found = &entries.entries[i];
                break;
            }
        if (!found)
            for (size_t i = 0; i < entries.count; ++i)
                if (!compare_names(entries.entries[i].name, query)) {
                    found = &entries.entries[i];
                    break;
                }
        if (found) {
            qa_buffer text = {0};
            if (!qa_console_entry_help(found, &text, error))
                goto fail;
            owner->options.print(owner->options.context, &command->context,
                                 (const char *)text.data);
            qa_buffer_free(&text);
            qa_console_discovery_free(&entries);
            return true;
        }
        if (!qac_text_string(&out, "No command, setting, or alias named ", error) ||
            !quoted(&out, query, error) || !qac_text_string(&out, ". Use find <text>.\n", error))
            goto fail;
    } else {
        size_t matches = 0;
        for (size_t i = 0; i < entries.count; ++i)
            if (qa_console_entry_matches(&entries.entries[i], query))
                ++matches;
        size_t pages = matches ? 1 + (matches - 1) / 30 : 1;
        char summary[160];
        if (page > pages) {
            snprintf(summary, sizeof(summary), "Page %zu is out of range; %zu page(s).\n", page,
                     pages);
            if (!qac_text_string(&out, summary, error))
                goto fail;
        } else {
            size_t index = 0, start = (page - 1) * 30;
            for (size_t i = 0; i < entries.count; ++i) {
                const qa_console_discovery_entry *entry = &entries.entries[i];
                if (!qa_console_entry_matches(entry, query))
                    continue;
                size_t ordinal = index++;
                if (ordinal < start || ordinal - start >= 30)
                    continue;
                if (!qac_text_string(&out, entry->name, error) ||
                    !qac_text_string(&out, " (", error) ||
                    !qac_text_string(&out, kind_name(entry->kind), error) ||
                    !qac_text_string(&out, ")", error))
                    goto fail;
                if (entry->summary && *entry->summary &&
                    (!qac_text_string(&out, " - ", error) ||
                     !qac_text_string(&out, entry->summary, error)))
                    goto fail;
                if (!qac_text_string(&out, "\n", error))
                    goto fail;
            }
            snprintf(summary, sizeof(summary),
                     "%zu match(es), page %zu/%zu. Use help <name> for details.\n", matches, page,
                     pages);
            if (!qac_text_string(&out, summary, error))
                goto fail;
            if (page < pages) {
                snprintf(summary, sizeof(summary), " %zu\n", page + 1);
                if (!qac_text_string(&out, "Next page: find ", error) ||
                    !quoted(&out, query, error) || !qac_text_string(&out, summary, error))
                    goto fail;
            }
        }
    }
    qa_console_discovery_free(&entries);
    owner->options.print(owner->options.context, &command->context, out.data ? out.data : "");
    free(out.data);
    return true;
fail:
    qa_console_discovery_free(&entries);
    free(out.data);
    return false;
}
qa_console_discovery_commands *
qa_console_discovery_register(const qa_console_discovery_options *options, qa_error *error) {
    if (!options || !options->commands || !options->print) {
        qac_fail(error, QA_ERROR_ARGUMENT, "missing console discovery services");
        return NULL;
    }
    qa_console_discovery_commands *owner = calloc(1, sizeof(*owner));
    if (!owner) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating console discovery commands");
        return NULL;
    }
    owner->options = *options;
    const char *names[] = {"find", "help"};
    const char *descriptions[] = {"Search command and setting names and descriptions.",
                                  "Show usage and documentation for a command, setting, or alias."};
    const char *usages[] = {"find <text> [page]", "help <name>"};
    for (size_t i = 0; i < 2; ++i) {
        if (qa_console_find(options->commands, NULL, names[i]))
            continue;
        if (!qa_console_register(options->commands, names[i], descriptions[i], options->owner, true,
                                 search_command, owner, error))
            goto fail;
        if (i)
            owner->help = true;
        else
            owner->find = true;
        qa_console_documentation doc = {.usage = usages[i]};
        if (!qa_console_document(options->commands, names[i], options->owner, &doc, error))
            goto fail;
    }
    return owner;
fail:
    qa_console_discovery_unregister(owner);
    return NULL;
}
void qa_console_discovery_unregister(qa_console_discovery_commands *owner) {
    if (!owner)
        return;
    if (owner->find)
        qa_console_unregister(owner->options.commands, "find", owner->options.owner);
    if (owner->help)
        qa_console_unregister(owner->options.commands, "help", owner->options.owner);
    free(owner);
}
