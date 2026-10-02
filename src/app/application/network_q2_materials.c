#include "network_q2_materials.h"
#include "qa/network_q2_materials.h"
#include "qa/material_library_save.h"
#include "qa/binary.h"
#include "qa/model.h"
#include "qa/image.h"
#include "qa/scene_resource_save.h"
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
static bool dependency(material_reader *r,qa_q2_material_dependency_kind kind,bool builtin_images,
    qa_q2_material_dependency_fn fn,void *context,material_token *token,qa_error *error)
{
    r->missing_parameter=false;
    if (!next_token(r,false,token)) { r->missing_parameter=true; return !r->failed; }
    qa_q2_material_dependency value={kind,token->text,token->begin,token->end,builtin_images};
    return fn(context,&value,error);
}
static bool consume(material_reader *r,size_t count)
{ material_token t; while (count--) if (!next_token(r,false,&t)) return false; return true; }

bool qa_q2_material_script_dependencies(qa_bytes bytes,qa_q2_material_dependency_fn fn,
    void *context,qa_error *error)
{
    material_token name,t,arg; size_t begin,end;
    if (!fn || !program_span(bytes,&name,&begin,&end,error)) return false;
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
                    if (!fn(context,&value,error)) return false;
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

bool qa_q2_material_script_import(qa_material_library *library,const char *alias,qa_bytes bytes,
    const qa_scene_image_options *options,qa_error *error)
{
    material_token name; size_t begin,end; qa_buffer source={0};
    qa_q2_material_scope scope={0};
    if (!library || !alias || !*alias || !options || !program_span(bytes,&name,&begin,&end,error) ||
        !qa_q2_material_script_scope(bytes,&scope,error)) return false;
    for (const unsigned char *p=(const unsigned char*)alias; *p; ++p)
        if (*p<=' ' || *p=='"' || *p=='{' || *p=='}')
            return application_fail(error,QA_ERROR_FORMAT,"Material delivery alias is not a shader name");
    qa_scene_image_options actual=*options;
    actual.family=scope.family; actual.palette_rgb=scope.palette_present?(qa_bytes){scope.palette,768}:(qa_bytes){0};
    bool ok=text_append(&source,alias,error) && text_append(&source,"\n",error) &&
        append(&source,(qa_bytes){bytes.data+begin,end-begin},error) &&
        qa_material_library_parse_scoped(library,(qa_bytes){source.data,source.size},&actual,error);
    qa_buffer_free(&source); return ok;
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

static bool dependency_keep(closure_writer *w,size_t index,qa_error *error)
{
    if (w->count>=SIZE_MAX/sizeof(*w->dependencies)-1)
        return application_fail(error,QA_ERROR_MEMORY,"Material dependency count overflows");
    size_t *rows=realloc(w->dependencies,(w->count+1)*sizeof(*rows));
    if (!rows) return application_fail(error,QA_ERROR_MEMORY,"Retaining material dependency owners");
    rows[w->count++]=index; w->dependencies=rows; return true;
}

static bool image_winner(closure_writer *w,const char *name,qa_scene_image_usage usage,char **out,
    const qa_resource **source,qa_scene_image **decoded,qa_error *error)
{
    char *path=qa_scene_model_image_path(name,error);
    if (!path) return false;
    qa_scene_image *image=NULL; qa_error issue={0};
    qa_scene_image_options options={.family=w->family,.wrap=QA_SCENE_REPEAT,.usage=usage};
    bool loaded=qa_scene_image_load(w->images,path,&options,&image,&issue),ok=true;
    if (loaded) {
        qa_scene_image_request request;
        if (!qa_scene_image_request_read(w->images,image,&request) || !request.source)
            ok=application_fail(error,QA_ERROR_ARGUMENT,"Material image lost its actual successful file admission");
        else {
            const char *winner=NULL;
            for (size_t i=0;ok && i<qa_vfs_retained_read_count(w->model->view);++i) {
                qa_vfs_read_reference row;
                if (!qa_vfs_retained_read_at(w->model->view,i,&row) || row.resource!=request.source ||
                    row.mount!=request.source_mount || !row.path || !image_path_matches(path,row.path,NULL)) continue;
                if (winner && strcmp(winner,row.path)) ok=application_fail(error,QA_ERROR_ARGUMENT,"Material image has ambiguous actual requested paths");
                else winner=row.path;
            }
            if (ok && !winner) ok=application_fail(error,QA_ERROR_ARGUMENT,"Material image lost its actual VFS requested path");
            char *actual=ok?application_network_q2_copy(winner,error):NULL;
            if (!actual) ok=false;
            else { free(path); path=actual; *source=request.source; qa_resource_retain((qa_resource*)*source); }
        }
    } else if (issue.code==QA_ERROR_NOT_FOUND) {
        const char *slash=strrchr(path,'/'),*dot=strrchr(path,'.');
        if (!dot || (slash && dot<slash)) {
            size_t length=strlen(path); char *extended=realloc(path,length+5);
            if (!extended) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining missing material image path");
            else { path=extended; memcpy(path+length,".tga",5); }
        }
    } else { if (error) *error=issue; ok=false; }
    if (ok && decoded) { *decoded=image; image=NULL; }
    qa_scene_image_release(image);
    if (ok) *out=path; else free(path);
    return ok;
}
static bool image_dependency(closure_writer *w,const char *name,size_t *out,qa_error *error)
{
    char *path=NULL; const qa_resource *resource=NULL;
    bool ok=image_winner(w,name,QA_IMAGE_USAGE_SKIN,&path,&resource,NULL,error) &&
        application_network_q2_dependency_receipt(w->owner,w->model,path,resource,NULL,out,error);
    qa_resource_release((qa_resource*)resource); free(path); return ok;
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
        size_t indices[6]; char *paths[6]={0}; const qa_resource *resources[6]={0};
        bool ok=true;
        for (size_t i=0;ok && i<6;++i) {
            qa_buffer name={0};
            ok=text_append(&name,dependency->path,error) && text_append(&name,"_",error) &&
                text_append(&name,faces[i],error) && text_append(&name,".tga",error) &&
                append(&name,(qa_bytes){(const uint8_t*)"",1},error) &&
                image_winner(w,(const char*)name.data,QA_IMAGE_USAGE_SKY,&paths[i],&resources[i],NULL,error);
            qa_buffer_free(&name);
        }
        if (ok) ok=application_network_q2_sky_dependencies(w->owner,w->model,dependency->path,
            (const char *const*)paths,resources,NULL,indices,sky,error);
        for (size_t i=0;i<6;++i) { free(paths[i]); qa_resource_release((qa_resource*)resources[i]); }
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
    char *path=NULL; const qa_resource *resource=NULL; qa_scene_image *image=NULL;
    bool ok=image_winner(w,name,QA_IMAGE_USAGE_SKIN,&path,&resource,&image,error);
    qa_buffer png={0}; qa_scene_image_request request={0}; qa_bytes palette={0};
    qa_scene_palette_source palette_source={0};
    if (ok && resource && image && image->animation_count<=1) {
        ok=qa_scene_image_request_read(w->images,image,&request) && image_png(image,&png,error);
        if (ok) {
            (void)qa_scene_resources_palette_read(w->images,request.options.family,&palette);
            bool held_palette=qa_scene_resources_palette_source_read(w->images,request.options.family,&palette_source);
            if (palette.size && !held_palette)
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Image closure lost its actual palette admission");
            if (ok) ok=application_network_q2_dependency_image(w->owner,w->model,path,resource,NULL,
                &request.options,palette,held_palette?palette_source.opening->path:NULL,
                held_palette?palette_source.resource:NULL,held_palette?palette_source.opening:NULL,
                (qa_bytes){png.data,png.size},out,error);
        }
    } else if (ok) ok=application_network_q2_dependency_receipt(w->owner,w->model,path,resource,NULL,out,error);
    if (!ok && (!error || error->code==QA_OK))
        application_fail(error,QA_ERROR_ARGUMENT,"Image closure lost its actual file admission");
    qa_buffer_free(&png); qa_scene_image_release(image); qa_resource_release((qa_resource*)resource); free(path);
    return ok;
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
        ok=ok && scope_write(&artifact,w->family,palette,error) && append(&artifact,(qa_bytes){body.bytes.data,body.bytes.size},error) &&
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
    if (!d || d->kind!=APPLICATION_Q2_HELD_DEPENDENCY || d->provider!=r->held->provider ||
        !qa_sha256_equal(&d->identity,&r->held->identity) || !qa_sha256_equal(&d->authority,&r->held->authority) ||
        !qa_vfs_lookup_equal(d->view,r->held->view)) {
        application_fail(error,QA_ERROR_FORMAT,"Material program lost its actual BODY media scope"); return NULL;
    }
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
