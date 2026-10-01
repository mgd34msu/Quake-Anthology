/* Source Quake v5 and rerelease v6 text saves. */
#include "qa/q1_save.h"
#include "qa/text.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct scanner { qa_bytes bytes; size_t offset; qa_error *error; } scanner;
typedef struct writer { qa_buffer bytes; size_t capacity; qa_error *error; } writer;
static bool js_space(uint8_t c) { return c==32 || c==160 || (c>=9 && c<=13); }
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
static bool text_token(scanner *s,char **out)
{ qa_bytes bytes; if (!token(s,&bytes)) return false; *out=copy(bytes,s->error); return *out!=NULL; }
static bool is_token(qa_bytes bytes,char value)
{ return bytes.size==1 && bytes.data[0]==(uint8_t)value; }
static bool number(scanner *s,double *out)
{
    qa_bytes bytes; if (!token(s,&bytes)) return false;
    while (bytes.size && js_space(bytes.data[0])) { ++bytes.data; --bytes.size; }
    while (bytes.size && js_space(bytes.data[bytes.size-1])) --bytes.size;
    double value=0;
    if (bytes.size>=2 && bytes.data[0]=='0' && (bytes.data[1]=='b' || bytes.data[1]=='B' || bytes.data[1]=='o' || bytes.data[1]=='O' || bytes.data[1]=='x' || bytes.data[1]=='X')) {
        unsigned base=bytes.data[1]=='b'||bytes.data[1]=='B'?2:bytes.data[1]=='o'||bytes.data[1]=='O'?8:16;
        if (bytes.size==2) return fail(s->error,s->offset,"Invalid source header number");
        for (size_t i=2;i<bytes.size;++i) {
            uint8_t c=bytes.data[i];
            unsigned digit=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:16;
            if (digit>=base) return fail(s->error,s->offset,"Invalid source header number");
            value=value*base+digit;
        }
    } else if (bytes.size) {
        size_t p=0,digits=0;
        if (bytes.data[p]=='+' || bytes.data[p]=='-') ++p;
        while (p<bytes.size && bytes.data[p]>='0' && bytes.data[p]<='9') { ++p; ++digits; }
        if (p<bytes.size && bytes.data[p]=='.') {
            ++p;
            while (p<bytes.size && bytes.data[p]>='0' && bytes.data[p]<='9') { ++p; ++digits; }
        }
        if (!digits) return fail(s->error,s->offset,"Invalid source header number");
        if (p<bytes.size && (bytes.data[p]=='e' || bytes.data[p]=='E')) {
            ++p; size_t exponent_digits=0;
            if (p<bytes.size && (bytes.data[p]=='+' || bytes.data[p]=='-')) ++p;
            while (p<bytes.size && bytes.data[p]>='0' && bytes.data[p]<='9') { ++p; ++exponent_digits; }
            if (!exponent_digits) return fail(s->error,s->offset,"Invalid source header number");
        }
        if (p!=bytes.size || !qa_parse_number(bytes,&value,s->error))
            return fail(s->error,s->offset,"Invalid source header number");
    }
    if (!isfinite(value)) return fail(s->error,s->offset,"Nonfinite source header number");
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
    scanner s={bytes,0,error}; double value=0;
    bool ok=number(&s,&value) && (value==5 || value==6);
    if (ok) save->version=(uint32_t)value;
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
{
    if (size>SIZE_MAX-w->bytes.size) return fail(w->error,w->bytes.size,"Source output extent overflows");
    size_t required=w->bytes.size+size;
    if (required>w->capacity) {
        size_t capacity=w->capacity?w->capacity:512;
        while (capacity<required) { if (capacity>SIZE_MAX/2) { capacity=required; break; } capacity*=2; }
        uint8_t *next=realloc(w->bytes.data,capacity);
        if (!next) { qa_error_set(w->error,QA_ERROR_MEMORY,w->bytes.size,"Allocating source output"); return false; }
        w->bytes.data=next; w->capacity=capacity;
    }
    if (size) memcpy(w->bytes.data+w->bytes.size,data,size);
    w->bytes.size=required; return true;
}
static bool line(writer *w,const char *text)
{ return append(w,text,strlen(text)) && append(w,"\n",1); }
static bool header_token(writer *w,const char *text)
{
    if (!text || !*text) return fail(w->error,w->bytes.size,"Empty source header token");
    for (const unsigned char *p=(const unsigned char *)text;*p;++p)
        if (js_space(*p) || *p=='"') return fail(w->error,w->bytes.size,"Invalid source header token");
    return line(w,text);
}
/* Binary64 header numbers follow Number.toFixed, while QC FIELD values retain
 * the separate source C %f formatter. The largest binary64 integer needs 1024
 * bits. Two spare words cover the adjacent decimal candidates. */
typedef struct header_integer { uint32_t words[34]; } header_integer;
static header_integer integer_u64(uint64_t value)
{ header_integer n={{(uint32_t)value,(uint32_t)(value>>32)}}; return n; }
static void integer_multiply(header_integer *n,uint32_t factor)
{
    uint64_t carry=0;
    for (size_t i=0;i<34;++i) { uint64_t v=(uint64_t)n->words[i]*factor+carry; n->words[i]=(uint32_t)v; carry=v>>32; }
}
static int integer_compare(const header_integer *a,const header_integer *b)
{
    for (size_t i=34;i>0;--i) if (a->words[i-1]!=b->words[i-1]) return a->words[i-1]<b->words[i-1]?-1:1;
    return 0;
}
static header_integer integer_distance(const header_integer *a,const header_integer *b)
{
    if (integer_compare(a,b)<0) { const header_integer *swap=a; a=b; b=swap; }
    header_integer result={{0}}; uint64_t borrow=0;
    for (size_t i=0;i<34;++i) {
        uint64_t right=(uint64_t)b->words[i]+borrow,left=a->words[i];
        result.words[i]=(uint32_t)(left-right); borrow=left<right;
    }
    return result;
}
static header_integer integer_shift(const header_integer *n,int shift)
{
    header_integer result={{0}};
    for (int bit=0;bit<1088;++bit) if ((n->words[bit/32]>>(bit%32))&1u) {
        int target=bit+shift;
        if (target>=0 && target<1088) result.words[target/32]|=UINT32_C(1)<<(target%32);
    }
    return result;
}
static void integer_increment(header_integer *n)
{ for (size_t i=0;i<34;++i) if (++n->words[i]) break; }
static uint32_t integer_divide10(header_integer *n)
{
    uint64_t remainder=0;
    for (size_t i=34;i>0;--i) {
        uint64_t value=(remainder<<32)|n->words[i-1];
        n->words[i-1]=(uint32_t)(value/10); remainder=value%10;
    }
    return (uint32_t)remainder;
}
static bool scientific(uint64_t coefficient,int exponent,bool negative,char text[384])
{
    char digits[32]; int count=snprintf(digits,sizeof(digits),"%llu",(unsigned long long)coefficient);
    if (count<1 || (size_t)count>=sizeof(digits)) return false;
    int exp=exponent+count-1;
    while (count>1 && digits[count-1]=='0') --count;
    size_t used=0;
    if (negative) text[used++]='-';
    text[used++]=digits[0];
    if (count>1) { text[used++]='.'; memcpy(text+used,digits+1,(size_t)count-1); used+=(size_t)count-1; }
    int tail=snprintf(text+used,384-used,"e+%d",exp);
    return tail>0 && (size_t)tail<384-used;
}
static bool shortest_header(double value,const header_integer *actual,char text[384],qa_error *error)
{
    char initial[32]; double magnitude=fabs(value);
    if (!qa_format_number(magnitude,initial,error)) return false;
    const char *e=strchr(initial,'e'); if (!e) e=strchr(initial,'E');
    if (!e) return fail(error,0,"Large source number has no scientific representation");
    int exponent=0; const char *p=e+1; if (*p=='+') ++p;
    for (;*p;++p) { if (*p<'0' || *p>'9') return fail(error,0,"Invalid numeric exponent"); exponent=exponent*10+*p-'0'; }
    uint64_t full=0; size_t total=0;
    for (p=initial;p<e;++p) if (*p!='.') { full=full*10+(unsigned)(*p-'0'); ++total; }
    uint64_t divisor=1;
    for (size_t i=1;i<total;++i) divisor*=10;
    for (size_t precision=1;precision<=total;++precision,divisor/=10) {
        uint64_t floor=full/divisor,best=0; header_integer distance={{0}}; bool found=false;
        int power=exponent-(int)precision+1;
        for (int delta=-1;delta<=1;++delta) {
            uint64_t candidate=delta<0?floor-1:floor+(unsigned)delta;
            if (!candidate || !scientific(candidate,power,value<0,text)) continue;
            double parsed=0;
            qa_error candidate_error={0};
            if (!qa_parse_number((qa_bytes){(const uint8_t *)text,strlen(text)},&parsed,&candidate_error)) continue;
            if (parsed!=value) continue;
            header_integer decimal=integer_u64(candidate);
            for (int i=0;i<power;++i) integer_multiply(&decimal,10);
            header_integer difference=integer_distance(actual,&decimal);
            int order=found?integer_compare(&difference,&distance):-1;
            if (!found || order<0 || (!order && !(candidate&1u))) { found=true; best=candidate; distance=difference; }
        }
        if (found) return scientific(best,power,value<0,text);
    }
    return fail(error,0,"Cannot serialize source header number");
}
static bool decimal(writer *w,double value,unsigned digits)
{
    if (!isfinite(value)) return fail(w->error,w->bytes.size,"Nonfinite source header number");
    if (!digits) { char integer[384]; return qa_format_fixed(value,0,integer,sizeof(integer),w->error) && line(w,integer); }
    _Static_assert(sizeof(double)==8 && FLT_RADIX==2 && DBL_MANT_DIG==53 && DBL_MAX_EXP==1024,
        "Source header codec requires binary64 numbers");
    uint64_t bits=0; memcpy(&bits,&value,8);
    uint32_t exponent=(uint32_t)((bits>>52)&2047u);
    uint64_t mantissa=bits&UINT64_C(0xfffffffffffff);
    if (exponent) mantissa|=UINT64_C(1)<<52;
    int shift=exponent?(int)exponent-1075:-1074;
    header_integer exact=integer_u64(mantissa); char text[384];
    if (fabs(value)>=1e21) {
        exact=integer_shift(&exact,shift);
        return shortest_header(value,&exact,text,w->error) && line(w,text);
    }
    integer_multiply(&exact,1000000);
    header_integer rounded=integer_shift(&exact,shift);
    int half=-shift-1;
    if (shift<0 && half<1088 && ((exact.words[half/32]>>(half%32))&1u)) integer_increment(&rounded);
    char reverse[384]; size_t count=0; header_integer zero={{0}};
    do { reverse[count++]=(char)('0'+integer_divide10(&rounded)); } while (integer_compare(&rounded,&zero));
    while (count<=6) reverse[count++]='0';
    size_t used=0;
    if (value<0) text[used++]='-';
    for (size_t i=count;i>0;--i) { if (i==6) text[used++]='.'; text[used++]=reverse[i-1]; }
    text[used]=0; return line(w,text);
}
static bool put_record(writer *w,const qa_q1_save_record *record)
{
    if (!record || (record->count && !record->pairs) || !line(w,"{")) return false;
    for (size_t i=0;i<record->count;++i) {
        const qa_q1_save_pair *pair=record->pairs+i;
        if (!pair->key || !pair->value || strchr(pair->key,'"') || strchr(pair->value,'"'))
            return fail(w->error,w->bytes.size,"Source fields cannot contain quotes");
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
    writer w={{0},0,error}; bool ok=line(&w,save->version==5?"5":"6");
    if (ok && save->version==6) ok=header_token(&w,save->game_directories);
    if (ok) ok=header_token(&w,save->comment);
    for (size_t i=0;ok && i<16;++i) ok=decimal(&w,save->spawn_parameters[i],6);
    if (ok) ok=decimal(&w,save->skill,0) && header_token(&w,save->map) && decimal(&w,save->time,6);
    for (size_t i=0;ok && i<64;++i) ok=header_token(&w,save->lightstyles[i] && *save->lightstyles[i]?save->lightstyles[i]:"m");
    if (ok) ok=put_record(&w,&save->globals);
    for (size_t i=0;ok && i<save->entity_count;++i) ok=put_record(&w,save->entities+i);
    if (ok && save->extension.size && memchr(save->extension.data,0,save->extension.size))
        ok=fail(error,w.bytes.size,"NUL in source extension text");
    if (ok) ok=append(&w,save->extension.data,save->extension.size);
    if (!ok) { qa_buffer_free(&w.bytes); return false; }
    *out=w.bytes; return true;
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
        } else if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-'))
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
