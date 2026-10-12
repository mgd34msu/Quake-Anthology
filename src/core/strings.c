#include "qa/strings.h"
#include "qa/arena.h"
#include "qa/text.h"

#include <stdlib.h>
#include <string.h>

typedef struct string_entry { qa_bytes text; uint64_t hash; uint32_t name_flags; } string_entry;
typedef struct bound_names {
    struct bound_names *next;
    const char *const *names;
    size_t count;
    qa_string_id ids[];
} bound_names;
struct qa_strings {
    qa_arena arena;
    bound_names *bound;
    string_entry *entries;
    qa_string_id *buckets;
    size_t count, capacity, bucket_count, references;
    qa_string_id byte_names[256];
    bool bytes_bound;
};

static uint8_t string_byte(uint8_t byte, bool ascii_lower) {
    return ascii_lower ? qa_text_ascii_lower(byte) : byte;
}

static uint64_t string_hash(qa_bytes text, bool ascii_lower) {
    uint64_t hash=UINT64_C(14695981039346656037);
    for (size_t i=0;i<text.size;++i) { hash^=string_byte(text.data[i],ascii_lower); hash*=UINT64_C(1099511628211); }
    return hash;
}

bool qa_strings_create(qa_strings **out, qa_error *error) {
    if (!out) { qa_error_set(error,QA_ERROR_ARGUMENT,0,"missing string table output"); return false; }
    qa_strings *strings=calloc(1,sizeof(*strings));
    if (!strings) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating shared string table"); return false; }
    qa_arena_init(&strings->arena,16384);
    strings->references=1;
    *out=strings; return true;
}
void qa_strings_retain(qa_strings *strings) {
    if (strings) ++strings->references;
}
bool qa_strings_bind_bytes(qa_strings *strings, const qa_string_id **out, qa_error *error) {
    if (!strings->bytes_bound) {
        for (unsigned i=0;i<256;++i) {
            uint8_t byte=(uint8_t)i;
            if (!qa_strings_intern(strings,(qa_bytes){&byte,i?1:0},strings->byte_names+i,error)) return false;
        }
        strings->bytes_bound=true;
    }
    *out=strings->byte_names;
    return true;
}
void qa_strings_destroy(qa_strings *strings) {
    if (!strings || --strings->references) return;
    while (strings->bound) { bound_names *row=strings->bound; strings->bound=row->next; free(row); }
    qa_arena_destroy(&strings->arena);
    free(strings->entries); free(strings->buckets); free(strings);
}

static qa_string_id find(const qa_strings *strings, qa_bytes text, uint64_t hash,
    bool ascii_lower, size_t *vacant) {
    if (!strings->bucket_count) return QA_STRING_NONE;
    size_t position=(size_t)hash & (strings->bucket_count-1);
    while (strings->buckets[position]) {
        qa_string_id id=strings->buckets[position];
        const string_entry *entry=&strings->entries[id-1u];
        if (entry->hash==hash && entry->text.size==text.size) {
            if (!ascii_lower) {
                if (!text.size || !memcmp(entry->text.data,text.data,text.size)) return id;
            } else {
                size_t i=0;
                while (i<text.size && entry->text.data[i]==string_byte(text.data[i],true)) ++i;
                if (i==text.size) return id;
            }
        }
        position=(position+1)&(strings->bucket_count-1);
    }
    if (vacant) *vacant=position;
    return QA_STRING_NONE;
}

qa_string_id qa_strings_find(const qa_strings *strings, qa_bytes text) {
    if (!strings || (!text.data && text.size)) return QA_STRING_NONE;
    return find(strings,text,string_hash(text,false),false,NULL);
}

qa_string_id qa_strings_find_ascii_lower(const qa_strings *strings, qa_bytes text) {
    if (!strings || (!text.data && text.size)) return QA_STRING_NONE;
    return find(strings,text,string_hash(text,true),true,NULL);
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

static bool intern(qa_strings *strings, qa_bytes text, bool ascii_lower,
    qa_string_id *out, qa_error *error) {
    if (!strings || !out || (!text.data && text.size) || text.size==SIZE_MAX) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"invalid interned string input"); return false;
    }
    uint64_t hash=string_hash(text,ascii_lower);
    qa_string_id existing=find(strings,text,hash,ascii_lower,NULL);
    if (existing) { *out=existing; return true; }
    if (!reserve(strings,error)) return false;
    uint8_t *copy=qa_arena_alloc(&strings->arena,text.size+1,1,error);
    if (!copy) return false;
    if (ascii_lower) {
        for (size_t i=0;i<text.size;++i) copy[i]=string_byte(text.data[i],true);
    } else if (text.size) memcpy(copy,text.data,text.size);
    copy[text.size]=0;
    size_t bucket=0;
    text=(qa_bytes){copy,text.size};
    (void)find(strings,text,hash,false,&bucket);
    uint32_t flags=0;
    if(text.size&&text.data[0]=='*') flags|=QA_NAME_SEXED_SOUND;
    if(text.size>=8&&!memcmp(text.data,"monster_",8)) flags|=QA_NAME_MONSTER;
    if(text.size>=12&&!memcmp(text.data,"info_player_",12)) flags|=QA_NAME_PLAYER_START;
    if(text.size>=10&&!memcmp(text.data,"item_flag_",10)) flags|=QA_NAME_ITEM_FLAG;
    if(text.size>=9&&!memcmp(text.data,"info_flag",9)) flags|=QA_NAME_INFO_FLAG;
    if(text.size>=4&&!memcmp(text.data,"func",4)) flags|=QA_NAME_FUNC;
    if(text.size>=8&&!memcmp(text.data,"q1:ammo/",8)) flags|=QA_NAME_Q1_AMMO;
    static const struct { const char *text; size_t size; uint32_t folded, exact; } names[] = {
        {"red", 3, QA_NAME_TEAM_RED_ALIAS, QA_NAME_TEAM_RED_EXACT},
        {"team:red", 8, QA_NAME_TEAM_RED_ALIAS, QA_NAME_TEAM_RED_EXACT},
        {"blue", 4, QA_NAME_TEAM_BLUE_ALIAS, QA_NAME_TEAM_BLUE_EXACT},
        {"team:blue", 9, QA_NAME_TEAM_BLUE_ALIAS, QA_NAME_TEAM_BLUE_EXACT},
        {"5", 1, QA_NAME_TEAM_RED_ALIAS, 0}, {"14", 2, QA_NAME_TEAM_BLUE_ALIAS, 0},
        {"q2:1", 4, QA_NAME_TEAM_RED_ALIAS, 0}, {"q3:1", 4, QA_NAME_TEAM_RED_ALIAS, 0},
        {"q2:2", 4, QA_NAME_TEAM_BLUE_ALIAS, 0}, {"q3:2", 4, QA_NAME_TEAM_BLUE_ALIAS, 0},
        {"q2", 2, QA_NAME_TEAM_COLOR_NAMESPACE, 0}, {"q3", 2, QA_NAME_TEAM_COLOR_NAMESPACE, 0},
        {"team_ctf_redflag", sizeof("team_ctf_redflag") - 1, QA_NAME_Q3_RED_FLAG_BASE, 0},
        {"team_ctf_blueflag", sizeof("team_ctf_blueflag") - 1, QA_NAME_Q3_BLUE_FLAG_BASE, 0},
        {"team_redobelisk", sizeof("team_redobelisk") - 1, QA_NAME_Q3_RED_OBELISK, 0},
        {"team_blueobelisk", sizeof("team_blueobelisk") - 1, QA_NAME_Q3_BLUE_OBELISK, 0},
        {"team_neutralobelisk", sizeof("team_neutralobelisk") - 1, QA_NAME_Q3_NEUTRAL_OBELISK, 0}
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        qa_bytes candidate = {(const uint8_t *)names[i].text, names[i].size};
        if (qa_text_bytes_equal(text, candidate, true)) {
            flags |= names[i].folded;
            if (names[i].exact && qa_text_bytes_equal(text, candidate, false)) flags |= names[i].exact;
            break;
        }
    }
    if(text.size>=4 && text.data[text.size-4]=='.' &&
        (text.data[text.size-3]=='b'||text.data[text.size-3]=='B') &&
        (text.data[text.size-2]=='s'||text.data[text.size-2]=='S') &&
        (text.data[text.size-1]=='p'||text.data[text.size-1]=='P')) flags|=QA_NAME_BRUSH_PATH;
    strings->entries[strings->count]=(string_entry){{copy,text.size},hash,flags};
    qa_string_id id=(qa_string_id)++strings->count;
    strings->buckets[bucket]=id;
    *out=id; return true;
}
bool qa_strings_intern(qa_strings *strings, qa_bytes text, qa_string_id *out, qa_error *error) {
    return intern(strings,text,false,out,error);
}
bool qa_strings_intern_ascii_lower(qa_strings *strings, qa_bytes text,
    qa_string_id *out, qa_error *error) {
    return intern(strings,text,true,out,error);
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
uint32_t qa_strings_name_flags(const qa_strings *strings,qa_string_id id)
{ return strings&&id&&(size_t)id<=strings->count?strings->entries[id-1u].name_flags:0; }

bool qa_strings_bind_names(qa_strings *strings, const char *const *names, size_t count,
    const qa_string_id **out, qa_error *error)
{
    for (bound_names *row=strings->bound;row;row=row->next)
        if (row->names==names && row->count==count) { *out=row->ids; return true; }
    if (count>(SIZE_MAX-sizeof(bound_names))/sizeof(qa_string_id)) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Static name table exceeds storage"); return false;
    }
    bound_names *row=malloc(sizeof(*row)+count*sizeof(*row->ids));
    if (!row) { qa_error_set(error,QA_ERROR_MEMORY,0,"Binding static names"); return false; }
    for (size_t i=0;i<count;++i) {
        row->ids[i]=QA_STRING_NONE;
        if (names[i] && !qa_strings_intern_cstr(strings,names[i],row->ids+i,error)) { free(row); return false; }
    }
    row->names=names;row->count=count;row->next=strings->bound;strings->bound=row;*out=row->ids;
    return true;
}
