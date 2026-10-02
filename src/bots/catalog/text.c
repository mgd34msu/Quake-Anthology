#include "internal.h"

bool bot_catalog_fail(qa_error *e,qa_status code,const char *text) {
    qa_error_set(e,code,0,"%s",text);return false;
}
bool bot_catalog_enter(qa_bot_catalog *c,qa_error *e) {
    if(!c || c->calls==SIZE_MAX) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"bot catalogue owner is absent or its call depth is exhausted");
    ++c->calls;return true;
}
static unsigned char lower(unsigned char c) {return c>='A' && c<='Z'?(unsigned char)(c+32):c;}
bool bot_catalog_equal(const char *a,const char *b) {
    while(*a && *b) if(lower((unsigned char)*a++)!=lower((unsigned char)*b++)) return false;
    return !*a && !*b;
}
bool bot_catalog_latin1(const char *text,char *out,size_t capacity,qa_error *e) {
    qa_bytes bytes={(const uint8_t *)text,strlen(text)};
    if(!qa_utf8_valid(bytes)) return bot_catalog_fail(e,QA_ERROR_FORMAT,"public bot catalogue text is not UTF-8");
    size_t cursor=0,count=0;uint32_t scalar;
    while(qa_utf8_next(bytes,&cursor,&scalar)) {
        if(scalar>255) return bot_catalog_fail(e,QA_ERROR_FORMAT,"Command text requires source bytes");
        if(count+1>=capacity) return bot_catalog_fail(e,QA_ERROR_FORMAT,"source catalogue text exceeds its byte storage");
        out[count++]=(char)scalar;
    }
    out[count]=0;return true;
}
bool bot_catalog_utf8(const char *text,qa_buffer *out,qa_error *e) {
    size_t size=strlen(text);
    if(size>(SIZE_MAX-1)/2 || !out || out->data) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"catalogue UTF-8 output requires empty bounded storage");
    uint8_t *bytes=malloc(size*2+1);if(!bytes) return bot_catalog_fail(e,QA_ERROR_MEMORY,"publishing catalogue Latin-1 text as UTF-8");
    size_t written=0;
    for(size_t i=0;i<size;++i) {
        uint8_t ch=(uint8_t)text[i];
        if(ch<128) bytes[written++]=ch;
        else {bytes[written++]=(uint8_t)(0xc0u|(ch>>6));bytes[written++]=(uint8_t)(0x80u|(ch&63u));}
    }
    bytes[written]=0;out->data=bytes;out->size=written;return true;
}
bool bot_catalog_info_value(const char *info,const char *key,char *out,size_t capacity,qa_error *e) {
    if(!info || !key || !out || !capacity) return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"source catalogue info requires bounded output");
    if(strlen(info)>=8192) return bot_catalog_fail(e,QA_ERROR_FORMAT,"Info_ValueForKey: oversize infostring");
    out[0]=0;const char *p=info;if(*p=='\\') ++p;
    while(*p) {
        const char *separator=strchr(p,'\\');if(!separator) return true;
        const char *value=separator+1,*next=strchr(value,'\\');size_t n=next?(size_t)(next-value):strlen(value);
        size_t k=(size_t)(separator-p),wanted=strlen(key);bool same=k==wanted;
        for(size_t i=0;same && i<k;++i) same=lower((unsigned char)p[i])==lower((unsigned char)key[i]);
        if(same) {if(n>=capacity) n=capacity-1;memcpy(out,value,n);out[n]=0;return true;}
        if(!next) return true;
        p=next+1;
    }
    return true;
}
bool bot_catalog_info_set(qa_bot_catalog *c,char info[1024],const char *key,const char *value,qa_error *e) {
    size_t size=strlen(info);if(size>=1024) return bot_catalog_fail(e,QA_ERROR_FORMAT,"Info_SetValueForKey: oversize infostring");
    const char *diagnostic=strchr(key,'\\') || strchr(value,'\\')?"Can't use keys or values with a \\\n":
        strchr(key,';') || strchr(value,';')?"Can't use keys or values with a semicolon\n":
        strchr(key,'"') || strchr(value,'"')?"Can't use keys or values with a \"\n":NULL;
    if(diagnostic) return c->services.print(c->services.context,diagnostic,e);
    size_t cursor=0;
    while(cursor<size) {
        size_t start=cursor;if(info[cursor]=='\\') ++cursor;
        const char *separator=strchr(info+cursor,'\\');if(!separator) break;
        const char *next=strchr(separator+1,'\\');size_t end=next?(size_t)(next-info):size;
        size_t length=(size_t)(separator-(info+cursor));
        if(strlen(key)==length && !memcmp(info+cursor,key,length)) {
            memmove(info+start,info+end,size-end+1);size-=end-start;break;
        }
        cursor=end;
    }
    if(!*value) return true;
    char pair[1024];size_t k=strlen(key),v=strlen(value),n=0;
    pair[n++]='\\';for(size_t i=0;i<k && n<1023;++i) pair[n++]=key[i];
    if(n<1023) pair[n++]='\\';
    for(size_t i=0;i<v && n<1023;++i) pair[n++]=value[i];
    if(n+size>1024) return c->services.print(c->services.context,"Info string length exceeded\n",e);
    if(n+size==1024) return bot_catalog_fail(e,QA_ERROR_FORMAT,"Info string overflows source terminator");
    memmove(info+n,info,size+1);memcpy(info,pair,n);return true;
}
bool bot_catalog_text(qa_bot_catalog *c,qa_bot_source_record record,qa_buffer *out,qa_error *e) {
    if(!out || out->data || record.offset>QA_BOT_GAME_MEMORY_BYTES ||
       record.length>QA_BOT_GAME_MEMORY_BYTES-record.offset)
        return bot_catalog_fail(e,QA_ERROR_ARGUMENT,"catalogue string view exceeds its real GAME pool");
    uint32_t remaining=QA_BOT_GAME_MEMORY_BYTES-record.offset;
    uint8_t *bytes=malloc((size_t)remaining+1);
    if(!bytes) return bot_catalog_fail(e,QA_ERROR_MEMORY,"reading actual catalogue memory string");
    if(!c->services.memory.read(c->services.memory.context,record.offset,bytes,remaining,e)) {free(bytes);return false;}
    uint8_t *zero=memchr(bytes,0,remaining);
    if(!zero) {free(bytes);return bot_catalog_fail(e,QA_ERROR_FORMAT,"Game string reads beyond the source memory pool");}
    out->data=bytes;out->size=(size_t)(zero-bytes);return true;
}
bool bot_catalog_write(qa_bot_catalog *c,qa_bot_source_record record,const char *text,qa_error *e) {
    size_t size=strlen(text)+1;
    if(size>record.length) return bot_catalog_fail(e,QA_ERROR_FORMAT,"Game string exceeds its source allocation");
    return qa_bot_source_record_write(&c->services.memory,(qa_bot_source_record){record.offset,(uint32_t)size},text,e);
}
bool bot_catalog_lookup(qa_bot_catalog *c,bot_catalog_infos *infos,const char *key,const char *wanted,
    qa_buffer *out,bool *found,qa_error *e) {
    *found=false;
    for(uint32_t i=0;i<infos->count;++i) {
        qa_buffer text={0};char value[8192];
        if(!bot_catalog_text(c,infos->records[i],&text,e)) return false;
        bool okay=bot_catalog_info_value((const char *)text.data,key,value,sizeof(value),e);
        if(okay && bot_catalog_equal(value,wanted)) {*out=text;*found=true;return true;}
        qa_buffer_free(&text);if(!okay) return false;
    }
    return true;
}
int32_t bot_catalog_word(uint32_t bits) {int32_t value;memcpy(&value,&bits,4);return value;}
static bool whitespace(unsigned char c) {return c<=32 || c>=128;}
float bot_catalog_atof(const char *text) {
    while(*text && whitespace((unsigned char)*text)) ++text;
    float sign=1;if(*text=='-' || *text=='+') {if(*text=='-') sign=-1;++text;}
    volatile float value=0,fraction=.1f;int character=(unsigned char)*text;
    if(character!='.') {
        for(;;) {
            character=(unsigned char)*text;if(*text) ++text;
            if(character<'0' || character>'9') break;
            volatile float product=value*10.0f;value=product+(float)(character-'0');
        }
    } else ++text;
    if(character=='.') for(;;) {
        character=(unsigned char)*text;if(*text) ++text;
        if(character<'0' || character>'9') break;
        volatile float part=(float)(character-'0')*fraction;value=value+part;fraction=fraction*.1f;
    }
    return value*sign;
}
int32_t bot_catalog_atoi(const char *text) {
    while(*text && whitespace((unsigned char)*text)) ++text;
    uint32_t sign=1;if(*text=='-' || *text=='+') {if(*text=='-') sign=UINT32_MAX;++text;}
    uint32_t value=0;while(*text>='0' && *text<='9') value=value*10u+(uint32_t)(*text++-'0');
    return bot_catalog_word(value*sign);
}
