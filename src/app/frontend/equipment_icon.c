#include "equipment_icon.h"
#include "qa/json.h"
#include "qa/image.h"
#include "../../render/scene/resources_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool text(const qa_json_document *doc,qa_json_id value,char **out,qa_error *error)
{
    qa_buffer bytes={0};
    if(!qa_json_string(doc,value,&bytes,error))return false;
    if(!bytes.size||memchr(bytes.data,0,bytes.size)) {
        qa_buffer_free(&bytes);qa_error_set(error,QA_ERROR_FORMAT,0,"Item icon contains an empty or NUL name");return false;
    }
    *out=(char *)bytes.data;return true;
}
bool frontend_equipment_icon_key(const char *path, const char *lump, char *out,
    size_t capacity, qa_error *error)
{
    /* Material COM_StripExtension ends at the first dot. Keep the authored
     * WAD lump before its container extension; image/resource names retain
     * their actual path and lump independently of this material identity. */
    int length=lump && *lump ? snprintf(out,capacity,"%s/%s",lump,path) :
        snprintf(out,capacity,"%s",path);
    if (length<0 || (size_t)length>=capacity) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Item icon material identity exceeds its extent");return false;
    }
    return true;
}
bool frontend_equipment_icon_load(qa_bytes declaration,qa_scene_family family,qa_vfs *files,
    qa_scene_resources *images,qa_material_library *library,const qa_material **out,
    qa_resource **source,qa_error *error)
{
    if(!declaration.size||!files||!images||!library||!out||*out||!source||*source)return false;
    qa_json_document *doc=NULL;char *path=NULL,*lump=NULL,*name=NULL,*material_key=NULL;
    bool okay=qa_json_parse(declaration,&doc,error);
    qa_json_id root=okay?qa_json_root(doc):QA_JSON_NONE;
    qa_json_id kind=okay?qa_json_get(doc,root,"kind"):QA_JSON_NONE;
    bool shader=okay&&qa_json_string_equal(doc,kind,"shader");
    bool wad=okay&&qa_json_string_equal(doc,kind,"wad-picture");
    if(okay&&!shader&&!wad&&!qa_json_string_equal(doc,kind,"image")) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Item icon has no admitted image, WAD picture or shader kind");okay=false;
    }
    if(okay)okay=text(doc,qa_json_get(doc,root,shader?"name":"path"),&path,error);
    if(okay&&wad)okay=text(doc,qa_json_get(doc,root,"lump"),&lump,error);
    if(okay) {
        name=qa_vfs_normalize_path(path,error);okay=name!=NULL;
    }
    qa_scene_image_options options={.family=family,.wrap=QA_SCENE_CLAMP,.filter=QA_SCENE_LINEAR,
        .usage=QA_IMAGE_USAGE_PICTURE,.transparent=true,.transparent_index=255};
    const qa_material *material=NULL;qa_scene_image *image=NULL;qa_resource *resource=NULL;
    if(okay&&shader)okay=qa_material_register_kind(library,name,&options,QA_MATERIAL_PICTURE,&material,error);
    else if(okay&&family!=QA_SCENE_Q1) {
        okay=qa_scene_image_load(images,name,&options,&image,error);
        if(okay)okay=qa_material_register_generated_picture(library,name,image,&material,error);
    } else if(okay) {
        okay=qa_vfs_acquire(files,name,&resource,NULL,error);
        qa_bytes bytes=resource?qa_resource_bytes(resource):(qa_bytes){0};qa_wad directory={0};
        if(okay&&wad) {
            okay=qa_wad_decode(bytes,&directory,error);
            bool found=false;
            for(size_t i=0;okay&&i<directory.count;++i)if(!strcmp(directory.lumps[i].name,lump)) {
                bytes=directory.lumps[i].bytes;found=true;break;
            }
            if(okay&&!found){qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Declared item WAD picture is absent");okay=false;}
        }
        qa_image indexed={0},rgba={0};qa_bytes palette={0};
        if(okay)okay=qa_image_decode_qpic(bytes,&indexed,error)&&qa_scene_resources_palette(images,QA_SCENE_Q1,&palette,error);
        qa_indexed_level level={indexed.width,indexed.height,indexed.indices};
        qa_palette_options expand={.transparent_index=255,.fullbright_first=-1,.fullbright_last=-1,.layer=QA_PALETTE_COMBINED};
        if(okay)okay=qa_image_expand_indexed(&level,palette,&expand,&rgba,error);
        if(okay&&wad) {
            size_t a=strlen(name),b=strlen(lump);
            if(b>SIZE_MAX-2||a>SIZE_MAX-b-2){okay=false;qa_error_set(error,QA_ERROR_MEMORY,0,"Item icon identity exceeds address space");}
            else {char *key=malloc(a+b+2);material_key=malloc(a+b+2);
                if(!key||!material_key){free(key);okay=false;qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining WAD picture identity");}
                else {okay=frontend_equipment_icon_key(name,lump,material_key,a+b+2,error);
                    memcpy(key,name,a);key[a]='#';memcpy(key+a+1,lump,b+1);free(name);name=key;}}
        }
        qa_scene_image_level pixels={rgba.width,rgba.height,rgba.rgba.data,rgba.rgba.size};
        if(okay)okay=qa_scene_image_create(images,name,QA_SCENE_RGBA8,&pixels,1,QA_SCENE_CLAMP,QA_SCENE_LINEAR,
            (qa_scene_vec4){0},&image,error);
        if(okay) {
            image_asset_recipe recipe={.kind=1,.level_count=1,.source=resource,
                .offsets={(uint64_t)(bytes.data-qa_resource_bytes(resource).data)+8},
                .widths={indexed.width},.heights={indexed.height},.fullbright_first=256,
                .fullbright_last=-1,.layer=QA_PALETTE_COMBINED};
            options.palette_rgb=palette;scene_image_asset_palette(images,&recipe,&options);
            okay=scene_image_asset_copy(image,&recipe,error)&&
                qa_material_register_generated_picture(library,material_key?material_key:name,image,&material,error);
        }
        qa_image_free(&indexed);qa_image_free(&rgba);qa_wad_free(&directory);
    }
    if(okay){*out=material;*source=resource;resource=NULL;}
    qa_resource_release(resource);free(path);free(lump);free(name);free(material_key);qa_json_destroy(doc);return okay;
}
