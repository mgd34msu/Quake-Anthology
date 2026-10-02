#include "network_q2_materials.h"
#include "qa/network_q2_materials.h"
#include "qa/material_library_save.h"
#include "qa/binary.h"
#include "qa/model.h"
#include "qa/image.h"
#include "qa/scene_resource_save.h"
#include "qa/source_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct material_token { char text[1024]; size_t begin, end; } material_token;
typedef struct material_reader { qa_bytes bytes; size_t at; bool failed, missing_parameter; qa_error *error; } material_reader;

static unsigned char byte_at(const material_reader *r, size_t at)
{ return at < r->bytes.size ? r->bytes.data[at] : 0; }
static bool next_token(material_reader *r, bool lines, material_token *token)
{
    bool newline = false;
    if (r->failed) return false;
    for (;;) {
        unsigned char c;
        while ((c = byte_at(r, r->at)) && c <= ' ') {
            if (c == '\n' || c == '\r') newline = true;
            ++r->at;
        }
        if (byte_at(r,r->at)=='/' && byte_at(r,r->at+1)=='/') {
            r->at += 2;
            while ((c=byte_at(r,r->at)) && c!='\n' && c!='\r') ++r->at;
        } else if (byte_at(r,r->at)=='/' && byte_at(r,r->at+1)=='*') {
            r->at += 2;
            while ((c=byte_at(r,r->at)) && !(c=='*' && byte_at(r,r->at+1)=='/')) {
                if (c=='\n' || c=='\r') newline=true;
                ++r->at;
            }
            if (c) r->at += 2;
        } else break;
    }
    if ((!lines && newline) || !byte_at(r,r->at)) return false;
    token->begin=r->at;
    bool quoted=byte_at(r,r->at)=='"';
    if (quoted) ++r->at;
    size_t count=0; unsigned char c;
    while ((c=byte_at(r,r->at)) && (quoted ? c!='"' : c>' ')) {
        if (count+1==sizeof(token->text)) {
            qa_error_set(r->error,QA_ERROR_FORMAT,token->begin,"Material token exceeds its actual shader extent");
            r->failed=true; return false;
        }
        token->text[count++]=(char)c; ++r->at;
    }
    if (quoted && c=='"') ++r->at;
    token->text[count]=0; token->end=r->at; return true;
}
static bool equal(const char *a,const char *b)
{
    while (*a || *b) {
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if (x>='A' && x<='Z') x+='a'-'A';
        if (y>='A' && y<='Z') y+='a'-'A';
        if (x!=y) return false;
    }
    return true;
}
static bool prefix(const char *a,const char *b)
{
    while (*b) {
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if (x>='A' && x<='Z') x+='a'-'A';
        if (y>='A' && y<='Z') y+='a'-'A';
        if (x!=y) return false;
    }
    return true;
}
bool qa_q2_material_dependency_builtin(const qa_q2_material_dependency *dependency)
{
    if (!dependency || dependency->kind!=QA_Q2_MATERIAL_IMAGE || !dependency->path) return false;
    return (dependency->builtin_images &&
        (equal(dependency->path,"$whiteimage") || equal(dependency->path,"$lightmap"))) ||
        !strcmp(dependency->path,"$whiteimage") || !strcmp(dependency->path,"*white") ||
        !strcmp(dependency->path,"*default");
}
static bool append(qa_buffer *buffer,qa_bytes bytes,qa_error *error)
{
    if (bytes.size>SIZE_MAX-buffer->size)
        return application_fail(error,QA_ERROR_MEMORY,"Material closure bytes overflow");
    if (!bytes.size) return true;
    uint8_t *data=realloc(buffer->data,buffer->size+bytes.size);
    if (!data) return application_fail(error,QA_ERROR_MEMORY,"Retaining material closure bytes");
    memcpy(data+buffer->size,bytes.data,bytes.size); buffer->data=data; buffer->size+=bytes.size; return true;
}
static bool text_append(qa_buffer *buffer,const char *text,qa_error *error)
{ return append(buffer,(qa_bytes){(const uint8_t*)text,strlen(text)},error); }

static bool scope_write(qa_buffer *bytes,qa_scene_family family,qa_bytes palette,qa_error *error)
{
    static const char hex[]="0123456789abcdef";
    const char *name=family==QA_SCENE_Q1?"q1":family==QA_SCENE_Q2?"q2":"q3";
    bool ok=text_append(bytes,"//qa-material 1 ",error) && text_append(bytes,name,error) && text_append(bytes," ",error);
    if (ok && !palette.size) ok=text_append(bytes,"-",error);
    else if (ok) {
        if (palette.size!=768) return application_fail(error,QA_ERROR_FORMAT,"Material Source palette is not genuine RGB256");
        char encoded[1536];
        for (size_t i=0;i<768;++i) { encoded[2*i]=hex[palette.data[i]>>4]; encoded[2*i+1]=hex[palette.data[i]&15]; }
        ok=append(bytes,(qa_bytes){(const uint8_t*)encoded,sizeof(encoded)},error);
    }
    return ok && text_append(bytes,"\n",error);
}
static int hex_value(uint8_t c)
{ return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:-1; }

typedef struct material_alias {
    char alias[1024], source_alias[1024], request[1024], path[1024], logical[1024], logical_path[1024], palette_path[1024];
    qa_scene_image_options options;
    uint8_t palette[768], translation[256];
    bool missing, palette_attempted;
    qa_status rejection, palette_error;
} material_alias;
typedef bool (*material_alias_fn)(void *, const material_alias *, qa_error *);
static bool natural_write(qa_buffer *out,uint32_t value,qa_error *error)
{ uint8_t bytes[4]; qa_store_u32le(bytes,value); return append(out,(qa_bytes){bytes,4},error); }
static bool string_write(qa_buffer *out,const char *value,qa_error *error)
{
    size_t size=strlen(value);
    return size<1024 && natural_write(out,(uint32_t)size,error) &&
        append(out,(qa_bytes){(const uint8_t*)value,size},error);
}
static bool natural_read(qa_bytes bytes,size_t *at,uint32_t *value)
{
    if (*at>bytes.size || bytes.size-*at<4) return false;
    *value=qa_load_u32le(bytes.data+*at); *at+=4; return true;
}
static bool string_read(qa_bytes bytes,size_t *at,char out[1024])
{
    uint32_t size;
    if (!natural_read(bytes,at,&size) || size>=1024 || bytes.size-*at<size || memchr(bytes.data+*at,0,size)) return false;
    memcpy(out,bytes.data+*at,size); out[size]=0; *at+=size; return true;
}
static bool alias_encode(const material_alias *row,qa_buffer *out,qa_error *error)
{
    qa_buffer raw={0}; const qa_scene_image_options *o=&row->options;
    bool ok=string_write(&raw,row->alias,error) && string_write(&raw,row->source_alias,error) && string_write(&raw,row->request,error) &&
        string_write(&raw,row->path,error) && string_write(&raw,row->logical,error) &&
        string_write(&raw,row->logical_path,error) && string_write(&raw,row->palette_path,error);
    uint32_t values[]={o->family,o->wrap,o->filter,o->usage,o->mipmap,o->transparent,o->fullbright_only,
        (uint32_t)o->transparent_index,row->missing,row->palette_attempted,row->rejection,row->palette_error,
        (uint32_t)o->palette_rgb.size,(uint32_t)o->translation.size};
    for (size_t i=0;ok && i<sizeof(values)/sizeof(*values);++i) ok=natural_write(&raw,values[i],error);
    ok=ok && !o->source_q3 && append(&raw,o->palette_rgb,error) && append(&raw,o->translation,error) &&
        text_append(out,"//qa-image 1 ",error);
    static const char hex[]="0123456789abcdef";
    for (size_t i=0;ok && i<raw.size;++i) {
        uint8_t pair[2]={(uint8_t)hex[raw.data[i]>>4],(uint8_t)hex[raw.data[i]&15]};
        ok=append(out,(qa_bytes){pair,2},error);
    }
    ok=ok && text_append(out,"\n",error); qa_buffer_free(&raw); return ok;
}
static bool alias_decode(qa_bytes bytes,material_alias *row,qa_error *error)
{
    size_t at=0; uint32_t v[14];
    bool ok=string_read(bytes,&at,row->alias) && string_read(bytes,&at,row->source_alias) && string_read(bytes,&at,row->request) &&
        string_read(bytes,&at,row->path) && string_read(bytes,&at,row->logical) &&
        string_read(bytes,&at,row->logical_path) && string_read(bytes,&at,row->palette_path);
    for (size_t i=0;ok && i<14;++i) ok=natural_read(bytes,&at,v+i);
    if (!ok || !row->alias[0] || !row->source_alias[0] || !row->request[0] || !row->path[0] ||
        (!!row->logical[0]!=!!row->logical_path[0]) || v[0]>QA_SCENE_Q3 || v[1]>QA_SCENE_CLAMP ||
        v[2]>QA_SCENE_LINEAR_MIPMAP_LINEAR || v[3]>QA_IMAGE_USAGE_SKY ||
        v[4]>1 || v[5]>1 || v[6]>1 || v[8]>1 || v[9]>1 ||
        v[10]>QA_ERROR_NOT_FOUND || v[11]>QA_ERROR_NOT_FOUND ||
        v[10]==QA_ERROR_MEMORY || v[11]==QA_ERROR_MEMORY ||
        (v[12] && v[12]!=768) || (v[13] && v[13]!=256) || bytes.size-at!=(size_t)v[12]+v[13])
        return application_fail(error,QA_ERROR_FORMAT,"Material image receipt is malformed");
    int32_t index; memcpy(&index,v+7,4);
    row->options=(qa_scene_image_options){.family=(qa_scene_family)v[0],.wrap=(qa_scene_wrap)v[1],
        .filter=(qa_scene_filter)v[2],.usage=(qa_scene_image_usage)v[3],.mipmap=v[4]!=0,
        .transparent=v[5]!=0,.fullbright_only=v[6]!=0,.transparent_index=index};
    row->missing=v[8]!=0; row->palette_attempted=v[9]!=0;
    row->rejection=(qa_status)v[10]; row->palette_error=(qa_status)v[11];
    if (v[12]) { memcpy(row->palette,bytes.data+at,768); row->options.palette_rgb=(qa_bytes){row->palette,768}; at+=768; }
    if (v[13]) { memcpy(row->translation,bytes.data+at,256); row->options.translation=(qa_bytes){row->translation,256}; }
    if ((row->missing && row->rejection==QA_OK) ||
        (row->palette_path[0] && !row->palette_attempted) ||
        (!row->palette_attempted && row->palette_error!=QA_OK) ||
        (row->palette_attempted && !row->palette_path[0] && row->palette_error==QA_OK))
        return application_fail(error,QA_ERROR_FORMAT,"Material image receipt contradicts its retained admission");
    const char *aliases[]={row->alias,row->source_alias,row->logical,row->palette_path};
    for (size_t i=0;i<4;++i) if (*aliases[i]) {
        if (strncmp(aliases[i],"models/qa/",10) && strncmp(aliases[i],"players/qa/",11))
            return application_fail(error,QA_ERROR_FORMAT,"Material image receipt escapes its negotiated resource namespace");
        char *normal=qa_vfs_normalize_path(aliases[i],error);
        if (!normal) return false;
        bool same=!strcmp(normal,aliases[i]); free(normal);
        if (!same) return application_fail(error,QA_ERROR_FORMAT,"Material image alias is not canonical");
    }
    return true;
}
static bool aliases_read(qa_bytes bytes,material_alias_fn fn,void *context,qa_error *error)
{
    size_t at=0;
    while (at<bytes.size && bytes.data[at]!='\n') ++at;
    if (at<bytes.size) ++at;
    static const char prefix[]="//qa-image 1 ";
    while (bytes.size-at>=sizeof(prefix)-1 && !memcmp(bytes.data+at,prefix,sizeof(prefix)-1)) {
        at+=sizeof(prefix)-1; size_t begin=at;
        while (at<bytes.size && bytes.data[at]!='\n') ++at;
        if (at==bytes.size || (at-begin)%2 || at-begin>19000)
            return application_fail(error,QA_ERROR_FORMAT,"Material image receipt is truncated");
        qa_buffer raw={.size=(at-begin)/2}; bool ok=true;
        raw.data=raw.size?malloc(raw.size):NULL;
        if (raw.size && !raw.data) return application_fail(error,QA_ERROR_MEMORY,"Decoding actual image receipt");
        for (size_t i=begin;ok && i<at;i+=2) {
            int a=hex_value(bytes.data[i]),b=hex_value(bytes.data[i+1]);
            if (a<0 || b<0) { ok=false; break; }
            raw.data[(i-begin)/2]=(uint8_t)((a<<4)|b);
        }
        material_alias row={0};
        ok=ok && alias_decode((qa_bytes){raw.data,raw.size},&row,error) && fn(context,&row,error);
        qa_buffer_free(&raw);
        if (!ok) {
            if (!error || error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Material image receipt has invalid encoding");
            return false;
        }
        ++at;
    }
    if (bytes.size-at>=11 && !memcmp(bytes.data+at,"//qa-image ",11))
        return application_fail(error,QA_ERROR_FORMAT,"Material image receipt version is unsupported");
    return true;
}
static bool alias_valid(void *context,const material_alias *row,qa_error *error)
{ (void)context; (void)row; (void)error; return true; }
bool qa_q2_material_script_scope(qa_bytes bytes,qa_q2_material_scope *out,qa_error *error)
{
    static const char prefix[]="//qa-material 1 q";
    if (!out || !bytes.data || bytes.size<sizeof(prefix)+3 || memcmp(bytes.data,prefix,sizeof(prefix)-1))
        return application_fail(error,QA_ERROR_FORMAT,"Material artifact lacks its actual Source scope declaration");
    size_t at=sizeof(prefix)-1; uint8_t family=bytes.data[at++];
    if (family<'1' || family>'3' || bytes.data[at++]!=' ')
        return application_fail(error,QA_ERROR_FORMAT,"Material artifact Source family is invalid");
    qa_q2_material_scope scope={.family=(qa_scene_family)(family-'1')};
    if (bytes.data[at]=='-') ++at;
    else {
        if (bytes.size-at<1537) return application_fail(error,QA_ERROR_FORMAT,"Material Source palette is truncated");
        for (size_t i=0;i<768;++i) {
            int a=hex_value(bytes.data[at++]),b=hex_value(bytes.data[at++]);
            if (a<0 || b<0) return application_fail(error,QA_ERROR_FORMAT,"Material Source palette contains invalid bytes");
            scope.palette[i]=(uint8_t)((a<<4)|b);
        }
        scope.palette_present=true;
    }
    if (at>=bytes.size || bytes.data[at]!='\n')
        return application_fail(error,QA_ERROR_FORMAT,"Material Source scope has trailing data");
    *out=scope; return true;
}

static bool program_span(qa_bytes bytes,material_token *name,size_t *begin,size_t *end,qa_error *error)
{
    material_reader r={.bytes=bytes,.error=error}; material_token t;
    if (!bytes.data || !next_token(&r,true,name) || !*name->text ||
        !next_token(&r,true,&t) || t.text[0]!='{')
        return application_fail(error,QA_ERROR_FORMAT,"Material artifact must contain its actual named program");
    *begin=t.begin; size_t depth=1;
    while (depth && next_token(&r,true,&t)) {
        if (t.text[0]=='{') ++depth;
        else if (t.text[0]=='}') --depth;
    }
    if (r.failed) return false;
    if (depth) return application_fail(error,QA_ERROR_FORMAT,"Material artifact program is truncated");
    *end=t.end;
    if (next_token(&r,true,&t) || r.failed || r.at!=bytes.size)
        return application_fail(error,QA_ERROR_FORMAT,"Material artifact has extra programs or trailing bytes");
    return true;
}
static void skip_line(material_reader *r)
{
    material_token ignored;
    for (;;) {
        size_t at=r->at;
        if (!next_token(r,false,&ignored)) break;
        if (!strcmp(ignored.text,"{") || !strcmp(ignored.text,"}")) { r->at=at; break; }
    }
}
typedef struct alias_dependencies { qa_q2_material_dependency_fn fn; void *context; } alias_dependencies;
static bool alias_dependency(void *context,const material_alias *row,qa_error *error)
{
    alias_dependencies *d=context;
    const char *paths[]={row->missing?NULL:row->source_alias,row->logical[0]?row->logical:NULL,
        row->palette_path[0]?row->palette_path:NULL};
    for (size_t i=0;i<3;++i) if (paths[i]) {
        qa_q2_material_dependency value={.kind=QA_Q2_MATERIAL_IMAGE,.path=paths[i]};
        if (!d->fn(d->context,&value,error)) return false;
    }
    return true;
}
typedef struct alias_match { const qa_q2_material_dependency *dependency; bool found; } alias_match;
static bool alias_matches(void *context,const material_alias *row,qa_error *error)
{
    (void)error; alias_match *m=context;
    if (m->dependency->kind==QA_Q2_MATERIAL_IMAGE && !strcmp(row->alias,m->dependency->path)) m->found=true;
    if (m->dependency->kind==QA_Q2_MATERIAL_SKY) {
        size_t length=strlen(m->dependency->path);
        if (!strncmp(row->alias,m->dependency->path,length) && !strncmp(row->alias+length,"_rt.",4)) m->found=true;
    }
    return true;
}
static bool dependency_emit(qa_bytes bytes,const qa_q2_material_dependency *value,
    qa_q2_material_dependency_fn fn,void *context,qa_error *error)
{
    alias_match match={.dependency=value};
    return aliases_read(bytes,alias_matches,&match,error) && (match.found || fn(context,value,error));
}
static bool dependency(material_reader *r,qa_q2_material_dependency_kind kind,bool builtin_images,
    qa_q2_material_dependency_fn fn,void *context,material_token *token,qa_error *error)
{
    r->missing_parameter=false;
    if (!next_token(r,false,token)) { r->missing_parameter=true; return !r->failed; }
    qa_q2_material_dependency value={kind,token->text,token->begin,token->end,builtin_images};
    return dependency_emit(r->bytes,&value,fn,context,error);
}
static bool consume(material_reader *r,size_t count)
{ material_token t; while (count--) if (!next_token(r,false,&t)) return false; return true; }

bool qa_q2_material_script_dependencies(qa_bytes bytes,qa_q2_material_dependency_fn fn,
    void *context,qa_error *error)
{
    material_token name,t,arg; size_t begin,end;
    alias_dependencies aliases={fn,context};
    if (!fn || !program_span(bytes,&name,&begin,&end,error) ||
        !aliases_read(bytes,alias_dependency,&aliases,error)) return false;
    material_reader r={.bytes=bytes,.at=begin,.error=error}; size_t depth=0,stage_count=0;
    bool rejected=false;
    while (r.at<end && next_token(&r,true,&t)) {
        if (t.text[0]=='{') { ++depth; if (depth==2 && ++stage_count>8) rejected=true; continue; }
        if (t.text[0]=='}') { if (depth) --depth; continue; }
        if (rejected) continue;
        if (depth==1 && equal(t.text,"skyparms")) {
            if (!dependency(&r,QA_Q2_MATERIAL_SKY,false,fn,context,&arg,error)) return false;
            if (r.missing_parameter || !consume(&r,1)) continue;
            if (!dependency(&r,QA_Q2_MATERIAL_SKY,false,fn,context,&arg,error)) return false;
        } else if (depth==2 && (equal(t.text,"map") || equal(t.text,"clampmap"))) {
            if (!dependency(&r,QA_Q2_MATERIAL_IMAGE,equal(t.text,"map"),fn,context,&arg,error)) return false;
            if (r.missing_parameter) rejected=true;
        } else if (depth==2 && equal(t.text,"videomap")) {
            if (!dependency(&r,QA_Q2_MATERIAL_MOVIE,false,fn,context,&arg,error)) return false;
            if (r.missing_parameter) rejected=true;
        } else if (depth==2 && equal(t.text,"animmap")) {
            if (!consume(&r,1)) { rejected=true; continue; }
            size_t count=0;
            for (;;) {
                size_t at=r.at;
                if (!next_token(&r,false,&arg)) break;
                if (!strcmp(arg.text,"{") || !strcmp(arg.text,"}")) { r.at=at; break; }
                if (count++<8) {
                    qa_q2_material_dependency value={QA_Q2_MATERIAL_IMAGE,arg.text,arg.begin,arg.end,false};
                    if (!dependency_emit(bytes,&value,fn,context,error)) return false;
                }
            }
        } else if (depth==2 && equal(t.text,"blendfunc")) {
            if (next_token(&r,false,&arg) && !equal(arg.text,"add") && !equal(arg.text,"filter") && !equal(arg.text,"blend")) consume(&r,1);
        } else if (depth==2 && (equal(t.text,"rgbgen") || equal(t.text,"alphagen"))) {
            if (next_token(&r,false,&arg)) {
                if (equal(arg.text,"wave")) consume(&r,5);
                else if (equal(arg.text,"const")) consume(&r,equal(t.text,"rgbgen")?5:1);
                else if (equal(arg.text,"portal")) consume(&r,1);
            }
        } else if (depth==2 && (equal(t.text,"tcgen") || equal(t.text,"texgen"))) {
            if (next_token(&r,false,&arg) && equal(arg.text,"vector")) consume(&r,10);
        } else if (depth==1 && equal(t.text,"deformvertexes")) {
            if (next_token(&r,false,&arg)) {
                if (equal(arg.text,"bulge")) consume(&r,3);
                else if (equal(arg.text,"normal")) consume(&r,2);
                else if (equal(arg.text,"move")) consume(&r,8);
                else if (equal(arg.text,"wave")) consume(&r,6);
            }
        } else if (depth==1 && equal(t.text,"fogparms")) { consume(&r,6); skip_line(&r); }
        else if (depth==1 && equal(t.text,"q3map_sun")) consume(&r,6);
        else if ((depth==2 && equal(t.text,"tcmod")) ||
            (depth==1 && (prefix(t.text,"qer") || prefix(t.text,"q3map") || equal(t.text,"tesssize")))) skip_line(&r);
        else if ((depth==2 && (equal(t.text,"depthfunc") || equal(t.text,"alphafunc"))) ||
            (depth==1 && (equal(t.text,"surfaceparm") || equal(t.text,"clamptime") || equal(t.text,"sort") || equal(t.text,"cull") || equal(t.text,"light")))) consume(&r,1);
        else if ((depth==2 && (equal(t.text,"depthwrite") || equal(t.text,"detail"))) ||
            (depth==1 && (equal(t.text,"nomipmaps") || equal(t.text,"nopicmip") || equal(t.text,"polygonoffset") || equal(t.text,"entitymergable") || equal(t.text,"portal")))) {}
        else rejected=true;
    }
    return !r.failed;
}

typedef struct alias_import { qa_scene_resources *bank; qa_vfs *view; } alias_import;
static bool import_alias(void *context,const material_alias *row,qa_error *error)
{
    alias_import *owner=context;
    qa_resource *source=NULL,*logical=NULL,*palette=NULL;
    qa_vfs_acquisition opening={0},logical_opening={0},palette_opening={0};
    bool ok=(row->missing || qa_vfs_acquire_receipt(owner->view,row->source_alias,&source,&opening,error)) &&
        (!row->logical[0] || qa_vfs_acquire_receipt(owner->view,row->logical,&logical,&logical_opening,error)) &&
        (!row->palette_path[0] || qa_vfs_acquire_receipt(owner->view,row->palette_path,&palette,&palette_opening,error));
    qa_scene_image_alias_source actual={.source=source,.logical_source=logical,.palette_source=palette,
        .source_opening=source?&opening:NULL,.logical_opening=logical?&logical_opening:NULL,
        .palette_opening=palette?&palette_opening:NULL,.request=row->request,.source_path=row->path,
        .logical_path=row->logical_path,.decode_options=row->options,.palette_attempted=row->palette_attempted,
        .palette_error=row->palette_error,.source_error=row->rejection};
    if (ok) ok=qa_scene_image_alias_bind(owner->bank,row->alias,&actual,error);
    qa_resource_release(source); qa_resource_release(logical); qa_resource_release(palette);
    qa_vfs_acquisition_dispose(&opening); qa_vfs_acquisition_dispose(&logical_opening);
    qa_vfs_acquisition_dispose(&palette_opening); return ok;
}
bool qa_q2_material_script_import(qa_material_library *library,qa_scene_resources *bank,qa_vfs *view,const char *alias,qa_bytes bytes,
    const qa_scene_image_options *options,qa_error *error)
{
    material_token name; size_t begin,end; qa_buffer source={0};
    qa_q2_material_scope scope={0};
    if (!library || !bank || !view || !qa_vfs_lookup_equal(qa_scene_resources_files(bank),view) ||
        !alias || !*alias || !options || !program_span(bytes,&name,&begin,&end,error) ||
        !qa_q2_material_script_scope(bytes,&scope,error) || !aliases_read(bytes,alias_valid,NULL,error)) return false;
    for (const unsigned char *p=(const unsigned char*)alias; *p; ++p)
        if (*p<=' ' || *p=='"' || *p=='{' || *p=='}')
            return application_fail(error,QA_ERROR_FORMAT,"Material delivery alias is not a shader name");
    qa_scene_image_options actual=*options;
    actual.family=scope.family; actual.palette_rgb=scope.palette_present?(qa_bytes){scope.palette,768}:(qa_bytes){0};
    alias_import owner={bank,view};
    bool ok=aliases_read(bytes,import_alias,&owner,error) && text_append(&source,alias,error) && text_append(&source,"\n",error) &&
        append(&source,(qa_bytes){bytes.data+begin,end-begin},error) &&
        qa_material_library_parse_scoped(library,(qa_bytes){source.data,source.size},&actual,error);
    qa_buffer_free(&source); return ok;
}

static bool image_document(qa_bytes bytes,qa_error *error)
{
    static const char header[]="//qa-image-owner 1\n//qa-image 1 ";
    if (!bytes.data || bytes.size<sizeof(header) || memcmp(bytes.data,header,sizeof(header)-1) ||
        bytes.data[bytes.size-1]!='\n' || memchr(bytes.data+sizeof(header)-1,'\n',bytes.size-sizeof(header)))
        return application_fail(error,QA_ERROR_FORMAT,"Image admission artifact requires one complete actual receipt");
    return true;
}
bool qa_q2_material_image_dependencies(qa_bytes bytes,qa_q2_material_dependency_fn fn,void *context,qa_error *error)
{
    alias_dependencies owner={fn,context};
    return fn && image_document(bytes,error) && aliases_read(bytes,alias_dependency,&owner,error);
}
typedef struct image_import { alias_import owner; const char *name; } image_import;
static bool import_image(void *context,const material_alias *row,qa_error *error)
{
    image_import *owner=context;
    return !strcmp(owner->name,row->alias)?import_alias(&owner->owner,row,error):
        application_fail(error,QA_ERROR_FORMAT,"Image admission artifact belongs to another qualified model image");
}
bool qa_q2_material_image_import(qa_scene_resources *bank,qa_vfs *view,const char *name,qa_bytes bytes,qa_error *error)
{
    image_import owner={{bank,view},name};
    return bank && view && qa_vfs_lookup_equal(qa_scene_resources_files(bank),view) && name &&
        image_document(bytes,error) && aliases_read(bytes,alias_valid,NULL,error) &&
        aliases_read(bytes,import_image,&owner,error);
}

typedef struct closure_writer {
    qa_application_network_q2 *owner;
    const application_q2_held_resource *model;
    qa_scene_resources *images;
    qa_scene_family family;
    qa_bytes source;
    qa_buffer bytes;
    size_t position;
    size_t *dependencies,count;
} closure_writer;
static bool image_path_matches(const char *,const char *,qa_error *);

static bool alias_from_held(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,material_alias *row,qa_error *error)
{
    if (held->kind!=APPLICATION_Q2_HELD_ALIAS || !held->image_request || !held->path || !held->wire_path)
        return application_fail(error,QA_ERROR_FORMAT,"Material alias lost its actual retained request");
    const application_q2_held_resource *logical=held->image_logical_dependency<owner->held_resource_count?
        owner->held_resources+held->image_logical_dependency:NULL;
    const application_q2_held_resource *palette=held->image_palette_dependency<owner->held_resource_count?
        owner->held_resources+held->image_palette_dependency:NULL;
    const char *strings[]={held->wire_path,held->wire_path,held->image_request,held->path,logical?logical->wire_path:"",
        logical?held->image_logical_path:"",palette?palette->wire_path:""};
    char *outputs[]={row->alias,row->source_alias,row->request,row->path,row->logical,row->logical_path,row->palette_path};
    for (size_t i=0;i<7;++i) {
        if (!strings[i] || strlen(strings[i])>=1024)
            return application_fail(error,QA_ERROR_FORMAT,"Material alias path exceeds its real extent");
        strcpy(outputs[i],strings[i]);
    }
    if (held->sky_face) {
        char *dot=strrchr(row->alias,'.');
        if (!dot || (size_t)(dot-row->alias)>sizeof(row->alias)-5)
            return application_fail(error,QA_ERROR_FORMAT,"Sky alias lost its actual shader face request");
        memcpy(dot,".tga",5);
    }
    row->options=held->image_options;
    row->options.palette_rgb=(qa_bytes){held->image_palette.data,held->image_palette.size};
    row->options.translation=(qa_bytes){held->image_translation.data,held->image_translation.size};
    row->missing=held->missing; row->rejection=held->image_rejection; row->palette_error=held->image_palette_error;
    row->palette_attempted=held->image_palette_attempted;
    return true;
}
static bool aliases_write(qa_buffer *out,const qa_application_network_q2 *owner,
    const size_t *dependencies,size_t count,qa_error *error)
{
    for (size_t i=0;i<count;++i) {
        if (dependencies[i]>=owner->held_resource_count)
            return application_fail(error,QA_ERROR_FORMAT,"Material metadata references an absent real holder");
        const application_q2_held_resource *held=owner->held_resources+dependencies[i];
        if (held->kind!=APPLICATION_Q2_HELD_ALIAS) continue;
        bool duplicate=false;
        for (size_t j=0;j<i;++j) if (dependencies[j]==dependencies[i]) duplicate=true;
        if (!duplicate) {
            material_alias row={0};
            if (!alias_from_held(owner,held,&row,error) || !alias_encode(&row,out,error)) return false;
        }
    }
    return true;
}
bool application_network_q2_materials_image_receipt_encode(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,const char *name,qa_buffer *out,qa_error *error)
{
    if (!owner || !held || !name || !*name || strlen(name)>=1024 || !out || out->data || out->size)
        return application_fail(error,QA_ERROR_ARGUMENT,"Image receipt encoding needs its actual alias and empty output");
    material_alias row={0}; qa_buffer bytes={0};
    bool ok=alias_from_held(owner,held,&row,error);
    if (ok) { strcpy(row.alias,name); ok=text_append(&bytes,"//qa-image-owner 1\n",error) && alias_encode(&row,&bytes,error); }
    if (ok) { *out=bytes; bytes=(qa_buffer){0}; }
    qa_buffer_free(&bytes); return ok;
}

static bool dependency_keep(closure_writer *w,size_t index,qa_error *error)
{
    if (w->count>=SIZE_MAX/sizeof(*w->dependencies)-1)
        return application_fail(error,QA_ERROR_MEMORY,"Material dependency count overflows");
    size_t *rows=realloc(w->dependencies,(w->count+1)*sizeof(*rows));
    if (!rows) return application_fail(error,QA_ERROR_MEMORY,"Retaining material dependency owners");
    rows[w->count++]=index; w->dependencies=rows; return true;
}

typedef struct captured_image {
    qa_scene_image_load_receipt observed;
    qa_scene_image_options options;
    qa_scene_palette_source palette_source;
    application_q2_image_receipt receipt;
    char *request,*missing;
} captured_image;
static void captured_image_dispose(captured_image *row)
{
    qa_scene_image_load_receipt_dispose(&row->observed); free(row->request); free(row->missing);
    *row=(captured_image){0};
}
static bool capture_image(closure_writer *w,const char *name,qa_scene_image_usage usage,
    captured_image *row,qa_error *error)
{
    qa_scene_image_load_receipt *observed=&row->observed; qa_scene_image *image=NULL; qa_error issue={0};
    row->request=qa_scene_model_image_path(name,error);
    if (!row->request) return false;
    row->options=(qa_scene_image_options){.family=w->family,.wrap=QA_SCENE_REPEAT,.usage=usage};
    bool loaded=qa_scene_image_load_observed(w->images,row->request,&row->options,&image,observed,&issue);
    bool ok=loaded || issue.code!=QA_ERROR_MEMORY;
    if (ok && !observed->source) {
        qa_buffer path={0}; const char *slash=strrchr(row->request,'/'),*dot=strrchr(row->request,'.');
        ok=text_append(&path,row->request,error) && ((dot && (!slash || dot>slash)) || text_append(&path,".tga",error)) &&
            append(&path,(qa_bytes){(const uint8_t*)"",1},error);
        if (ok) row->missing=(char*)path.data; else qa_buffer_free(&path);
    }
    qa_bytes palette={0};
    (void)qa_scene_resources_palette_read(w->images,w->family,&palette);
    row->palette_source=(qa_scene_palette_source){observed->palette_source,&observed->palette_opening};
    row->receipt=(application_q2_image_receipt){.request=row->request,.path=observed->source?observed->source_opening.path:row->missing,
        .source=observed->source,.opening=observed->source?&observed->source_opening:NULL,
        .logical_path=observed->logical_source?observed->logical_path:NULL,
        .logical_source=observed->logical_source,.logical_opening=observed->logical_source?&observed->logical_opening:NULL,
        .options=&row->options,.palette_rgb=palette,.palette_source=observed->palette_source?&row->palette_source:NULL,
        .palette_attempted=observed->palette_attempted,.palette_error=observed->palette_error,
        .rejection=loaded?QA_OK:issue.code};
    if (!ok && issue.code==QA_ERROR_MEMORY && error) *error=issue;
    qa_scene_image_release(image); return ok;
}
static bool image_dependency(closure_writer *w,const char *name,size_t *out,qa_error *error)
{
    captured_image row={0};
    bool ok=capture_image(w,name,QA_IMAGE_USAGE_SKIN,&row,error) &&
        application_network_q2_dependency_alias(w->owner,w->model,&row.receipt,out,error);
    captured_image_dispose(&row); return ok;
}

static bool rewrite_dependency(void *context,const qa_q2_material_dependency *dependency,qa_error *error)
{
    closure_writer *w=context; const char *alias=NULL; char sky[64]; size_t index;
    if ((dependency->kind==QA_Q2_MATERIAL_SKY && !strcmp(dependency->path,"-")) ||
        qa_q2_material_dependency_builtin(dependency)) return true;
    if (dependency->kind==QA_Q2_MATERIAL_IMAGE) {
        if (!image_dependency(w,dependency->path,&index,error) || !dependency_keep(w,index,error)) return false;
        alias=w->owner->held_resources[index].wire_path;
    } else if (dependency->kind==QA_Q2_MATERIAL_MOVIE) {
        qa_buffer path={0}; bool ok=true;
        if (!strchr(dependency->path,'/') && !strchr(dependency->path,'\\')) ok=text_append(&path,"video/",error);
        ok=ok && text_append(&path,dependency->path,error) && append(&path,(qa_bytes){(const uint8_t*)"",1},error);
        if (ok) ok=application_network_q2_dependency(w->owner,w->model,(const char*)path.data,&index,error) && dependency_keep(w,index,error);
        qa_buffer_free(&path); if (!ok) return false;
        alias=w->owner->held_resources[index].wire_path;
    } else {
        static const char *const faces[]={"rt","bk","lf","ft","up","dn"};
        size_t indices[6]; captured_image rows[6]={0}; application_q2_image_receipt receipts[6]={0};
        bool ok=true;
        for (size_t i=0;ok && i<6;++i) {
            qa_buffer name={0};
            ok=text_append(&name,dependency->path,error) && text_append(&name,"_",error) &&
                text_append(&name,faces[i],error) && text_append(&name,".tga",error) &&
                append(&name,(qa_bytes){(const uint8_t*)"",1},error) &&
                capture_image(w,(const char*)name.data,QA_IMAGE_USAGE_SKY,rows+i,error);
            if (ok) receipts[i]=rows[i].receipt;
            qa_buffer_free(&name);
        }
        if (ok) ok=application_network_q2_sky_aliases(w->owner,w->model,dependency->path,
            receipts,indices,sky,error);
        for (size_t i=0;i<6;++i) captured_image_dispose(rows+i);
        if (!ok) return false;
        for (size_t i=0;i<6;++i) if (!dependency_keep(w,indices[i],error)) return false;
        alias=sky;
    }
    if (dependency->begin<w->position || dependency->end>w->source.size)
        return application_fail(error,QA_ERROR_FORMAT,"Material closure token spans overlap");
    bool ok=append(&w->bytes,(qa_bytes){w->source.data+w->position,dependency->begin-w->position},error) &&
        text_append(&w->bytes,alias,error);
    if (ok) w->position=dependency->end;
    return ok;
}

static bool image_png(const qa_scene_image *image,qa_buffer *out,qa_error *error)
{
    if (!image || image->kind==QA_SCENE_DEPTH32F || !image->level_count || image->animation_count>1 ||
        !image->levels || !image->levels[0].pixels)
        return application_fail(error,QA_ERROR_ARGUMENT,"Image closure requires its actual static color pixels");
    const qa_scene_image_level *level=image->levels;
    qa_image pixels={.width=level->width,.height=level->height,
        .rgba={(uint8_t*)level->pixels,level->bytes}};
    return qa_image_encode_png(&pixels,out,error);
}
static bool implicit_image(closure_writer *w,const char *name,size_t *out,qa_error *error)
{
    size_t alias;
    return image_dependency(w,name,&alias,error) &&
        application_network_q2_dependency_image_receipt(w->owner,w->model,alias,out,error);
}
static bool shader_resource(closure_writer *w,qa_material_library *library,const char *name,
    size_t *out,qa_error *error)
{
    qa_material_script_view script;
    if (!qa_material_library_script_read(library,name,&script)) {
        qa_q2_material_dependency image={.kind=QA_Q2_MATERIAL_IMAGE,.path=name};
        if (qa_q2_material_dependency_builtin(&image)) { *out=SIZE_MAX; return true; }
        return implicit_image(w,name,out,error);
    }
    qa_buffer original={0};
    bool ok=text_append(&original,script.name,error) && text_append(&original,"\n",error) && append(&original,script.body,error);
    closure_writer body={.owner=w->owner,.model=w->model,.images=w->images,.family=w->family,
        .source={original.data,original.size}};
    qa_buffer artifact={0}; qa_bytes palette={0}; qa_scene_palette_source palette_source={0};
    if (ok) ok=qa_q2_material_script_dependencies(body.source,rewrite_dependency,&body,error) &&
        append(&body.bytes,(qa_bytes){body.source.data+body.position,body.source.size-body.position},error);
    if (ok) {
        (void)qa_scene_resources_palette_read(w->images,w->family,&palette);
        bool held_palette=qa_scene_resources_palette_source_read(w->images,w->family,&palette_source);
        if (palette.size && !held_palette) ok=application_fail(error,QA_ERROR_ARGUMENT,"Material closure lost its actual palette admission");
        ok=ok && scope_write(&artifact,w->family,palette,error) &&
        aliases_write(&artifact,w->owner,body.dependencies,body.count,error) &&
        append(&artifact,(qa_bytes){body.bytes.data,body.bytes.size},error) &&
        application_network_q2_material_resource(w->owner,w->model,&script,
            (qa_bytes){artifact.data,artifact.size},w->family,palette,held_palette?&palette_source:NULL,
            body.dependencies,body.count,out,error);
    }
    qa_buffer_free(&original); qa_buffer_free(&body.bytes); qa_buffer_free(&artifact); free(body.dependencies); return ok;
}

bool application_network_q2_materials_derive(qa_application_network_q2 *owner,
    application_q2_held_resource *held,qa_error *error)
{
    if (!owner || !held || !held->view || !held->resource || held->wire_bytes.data || held->dependency_count)
        return application_fail(error,QA_ERROR_ARGUMENT,"MD3 materials require their actual fresh BODY model holder");
    qa_bytes original=qa_resource_bytes(held->resource); qa_model model={0};
    if (!qa_model_load(original,&model,error)) return false;
    if (model.format!=QA_MODEL_MD3) { qa_model_free(&model); return application_fail(error,QA_ERROR_ARGUMENT,"Material closure needs an actual decoded MD3"); }
    application_provider *provider=NULL;
    for (size_t i=0;i<owner->app->provider_count;++i)
        if (owner->app->providers[i]->owner==held->provider) {
            if (provider) { qa_model_free(&model); return application_fail(error,QA_ERROR_ARGUMENT,"MD3 BODY Source owner is ambiguous"); }
            provider=owner->app->providers[i];
        }
    if (!provider || !provider->product) { qa_model_free(&model); return application_fail(error,QA_ERROR_ARGUMENT,"MD3 catalog lost its genuine BODY Source"); }
    qa_scene_family family=provider->product->family==QA_GAME_Q1?QA_SCENE_Q1:
        provider->product->family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q3;
    qa_scene_resources *images=qa_scene_resources_create(held->view,error);
    qa_material_library *library=images?qa_material_library_create_detached(images,error):NULL;
    qa_scene_image_options options={.family=family};
    bool ok=library && qa_material_library_load_scripts(library,held->view,&options,error);
    closure_writer w={.owner=owner,.model=held,.images=images,.family=family};
    qa_buffer derived={0};
    if (ok) ok=append(&derived,original,error);
    size_t base=original.size>=108?qa_load_u32le(original.data+100):0;
    for (uint32_t i=0;ok && i<model.mesh_count;++i) {
        size_t first=base+qa_load_u32le(original.data+base+92);
        for (uint32_t j=0;ok && j<model.meshes[i].shader_count;++j) {
            const char *name=model.meshes[i].shaders[j].name; size_t index=SIZE_MAX;
            if (*name) ok=shader_resource(&w,library,name,&index,error);
            if (ok) ok=dependency_keep(&w,index,error);
            if (ok && index!=SIZE_MAX) {
                const char *alias=owner->held_resources[index].wire_path;
                if (strlen(alias)>=64) ok=application_fail(error,QA_ERROR_FORMAT,"Material alias exceeds the genuine MD3 shader name extent");
                else { memset(derived.data+first+j*68,0,64); memcpy(derived.data+first+j*68,alias,strlen(alias)); }
            }
        }
        base+=qa_load_u32le(original.data+base+104);
    }
    if (ok) { held->wire_bytes=derived; derived=(qa_buffer){0}; held->dependencies=w.dependencies;
        held->dependency_count=w.count; w.dependencies=NULL; }
    qa_buffer_free(&derived); free(w.dependencies); qa_material_library_destroy(library);
    qa_scene_resources_destroy(images); qa_model_free(&model); return ok;
}

typedef struct closure_replay {
    const qa_application_network_q2 *owner;
    const application_q2_held_resource *held;
    qa_bytes source;
    qa_buffer bytes;
    size_t position,used;
} closure_replay;

static const application_q2_held_resource *replay_next(closure_replay *r,qa_error *error)
{
    if (r->used==r->held->dependency_count) {
        application_fail(error,QA_ERROR_FORMAT,"Material program lost an actual media dependency"); return NULL;
    }
    size_t index=r->held->dependencies[r->used++];
    const application_q2_held_resource *d=index<r->owner->held_resource_count?&r->owner->held_resources[index]:NULL;
    if (!d || (d->kind!=APPLICATION_Q2_HELD_DEPENDENCY && d->kind!=APPLICATION_Q2_HELD_ALIAS) || d->provider!=r->held->provider ||
        !qa_sha256_equal(&d->identity,&r->held->identity) || !qa_sha256_equal(&d->authority,&r->held->authority) ||
        !qa_vfs_lookup_equal(d->view,r->held->view)) {
        application_fail(error,QA_ERROR_FORMAT,"Material program lost its actual BODY media scope"); return NULL;
    }
    if (d->kind==APPLICATION_Q2_HELD_ALIAS && !application_network_q2_materials_alias_validate(r->owner,d,error)) return NULL;
    return d;
}
static bool image_path_matches(const char *requested,const char *actual,qa_error *error)
{
    char *path=qa_scene_model_image_path(requested,error);
    if (!path) return false;
    const char *slash=strrchr(path,'/'),*dot=strrchr(path,'.');
    size_t base=dot && (!slash || dot>slash)?(size_t)(dot-path):strlen(path);
    bool ok=!strncmp(path,actual,base) && actual[base]=='.';
    if (ok) {
        static const char *const extensions[]={".lmp",".wal",".pcx",".png",".jpg",".tga",".jpeg",".bmp",".gif"};
        ok=dot && !strcmp(actual+base,dot);
        for (size_t i=0;!ok && i<sizeof(extensions)/sizeof(*extensions);++i) ok=!strcmp(actual+base,extensions[i]);
    }
    free(path); return ok;
}
static bool replay_dependency(void *context,const qa_q2_material_dependency *dependency,qa_error *error)
{
    closure_replay *r=context; const char *alias=NULL; char sky[64]; bool ok=true;
    if ((dependency->kind==QA_Q2_MATERIAL_SKY && !strcmp(dependency->path,"-")) ||
        qa_q2_material_dependency_builtin(dependency)) return true;
    if (dependency->kind==QA_Q2_MATERIAL_SKY) {
        static const char *const faces[]={"rt","bk","lf","ft","up","dn"};
        const application_q2_held_resource *first=NULL,*rows[6]={0};
        char *base=qa_scene_model_image_path(dependency->path,error);
        if (!base) return false;
        for (size_t i=0;ok && i<6;++i) {
            const application_q2_held_resource *d=replay_next(r,error); qa_buffer requested={0};
            rows[i]=d;
            ok=d && d->sky_base && !strcmp(d->sky_base,base) && d->sky_face==i+1 &&
                (!first || qa_sha256_equal(&first->sky_group,&d->sky_group)) &&
                text_append(&requested,dependency->path,error) && text_append(&requested,"_",error) &&
                text_append(&requested,faces[i],error) && text_append(&requested,".tga",error) &&
                append(&requested,(qa_bytes){(const uint8_t*)"",1},error) &&
                image_path_matches((const char*)requested.data,d->path,error);
            qa_buffer_free(&requested);
            if (ok && !first) {
                first=d; const char *suffix=strrchr(d->wire_path,'_');
                size_t size=suffix?(size_t)(suffix-d->wire_path):0;
                if (!size || size>=sizeof(sky)) ok=false;
                else { memcpy(sky,d->wire_path,size); sky[size]=0; }
            }
        }
        if (ok) ok=application_network_q2_sky_group_valid(r->held,rows);
        free(base);
        alias=sky;
    } else {
        const application_q2_held_resource *d=replay_next(r,error);
        if (!d || d->sky_face) return application_fail(error,QA_ERROR_FORMAT,"Material token references an unrelated sky face");
        if (dependency->kind==QA_Q2_MATERIAL_IMAGE) ok=image_path_matches(dependency->path,d->path,error);
        else {
            qa_buffer path={0};
            if (!strchr(dependency->path,'/') && !strchr(dependency->path,'\\')) ok=text_append(&path,"video/",error);
            ok=ok && text_append(&path,dependency->path,error) && append(&path,(qa_bytes){(const uint8_t*)"",1},error);
            char *normalized=ok?qa_scene_model_image_path((const char*)path.data,error):NULL;
            ok=normalized && !strcmp(normalized,d->path); free(normalized); qa_buffer_free(&path);
        }
        alias=d->wire_path;
    }
    if (!ok) return application_fail(error,QA_ERROR_FORMAT,"Material dependency changes its actual Source request");
    if (dependency->begin<r->position || dependency->end>r->source.size)
        return application_fail(error,QA_ERROR_FORMAT,"Restored material token spans overlap");
    ok=append(&r->bytes,(qa_bytes){r->source.data+r->position,dependency->begin-r->position},error) && text_append(&r->bytes,alias,error);
    if (ok) r->position=dependency->end;
    return ok;
}
static bool palette_valid(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,qa_error *error)
{
    if (!held->image_palette.size) return held->image_palette_dependency==SIZE_MAX;
    size_t index=held->image_palette_dependency;
    const application_q2_held_resource *row=index<owner->held_resource_count?owner->held_resources+index:NULL;
    const char *path=held->image_options.family==QA_SCENE_Q1?"gfx/palette.lmp":"pics/colormap.pcx";
    if (held->image_palette.size!=768 || !held->image_palette.data || !row ||
        row->kind!=APPLICATION_Q2_HELD_DEPENDENCY || row->missing || !row->resource || !row->path ||
        strcmp(row->path,path) || row->provider!=held->provider ||
        !qa_sha256_equal(&row->identity,&held->identity) || !qa_sha256_equal(&row->authority,&held->authority) ||
        !qa_vfs_lookup_equal(row->view,held->view) || !qa_vfs_acquisition_retained(row->view,&row->opening,error))
        return application_fail(error,QA_ERROR_FORMAT,"Image palette differs from its actual Source admission");
    qa_bytes bytes=qa_resource_bytes(row->resource); bool ok;
    if (held->image_options.family==QA_SCENE_Q1)
        ok=bytes.size==768 && !memcmp(bytes.data,held->image_palette.data,768);
    else {
        qa_image image={0};
        ok=qa_image_decode_pcx(bytes,QA_IMAGE_FORMAT,&image,error) && image.palette.size>=1024;
        for (size_t n=0;ok && n<256;++n) ok=!memcmp(image.palette.data+n*4,held->image_palette.data+n*3,3);
        qa_image_free(&image);
    }
    return ok || application_fail(error,QA_ERROR_FORMAT,"Image palette pixels differ from their retained Source file");
}
static bool same_dependency_scope(const application_q2_held_resource *a,const application_q2_held_resource *b)
{
    return a && b && a->provider==b->provider && qa_sha256_equal(&a->identity,&b->identity) &&
        qa_sha256_equal(&a->authority,&b->authority) && qa_vfs_lookup_equal(a->view,b->view);
}
bool application_network_q2_materials_alias_validate(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,qa_error *error)
{
    if (!owner || !held || held->kind!=APPLICATION_Q2_HELD_ALIAS || !held->view || !held->path ||
        !held->image_request || !*held->image_request || held->image_options.source_q3 ||
        (held->resource && !qa_vfs_acquisition_retained(held->view,&held->opening,error)))
        return application_fail(error,QA_ERROR_FORMAT,"Image alias lost its original Source admission");
    const application_q2_held_resource *logical=held->image_logical_dependency<owner->held_resource_count?
        owner->held_resources+held->image_logical_dependency:NULL;
    const application_q2_held_resource *palette=held->image_palette_dependency<owner->held_resource_count?
        owner->held_resources+held->image_palette_dependency:NULL;
    if ((logical && (!same_dependency_scope(held,logical) || !logical->resource ||
        !qa_vfs_acquisition_retained(logical->view,&logical->opening,error))) ||
        (palette && (!same_dependency_scope(held,palette) || !palette->resource ||
        !qa_vfs_acquisition_retained(palette->view,&palette->opening,error))))
        return application_fail(error,QA_ERROR_FORMAT,"Image alias has unrelated logical or palette custody");
    if (palette) {
        qa_bytes bytes=qa_resource_bytes(palette->resource); uint8_t rgb[768]; qa_error issue={0};
        bool decoded;
        if (held->image_options.family==QA_SCENE_Q1) {
            decoded=bytes.size==768;
            if (decoded) memcpy(rgb,bytes.data,768);
            else qa_error_set(&issue,QA_ERROR_FORMAT,0,"Q1 palette requires exactly 256 RGB colors");
        } else {
            qa_image image={0}; decoded=qa_image_decode_pcx(bytes,QA_IMAGE_FORMAT,&image,&issue);
            if (decoded && image.palette.size<1024) { decoded=false; qa_error_set(&issue,QA_ERROR_FORMAT,0,"Q2 colormap has no complete palette"); }
            for (size_t i=0;decoded && i<256;++i) memcpy(rgb+i*3,image.palette.data+i*4,3);
            qa_image_free(&image);
        }
        if (!decoded && issue.code==QA_ERROR_MEMORY) { if (error) *error=issue; return false; }
        if ((decoded?QA_OK:issue.code)!=held->image_palette_error ||
            (decoded && (held->image_palette.size!=768 || !held->image_palette.data ||
                memcmp(rgb,held->image_palette.data,768))))
            return application_fail(error,QA_ERROR_FORMAT,"Image alias changes its genuine Source palette outcome");
    }
    qa_scene_image_alias_source source={.source=held->resource,.source_opening=held->resource?&held->opening:NULL,
        .request=held->image_request,.source_path=held->path,.logical_source=logical?logical->resource:NULL,
        .logical_opening=logical?&logical->opening:NULL,.logical_path=logical?held->image_logical_path:NULL,
        .palette_source=palette?palette->resource:NULL,.palette_opening=palette?&palette->opening:NULL,
        .decode_options=held->image_options,.palette_attempted=held->image_palette_attempted,
        .palette_error=held->image_palette_error,.source_error=held->image_rejection};
    source.decode_options.palette_rgb=(qa_bytes){held->image_palette.data,held->image_palette.size};
    source.decode_options.translation=(qa_bytes){held->image_translation.data,held->image_translation.size};
    qa_scene_resources *bank=qa_scene_resources_create_detached(held->view,error);
    qa_scene_image *image=NULL; qa_error issue={0};
    bool ok=bank && qa_scene_image_alias_bind(bank,"__qa_retained_image",&source,error);
    bool loaded=ok && qa_scene_image_load(bank,"__qa_retained_image",&source.decode_options,&image,&issue);
    if (ok) ok=(loaded?QA_OK:issue.code)==held->image_rejection;
    if (!ok && (!error || error->code==QA_OK))
        application_fail(error,QA_ERROR_FORMAT,"Image alias changes its original Source decode outcome");
    qa_scene_image_release(image); qa_scene_resources_destroy(bank); return ok;
}
bool application_network_q2_materials_image_validate(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,qa_error *error)
{
    if (!owner || !held || held->kind!=APPLICATION_Q2_HELD_IMAGE || !held->resource || !held->path ||
        !held->view || !held->wire_bytes.size || !held->wire_bytes.data ||
        !qa_vfs_acquisition_retained(held->view,&held->opening,error) || !palette_valid(owner,held,error))
        return application_fail(error,QA_ERROR_FORMAT,"Image artifact lost its actual Source bytes or palette");
    const application_provider *provider=NULL;
    for (size_t i=0;i<owner->app->provider_count;++i) if (owner->app->providers[i]->owner==held->provider) {
        if (provider) return application_fail(error,QA_ERROR_FORMAT,"Image artifact has ambiguous BODY ownership");
        provider=owner->app->providers[i];
    }
    if (!provider || !provider->product || held->image_options.family!=
        (provider->product->family==QA_GAME_Q1?QA_SCENE_Q1:provider->product->family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q3))
        return application_fail(error,QA_ERROR_FORMAT,"Image artifact changed its actual BODY family");
    qa_scene_resources *bank=qa_scene_resources_create_detached(NULL,error);
    qa_scene_image *image=NULL; qa_buffer png={0};
    qa_scene_image_options options=held->image_options;
    options.palette_rgb=(qa_bytes){held->image_palette.data,held->image_palette.size};
    options.translation=(qa_bytes){held->image_translation.data,held->image_translation.size};
    bool ok=bank && qa_scene_image_decode_retained(bank,held->path,held->path,
        qa_resource_bytes(held->resource),&options,&image,error) && image_png(image,&png,error) &&
        png.size==held->wire_bytes.size && !memcmp(png.data,held->wire_bytes.data,png.size);
    if (!ok && (!error || error->code==QA_OK))
        application_fail(error,QA_ERROR_FORMAT,"Image artifact differs from its retained Source pixels");
    qa_buffer_free(&png); qa_scene_image_release(image); qa_scene_resources_destroy(bank);
    return ok;
}
static bool material_valid(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,qa_error *error)
{
    qa_bytes catalog=held->resource?qa_resource_bytes(held->resource):(qa_bytes){held->catalog_bytes.data,held->catalog_bytes.size};
    qa_scene_resources *images=qa_scene_resources_create_detached(NULL,error);
    qa_material_library *library=images?qa_material_library_create_detached(images,error):NULL;
    qa_scene_image_options options={.family=QA_SCENE_Q3}; qa_material_script_view script={0}; qa_buffer original={0};
    qa_q2_material_scope scope={0};
    bool ok=library && held->script_name && qa_material_library_parse(library,catalog,&options,error) &&
        qa_material_library_script_read(library,held->script_name,&script) &&
        script.source_offset==held->source_offset && script.body.size==held->script_size &&
        script.name_offset==held->name_offset && script.name_size==held->name_size && !strcmp(script.name,held->script_name) &&
        qa_q2_material_script_scope((qa_bytes){held->wire_bytes.data,held->wire_bytes.size},&scope,error);
    const application_provider *provider=NULL;
    for (size_t i=0;i<owner->app->provider_count;++i) if (owner->app->providers[i]->owner==held->provider) {
        if (provider) { ok=false; break; } provider=owner->app->providers[i];
    }
    if (ok) ok=provider && provider->product && scope.family==
        (provider->product->family==QA_GAME_Q1?QA_SCENE_Q1:provider->product->family==QA_GAME_Q2?QA_SCENE_Q2:QA_SCENE_Q3);
    if (ok) ok=held->image_options.family==scope.family &&
        held->image_palette.size==(scope.palette_present?768:0) &&
        (!scope.palette_present || (held->image_palette.data && !memcmp(held->image_palette.data,scope.palette,768))) &&
        palette_valid(owner,held,error);
    if (ok) ok=text_append(&original,script.name,error) && text_append(&original,"\n",error) && append(&original,script.body,error);
    closure_replay replay={.owner=owner,.held=held,.source={original.data,original.size}};
    if (ok) ok=qa_q2_material_script_dependencies(replay.source,replay_dependency,&replay,error) &&
        replay.used==held->dependency_count &&
        append(&replay.bytes,(qa_bytes){replay.source.data+replay.position,replay.source.size-replay.position},error);
    qa_buffer artifact={0};
    if (ok) ok=scope_write(&artifact,scope.family,scope.palette_present?(qa_bytes){scope.palette,768}:(qa_bytes){0},error) &&
        aliases_write(&artifact,owner,held->dependencies,held->dependency_count,error) &&
        append(&artifact,(qa_bytes){replay.bytes.data,replay.bytes.size},error) &&
        artifact.size==held->wire_bytes.size && !memcmp(artifact.data,held->wire_bytes.data,artifact.size);
    if (!ok && (!error || error->code==QA_OK)) application_fail(error,QA_ERROR_FORMAT,"Material artifact differs from its retained Source program");
    qa_buffer_free(&original); qa_buffer_free(&replay.bytes); qa_buffer_free(&artifact); qa_material_library_destroy(library); qa_scene_resources_destroy(images);
    return ok;
}

bool application_network_q2_materials_validate(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,qa_error *error)
{
    if (!owner || !held) return application_fail(error,QA_ERROR_ARGUMENT,"Material validation needs its real retained holder");
    if (held->kind==APPLICATION_Q2_HELD_MATERIAL) return material_valid(owner,held,error);
    if (held->kind==APPLICATION_Q2_HELD_IMAGE_RECEIPT) {
        const application_q2_held_resource *alias=held->dependency_count==1 && held->dependencies &&
            held->dependencies[0]<owner->held_resource_count?owner->held_resources+held->dependencies[0]:NULL;
        qa_buffer artifact={0};
        bool ok=alias && held->wire_path && same_dependency_scope(held,alias) &&
            application_network_q2_materials_alias_validate(owner,alias,error) &&
            application_network_q2_materials_image_receipt_encode(owner,alias,held->wire_path,&artifact,error) &&
            artifact.size==held->wire_bytes.size && (!artifact.size ||
                (held->wire_bytes.data && !memcmp(artifact.data,held->wire_bytes.data,artifact.size)));
        qa_buffer_free(&artifact);
        if (!ok && (!error || error->code==QA_OK))
            application_fail(error,QA_ERROR_FORMAT,"Image receipt wrapper changes its genuine Source child");
        return ok;
    }
    qa_bytes original=held->resource?qa_resource_bytes(held->resource):(qa_bytes){0}; qa_model model={0};
    if (!qa_model_load(original,&model,error)) return false;
    if (model.format!=QA_MODEL_MD3) { qa_model_free(&model); return application_fail(error,QA_ERROR_ARGUMENT,"Material validation needs its real retained MD3"); }
    bool ok=held->wire_bytes.size==original.size;
    size_t used=0,position=0,base=qa_load_u32le(original.data+100);
    for (uint32_t i=0;ok && i<model.mesh_count;++i) {
        size_t first=base+qa_load_u32le(original.data+base+92);
        for (uint32_t j=0;ok && j<model.meshes[i].shader_count;++j) {
            size_t offset=first+j*68; const char *name=model.meshes[i].shaders[j].name;
            ok=used<held->dependency_count && !memcmp(original.data+position,held->wire_bytes.data+position,offset-position);
            if (!ok) break;
            size_t index=held->dependencies[used++]; uint8_t replacement[64]={0};
            qa_q2_material_dependency image={.kind=QA_Q2_MATERIAL_IMAGE,.path=name};
            if (!*name || (index==SIZE_MAX && qa_q2_material_dependency_builtin(&image)))
                ok=index==SIZE_MAX && !memcmp(original.data+offset,held->wire_bytes.data+offset,64);
            else {
                const application_q2_held_resource *d=index<owner->held_resource_count?&owner->held_resources[index]:NULL;
                ok=d && application_network_q2_dependency_of(held,d) && d->wire_path && strlen(d->wire_path)<sizeof(replacement);
                if (ok && d->kind==APPLICATION_Q2_HELD_MATERIAL) {
                    size_t length=strcspn(name,"."); char canonical[65];
                    ok=length<sizeof(canonical);
                    if (ok) {
                        for (size_t k=0;k<length;++k) canonical[k]=name[k]>='A' && name[k]<='Z'?(char)(name[k]+'a'-'A'):name[k];
                        canonical[length]=0; ok=d->script_name && !strcmp(canonical,d->script_name) && material_valid(owner,d,error);
                    }
                } else if (ok && d->kind==APPLICATION_Q2_HELD_IMAGE_RECEIPT) {
                    const application_q2_held_resource *source=d->dependency_count==1 && d->dependencies &&
                        d->dependencies[0]<owner->held_resource_count?owner->held_resources+d->dependencies[0]:NULL;
                    char *request=qa_scene_model_image_path(name,error);
                    ok=source && source->image_request && request && !strcmp(request,source->image_request) &&
                        !source->sky_face && application_network_q2_materials_validate(owner,d,error);
                    free(request);
                } else if (ok) ok=(d->kind==APPLICATION_Q2_HELD_DEPENDENCY || d->kind==APPLICATION_Q2_HELD_IMAGE) &&
                    !d->sky_face && image_path_matches(name,d->path,error) &&
                    (d->kind!=APPLICATION_Q2_HELD_IMAGE || application_network_q2_materials_image_validate(owner,d,error));
                if (ok) { memcpy(replacement,d->wire_path,strlen(d->wire_path)); ok=!memcmp(replacement,held->wire_bytes.data+offset,64); }
            }
            position=offset+64;
        }
        base+=qa_load_u32le(original.data+base+104);
    }
    if (ok) ok=used==held->dependency_count && !memcmp(original.data+position,held->wire_bytes.data+position,original.size-position);
    if (!ok && (!error || error->code==QA_OK)) application_fail(error,QA_ERROR_FORMAT,"Derived MD3 changes geometry or loses its actual shader closure");
    qa_model_free(&model); return ok;
}

static bool model_scope_alias(const char *path,qa_error *error)
{
    if (!path || (strncmp(path,"models/qa/",10) && strncmp(path,"players/qa/",11)))
        return application_fail(error,QA_ERROR_FORMAT,"Indexed model scope escapes its negotiated namespace");
    char *normal=qa_vfs_normalize_path(path,error);
    if (!normal) return false;
    bool same=!strcmp(path,normal); free(normal);
    return same || application_fail(error,QA_ERROR_FORMAT,"Indexed model scope alias is not canonical");
}

bool qa_q2_material_model_scope_path(const char *model_alias,char out[1024],qa_error *error)
{
    if (!out || !model_alias) return application_fail(error,QA_ERROR_ARGUMENT,"Indexed model companion needs its actual model alias");
    if (!model_scope_alias(model_alias,error)) return false;
    const char *extension=strrchr(model_alias,'.');
    if (!extension || (strcmp(extension,".mdl") && strcmp(extension,".spr")) ||
        (size_t)(extension-model_alias)>1019)
        return application_fail(error,QA_ERROR_FORMAT,"Indexed model companion needs its actual MDL or sprite alias");
    size_t prefix_size=(size_t)(extension-model_alias);
    memmove(out,model_alias,prefix_size); memcpy(out+prefix_size,".qpm",5); return true;
}

static bool model_scope_text(qa_source_save_io *io,char text[1024])
{
    uint32_t size=io->direction==QA_SOURCE_SAVE_WRITE?(uint32_t)strlen(text):0;
    if (!qa_source_save_u32(io,&size) || size>=1024 || !qa_source_save_bytes(io,text,size)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (memchr(text,0,size)) return application_fail(io->error,QA_ERROR_FORMAT,"Indexed model scope text contains NUL");
        text[size]=0;
    }
    return true;
}

static void model_scope_spans(qa_q2_material_model_scope *row)
{
    row->options.palette_rgb=(qa_bytes){row->palette,sizeof(row->palette)};
    row->options.translation=(qa_bytes){row->options.translation.size?row->translation:NULL,row->options.translation.size};
}

static bool model_scope_fields(qa_source_save_io *io,qa_q2_material_model_scope *row)
{
    uint8_t magic[6]={'Q','A','Q','P','M',1};
    qa_scene_image_options *o=&row->options;
    uint32_t family=(uint32_t)o->family,wrap=(uint32_t)o->wrap,filter=(uint32_t)o->filter,usage=(uint32_t)o->usage;
    int32_t transparent_index=o->transparent_index;
    bool translation=o->translation.size!=0;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QAQPM\1",sizeof(magic)) ||
        !model_scope_text(io,row->model_alias) || !model_scope_text(io,row->companion_path) ||
        !model_scope_text(io,row->palette_alias) || !model_scope_text(io,row->palette_path) ||
        !qa_source_save_bytes(io,row->model_digest.bytes,sizeof(row->model_digest.bytes)) ||
        !qa_source_save_u32(io,&family) || family>QA_SCENE_Q3 ||
        !qa_source_save_u32(io,&wrap) || wrap>QA_SCENE_CLAMP ||
        !qa_source_save_u32(io,&filter) || filter>QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        !qa_source_save_u32(io,&usage) || (usage!=QA_IMAGE_USAGE_SKIN && usage!=QA_IMAGE_USAGE_SPRITE) ||
        !qa_source_save_bool(io,&o->mipmap) || !qa_source_save_bool(io,&o->transparent) ||
        !qa_source_save_bool(io,&o->fullbright_only) || !qa_source_save_i32(io,&transparent_index) ||
        !qa_source_save_bool(io,&o->source_q3) ||
        (o->source_q3 && !qa_q3_image_upload_options_codec(io,&o->source_upload)) ||
        !qa_source_save_bytes(io,row->palette,sizeof(row->palette)) ||
        !qa_source_save_bool(io,&translation) ||
        (translation && !qa_source_save_bytes(io,row->translation,sizeof(row->translation)))) return false;
    o->family=(qa_scene_family)family; o->wrap=(qa_scene_wrap)wrap;
    o->filter=(qa_scene_filter)filter; o->usage=(qa_scene_image_usage)usage;
    o->transparent_index=transparent_index; o->translation.size=translation?256:0;
    model_scope_spans(row);
    char companion[1024];
    const char *model_extension=strrchr(row->model_alias,'.');
    const char *palette_extension=strrchr(row->palette_alias,'.');
    return qa_q2_material_model_scope_path(row->model_alias,companion,io->error) &&
        !strcmp(companion,row->companion_path) && model_scope_alias(row->palette_alias,io->error) &&
        palette_extension && !strcmp(palette_extension,".lmp") &&
        !strcmp(row->palette_path,"gfx/palette.lmp") && model_extension &&
        o->usage==(!strcmp(model_extension,".mdl")?QA_IMAGE_USAGE_SKIN:QA_IMAGE_USAGE_SPRITE);
}

static bool model_scope_decode(qa_bytes bytes,qa_q2_material_model_scope *out,qa_error *error)
{
    qa_source_save_io io={0}; qa_q2_material_model_scope row={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && model_scope_fields(&io,&row) &&
        qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok) {
        if (!error || error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Indexed model scope is malformed");
        return false;
    }
    *out=row; model_scope_spans(out); return true;
}

static bool model_scope_model(const qa_q2_material_model_scope *row,qa_bytes bytes,qa_error *error)
{
    qa_sha256_digest digest; qa_sha256(bytes,&digest);
    if (!qa_sha256_equal(&digest,&row->model_digest))
        return application_fail(error,QA_ERROR_FORMAT,"Indexed model scope changes its actual model bytes");
    qa_model model={0};
    if (!qa_model_load(bytes,&model,error)) return false;
    const char *extension=strrchr(row->model_alias,'.');
    bool ok=extension && ((model.format==QA_MODEL_MDL && !strcmp(extension,".mdl") && row->options.usage==QA_IMAGE_USAGE_SKIN) ||
        (model.format==QA_MODEL_SPR && !strcmp(extension,".spr") && row->options.usage==QA_IMAGE_USAGE_SPRITE));
    qa_model_free(&model);
    return ok || application_fail(error,QA_ERROR_FORMAT,"Indexed model scope belongs to a different model format");
}

bool qa_q2_material_model_scope_read(qa_bytes artifact,const char *model_alias,qa_bytes model,
    qa_q2_material_model_scope *out,qa_error *error)
{
    if (!out || !model_alias || (model.size && !model.data))
        return application_fail(error,QA_ERROR_ARGUMENT,"Indexed model scope needs its retained model bytes");
    qa_q2_material_model_scope row={0};
    if (!model_scope_decode(artifact,&row,error)) return false;
    if (strcmp(row.model_alias,model_alias))
        return application_fail(error,QA_ERROR_FORMAT,"Indexed model companion addresses another model");
    if (!model_scope_model(&row,model,error)) return false;
    *out=row; model_scope_spans(out); return true;
}

bool qa_q2_material_model_scope_dependencies(qa_bytes artifact,qa_q2_material_dependency_fn fn,void *context,qa_error *error)
{
    if (!fn) return application_fail(error,QA_ERROR_ARGUMENT,"Indexed model scope needs its actual dependency consumer");
    qa_q2_material_model_scope row={0};
    if (!model_scope_decode(artifact,&row,error)) return false;
    qa_q2_material_dependency dependency={.kind=QA_Q2_MATERIAL_IMAGE,.path=row.palette_alias};
    return fn(context,&dependency,error);
}

bool qa_q2_material_model_scope_apply(const qa_q2_material_model_scope *row,qa_bytes palette,
    qa_scene_image_options *out,qa_error *error)
{
    if (!row || !out) return application_fail(error,QA_ERROR_ARGUMENT,"Indexed model scope needs its retained constructor");
    if (palette.size!=768 || !palette.data || memcmp(palette.data,row->palette,768) ||
        row->options.palette_rgb.size!=768 || (row->options.translation.size && row->options.translation.size!=256))
        return application_fail(error,QA_ERROR_FORMAT,"Indexed model palette differs from its actual Source file");
    *out=row->options; out->palette_rgb=(qa_bytes){row->palette,768};
    out->translation=(qa_bytes){row->options.translation.size?row->translation:NULL,row->options.translation.size};
    return true;
}

bool application_network_q2_materials_model_scope_encode(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,qa_buffer *out,qa_error *error)
{
    if (!owner || !held || !out || out->data || out->size || held->kind!=APPLICATION_Q2_HELD_MODEL ||
        !held->model_scope || !held->resource || !held->wire_path || !held->model_scope_path ||
        held->image_palette.size!=768 || !held->image_palette.data ||
        (held->image_translation.size && (held->image_translation.size!=256 || !held->image_translation.data)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Indexed model scope needs its actual retained Source holder");
    const application_q2_held_resource *palette=held->image_palette_dependency<owner->held_resource_count?
        owner->held_resources+held->image_palette_dependency:NULL;
    if (!palette || palette->kind!=APPLICATION_Q2_HELD_DEPENDENCY || palette->missing || !palette->resource ||
        !palette->wire_path || !palette->path || strcmp(palette->path,"gfx/palette.lmp") ||
        !application_network_q2_dependency_of(held,palette) ||
        !qa_vfs_acquisition_retained(palette->view,&palette->opening,error)) {
        if (!error || error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Indexed model loses its actual Source palette opening");
        return false;
    }
    qa_q2_material_model_scope row={.options=held->image_options};
    const char *texts[]={held->wire_path,held->model_scope_path,palette->wire_path,palette->path};
    char *targets[]={row.model_alias,row.companion_path,row.palette_alias,row.palette_path};
    for (size_t i=0;i<4;++i) {
        if (strlen(texts[i])>=1024) return application_fail(error,QA_ERROR_FORMAT,"Indexed model scope text exceeds its actual namespace");
        strcpy(targets[i],texts[i]);
    }
    row.model_digest=*qa_resource_digest(held->resource);
    memcpy(row.palette,held->image_palette.data,768);
    if (held->image_translation.size) memcpy(row.translation,held->image_translation.data,256);
    row.options.translation.size=held->image_translation.size; model_scope_spans(&row);
    qa_scene_image_options admitted;
    if (!qa_q2_material_model_scope_apply(&row,qa_resource_bytes(palette->resource),&admitted,error) ||
        !model_scope_model(&row,qa_resource_bytes(held->resource),error)) return false;
    qa_source_save_io io={0}; qa_buffer bytes={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && model_scope_fields(&io,&row) && qa_source_save_finish(&io,&bytes);
    qa_source_save_dispose(&io);
    if (!ok) {
        qa_buffer_free(&bytes);
        if (!error || error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Indexed model scope cannot encode its actual constructor");
        return false;
    }
    *out=bytes; return true;
}

bool application_network_q2_materials_model_scope_validate(const qa_application_network_q2 *owner,
    const application_q2_held_resource *held,qa_error *error)
{
    qa_buffer artifact={0};
    bool ok=application_network_q2_materials_model_scope_encode(owner,held,&artifact,error);
    if (ok) ok=held->model_scope_bytes.size==artifact.size && held->model_scope_bytes.data &&
        !memcmp(artifact.data,held->model_scope_bytes.data,artifact.size);
    qa_buffer_free(&artifact);
    if (!ok && (!error || error->code==QA_OK))
        application_fail(error,QA_ERROR_FORMAT,"Indexed model companion differs from its retained Source constructor");
    return ok;
}
