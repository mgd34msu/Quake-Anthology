/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/strings.h"
#include "qa/arena.h"

#include <stdlib.h>
#include <string.h>

typedef struct string_entry { qa_bytes text; uint64_t hash; } string_entry;
struct qa_strings {
    qa_arena arena;
    string_entry *entries;
    qa_string_id *buckets;
    size_t count, capacity, bucket_count;
};

static uint64_t string_hash(qa_bytes text) {
    uint64_t hash=UINT64_C(14695981039346656037);
    for (size_t i=0;i<text.size;++i) { hash^=text.data[i]; hash*=UINT64_C(1099511628211); }
    return hash;
}

bool qa_strings_create(qa_strings **out, qa_error *error) {
    if (!out) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"missing string table output"); return false; }
    qa_strings *strings=calloc(1,sizeof(*strings));
    if (!strings) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating shared string table"); return false; }
    qa_arena_init(&strings->arena,16384);
    *out=strings; return true;
}
void qa_strings_destroy(qa_strings *strings) {
    if (!strings) return;
    qa_arena_destroy(&strings->arena);
    free(strings->entries); free(strings->buckets); free(strings);
}

static qa_string_id find(const qa_strings *strings, qa_bytes text, uint64_t hash, size_t *vacant) {
    if (!strings->bucket_count) return QA_STRING_NONE;
    size_t position=(size_t)hash & (strings->bucket_count-1);
    while (strings->buckets[position]) {
        qa_string_id id=strings->buckets[position];
        const string_entry *entry=&strings->entries[id-1u];
        if (entry->hash==hash && entry->text.size==text.size &&
            (!text.size || !memcmp(entry->text.data,text.data,text.size))) return id;
        position=(position+1)&(strings->bucket_count-1);
    }
    if (vacant) *vacant=position;
    return QA_STRING_NONE;
}

qa_string_id qa_strings_find(const qa_strings *strings, qa_bytes text) {
    if (!strings || (!text.data && text.size)) return QA_STRING_NONE;
    return find(strings,text,string_hash(text),NULL);
}

static bool reserve(qa_strings *strings, qa_error *error) {
    if (strings->count==UINT32_MAX) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"string identity space exhausted"); return false;
    }
    if (strings->count==strings->capacity) {
        size_t capacity=strings->capacity ? strings->capacity*2 : 64;
        if (capacity<strings->capacity || capacity>UINT32_MAX) capacity=UINT32_MAX;
        if (capacity>SIZE_MAX/sizeof(*strings->entries)) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"string table capacity overflow"); return false;
        }
        string_entry *entries=realloc(strings->entries,capacity*sizeof(*entries));
        if (!entries) { qa_error_set(error,QA_ERROR_MEMORY,0,"growing string table"); return false; }
        strings->entries=entries; strings->capacity=capacity;
    }
    /* Keep one empty bucket and bounded probes without division in lookup. */
    if (!strings->bucket_count || strings->count>=strings->bucket_count/2) {
        size_t count=strings->bucket_count ? strings->bucket_count*2 : 128;
        if (count<strings->bucket_count || count>SIZE_MAX/sizeof(*strings->buckets)) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"string hash capacity overflow"); return false;
        }
        qa_string_id *buckets=calloc(count,sizeof(*buckets));
        if (!buckets) { qa_error_set(error,QA_ERROR_MEMORY,0,"growing string index"); return false; }
        for (size_t i=0;i<strings->count;++i) {
            size_t position=(size_t)strings->entries[i].hash&(count-1);
            while (buckets[position]) position=(position+1)&(count-1);
            buckets[position]=(qa_string_id)(i+1);
        }
        free(strings->buckets); strings->buckets=buckets; strings->bucket_count=count;
    }
    return true;
}

bool qa_strings_intern(qa_strings *strings, qa_bytes text, qa_string_id *out, qa_error *error) {
    if (!strings || !out || (!text.data && text.size) || text.size==SIZE_MAX) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"invalid interned string input"); return false;
    }
    uint64_t hash=string_hash(text);
    qa_string_id existing=find(strings,text,hash,NULL);
    if (existing) { *out=existing; return true; }
    if (!reserve(strings,error)) return false;
    uint8_t *copy=qa_arena_alloc(&strings->arena,text.size+1,1,error);
    if (!copy) return false;
    if (text.size) memcpy(copy,text.data,text.size);
    copy[text.size]=0;
    size_t bucket=0;
    (void)find(strings,text,hash,&bucket);
    strings->entries[strings->count]=(string_entry){{copy,text.size},hash};
    qa_string_id id=(qa_string_id)++strings->count;
    strings->buckets[bucket]=id;
    *out=id; return true;
}
bool qa_strings_intern_cstr(qa_strings *strings, const char *text, qa_string_id *out, qa_error *error) {
    if (!text) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"missing string text"); return false; }
    return qa_strings_intern(strings,(qa_bytes){(const uint8_t *)text,strlen(text)},out,error);
}
qa_bytes qa_strings_text(const qa_strings *strings, qa_string_id id) {
    return strings && id && (size_t)id<=strings->count ? strings->entries[id-1u].text : (qa_bytes){0};
}
const char *qa_strings_cstr(const qa_strings *strings, qa_string_id id) {
    qa_bytes text=qa_strings_text(strings,id);
    if (!text.data || (text.size && memchr(text.data,0,text.size))) return NULL;
    return (const char *)text.data;
}
size_t qa_strings_count(const qa_strings *strings) { return strings ? strings->count : 0; }
