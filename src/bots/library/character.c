#include "internal.h"
#include "character_load.h"
#include "character_source.h"
#include "character_reader.h"

static const char default_path[] = "bots/default_c.c";
void qa_bot_character_retain(qa_bot_character *c) {
    if (c != NULL)
        atomic_fetch_add_explicit(&c->references, 1, memory_order_relaxed);
}
void qa_bot_character_release(qa_bot_character *c) {
    if (c != NULL && atomic_fetch_sub_explicit(&c->references, 1, memory_order_acq_rel) == 1) {
        qa_script_dispose(c->reader);
        free(c->script_host);
        bot_character_store_release(c->source);
        qa_arena_destroy(&c->arena);
        free(c);
    }
}
const qa_bot_character_view *qa_bot_character_read(const qa_bot_character *c) {
    return c != NULL && bot_character_project((qa_bot_character *)c, NULL) ? &c->view : NULL;
}
bool qa_bot_character_view_read(const qa_bot_character *c, const qa_bot_character_view **out,
                                qa_error *error) {
    if (!out || !c) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing character view owner/output"); return false; }
    *out = NULL;
    if (!bot_character_project((qa_bot_character *)c, error)) return false;
    *out = &c->view; return true;
}
static bool create(qa_bot_library *library, const char *path, float skill,
                   qa_bot_character **out, qa_error *e) {
    if (!library->character_store &&
        !bot_character_store_create(library->memory, false, &library->character_store, e)) return false;
    return bot_character_create(library->character_store, path, skill, out, e);
}
static bool copy_value(qa_bot_character *c, uint32_t index, qa_bot_character_value value,
                       qa_error *e) {
    return bot_character_write(c, index, value, true, e);
}
bool qa_bot_character_restore(const qa_bot_character_view *view, qa_bot_character **out,
                              qa_error *e) {
    if (view == NULL || out == NULL || view->path == NULL || !isfinite(view->skill)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid character checkpoint");
        return false;
    }
    for (size_t i = 0; i < QA_BOT_CHARACTERISTICS; ++i) {
        const qa_bot_character_value *v = view->values + i;
        if ((unsigned)v->kind > QA_BOT_CHARACTER_STRING ||
            (v->kind == QA_BOT_CHARACTER_STRING && v->data.string == NULL)) {
            qa_error_set(e, QA_ERROR_FORMAT, i, "Invalid saved character value");
            return false;
        }
    }
    qa_bot_memory *memory = NULL;
    bot_character_store *store = NULL;
    qa_bot_character *c = NULL;
    if (!qa_bot_memory_create(NULL, &memory, e)) return false;
    bool built = bot_character_store_create(memory, true, &store, e);
    (void)qa_bot_memory_release(memory, NULL);
    if (!built) return false;
    built = bot_character_create(store, view->path, view->skill, &c, e);
    bot_character_store_release(store);
    if (!built) {
        qa_bot_character_release(c);
        return false;
    }
    for (uint32_t i = 0; i < QA_BOT_CHARACTERISTICS; ++i)
        if (!copy_value(c, i, view->values[i], e)) {
            qa_bot_character_release(c);
            return false;
        }
    *out = c;
    c->ready = true;
    return true;
}
static bool parse(qa_bot_library *library, const char *path, int desired, qa_bot_character **out,
                  bool *found, bool *source_failure, qa_error *e) {
    *out = NULL;
    *found = false;
    *source_failure = false;
    qa_script *s = NULL; bot_character_reader *host = NULL;
    if (!bot_character_reader_create(&library->options.scripts, &host, e)) return false;
    qa_script_services scripts = bot_character_reader_services(host);
    if (!qa_script_open(path, &scripts, &library->options.preprocessor, &s, e)) {
        *source_failure = !host->callback_failed && e &&
            (e->code == QA_ERROR_FORMAT || e->code == QA_ERROR_NOT_FOUND);
        if (*source_failure) bot_character_reader_report(host, NULL, path, e);
        free(host); return false;
    }
    qa_bot_character *c = NULL;
    bool created = create(library, path, 0, &c, e);
    if (!c) { qa_script_close(s); free(host); return false; }
    c->script_host = host;
    /* Attach before parser/allocator callbacks; a service failure retains the
     * real partial source record and PC rather than losing its owner. */
    if (library->last_character) library->last_character->next = c;
    else library->characters = c;
    library->last_character = c;
    qa_bot_character_retain(c);
    c->active = true; c->reader = s;
    if (!created) { c->active = false; qa_bot_character_release(c); return false; }
    bool ok = true, native_failure = false;
    for (;;) {
        qa_script_token token;
        bool next;
        if (!qa_script_next(s, &token, &next, e)) {
            ok = false;
            break;
        }
        if (!next)
            break;
        if (!qa_script_token_is(&token, "skill") || !bot_token(s, &token, e) ||
            token.kind != QA_SCRIPT_NUMBER) {
            ok = bot_fail(s, "Expected character skill number", e);
            break;
        }
        int32_t skill = token.integer;
        if (!qa_script_expect(s, "{", e)) {
            ok = false;
            break;
        }
        if (desired >= 0 && skill != desired) {
            size_t depth = 1;
            while (depth != 0) {
                if (!bot_token(s, &token, e)) {
                    ok = false;
                    break;
                }
                if (qa_script_token_is(&token, "{")) {
                    if (depth == SIZE_MAX) {
                        ok = bot_fail(s, "Character block nesting overflow", e);
                        break;
                    }
                    ++depth;
                } else if (qa_script_token_is(&token, "}"))
                    --depth;
            }
            if (!ok)
                break;
            continue;
        }
        if (!bot_character_skill(c, (float)skill, e)) {
            native_failure = true;
            ok = false;
            break;
        }
        for (;;) {
            if (!qa_script_next(s, &token, &next, e)) {
                ok = false;
                break;
            }
            if (!next) {
                bot_warning(library, s, "Character skill ends without closing brace");
                *found = true;
                break;
            }
            if (qa_script_token_is(&token, "}")) {
                *found = true;
                break;
            }
            if (token.kind != QA_SCRIPT_NUMBER || (token.subtype & QA_SCRIPT_INTEGER) == 0 ||
                token.integer < 0 || token.integer >= QA_BOT_CHARACTERISTICS) {
                ok = bot_fail(s, "Character index is outside 0..80", e);
                break;
            }
            uint32_t index = (uint32_t)token.integer;
            qa_bot_character_value_kind kind;
            if (!bot_character_kind(c, index, &kind, e)) { native_failure = true; ok = false; break; }
            if (kind != QA_BOT_CHARACTER_UNSET) {
                ok = bot_fail(s, "Duplicate character characteristic", e);
                break;
            }
            if (!bot_token(s, &token, e)) {
                ok = false;
                break;
            }
            qa_bot_character_value value = {0};
            char *text = NULL;
            if (token.kind == QA_SCRIPT_STRING) {
                value.kind = QA_BOT_CHARACTER_STRING;
                qa_bytes decoded = qa_script_token_value(&token);
                if (decoded.size == SIZE_MAX || !(text = malloc(decoded.size + 1))) {
                    qa_error_set(e, QA_ERROR_MEMORY, 0, "Reading character source string");
                    native_failure = true;
                    ok = false; break;
                }
                memcpy(text, decoded.data, decoded.size); text[decoded.size] = 0;
                value.data.string = text;
            } else if (token.kind == QA_SCRIPT_NUMBER && (token.subtype & QA_SCRIPT_FLOAT) != 0) {
                value.kind = QA_BOT_CHARACTER_FLOAT;
                value.data.number = (float)token.number;
                if (!isfinite(value.data.number)) {
                    ok = bot_fail(s, "Non-finite character float", e);
                    break;
                }
            } else if (token.kind == QA_SCRIPT_NUMBER) {
                value.kind = QA_BOT_CHARACTER_INTEGER;
                value.data.integer = token.integer;
            } else {
                ok = bot_fail(s, "Expected numeric or quoted character value", e);
                break;
            }
            bool written = bot_character_write(c, index, value, false, e);
            free(text);
            if (!written) { native_failure = true; ok = false; break; }
        }
        break;
    }
    c->active = false;
    *source_failure = !ok && !native_failure && !host->callback_failed && e &&
        (e->code == QA_ERROR_FORMAT || e->code == QA_ERROR_NOT_FOUND);
    if (*source_failure) bot_character_reader_report(host, s, path, e);
    if (ok || *source_failure) { qa_script_close(s); c->reader = NULL; }
    if (ok && *found) {
        c->ready = true; ok = bot_character_header(c, e);
        if (ok && library->character_publish)
            ok = library->character_publish(library->character_context, c, e);
    }
    if ((ok && !*found) || *source_failure) {
        if (!qa_bot_character_free(c, e)) { ok = false; *source_failure = false; }
        if (c->retired) bot_character_forget(library, c);
    }
    if (!ok || !*found)
        qa_bot_character_release(c);
    else
        *out = c;
    return ok;
}
static qa_bot_character *cached(qa_bot_library *library, const char *path, float skill) {
    for (qa_bot_character *c = library->characters; c != NULL; c = c->next)
        if (c->ready && bot_character_header(c, NULL) && strcmp(c->view.path, path) == 0 &&
            (skill < 0 || fabsf(c->view.skill - skill) < 0.01f))
            return c;
    return NULL;
}
static void store(qa_bot_library *library, qa_bot_character *c) {
    for (qa_bot_character *held = library->characters; held; held = held->next)
        if (held == c) return;
    qa_bot_character_retain(c);
    if (library->last_character != NULL)
        library->last_character->next = c;
    else
        library->characters = c;
    library->last_character = c;
}
static bool attempt(qa_bot_library *library, const char *path, int skill, bool use_cache,
                    qa_bot_character **out, bool *found, qa_error *e) {
    *found = false;
    if (library->character_available && !library->character_available(library->character_context)) {
        library->character_exhausted = true;
        return true;
    }
    if (use_cache) {
        qa_bot_character *c = cached(library, path, (float)skill);
        if (c != NULL) {
            qa_bot_character_retain(c);
            *out = c;
            *found = true;
            return true;
        }
    }
    qa_error local = {0};
    qa_bot_character *c;
    bool source_failure;
    if (!parse(library, path, skill, &c, found, &source_failure, &local)) {
        if (source_failure) {
            return true;
        }
        if (e != NULL)
            *e = local;
        return false;
    }
    if (!*found)
        return true;
    if (use_cache)
        store(library, c);
    *out = c;
    return true;
}
static bool load_cached(qa_bot_library *library, const char *path, float skill, bool reload,
                        qa_bot_character **out, bool *found, qa_error *e) {
    int selected = (int)(skill + 0.5f);
    const char *paths[] = {path, default_path, path, default_path};
    for (size_t i = 0; i < 4; ++i) {
        if (!attempt(library, paths[i], i < 2 ? selected : -1, !reload, out, found, e))
            return false;
        if (*found)
            return true;
    }
    return true;
}
static bool load_skill(qa_bot_library *library, const char *path, float skill,
                       qa_bot_character **out, bool *found, qa_error *e) {
    qa_bot_character *defaults = NULL, *c = NULL;
    bool have_defaults;
    if (!load_cached(library, default_path, skill, false, &defaults, &have_defaults, e))
        return false;
    if (!load_cached(library, path, skill, bot_reload_characters(library), &c, found, e)) {
        qa_bot_character_release(defaults);
        return false;
    }
    if (*found && have_defaults && c != defaults) {
        for (uint32_t i = 0; i < 80; ++i) {
            qa_bot_character_value_kind kind;
            if (!bot_character_kind(c, i, &kind, e)) {
                qa_bot_character_release(defaults); qa_bot_character_release(c); return false;
            }
            if (kind != QA_BOT_CHARACTER_UNSET) continue;
            qa_bot_character_value value;
            qa_bot_character_value_kind default_kind;
            if (!bot_character_kind(defaults, i, &default_kind, e) ||
                (default_kind != QA_BOT_CHARACTER_UNSET &&
                 (!bot_character_value(defaults, i, &value, e) || !copy_value(c, i, value, e)))) {
                qa_bot_character_release(defaults);
                qa_bot_character_release(c);
                return false;
            }
        }
    }
    qa_bot_character_release(defaults);
    if (*found)
        *out = c;
    return true;
}
static bool load_source(qa_bot_library *library, const char *path, float skill,
                        qa_bot_character **out, bool *interpolated, qa_error *e) {
    if (library == NULL || path == NULL || out == NULL || !interpolated || !isfinite(skill)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot character request");
        return false;
    }
    *interpolated = false;
    skill = fmaxf(1, fminf(5, skill));
    qa_bot_character *c = NULL;
    bool found;
    if (skill == 1 || skill == 4 || skill == 5) {
        if (!load_skill(library, path, skill, &c, &found, e))
            return false;
        if (found) {
            *out = c;
            return true;
        }
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "No bot character skill available: %s", path);
        return false;
    }
    c = cached(library, path, skill);
    if (c != NULL) {
        qa_bot_character_retain(c);
        *out = c;
        return true;
    }
    qa_bot_character *first, *second = NULL;
    if (!load_skill(library, path, skill < 4 ? 1 : 4, &first, &found, e))
        return false;
    if (!found) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "No lower bot character skill available: %s", path);
        return false;
    }
    if (!load_skill(library, path, skill < 4 ? 4 : 5, &second, &found, e)) {
        qa_bot_character_release(first);
        return false;
    }
    if (!found) {
        *out = first;
        return true;
    }
    if (library->character_available && !library->character_available(library->character_context)) {
        qa_bot_character_release(first); qa_bot_character_release(second); *out = NULL; return true;
    }
    if (!bot_character_header(first, e) || !bot_character_header(second, e) ||
        !create(library, "", skill, &c, e)) {
        if (c) store(library, c);
        qa_bot_character_release(c);
        qa_bot_character_release(first);
        qa_bot_character_release(second);
        return false;
    }
    store(library, c);
    if (!bot_character_header(first, e) || !bot_character_header(second, e) ||
        !bot_character_filename(c, first->view.path, e)) {
        qa_bot_character_release(first); qa_bot_character_release(second);
        qa_bot_character_release(c); return false;
    }
    if (library->character_publish &&
        !library->character_publish(library->character_context, c, e)) {
        qa_bot_character_release(first); qa_bot_character_release(second);
        qa_bot_character_release(c); return false;
    }
    c->ready = true;
    float scale = (skill - first->view.skill) / (second->view.skill - first->view.skill);
    bool ok = true;
    for (uint32_t i = 0; ok && i < 80; ++i) {
        qa_bot_character_value_kind lower_kind, upper_kind;
        if (!bot_character_kind(first, i, &lower_kind, e) ||
            !bot_character_kind(second, i, &upper_kind, e)) { ok = false; break; }
        qa_bot_character_value lower, upper;
        if (lower_kind == QA_BOT_CHARACTER_FLOAT && upper_kind == QA_BOT_CHARACTER_FLOAT) {
            if (!bot_character_value(first, i, &lower, e) || !bot_character_value(second, i, &upper, e)) { ok = false; break; }
            float delta = upper.data.number - lower.data.number;
            volatile float product = delta * scale;
            lower.data.number += product;
            ok = bot_character_write(c, i, lower, false, e);
        } else if (lower_kind == QA_BOT_CHARACTER_INTEGER || lower_kind == QA_BOT_CHARACTER_STRING)
            ok = bot_character_value(first, i, &lower, e) && copy_value(c, i, lower, e);
    }
    if (ok) {
        c->ready = true;
        ok = bot_character_header(c, e);
    }
    if (ok) {
        store(library, c);
    }
    qa_bot_character_release(first);
    qa_bot_character_release(second);
    if (!ok) {
        qa_bot_character_release(c);
        return false;
    }
    *out = c;
    *interpolated = true;
    return true;
}
bool bot_character_load(qa_bot_library *library, const char *path, float skill,
                        qa_bot_character **out, bool *interpolated, qa_error *e) {
    if (!library || library->character_loading || qa_bot_memory_disposed(library->memory)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Character source owner is absent, disposed or loading"); return false;
    }
    library->character_loading = true;
    library->character_exhausted = false;
    bool okay = load_source(library, path, skill, out, interpolated, e);
    library->character_loading = false; return okay;
}
bool qa_bot_character_load(qa_bot_library *library, const char *path, float skill,
                           qa_bot_character **out, qa_error *e) {
    bool interpolated;
    return bot_character_load(library, path, skill, out, &interpolated, e);
}
static const qa_bot_character_value *get(const qa_bot_character *c, uint32_t index, qa_error *e) {
    if (c == NULL || index >= 80) {
        qa_error_set(e, QA_ERROR_ARGUMENT, index, "Character characteristic is outside 0..79");
        return NULL;
    }
    qa_bot_character_value *v = ((qa_bot_character *)c)->view.values + index;
    qa_bot_character_value_kind kind;
    if (!bot_character_kind((qa_bot_character *)c, index, &kind, e)) return NULL;
    *v = (qa_bot_character_value){.kind = kind};
    if ((kind == QA_BOT_CHARACTER_INTEGER || kind == QA_BOT_CHARACTER_FLOAT) &&
        !bot_character_value((qa_bot_character *)c, index, v, e)) return NULL;
    if (v->kind == QA_BOT_CHARACTER_UNSET) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, index, "Character characteristic is unset");
        return NULL;
    }
    return v;
}
bool qa_bot_character_float(const qa_bot_character *c, uint32_t index, float *out, qa_error *e) {
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, index, "Missing characteristic float output");
        return false;
    }
    *out = 0;
    const qa_bot_character_value *v = get(c, index, e);
    if (v == NULL)
        return false;
    if (v->kind == QA_BOT_CHARACTER_FLOAT)
        *out = v->data.number;
    else if (v->kind == QA_BOT_CHARACTER_INTEGER)
        *out = (float)v->data.integer;
    else {
        qa_error_set(e, QA_ERROR_FORMAT, index, "Character characteristic is not numeric");
        return false;
    }
    return true;
}
bool qa_bot_character_integer(const qa_bot_character *c, uint32_t index, int32_t *out,
                              qa_error *e) {
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, index, "Missing characteristic integer output");
        return false;
    }
    *out = 0;
    const qa_bot_character_value *v = get(c, index, e);
    if (v == NULL)
        return false;
    if (v->kind == QA_BOT_CHARACTER_INTEGER)
        *out = v->data.integer;
    else if (v->kind == QA_BOT_CHARACTER_FLOAT) {
        double number =
            isfinite(v->data.number) ? fmod(trunc((double)v->data.number), 4294967296.0) : 0;
        if (number < 0)
            number += 4294967296.0;
        uint32_t word = (uint32_t)number;
        memcpy(out, &word, sizeof(word));
    } else {
        qa_error_set(e, QA_ERROR_FORMAT, index,
                     "Character characteristic cannot convert to signed integer");
        return false;
    }
    return true;
}
bool qa_bot_character_string(const qa_bot_character *c, uint32_t index, const char **out,
                             qa_error *e) {
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, index, "Missing characteristic string output");
        return false;
    }
    const qa_bot_character_value *v = get(c, index, e);
    if (v == NULL)
        return false;
    if (v->kind != QA_BOT_CHARACTER_STRING) {
        qa_error_set(e, QA_ERROR_FORMAT, index, "Character characteristic is not a string");
        return false;
    }
    qa_bot_character_value value;
    if (!bot_character_value((qa_bot_character *)c, index, &value, e)) return false;
    *out = value.data.string;
    return true;
}
bool qa_bot_character_bounded_float(const qa_bot_character *c, uint32_t index, float minimum,
                                    float maximum, float *out, qa_error *e) {
    if (!isfinite(minimum) || !isfinite(maximum) || minimum > maximum) {
        qa_error_set(e, QA_ERROR_ARGUMENT, index, "Invalid characteristic float bounds");
        return false;
    }
    float value;
    if (!qa_bot_character_float(c, index, &value, e))
        return false;
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, index, "Missing characteristic float output");
        return false;
    }
    *out = value < minimum ? minimum : value > maximum ? maximum : value;
    return true;
}
bool qa_bot_character_bounded_integer(const qa_bot_character *c, uint32_t index, int32_t minimum,
                                      int32_t maximum, int32_t *out, qa_error *e) {
    if (minimum > maximum || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, index, "Invalid characteristic integer bounds/output");
        return false;
    }
    int32_t value;
    if (!qa_bot_character_integer(c, index, &value, e))
        return false;
    *out = value < minimum ? minimum : value > maximum ? maximum : value;
    return true;
}
