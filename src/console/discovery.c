#include "commands_private.h"
#include "cvars_private.h"
#include "qa/console_discovery.h"
#include "qa/arena.h"
#include "qa/json.h"
#include "qa/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct entry_list {
    qa_console_discovery view;
    qa_arena *storage;
    size_t capacity;
} entry_list;
uint64_t qa_console_declaration_revision(const qa_console *console) {
    return console ? console->declaration_revision : 0;
}
static bool name_extent(const char *name, size_t *count, size_t *bytes,
    size_t *maximum, qa_error *error) {
    size_t length = strlen(name);
    if (*count == SIZE_MAX || length == SIZE_MAX || *bytes > SIZE_MAX - length - 1)
        return qac_fail(error, QA_ERROR_MEMORY, "console completion inventory overflows");
    ++*count;
    *bytes += length + 1;
    if (*maximum < length) *maximum = length;
    return true;
}
bool qac_console_completion_extent(qa_console *console, const qa_command_context *context,
    size_t *count, size_t *bytes, size_t *maximum, qa_error *error) {
    *count = *bytes = *maximum = 0;
    for (command_entry *command = console->commands; command; command = command->next)
        if (!name_extent(command->name, count, bytes, maximum, error)) return false;
    const qa_console_entry *entry;
    for (size_t i = 0; (entry = qac_console_builtin_at(i)); ++i)
        if (!name_extent(entry->name, count, bytes, maximum, error)) return false;
    for (alias_entry *alias = console->aliases; alias; alias = alias->next)
        if (!name_extent(alias->view.name, count, bytes, maximum, error)) return false;
    for (size_t i = 0;; ++i) {
        qa_cvars *registry = qa_console_visible_cvars(console, context, i);
        if (!registry) break;
        const cvar_values *values = qac_cvars_current_values(registry);
        if (values && values->canonical_values) values = values->canonical_values;
        for (const cvar *var = values ? values->first : NULL; var; var = var->next)
            if (!name_extent(var->view.name, count, bytes, maximum, error)) return false;
        for (const cvar_alias *alias = values ? values->aliases : NULL; alias; alias = alias->next)
            if (!name_extent(alias->name, count, bytes, maximum, error)) return false;
    }
    return true;
}
static int compare_entries(const void *a, const void *b) {
    const qa_console_discovery_entry *left = a, *right = b;
    int order = qa_text_ascii_compare(left->name, right->name);
    return order ? order : strcmp(left->name, right->name);
}
static bool add(entry_list *list, qa_console_discovery_entry entry, qa_error *error) {
    for (size_t i = 0; i < list->view.count; ++i) {
        qa_console_discovery_entry *current = &list->view.entries[i];
        if ((current->kind != QA_CONSOLE_CVAR && qa_text_ascii_equal(current->name, entry.name)) ||
            (current->kind == QA_CONSOLE_CVAR && entry.kind == QA_CONSOLE_CVAR &&
             !strcmp(current->name, entry.name)))
            return true;
    }
    if (list->view.count == list->capacity) {
        size_t count = list->capacity ? list->capacity * 2 : 64;
        if (count < list->capacity || count > SIZE_MAX / sizeof(entry))
            return qac_fail(error, QA_ERROR_MEMORY, "console discovery overflow");
        void *data = qa_arena_grow(list->storage, list->view.entries,
                                   list->capacity * sizeof(entry), count * sizeof(entry),
                                   _Alignof(qa_console_discovery_entry), error);
        if (!data)
            return qac_fail(error, QA_ERROR_MEMORY, "allocating console discovery");
        list->view.entries = data;
        list->capacity = count;
    }
    size_t first = 0, last = list->view.count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (compare_entries(&list->view.entries[middle], &entry) <= 0) first = middle + 1;
        else last = middle;
    }
    memmove(list->view.entries + first + 1, list->view.entries + first,
        (list->view.count - first) * sizeof(entry));
    list->view.entries[first] = entry;
    ++list->view.count;
    return true;
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
            if (qa_text_ascii_equal(alias->name, name)) {
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
                if (qa_text_ascii_equal(var->name, name) &&
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
                         qa_arena *storage, qa_console_discovery *out, qa_error *error) {
    if (!console || !context || !out)
        return qac_fail(error, QA_ERROR_ARGUMENT, "missing console discovery context");
    qa_command_context captured;
    if (!qa_console_cvar_context(console, context, &captured, error)) return false;
    return qac_console_discover_captured(console, &captured, storage, 0, out, error);
}
bool qac_console_discover_captured(qa_console *console, const qa_command_context *context,
    qa_arena *storage, size_t capacity, qa_console_discovery *out, qa_error *error) {
    entry_list list = {.storage = storage, .capacity = capacity};
    if (capacity) {
        if (capacity > SIZE_MAX / sizeof(*list.view.entries))
            return qac_fail(error, QA_ERROR_MEMORY, "console discovery inventory overflows");
        list.view.entries = qa_arena_alloc(storage, capacity * sizeof(*list.view.entries),
            _Alignof(qa_console_discovery_entry), error);
        if (!list.view.entries) return false;
    }
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
    *out = list.view;
    return true;
fail:
    return false;
}
static bool contains(const char *text, qa_bytes query) {
    if (!query.size)
        return true;
    if (!text)
        return false;
    qa_bytes span = {(const uint8_t *)text, strlen(text)};
    for (; span.size; ++span.data, --span.size) {
        if (qa_text_bytes_prefix(span, query, true))
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
    qa_bytes wanted = {(const uint8_t *)query, strlen(query)};
    for (size_t start = 0; start < sizeof(parts) / sizeof(*parts); ++start) {
        if (contains(parts[start], wanted))
            return true;
        for (size_t offset = 0; parts[start][offset]; ++offset) {
            size_t part = start, at = offset, matched = 0;
            while (query[matched]) {
                while (part < sizeof(parts) / sizeof(*parts) && !parts[part][at]) {
                    ++part;
                    at = 0;
                }
                if (part == sizeof(parts) / sizeof(*parts) ||
                    !qa_text_bytes_equal((qa_bytes){(const uint8_t *)parts[part] + at, 1},
                                         (qa_bytes){wanted.data + matched, 1}, true))
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
bool qa_console_entry_help(const qa_console_discovery_entry *entry, qa_buffer *result,
                           qa_error *error) {
    const qa_console_documentation *doc = entry->documentation;
    const char *values[] = {entry->value, entry->reset_value, entry->latched_value};
    size_t capacity = 8;
    if (!(doc && doc->usage) && entry->kind == QA_CONSOLE_CVAR) ++capacity;
    if (entry->kind == QA_CONSOLE_CVAR) {
        for (size_t i = 0; i < 3; ++i)
            if (values[i]) capacity += 3;
    } else if (entry->kind == QA_CONSOLE_ALIAS)
        capacity += 3;
    if (doc) {
        if (doc->has_allowed_values) {
            if (doc->allowed_count > (SIZE_MAX - capacity - 2) / 2)
                return qac_fail(error, QA_ERROR_MEMORY, "console help exceeds address space");
            capacity += doc->allowed_count ? 2 * doc->allowed_count + 1 : 2;
        }
        if (doc->example_count > (SIZE_MAX - capacity) / 3)
            return qac_fail(error, QA_ERROR_MEMORY, "console help exceeds address space");
        capacity += 3 * doc->example_count;
    }
    if (capacity > SIZE_MAX / sizeof(const char *))
        return qac_fail(error, QA_ERROR_MEMORY, "console help exceeds address space");
    const char **parts = malloc(capacity * sizeof(*parts));
    if (!parts)
        return qac_fail(error, QA_ERROR_MEMORY, "allocating console help parts");
    qa_buffer quotes[3] = {{0}};
    size_t count = 0;
    parts[count++] = entry->name;
    parts[count++] = " (";
    parts[count++] = kind_name(entry->kind);
    parts[count++] = ")\n";
    parts[count++] = entry->summary && *entry->summary ? entry->summary : "No description registered.";
    parts[count++] = "\nUsage: ";
    if (doc && doc->usage)
        parts[count++] = doc->usage;
    else if (entry->kind == QA_CONSOLE_CVAR) {
        parts[count++] = entry->name;
        parts[count++] = " [value]";
    } else
        parts[count++] = entry->kind == QA_CONSOLE_ALIAS ? entry->name : "not documented";
    parts[count++] = "\n";
    if (entry->kind == QA_CONSOLE_CVAR) {
        const char *labels[] = {"Current: ", "Default: ", "Pending: "};
        for (size_t i = 0; i < 3; ++i) {
            if (!values[i]) continue;
            if (!qa_json_quote((qa_bytes){(const uint8_t *)values[i], strlen(values[i])}, &quotes[i], error))
                goto fail;
            parts[count++] = labels[i];
            parts[count++] = (const char *)quotes[i].data;
            parts[count++] = "\n";
        }
    } else if (entry->kind == QA_CONSOLE_ALIAS) {
        parts[count++] = "Expands to: ";
        parts[count++] = entry->value;
        parts[count++] = "\n";
    }
    if (doc) {
        if (doc->has_allowed_values) {
            parts[count++] = "Allowed values: ";
            for (size_t i = 0; i < doc->allowed_count; ++i) {
                if (i) parts[count++] = ", ";
                parts[count++] = doc->allowed_values[i];
            }
            parts[count++] = "\n";
        }
        for (size_t i = 0; i < doc->example_count; ++i) {
            parts[count++] = "Example: ";
            parts[count++] = doc->examples[i];
            parts[count++] = "\n";
        }
    }
    size_t size = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        if (length >= SIZE_MAX - size) {
            qac_fail(error, QA_ERROR_MEMORY, "console help exceeds address space");
            goto fail;
        }
        size += length;
    }
    uint8_t *data = malloc(size + 1);
    if (!data) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating console help text");
        goto fail;
    }
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        memcpy(data + used, parts[i], length);
        used += length;
    }
    data[size] = 0;
    for (size_t i = 0; i < 3; ++i) qa_buffer_free(&quotes[i]);
    free(parts);
    *result = (qa_buffer){data, size};
    return true;
fail:
    for (size_t i = 0; i < 3; ++i) qa_buffer_free(&quotes[i]);
    free(parts);
    return false;
}
struct qa_console_discovery_commands {
    qa_console_discovery_options options;
    bool find, help;
};
static bool search_command(void *user, const qa_command_invocation *command, qa_error *error) {
    qa_console_discovery_commands *owner = user;
    enum { PAGE_ROWS = 30 };
    const char *parts[PAGE_ROWS * 7 + 4];
    size_t count = 0;
    qa_buffer quote = {0}, out = {0};
    char summary[160], next_page[160];
    qa_console_discovery entries = {0};
    qa_arena storage = {0};
    bool help = qa_text_ascii_equal(command->argv[0], "help");
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
    if (!qa_console_discover(command->console, &command->context, &storage, &entries, error))
        goto fail;
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
                if (qa_text_ascii_equal(entries.entries[i].name, query)) {
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
            qa_arena_destroy(&storage);
            return true;
        }
        if (!qa_json_quote((qa_bytes){(const uint8_t *)query, strlen(query)}, &quote, error))
            goto fail;
        parts[count++] = "No command, setting, or alias named ";
        parts[count++] = (const char *)quote.data;
        parts[count++] = ". Use find <text>.\n";
    } else {
        size_t matches = 0;
        for (size_t i = 0; i < entries.count; ++i)
            if (qa_console_entry_matches(&entries.entries[i], query))
                ++matches;
        size_t pages = matches ? 1 + (matches - 1) / PAGE_ROWS : 1;
        if (page > pages) {
            snprintf(summary, sizeof(summary), "Page %zu is out of range; %zu page(s).\n", page,
                     pages);
            parts[count++] = summary;
        } else {
            size_t index = 0, start = (page - 1) * PAGE_ROWS;
            for (size_t i = 0; i < entries.count; ++i) {
                const qa_console_discovery_entry *entry = &entries.entries[i];
                if (!qa_console_entry_matches(entry, query))
                    continue;
                size_t ordinal = index++;
                if (ordinal < start || ordinal - start >= PAGE_ROWS)
                    continue;
                parts[count++] = entry->name;
                parts[count++] = " (";
                parts[count++] = kind_name(entry->kind);
                parts[count++] = ")";
                if (entry->summary && *entry->summary) {
                    parts[count++] = " - ";
                    parts[count++] = entry->summary;
                }
                parts[count++] = "\n";
            }
            snprintf(summary, sizeof(summary),
                     "%zu match(es), page %zu/%zu. Use help <name> for details.\n", matches, page,
                     pages);
            parts[count++] = summary;
            if (page < pages) {
                if (!qa_json_quote((qa_bytes){(const uint8_t *)query, strlen(query)}, &quote, error))
                    goto fail;
                snprintf(next_page, sizeof(next_page), " %zu\n", page + 1);
                parts[count++] = "Next page: find ";
                parts[count++] = (const char *)quote.data;
                parts[count++] = next_page;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        if (length >= SIZE_MAX - out.size) {
            qac_fail(error, QA_ERROR_MEMORY, "console search exceeds address space");
            goto fail;
        }
        out.size += length;
    }
    out.data = malloc(out.size + 1);
    if (!out.data) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating console search text");
        goto fail;
    }
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        memcpy(out.data + used, parts[i], length);
        used += length;
    }
    out.data[out.size] = 0;
    qa_arena_destroy(&storage);
    qa_buffer_free(&quote);
    owner->options.print(owner->options.context, &command->context, (const char *)out.data);
    qa_buffer_free(&out);
    return true;
fail:
    qa_arena_destroy(&storage);
    qa_buffer_free(&quote);
    qa_buffer_free(&out);
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
