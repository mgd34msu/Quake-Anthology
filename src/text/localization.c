#include "qa/localization.h"
#include "qa/strings.h"
#include "qa/text.h"
#include "qa/caption_save.h"
#include "save_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct loc_entry {
    qa_localization_entry value;
    size_t layer;
} loc_entry;
struct qa_localization {
    size_t references, count, capacity;
    qa_strings *keys;
    loc_entry *entries;
};
typedef struct loc_cache {
    struct loc_cache *next;
    qa_localization *catalog;
    qa_sha256_digest key;
} loc_cache;
struct qa_localization_pool {
    loc_cache *first;
};
size_t qa_localization_pool_count(const qa_localization_pool *pool) {
    size_t count = 0;
    if (pool) for (const loc_cache *row = pool->first; row; row = row->next) ++count;
    return count;
}
typedef struct loc_reader {
    qa_bytes bytes;
    size_t at;
} loc_reader;

static unsigned lower(unsigned c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; }
static bool equal_folded(const char *a, const char *b) {
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}
static size_t prefix(const char *s, size_t length, size_t available, bool raw) {
    size_t n = length < available ? length : available;
    if (!raw && n < length)
        while (n && ((unsigned char)s[n] & 0xc0u) == 0x80u)
            --n;
    return n;
}
static size_t append(char *out, size_t used, size_t capacity, const char *s, size_t length,
                     bool raw) {
    if (!capacity)
        return 0;
    size_t n = prefix(s, length, capacity - 1 - used, raw);
    memcpy(out + used, s, n);
    out[used + n] = 0;
    return used + n;
}
static unsigned peek(loc_reader *reader, size_t ahead) {
    return ahead < reader->bytes.size - reader->at ? reader->bytes.data[reader->at + ahead] : 0;
}
static void token(loc_reader *reader, char *out, size_t capacity, bool escapes) {
    size_t used = 0;
    for (;;) {
        while (peek(reader, 0) && peek(reader, 0) <= 32)
            ++reader->at;
        if (peek(reader, 0) != '/' || (peek(reader, 1) != '/' && peek(reader, 1) != '*'))
            break;
        bool line = peek(reader, 1) == '/';
        reader->at += 2;
        while (peek(reader, 0)) {
            if (line && peek(reader, 0) == '\n')
                break;
            if (!line && peek(reader, 0) == '*' && peek(reader, 1) == '/') {
                reader->at += 2;
                break;
            }
            ++reader->at;
        }
    }
    if (peek(reader, 0) == '=') {
        ++reader->at;
        out[0] = '=';
        out[1] = 0;
        return;
    }
    bool quoted = peek(reader, 0) == '"';
    if (quoted)
        ++reader->at;
    while (peek(reader, 0)) {
        unsigned c = peek(reader, 0);
        if (quoted && c == '"') {
            ++reader->at;
            break;
        }
        if (!quoted && (c <= 32 || c == '='))
            break;
        ++reader->at;
        if (escapes && c == '\\') {
            if (!peek(reader, 0))
                break;
            c = peek(reader, 0);
            ++reader->at;
            if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
        }
        if (used + 1 < capacity)
            out[used++] = (char)c;
    }
    /* The source limits are UTF-8 bytes, independent of native locale. */
    if (used == capacity - 1 && used) {
        size_t begin = used;
        while (begin && ((unsigned char)out[begin - 1] & 0xc0u) == 0x80u)
            --begin;
        if (begin) {
            unsigned c = (unsigned char)out[begin - 1];
            size_t need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
            if (need > used - begin + 1)
                used = begin - 1;
        }
    }
    out[used] = 0;
}
static const char *parse_format(const char *text, qa_localization_entry *out) {
    size_t length = strlen(text), at = 0;
    int sequential = 0;
    out->argument_count = 0;
    while (at < length) {
        if (text[at++] != '{')
            continue;
        size_t start = at - 1;
        if (text[at] == '{')
            continue;
        if (out->argument_count == 8)
            return "Too many localization arguments";
        unsigned index = 0;
        size_t digits = at;
        while (text[at] >= '0' && text[at] <= '9')
            index = (index * 10u + (unsigned)(text[at++] - '0')) & 255u;
        if (at == digits) {
            if (sequential < 0)
                return "Sequential argument follows positional argument";
            index = (unsigned)sequential++;
        } else {
            if (sequential > 0)
                return "Positional argument follows sequential argument";
            sequential = -1;
        }
        bool closed = false;
        while (at < length) {
            if (text[at++] != '}')
                continue;
            if (text[at] == '}') {
                ++at;
                continue;
            }
            closed = true;
            break;
        }
        if (!closed)
            return "Unterminated localization argument";
        out->arguments[out->argument_count++] = (qa_localization_argument){
            .start = (uint16_t)start, .end = (uint16_t)at, .index = (uint8_t)index};
    }
    return NULL;
}
static bool insert(qa_localization *catalog, const char *key, const char *format,
                   qa_localization_entry *parsed, size_t layer, bool replace, qa_error *error) {
    qa_string_id id = qa_strings_find(catalog->keys, (qa_bytes){(const uint8_t *)key, strlen(key)});
    if (id && !replace && catalog->entries[id - 1].layer == layer)
        return true;
    if (!id && !qa_strings_intern_cstr(catalog->keys, key, &id, error))
        return false;
    if (id > catalog->capacity) {
        size_t capacity = catalog->capacity ? catalog->capacity * 2 : 64;
        if (capacity < id || capacity > SIZE_MAX / sizeof(*catalog->entries)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Localization table is too large");
            return false;
        }
        loc_entry *next = realloc(catalog->entries, capacity * sizeof(*next));
        if (!next) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating localization entries");
            return false;
        }
        memset(next + catalog->capacity, 0, (capacity - catalog->capacity) * sizeof(*next));
        catalog->entries = next;
        catalog->capacity = capacity;
    }
    size_t length = strlen(format);
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating localization format");
        return false;
    }
    memcpy(copy, format, length + 1);
    loc_entry *entry = &catalog->entries[id - 1];
    if (!entry->value.format)
        ++catalog->count;
    free((char *)entry->value.format);
    entry->value = *parsed;
    entry->value.format = copy;
    entry->layer = layer;
    return true;
}
static bool parse_layer(qa_localization *catalog, qa_bytes bytes, size_t layer,
                        const qa_localization_options *options, qa_error *error) {
    loc_reader reader = {.bytes = bytes};
    /* TextDecoder discards the optional leading UTF-8 BOM. */
    if (bytes.size >= 3 && !memcmp(bytes.data, "\xef\xbb\xbf", 3))
        reader.at = 3;
    for (;;) {
        char key[64], text[1024];
        token(&reader, key, sizeof(key), false);
        if (!key[0])
            return true;
        token(&reader, text, sizeof(text), false);
        bool platform = text[0] == '<', matches = false;
        if (platform) {
            bool last;
            do {
                size_t n = strlen(text);
                last = n && text[n - 1] == '>';
                if (last)
                    text[--n] = 0;
                const char *tag = text + (text[0] == '<');
                if (options->platform && equal_folded(tag, options->platform))
                    matches = true;
                if (!last)
                    token(&reader, text, sizeof(text), false);
            } while (!last && text[0]);
            token(&reader, text, sizeof(text), false);
        }
        if (strcmp(text, "="))
            return true;
        token(&reader, text, sizeof(text), true);
        qa_localization_entry parsed = {0};
        const char *problem = parse_format(text, &parsed);
        if (problem) {
            if (options->warning)
                options->warning(options->context, key, problem);
            continue;
        }
        if (platform && !matches)
            continue;
        if (!insert(catalog, key, text, &parsed, layer,
                    platform || options->profile == QA_LOCALIZATION_Q2_RERELEASE, error))
            return false;
    }
}
bool qa_localization_create(const qa_bytes *layers, size_t count,
                            const qa_localization_options *options, qa_localization **out,
                            qa_error *error) {
    qa_localization_options defaults = {0};
    if (!options)
        options = &defaults;
    if (!out || (count && !layers) ||
        (options->profile != QA_LOCALIZATION_Q1_RERELEASE &&
         options->profile != QA_LOCALIZATION_Q2_RERELEASE)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid localization catalog request");
        return false;
    }
    qa_localization *catalog = calloc(1, sizeof(*catalog));
    if (!catalog) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating localization catalog");
        return false;
    }
    catalog->references = 1;
    if (!qa_strings_create(&catalog->keys, error)) {
        free(catalog);
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        qa_buffer text = {0};
        if (!qa_utf8_repair(layers[i], &text, error)) {
            qa_localization_release(catalog);
            return false;
        }
        bool ok = parse_layer(catalog, (qa_bytes){text.data, text.size}, i + 1, options, error);
        qa_buffer_free(&text);
        if (!ok) {
            qa_localization_release(catalog);
            return false;
        }
    }
    *out = catalog;
    return true;
}
void qa_localization_retain(qa_localization *catalog) {
    if (catalog)
        ++catalog->references;
}
void qa_localization_release(qa_localization *catalog) {
    if (!catalog || --catalog->references)
        return;
    for (size_t i = 0; i < catalog->capacity; ++i)
        free((char *)catalog->entries[i].value.format);
    free(catalog->entries);
    qa_strings_destroy(catalog->keys);
    free(catalog);
}
size_t qa_localization_count(const qa_localization *catalog) {
    return catalog ? catalog->count : 0;
}
const qa_localization_entry *qa_localization_find(const qa_localization *catalog, const char *key) {
    if (!catalog || !key)
        return NULL;
    qa_string_id id = qa_strings_find(catalog->keys, (qa_bytes){(const uint8_t *)key, strlen(key)});
    return id ? &catalog->entries[id - 1].value : NULL;
}
static size_t localize(const qa_localization *catalog, const char *base, const char *const *arguments,
                   size_t argument_count, bool allow_in_place, bool raw_bytes, bool preserve_unknown,
                   char *out, size_t capacity) {
    if (!capacity)
        return 0;
    out[0] = 0;
    const qa_localization_entry *entry = NULL;
    qa_localization_entry parsed = {0};
    char format[1024];
    const char *original = base;
    if (*base == '$')
        entry = qa_localization_find(catalog, ++base);
    else if (allow_in_place) {
        bool has_args = false;
        for (size_t i = 0; base[i]; ++i)
            if (base[i] == '{') {
                if (!base[++i])
                    break;
                if (base[i] != '{') {
                    has_args = true;
                    break;
                }
            }
        if (has_args) {
            append(format, 0, sizeof(format), base, strlen(base), false);
            if (!parse_format(format, &parsed)) {
                parsed.format = format;
                entry = &parsed;
            }
        }
    }
    if (!entry) {
        const char *text = preserve_unknown ? original : base;
        return append(out, 0, capacity, text, strlen(text), raw_bytes);
    }
    if (!entry->argument_count)
        return append(out, 0, capacity, entry->format, strlen(entry->format), raw_bytes);
    for (size_t i = 0; i < entry->argument_count; ++i)
        if (entry->arguments[i].index >= argument_count)
            return append(out, 0, capacity, base, strlen(base), raw_bytes);
    for (size_t i = 0; i < argument_count; ++i)
        if (!arguments || !arguments[i])
            return append(out, 0, capacity, base, strlen(base), raw_bytes);
    size_t used = 0, at = 0;
    for (size_t i = 0; i < entry->argument_count; ++i) {
        qa_localization_argument arg = entry->arguments[i];
        used = append(out, used, capacity, entry->format + at, arg.start - at, raw_bytes);
        char value[1024];
        size_t n =
            localize(catalog, arguments[arg.index], NULL, 0, false, false, preserve_unknown, value, sizeof(value));
        used = append(out, used, capacity, value, n, raw_bytes);
        at = arg.end;
    }
    return append(out, used, capacity, entry->format + at, strlen(entry->format + at), raw_bytes);
}
size_t qa_localize(const qa_localization *catalog, const char *base, const char *const *arguments,
    size_t argument_count, bool allow_in_place, bool raw_bytes, char *out, size_t capacity)
{ return localize(catalog, base, arguments, argument_count, allow_in_place, raw_bytes, false, out, capacity); }
size_t qa_localize_presentation(const qa_localization *catalog, const char *base,
    const char *const *arguments, size_t argument_count, bool allow_in_place, char *out, size_t capacity)
{ return localize(catalog, base, arguments, argument_count, allow_in_place, false, true, out, capacity); }
const char *qa_localization_language(const char *locale) {
    static const char *const codes[] = {"en", "fr", "de", "it", "ru", "es"};
    static const char *const names[] = {"english", "french",  "german",
                                        "italian", "russian", "spanish"};
    if (!locale || !locale[0] || !locale[1] || (locale[2] && !strchr(".@-_", locale[2])))
        return names[0];
    for (size_t i = 0; i < 6; ++i)
        if (lower((unsigned char)locale[0]) == (unsigned)codes[i][0] &&
            lower((unsigned char)locale[1]) == (unsigned)codes[i][1])
            return names[i];
    return names[0];
}
bool qa_localization_language_valid(const char *language) {
    return language && *language &&
        strspn(language,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==strlen(language);
}
qa_localization_pool *qa_localization_pool_create(qa_error *error) {
    qa_localization_pool *pool = calloc(1, sizeof(*pool));
    if (!pool)
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating localization pool");
    return pool;
}
void qa_localization_pool_destroy(qa_localization_pool *pool) {
    if (!pool)
        return;
    while (pool->first) {
        loc_cache *next = pool->first->next;
        qa_localization_release(pool->first->catalog);
        free(pool->first);
        pool->first = next;
    }
    free(pool);
}
void qa_localization_pool_trim(qa_localization_pool *pool) {
    for (loc_cache **link = &pool->first; *link;) {
        loc_cache *entry = *link;
        if (entry->catalog->references == 1) {
            *link = entry->next;
            qa_localization_release(entry->catalog);
            free(entry);
        } else
            link = &entry->next;
    }
}
bool qa_localization_acquire(qa_localization_pool *pool, qa_vfs *view, const char *language,
                             const qa_localization_options *options, qa_localization **out,
                             qa_error *error) {
    qa_localization_options defaults = {0};
    if (!options)
        options = &defaults;
    if (!pool || !view || !qa_localization_language_valid(language) || !out ||
        (options->profile != QA_LOCALIZATION_Q1_RERELEASE &&
         options->profile != QA_LOCALIZATION_Q2_RERELEASE)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid localization resource language");
        return false;
    }
    qa_resource *resources[4] = {0};
    qa_bytes layers[4] = {0};
    uint8_t identity[1 + 32 + 4 * 33] = {0};
    identity[0] = (uint8_t)((unsigned)options->profile | (options->platform ? 2u : 0u));
    const char *platform = options->platform ? options->platform : "";
    size_t n = strlen(platform);
    char *folded = malloc(n + 1);
    if (!folded) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating localization platform");
        return false;
    }
    for (size_t i = 0; i <= n; ++i)
        folded[i] = (char)lower((unsigned char)platform[i]);
    qa_sha256_digest platform_digest;
    qa_sha256((qa_bytes){(const uint8_t *)folded, n}, &platform_digest);
    memcpy(identity + 1, &platform_digest, 32);
    free(folded);
    size_t count = !strcmp(language, "english") ? 2 : 4;
    bool ok = true;
    for (size_t i = 0; i < count; ++i) {
        const char *name=i<2?"english":language;
        size_t length=strlen(name);
        if (length>SIZE_MAX-sizeof("localization/loc__mod.txt")) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Localization resource path exceeds address space");
            ok=false; break;
        }
        size_t capacity=length+sizeof("localization/loc__mod.txt");
        char *path=malloc(capacity);
        if (!path) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating actual localization resource path");
            ok=false; break;
        }
        snprintf(path,capacity,"localization/loc_%s%s.txt",name,i&1?"_mod":"");
        qa_error missing = {0};
        bool acquired=qa_vfs_acquire(view,path,&resources[i],NULL,&missing);
        free(path);
        if (!acquired) {
            if (missing.code == QA_ERROR_NOT_FOUND)
                continue;
            if (error)
                *error = missing;
            ok = false;
            break;
        }
        layers[i] = qa_resource_bytes(resources[i]);
        identity[33 + i * 33] = 1;
        memcpy(identity + 34 + i * 33, qa_resource_digest(resources[i]), 32);
    }
    qa_sha256_digest key;
    qa_sha256((qa_bytes){identity, sizeof(identity)}, &key);
    if (ok) {
        loc_cache *entry;
        for (entry = pool->first; entry; entry = entry->next)
            if (qa_sha256_equal(&key, &entry->key))
                break;
        if (!entry) {
            entry = calloc(1, sizeof(*entry));
            if (!entry) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating localization cache entry");
                ok = false;
            } else if (!qa_localization_create(layers, count, options, &entry->catalog, error)) {
                free(entry);
                ok = false;
            } else {
                entry->key = key;
                entry->next = pool->first;
                pool->first = entry;
            }
        }
        if (ok) {
            qa_localization_retain(entry->catalog);
            *out = entry->catalog;
        }
    }
    for (size_t i = 0; i < 4; ++i)
        qa_resource_release(resources[i]);
    return ok;
}
bool qa_localization_pool_catalog_key(const qa_localization_pool *pool, const qa_localization *catalog, uint64_t *out)
{
    if (!pool || !out) return false;
    if (!catalog) { *out = 0; return true; }
    uint64_t key = 1;
    for (const loc_cache *row = pool->first; row; row = row->next, ++key)
        if (row->catalog == catalog) { *out = key; return true; }
    return false;
}
qa_localization *qa_localization_pool_catalog(const qa_localization_pool *pool, uint64_t key)
{
    if (!pool || !key) return NULL;
    const loc_cache *row = pool->first;
    while (row && --key) row = row->next;
    return row ? row->catalog : NULL;
}
static bool compiled_fields(qa_source_save_io *io, qa_localization *catalog)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = catalog->count, capacity = catalog->capacity;
    if (!qa_source_save_count(io, &count, reading ? (io->input.size - io->offset) / 36 : SIZE_MAX / sizeof(loc_entry)) ||
        !qa_source_save_count(io, &capacity, SIZE_MAX / sizeof(loc_entry)) || capacity < count) return false;
    for (size_t i = 0; i < capacity; ++i) { uint8_t zero = 0; if (!qa_source_save_u8(io, &zero) || zero) return false; }
    if (reading) {
        if (!qa_strings_create(&catalog->keys, io->error)) return false;
        catalog->entries = capacity ? calloc(capacity, sizeof(*catalog->entries)) : NULL;
        if (capacity && !catalog->entries) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring compiled localization table"); return false; }
        catalog->capacity = capacity;
    }
    for (size_t i = 0; i < count; ++i) {
        char *key = reading ? NULL : (char *)qa_strings_cstr(catalog->keys, (qa_string_id)i + 1);
        char *format = reading ? NULL : (char *)catalog->entries[i].value.format;
        loc_entry entry = reading ? (loc_entry){0} : catalog->entries[i];
        bool ok = qa_text_save_owned(io, &key) && key && *key && qa_text_save_owned(io, &format) && format &&
            qa_source_save_count(io, &entry.layer, SIZE_MAX) && qa_source_save_u8(io, &entry.value.argument_count) && entry.value.argument_count <= 8;
        size_t end = 0, length = format ? strlen(format) : 0;
        for (unsigned j = 0; ok && j < 8; ++j) {
            qa_localization_argument *argument = &entry.value.arguments[j];
            ok = qa_source_save_u16(io, &argument->start) && qa_source_save_u16(io, &argument->end) && qa_source_save_u8(io, &argument->index);
            if (ok && j < entry.value.argument_count) {
                ok = argument->start >= end && argument->start < argument->end && argument->end <= length;
                end = argument->end;
            }
        }
        if (ok && reading) {
            qa_string_id id = 0;
            ok = !qa_strings_find(catalog->keys, (qa_bytes){(const uint8_t *)key, strlen(key)}) &&
                qa_strings_intern_cstr(catalog->keys, key, &id, io->error) && id == i + 1;
            if (ok) {
                entry.value.format = format; format = NULL;
                catalog->entries[i] = entry; ++catalog->count;
            }
        }
        if (reading) { free(key); free(format); }
        if (!ok) return false;
    }
    return true;
}
static bool pool_fields(qa_source_save_io *io, qa_localization_pool *pool)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = qa_localization_pool_count(pool);
    if (!qa_text_save_header(io, "QLOC") ||
        !qa_source_save_count(io, &count, reading ? (io->input.size - io->offset) / 48 : SIZE_MAX)) return false;
    loc_cache **link = &pool->first;
    for (size_t i = 0; i < count; ++i) {
        loc_cache *entry = *link;
        if (reading) {
            entry = calloc(1, sizeof(*entry));
            if (!entry) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring localization cache entry"); return false; }
            *link = entry;
            entry->catalog = calloc(1, sizeof(*entry->catalog));
            if (!entry->catalog) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring compiled localization owner"); return false; }
            entry->catalog->references = 1;
        }
        if (!qa_source_save_bytes(io, entry->key.bytes, sizeof(entry->key.bytes)) || !compiled_fields(io, entry->catalog)) return false;
        for (loc_cache *prior = pool->first; prior != entry; prior = prior->next)
            if (qa_sha256_equal(&prior->key, &entry->key)) return false;
        link = &entry->next;
    }
    return true;
}
bool qa_localization_pool_checkpoint(const qa_localization_pool *pool, qa_buffer *out, qa_error *error)
{
    if (!pool || !out || out->data || out->size) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Localization capture requires its actual owner and empty output"); return false; }
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && pool_fields(&io, (qa_localization_pool *)pool) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid actual localization pool");
    return ok;
}
bool qa_localization_pool_restore(qa_localization_pool *pool, qa_bytes bytes, qa_error *error)
{
    if (!pool || pool->first) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Localization restore requires its empty actual pool"); return false; }
    qa_localization_pool *candidate = qa_localization_pool_create(error);
    if (!candidate) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && pool_fields(&io, candidate) && qa_source_save_finish(&io, NULL);
    if (ok) { pool->first = candidate->first; candidate->first = NULL; }
    qa_source_save_dispose(&io); qa_localization_pool_destroy(candidate);
    if (!ok && error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid saved localization pool");
    return ok;
}
