/* Source Quake v5 and rerelease v6 text saves. */
#include "qa/q1_save.h"
#include "qa/text.h"
#include "qa/source_save.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct scanner { qa_bytes bytes; size_t offset; qa_error *error; } scanner;
typedef qa_source_save_io writer;
static bool header_space(uint8_t c) { return c==32 || (c>=9 && c<=13); }
static bool fail(qa_error *error,size_t offset,const char *message)
{ qa_error_set(error,QA_ERROR_FORMAT,offset,"%s",message); return false; }
static char *copy(qa_bytes bytes,qa_error *error)
{
    if (bytes.size==SIZE_MAX) { fail(error,0,"Source text extent overflows"); return NULL; }
    char *text=malloc(bytes.size+1);
    if (!text) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating source save text"); return NULL; }
    if (bytes.size) memcpy(text,bytes.data,bytes.size);
    text[bytes.size]=0; return text;
}
void qa_q1_save_record_destroy(qa_q1_save_record *record)
{
    if (!record) return;
    for (size_t i=0;i<record->count;++i) { free(record->pairs[i].key); free(record->pairs[i].value); }
    free(record->pairs); *record=(qa_q1_save_record){0};
}
void qa_q1_save_destroy(qa_q1_save_data *save)
{
    if (!save) return;
    free(save->game_directories); free(save->comment); free(save->map);
    for (size_t i=0;i<64;++i) free(save->lightstyles[i]);
    qa_q1_save_record_destroy(&save->globals);
    for (size_t i=0;i<save->entity_count;++i) qa_q1_save_record_destroy(save->entities+i);
    free(save->entities); qa_buffer_free(&save->extension); free(save);
}
static void skip(scanner *s)
{
    for (;;) {
        while (s->offset<s->bytes.size && s->bytes.data[s->offset]<=32) ++s->offset;
        if (s->bytes.size-s->offset<2 || s->bytes.data[s->offset]!='/' || s->bytes.data[s->offset+1]!='/') return;
        while (s->offset<s->bytes.size && s->bytes.data[s->offset++]!='\n') {}
    }
}
static bool token(scanner *s,qa_bytes *out)
{
    skip(s);
    if (s->offset==s->bytes.size) return fail(s->error,s->offset,"Truncated source save");
    size_t start=s->offset; uint8_t first=s->bytes.data[s->offset++];
    if (first=='"') {
        start=s->offset;
        while (s->offset<s->bytes.size && s->bytes.data[s->offset]!='"') ++s->offset;
        if (s->offset==s->bytes.size) return fail(s->error,start,"Unterminated source field");
        *out=(qa_bytes){s->bytes.data+start,s->offset-start}; ++s->offset; return true;
    }
    if (first!='{' && first!='}')
        while (s->offset<s->bytes.size && s->bytes.data[s->offset]>32 &&
            s->bytes.data[s->offset]!='{' && s->bytes.data[s->offset]!='}') ++s->offset;
    *out=(qa_bytes){s->bytes.data+start,s->offset-start}; return true;
}
static bool header_read(scanner *s,qa_bytes *out)
{
    while (s->offset<s->bytes.size && header_space(s->bytes.data[s->offset])) ++s->offset;
    size_t start=s->offset;
    while (s->offset<s->bytes.size && !header_space(s->bytes.data[s->offset])) ++s->offset;
    if (start==s->offset) return fail(s->error,start,"Truncated source header");
    *out=(qa_bytes){s->bytes.data+start,s->offset-start}; return true;
}
static bool text_token(scanner *s,char **out)
{ qa_bytes bytes; if (!header_read(s,&bytes)) return false; *out=copy(bytes,s->error); return *out!=NULL; }
static bool is_token(qa_bytes bytes,char value)
{ return bytes.size==1 && bytes.data[0]==(uint8_t)value; }
static bool integer(scanner *s,int32_t *out)
{
    qa_bytes bytes; if (!header_read(s,&bytes)) return false;
    char *text=copy(bytes,s->error); if (!text) return false;
    char *end; int previous_errno=errno; errno=0;
    long value=strtol(text,&end,0);
    bool valid=end==text+bytes.size && errno!=ERANGE && value>=INT32_MIN && value<=INT32_MAX;
    errno=previous_errno; free(text);
    if (!valid) return fail(s->error,s->offset,"Invalid source header integer");
    *out=(int32_t)value; return true;
}
static bool number(scanner *s,double *out)
{
    qa_bytes bytes; if (!header_read(s,&bytes)) return false;
    char *text=copy(bytes,s->error); if (!text) return false;
    double parsed; size_t consumed; bool range_error; float value;
    bool valid=qa_parse_strtod(text,&parsed,&consumed,&range_error,s->error) &&
        consumed==bytes.size && qa_parse_atof_float(text,&value,s->error) && isfinite(value);
    free(text);
    if (!valid) return fail(s->error,s->offset,"Invalid source header float");
    *out=value; return true;
}
static bool record(scanner *s,qa_q1_save_record *out)
{
    qa_bytes key,value;
    if (!token(s,&key) || !is_token(key,'{')) return fail(s->error,s->offset,"Expected source record opening brace");
    for (;;) {
        if (!token(s,&key)) return false;
        if (is_token(key,'}')) return true;
        if (!token(s,&value) || is_token(value,'}')) return fail(s->error,s->offset,"Source field has no value");
        if (out->count>=SIZE_MAX/sizeof(*out->pairs)) return fail(s->error,s->offset,"Source pair count overflows");
        qa_q1_save_pair pair={copy(key,s->error),NULL};
        if (pair.key) pair.value=copy(value,s->error);
        if (!pair.key || !pair.value) { free(pair.key); free(pair.value); return false; }
        qa_q1_save_pair *next=realloc(out->pairs,(out->count+1)*sizeof(*next));
        if (!next) { free(pair.key); free(pair.value); qa_error_set(s->error,QA_ERROR_MEMORY,s->offset,"Allocating source pairs"); return false; }
        out->pairs=next; next[out->count++]=pair;
    }
}
bool qa_q1_save_decode(qa_bytes bytes,qa_q1_save_data **out,qa_error *error)
{
    if (!out || *out || !bytes.data || !bytes.size || memchr(bytes.data,0,bytes.size))
        return fail(error,0,"Source save requires nonempty byte text and an empty output");
    qa_q1_save_data *save=calloc(1,sizeof(*save));
    if (!save) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating source save"); return false; }
    scanner s={bytes,0,error}; double value=0; int32_t version=0;
    bool ok=integer(&s,&version) && (version==5 || version==6);
    if (ok) save->version=(uint32_t)version;
    else if (error && error->code==QA_OK) fail(error,s.offset,"Unsupported source save version");
    if (ok && save->version==6) ok=text_token(&s,&save->game_directories);
    if (ok) ok=text_token(&s,&save->comment);
    for (size_t i=0;ok && i<16;++i) ok=number(&s,save->spawn_parameters+i);
    if (ok) {
        ok=number(&s,&value) && trunc(value+0.1)>=INT32_MIN && trunc(value+0.1)<=INT32_MAX;
        if (ok) save->skill=(int32_t)trunc(value+0.1);
        else if (error && error->code==QA_OK) fail(error,s.offset,"Source skill overflows");
    }
    if (ok) ok=text_token(&s,&save->map) && number(&s,&save->time);
    for (size_t i=0;ok && i<64;++i) ok=text_token(&s,save->lightstyles+i);
    if (ok) ok=record(&s,&save->globals);
    while (ok) {
        while (s.offset<bytes.size && bytes.data[s.offset]<=32) ++s.offset;
        size_t extension=s.offset; skip(&s);
        if (s.offset==bytes.size || bytes.data[s.offset]!='{') {
            save->extension.size=bytes.size-extension;
            if (save->extension.size) {
                save->extension.data=malloc(save->extension.size);
                if (!save->extension.data) { qa_error_set(error,QA_ERROR_MEMORY,extension,"Retaining source extension text"); ok=false; }
                else memcpy(save->extension.data,bytes.data+extension,save->extension.size);
            }
            break;
        }
        if (save->entity_count>=SIZE_MAX/sizeof(*save->entities)) { ok=fail(error,s.offset,"Source edict count overflows"); break; }
        qa_q1_save_record *next=realloc(save->entities,(save->entity_count+1)*sizeof(*next));
        if (!next) { qa_error_set(error,QA_ERROR_MEMORY,s.offset,"Allocating source edicts"); ok=false; break; }
        save->entities=next; next[save->entity_count]=(qa_q1_save_record){0};
        ok=record(&s,next+save->entity_count++);
    }
    if (!ok) { qa_q1_save_destroy(save); return false; }
    *out=save; return true;
}
static bool append(writer *w,const void *data,size_t size)
{ return qa_source_save_bytes(w,(void *)data,size); }
static bool line(writer *w,const char *text)
{ return append(w,text,strlen(text)) && append(w,"\n",1); }
static bool header_token(writer *w,const char *text)
{
    if (!text || !*text) return fail(w->error,w->offset,"Empty source header token");
    for (const unsigned char *p=(const unsigned char *)text;*p;++p)
        if (header_space(*p) || *p=='"') return fail(w->error,w->offset,"Invalid source header token");
    return line(w,text);
}
static bool decimal(writer *w,double value)
{
    if (!isfinite(value)) return fail(w->error,w->offset,"Nonfinite source header number");
    char text[384];
    return qa_format_fixed(value,6,text,sizeof(text),w->error) && line(w,text);
}
static bool integer_line(writer *w,int32_t value)
{
    char text[12]; (void)snprintf(text,sizeof(text),"%d",value); return line(w,text);
}
static bool put_record(writer *w,const qa_q1_save_record *record)
{
    if (!record || (record->count && !record->pairs) || !line(w,"{")) return false;
    for (size_t i=0;i<record->count;++i) {
        const qa_q1_save_pair *pair=record->pairs+i;
        if (!pair->key || !pair->value || strchr(pair->key,'"') || strchr(pair->value,'"'))
            return fail(w->error,w->offset,"Source fields cannot contain quotes");
        if (!append(w,"\"",1) || !append(w,pair->key,strlen(pair->key)) || !append(w,"\" \"",3) ||
            !append(w,pair->value,strlen(pair->value)) || !line(w,"\"")) return false;
    }
    return line(w,"}");
}
bool qa_q1_save_encode(const qa_q1_save_data *save,qa_buffer *out,qa_error *error)
{
    if (!save || !out || out->data || out->size || (save->version!=5 && save->version!=6) ||
        (save->entity_count && !save->entities) || (save->extension.size && !save->extension.data))
        return fail(error,0,"Invalid source save/output");
    writer w={0}; bool ok=qa_source_save_writer(&w,NULL,error) && line(&w,save->version==5?"5":"6");
    if (ok && save->version==6) ok=header_token(&w,save->game_directories);
    if (ok) ok=header_token(&w,save->comment);
    for (size_t i=0;ok && i<16;++i) ok=decimal(&w,(float)save->spawn_parameters[i]);
    if (ok) ok=integer_line(&w,save->skill) && header_token(&w,save->map) && decimal(&w,save->time);
    for (size_t i=0;ok && i<64;++i) ok=header_token(&w,save->lightstyles[i] && *save->lightstyles[i]?save->lightstyles[i]:"m");
    if (ok) ok=put_record(&w,&save->globals);
    for (size_t i=0;ok && i<save->entity_count;++i) ok=put_record(&w,save->entities+i);
    if (ok && save->extension.size && memchr(save->extension.data,0,save->extension.size))
        ok=fail(error,w.offset,"NUL in source extension text");
    if (ok) ok=append(&w,save->extension.data,save->extension.size);
    if (ok) ok=qa_source_save_finish(&w,out);
    qa_source_save_dispose(&w); return ok;
}
bool qa_q1_save_singleplayer(const qa_q1_save_data *save,qa_error *error)
{
    if (!save || !save->map || !*save->map || !isfinite(save->time) || save->time<0 ||
        save->skill<0 || save->skill>3 || save->entity_count<2 || !save->entities || !save->entities[0].count)
        return fail(error,0,"Source save has no valid singleplayer world");
    const char *start=save->map;
    for (const char *p=start;;++p) {
        unsigned char c=(unsigned char)*p;
        if (!c || c=='/') {
            size_t size=(size_t)(p-start);
            if (!size || (size==1 && start[0]=='.') || (size==2 && start[0]=='.' && start[1]=='.'))
                return fail(error,0,"Invalid source map component");
            if (!c) return true;
            start=p+1;
        } else if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))
            return fail(error,0,"Invalid source map name");
    }
}
bool qa_saved_game_decode(qa_bytes bytes,qa_save_image **shared,qa_q1_save_data **source,qa_error *error)
{
    if (!shared || *shared || !source || *source || !bytes.data || !bytes.size)
        return fail(error,0,"Saved-game decoder requires empty outputs");
    if (bytes.size>=2 && bytes.data[0]=='Q' && bytes.data[1]=='A')
        return qa_save_image_decode(bytes,shared,error);
    qa_q1_save_data *save=NULL;
    if (!qa_q1_save_decode(bytes,&save,error)) return false;
    if (!qa_q1_save_singleplayer(save,error)) { qa_q1_save_destroy(save); return false; }
    *source=save; return true;
}
bool qa_saved_game_read(qa_fs_root *root,const char *name,qa_save_image **shared,qa_q1_save_data **source,qa_error *error)
{
    if (!root || !qa_save_slot_name(name,error)) return false;
    qa_fs_file *file=NULL; qa_fs_identity identity; qa_buffer bytes={0};
    bool ok=qa_fs_root_file_open(root,name,&file,&identity,error) &&
        qa_fs_file_read_snapshot(file,&identity,&bytes,error) &&
        qa_saved_game_decode((qa_bytes){bytes.data,bytes.size},shared,source,error);
    qa_fs_file_close(file); qa_buffer_free(&bytes); return ok;
}
bool qa_q1_save_write(qa_fs_root *root,const char *name,const qa_q1_save_data *save,uint64_t nonce,qa_error *error)
{
    qa_buffer bytes={0};
    bool ok=root && qa_save_slot_name(name,error) && qa_q1_save_encode(save,&bytes,error) &&
        qa_fs_root_replace(root,name,(qa_bytes){bytes.data,bytes.size},nonce,error);
    qa_buffer_free(&bytes); return ok;
}
