#include "scene_refs.h"
#include "internal.h"

static bool model_encode(void *context, const qa_model *model, uint64_t *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_model_encode(scope->models,model,out,error);
}
static bool model_decode(void *context, uint64_t key, qa_bytes bytes, const qa_model **out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_model_decode(scope->models,key,bytes,out,error);
}
static bool animation_encode(void *context, const qa_model_animation *animation, uint64_t *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_animation_encode(scope->models,animation,out,error);
}
static bool animation_decode(void *context, uint64_t key, qa_bytes bytes, const qa_model_animation **out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_animation_decode(scope->models,key,bytes,out,error);
}
static void model_release(void *token) { frontend_model_release(token); }
static void animation_release(void *token) { frontend_animation_release(token); }
bool frontend_scene_model_source_read(const qa_scene_model *model, qa_scene_model_content_kind kind,
    frontend_model_source *out)
{
    if (!out || (kind!=QA_SCENE_MODEL_CONTENT_SOURCE && kind!=QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE)) return false;
    qa_scene_model_content_lease lease={0}; frontend_model_source source;
    if (!qa_scene_model_content_read(model,kind,&lease) || !lease.context || lease.release!=model_release ||
        !frontend_model_lease_source(lease.context,&source)) return false;
    const qa_model_replacement *replacement=qa_scene_model_replacement_description(model);
    const qa_model *actual=kind==QA_SCENE_MODEL_CONTENT_SOURCE?qa_scene_model_source(model):replacement?replacement->source:NULL;
    if (source.model!=actual) return false;
    *out=source; return true;
}
bool frontend_scene_animation_source_read(const qa_scene_model *model, frontend_animation_source *out)
{
    if (!out) return false;
    qa_scene_model_content_lease lease={0}; frontend_animation_source source;
    const qa_model_replacement *replacement=qa_scene_model_replacement_description(model);
    if (!replacement || !qa_scene_model_content_read(model,QA_SCENE_MODEL_CONTENT_ANIMATION,&lease) ||
        !lease.context || lease.release!=animation_release || !frontend_animation_lease_source(lease.context,&source) ||
        source.animation!=replacement->animation) return false;
    *out=source; return true;
}
static bool model_retain(void *context, const qa_model *source, qa_scene_model_content_lease *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    if (!scope || !out || out->context || out->release)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene model retention requires its real inventory and empty lease");
    frontend_model_lease *token=NULL;
    if (!frontend_model_retain(scope->models,source,&token,error)) return false;
    *out=(qa_scene_model_content_lease){token,model_release}; return true;
}
static bool animation_retain(void *context, const qa_model_animation *source, qa_scene_model_content_lease *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    if (!scope || !out || out->context || out->release)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene animation retention requires its real inventory and empty lease");
    frontend_animation_lease *token=NULL;
    if (!frontend_animation_retain(scope->models,source,&token,error)) return false;
    *out=(qa_scene_model_content_lease){token,animation_release}; return true;
}
bool frontend_scene_q3_model_retain(void *context, const qa_model *source, qa_q3_asset_model_lease *out, qa_error *error)
{
    if (!out || out->context || out->release)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 model retention requires an empty actual holder lease");
    qa_scene_model_content_lease lease={0};
    if (!model_retain(context,source,&lease,error)) return false;
    *out=(qa_q3_asset_model_lease){lease.context,lease.release}; return true;
}
static bool source_qualify(void *context, const qa_model *source, qa_scene_resources *images,
    qa_material_library *materials, const qa_scene_image_options *options, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_model_source_qualify(scope->models,source,images,materials,options,error);
}
static bool geometry_encode(void *context, const qa_scene_geometry *geometry, uint64_t *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_scene_geometry_encode(scope->identity.space,geometry,out,error);
}
static bool geometry_decode(void *context, uint64_t key, const qa_scene_geometry **out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_scene_geometry_decode(scope->identity.space,key,out,error);
}
static bool image_encode(void *context, const qa_scene_image *image, uint64_t *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_scene_image_encode(scope->identity.space,image,out,error);
}
static bool image_decode(void *context, uint64_t key, const qa_scene_image **out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_scene_image_decode(scope->identity.space,key,out,error);
}
static bool material_encode(void *context, const qa_material *material, uint64_t *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_scene_material_encode(scope->identity.space,material,out,error);
}
static bool material_decode(void *context, uint64_t key, const qa_material **out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_scene_material_decode(scope->identity.space,key,out,error);
}
static bool identity_decode(void *context, qa_scene_model_identity_kind kind, size_t node,
    size_t ordinal, uint64_t saved, uint64_t *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_scene_model_install(&scope->identity,kind,node,ordinal,saved,out,error);
}
static bool identity_encode(void *context, qa_scene_model_identity_kind kind, size_t node,
    size_t ordinal, uint64_t actual, uint64_t *out, qa_error *error)
{
    frontend_scene_model_scope *scope=context;
    return frontend_scene_model_saved(&scope->identity,kind,node,ordinal,actual,out,error);
}
qa_scene_model_owner_refs frontend_scene_model_refs(frontend_scene_model_scope *scope)
{
    return (qa_scene_model_owner_refs){.context=scope,.model_encode=model_encode,.model_decode=model_decode,
        .animation_encode=animation_encode,.animation_decode=animation_decode,
        .model_retain=model_retain,.animation_retain=animation_retain,.source_qualify=source_qualify,
        .geometry_encode=geometry_encode,.geometry_decode=geometry_decode,.image_encode=image_encode,
        .image_decode=image_decode,.material_encode=material_encode,.material_decode=material_decode,
        .identity_decode=identity_decode,.identity_encode=identity_encode};
}
