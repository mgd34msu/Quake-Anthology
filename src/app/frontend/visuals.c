#include "internal.h"
#include "visual_restore.h"
#include "visual_access.h"
#include "native_q3_client.h"
#include "save_private.h"
#include "qa/persistence_content.h"
#include "qa/material_library_save.h"
#include "qa/scene_model_save.h"
#include "qa/scene_resource_save.h"
#include "shared_resource_policy.h"
#include "scene_refs.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_model_opening.h"
#include "qa/scene_effects.h"
#include "legacy_render_policy.h"
#include "material_movies.h"
#include "material_movies_save.h"
#include "renderer_materials.h"
#include "remote_q1_client.h"
#include "remote_q2_client.h"
#include "qa/media_library_save.h"
#include <limits.h>

typedef struct frontend_visual_content {
    size_t references;
    const qa_vfs *files;
    qa_resource *resource, *animation_resource, *scale_resource;
    qa_model model;
    qa_model_animation animation;
} frontend_visual_content;
static void visual_content_release(void *context)
{
    frontend_visual_content *content = context;
    if (!content || --content->references) return;
    qa_model_free(&content->model); qa_model_animation_free(&content->animation);
    qa_resource_release(content->resource); qa_resource_release(content->animation_resource);
    qa_resource_release(content->scale_resource); free(content);
}
static void visual_animation_release(void *context) { visual_content_release(context); }
static bool visual_content_lease(frontend_visual_content *content, bool animation,
    qa_scene_model_content_lease *out, qa_error *error)
{
    if (!content || !out || out->context || out->release || content->references == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual content lease requires its actual retained holder");
    ++content->references;
    *out = (qa_scene_model_content_lease){content, animation ? visual_animation_release : visual_content_release};
    return true;
}
bool frontend_visual_scene_model_source_read(const qa_scene_model *scene, qa_scene_model_content_kind kind,
    frontend_model_source *out)
{
    qa_scene_model_content_lease lease = {0};
    if (!out || (kind != QA_SCENE_MODEL_CONTENT_SOURCE && kind != QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE) ||
        !qa_scene_model_content_read(scene, kind, &lease) || !lease.context || lease.release != visual_content_release) return false;
    frontend_visual_content *content = lease.context;
    const qa_model_replacement *replacement = qa_scene_model_replacement_description(scene);
    const qa_model *actual = kind == QA_SCENE_MODEL_CONTENT_SOURCE ? qa_scene_model_source(scene) : replacement ? replacement->source : NULL;
    if (!content->references || actual != &content->model || !content->resource || !content->files) return false;
    *out = (frontend_model_source){.model = actual, .resource = content->resource, .files = content->files}; return true;
}
bool frontend_visual_scene_animation_source_read(const qa_scene_model *scene, frontend_animation_source *out)
{
    qa_scene_model_content_lease lease = {0};
    const qa_model_replacement *replacement = qa_scene_model_replacement_description(scene);
    if (!out || !replacement || !qa_scene_model_content_read(scene, QA_SCENE_MODEL_CONTENT_ANIMATION, &lease) ||
        !lease.context || lease.release != visual_animation_release) return false;
    frontend_visual_content *content = lease.context;
    if (!content->references || replacement->animation != &content->animation || !content->animation_resource || !content->files) return false;
    *out = (frontend_animation_source){&content->animation, content->animation_resource, content->files, content->scale_resource};
    return true;
}
bool frontend_visual_scene_model_content_clone(const qa_scene_model *scene, qa_scene_model_content_kind kind,
    qa_scene_model_content_lease *out, qa_error *error)
{
    qa_scene_model_content_lease lease = {0};
    if (!out || out->context || out->release || !qa_scene_model_content_read(scene, kind, &lease))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual content clone requires its genuine scene token and empty output");
    bool animation = kind == QA_SCENE_MODEL_CONTENT_ANIMATION;
    if (animation) {
        frontend_animation_source source;
        if (!frontend_visual_scene_animation_source_read(scene, &source))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual animation clone lacks its actual parsed holder");
    } else {
        frontend_model_source source;
        if (!frontend_visual_scene_model_source_read(scene, kind, &source))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual model clone lacks its actual parsed holder");
    }
    return visual_content_lease(lease.context, animation, out, error);
}

typedef struct frontend_model {
    struct frontend_model *next;
    qa_resource *resource;
    qa_model owned_model;
    frontend_visual_content *content, *replacement;
    qa_model_replacement replacement_description;
    int64_t source_rank;
    bool source_rank_known;
    const qa_model *model;
    frontend_model_lease *lease;
    qa_scene_model *scene;
    char path[];
} frontend_model;
typedef struct visual_model_recipe {
    char *path;
    const qa_resource *resource;
    int64_t rank;
    bool known;
} visual_model_recipe;
static void recipes_free(visual_model_recipe *recipes, size_t count)
{
    for (size_t i = 0; recipes && i < count; ++i) { free(recipes[i].path); qa_resource_release((qa_resource *)recipes[i].resource); }
    free(recipes);
}
static bool model_translation_equal(const qa_scene_model *first, const qa_scene_model *second)
{
    const qa_scene_image_options *a = qa_scene_model_image_options(first);
    const qa_scene_image_options *b = qa_scene_model_image_options(second);
    return a && b && a->translation.size == b->translation.size &&
        (!a->translation.size || !memcmp(a->translation.data, b->translation.data, a->translation.size));
}
struct frontend_visual_owner {
    frontend_visual_owner *next;
    qa_actor_owner owner;
    qa_scene_family family;
    qa_vfs *mounts;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_frontend *frontend;
    qa_command_context command;
    qa_media_library *media;
    frontend_material_movies *shader_movies;
    frontend_model *models;
    visual_model_recipe *recipes;
    size_t recipe_count, attached;
    bool recipes_present, construction_failed;
};
static bool visual_movie_current(void *context, const frontend_material_movie_source *source)
{
    const frontend_visual_owner *owner = context;
    if (!owner || owner->construction_failed || !source || source->frontend != owner->frontend || source->files != owner->mounts ||
        source->images != owner->images || source->materials != owner->materials || source->media != owner->media ||
        !owner->frontend || !owner->frontend->application || owner->command.owner != owner->owner ||
        !qa_application_command_context_active(owner->frontend->application, &owner->command)) return false;
    const qa_vfs *files = qa_application_context_files(owner->frontend->application, &owner->command, NULL);
    return files && qa_vfs_lookup_equal(files, owner->mounts);
}
static const frontend_visual_owner *owner_at(const qa_frontend *frontend, size_t index)
{
    if (!frontend || !frontend->application || frontend->stepping) return NULL;
    const frontend_visual_owner *owner=frontend->visuals;
    while (owner && index) { owner=owner->next; --index; }
    return owner;
}
size_t frontend_visual_owner_count(const qa_frontend *frontend)
{
    if (!frontend || !frontend->application || frontend->stepping) return 0;
    size_t count=0;
    for (const frontend_visual_owner *owner=frontend->visuals;owner;owner=owner->next) ++count;
    return count;
}
bool frontend_visual_owner_read(const qa_frontend *frontend, size_t index, frontend_visual_owner_view *out)
{
    const frontend_visual_owner *owner=owner_at(frontend,index);
    if (!owner || !out || owner->construction_failed || !owner->owner || !owner->mounts || !owner->images || !owner->materials || !owner->media ||
        qa_scene_resources_files(owner->images)!=owner->mounts || qa_material_library_resource_owner(owner->materials)!=owner->images)
        return false;
    *out=(frontend_visual_owner_view){owner->owner,owner->family,owner->mounts,owner->images,owner->materials,
        owner->media,owner->shader_movies}; return true;
}
bool frontend_visual_movie_source_read(qa_frontend *frontend, size_t index,
    frontend_material_movie_source *out, qa_error *error)
{
    frontend_visual_owner *owner = (frontend_visual_owner *)owner_at(frontend, index);
    if (!owner || !out || owner->construction_failed || !owner->media)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual movie source requires its actual complete owner");
    if (!owner->command.registry) {
        qa_command_context command = {.owner = owner->owner, .origin = QA_COMMAND_SERVER,
            .dialect = owner->family == QA_SCENE_Q2 ? QA_CONSOLE_Q2 :
                owner->family == QA_SCENE_Q3 ? QA_CONSOLE_Q3 : QA_CONSOLE_Q1};
        if (!qa_application_capture_command_context(frontend->application, &command, &owner->command, error)) return false;
    }
    frontend_material_movie_source source = {.frontend = frontend, .files = owner->mounts,
        .images = owner->images, .materials = owner->materials, .media = owner->media,
        .context = owner, .current = visual_movie_current};
    if (!visual_movie_current(owner, &source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual shader movies lost their genuine provider namespace");
    *out = source; return true;
}
bool frontend_visual_movies_restore(qa_frontend *frontend, size_t index,
    const frontend_material_movies_refs *refs, qa_bytes bytes, qa_error *error)
{
    frontend_visual_owner *owner = (frontend_visual_owner *)owner_at(frontend, index);
    frontend_material_movie_source source;
    if (!owner || owner->shader_movies || !frontend->source_restoring)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual movie import requires its actual empty isolated owner");
    if (!frontend_visual_movie_source_read(frontend, index, &source, error)) return false;
    return frontend_material_movies_restore(&source, refs, bytes, &owner->shader_movies, error);
}
size_t frontend_visual_model_count(const qa_frontend *frontend, size_t index)
{
    const frontend_visual_owner *owner=owner_at(frontend,index); size_t count=0;
    if (owner) for (const frontend_model *model=owner->models;model;model=model->next) ++count;
    return count;
}
bool frontend_visual_model_read(const qa_frontend *frontend, size_t index, size_t ordinal, frontend_visual_model_view *out)
{
    const frontend_visual_owner *owner=owner_at(frontend,index);
    const frontend_model *model=owner?owner->models:NULL;
    while (model && ordinal) { model=model->next; --ordinal; }
    if (!model || !out || !model->resource || !model->path[0]) return false;
    *out=(frontend_visual_model_view){model->path,model->resource,model->model,model->scene}; return true;
}
bool frontend_visual_model_attach_restored(qa_frontend *frontend, size_t owner_index, const char *path,
    const qa_resource *resource, const qa_model *parsed, qa_scene_model *scene,
    frontend_model_inventory *inventory, qa_error *error)
{
    frontend_visual_owner *owner=(frontend_visual_owner *)owner_at(frontend,owner_index);
    if (!owner || !path || !*path || !resource || !parsed || !scene || !inventory || !qa_scene_model_idle(scene) ||
        qa_scene_model_source(scene)!=parsed || qa_scene_model_resource_owner(scene)!=owner->images ||
        qa_scene_model_material_owner(scene)!=owner->materials ||
        qa_resource_pool_find(qa_vfs_resources(owner->mounts),qa_resource_id(resource))!=resource)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restored appearance model lacks its actual content and scene owners");
    qa_bytes bytes=qa_resource_bytes(resource);
    if (parsed->source.size!=bytes.size || (bytes.size && memcmp(parsed->source.data,bytes.data,bytes.size)))
        return frontend_fail(error,QA_ERROR_FORMAT,"Restored appearance parsed holder differs from its retained source");
    const visual_model_recipe *recipe = NULL;
    if (owner->recipes_present) {
        if (owner->attached >= owner->recipe_count) return frontend_fail(error, QA_ERROR_FORMAT, "Imported visual cache exceeds its genuine opening receipts");
        recipe = owner->recipes + owner->attached;
        if (recipe->resource != resource || strcmp(recipe->path, path) ||
            (recipe->known && parsed->format != QA_MODEL_MDL && parsed->format != QA_MODEL_MD2))
            return frontend_fail(error, QA_ERROR_FORMAT, "Imported visual holder differs from its exact opening receipt row");
    }
    for (frontend_visual_owner *other=frontend->visuals;other;other=other->next)
        for (const frontend_model *model=other->models;model;model=model->next)
            if (model->scene==scene || (other==owner && model->resource==resource &&
                !strcmp(model->path,path) && model_translation_equal(model->scene,scene)))
                return frontend_fail(error,QA_ERROR_FORMAT,"Restored appearance repeats a scene owner or path");
    size_t length=strlen(path);
    if (length>SIZE_MAX-sizeof(frontend_model)-1)
        return frontend_fail(error,QA_ERROR_MEMORY,"Restored appearance path exceeds address space");
    frontend_model *model=calloc(1,sizeof(*model)+length+1);
    if (!model) return frontend_fail(error,QA_ERROR_MEMORY,"Attaching restored appearance holder");
    if (!frontend_model_retain(inventory,parsed,&model->lease,error)) { free(model); return false; }
    memcpy(model->path,path,length+1); model->model=parsed; model->scene=scene;
    model->resource=(qa_resource *)resource; qa_resource_retain(model->resource);
    if (recipe) { model->source_rank = recipe->rank; model->source_rank_known = recipe->known; }
    frontend_model **tail=&owner->models;
    while (*tail) tail=&(*tail)->next;
    *tail=model; ++owner->attached; return true;
}
typedef struct visual_owner_plan {
    qa_actor_owner owner; qa_scene_family family; uint64_t view;
    visual_model_recipe *recipes; size_t count; bool present;
} visual_owner_plan;
static void plans_free(visual_owner_plan *plans, size_t count)
{ for (size_t i = 0; plans && i < count; ++i) recipes_free(plans[i].recipes, plans[i].count); free(plans); }
static bool topology_fields(qa_source_save_io *io, qa_application *application, visual_owner_plan **plans, size_t *count)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; uint8_t magic[4]={'Q','F','V','T'}; uint32_t schema=2;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFVT",4) || !qa_source_save_u32(io,&schema) || (schema!=1 && schema!=2) ||
        !qa_source_save_count(io,count,reading?io->input.size/22:SIZE_MAX/sizeof(**plans))) return false;
    if (reading && *count) {
        *plans=calloc(*count,sizeof(**plans));
        if (!*plans) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining actual appearance owner topology");
    }
    qa_strings *strings=qa_session_strings(qa_application_session(application));
    qa_application_content_graph *graph = qa_application_content_graph_read(application);
    if (!graph) return false;
    for (size_t i=0;i<*count;++i) {
        visual_owner_plan *plan=&(*plans)[i]; uint32_t family=plan->family;
        char *key=!reading && plan->owner<=UINT32_MAX?(char *)qa_strings_cstr(strings,(qa_string_id)plan->owner):NULL;
        if (!reading && (!plan->owner || !key)) return false;
        bool ok=frontend_save_text(io,&key);
        if (reading) {
            plan->owner=key?qa_strings_find(strings,(qa_bytes){(const uint8_t *)key,strlen(key)}):0;
            free(key);
        }
        if (!ok || !plan->owner || !qa_source_save_u32(io,&family) || family>QA_SCENE_Q3 ||
            !qa_source_save_u64(io,&plan->view) || !plan->view) return false;
        plan->family=(qa_scene_family)family;
        for (size_t j=0;j<i;++j) if (plan->view==(*plans)[j].view) return false;
        if (schema < 2) continue;
        plan->present = true;
        if (!qa_source_save_count(io, &plan->count, reading ? io->input.size / 26 : SIZE_MAX / sizeof(*plan->recipes))) return false;
        if (reading && plan->count) {
            plan->recipes = calloc(plan->count, sizeof(*plan->recipes));
            if (!plan->recipes) return frontend_fail(io->error, QA_ERROR_MEMORY, "Importing actual visual opening receipts");
        }
        for (size_t row = 0; row < plan->count; ++row) {
            visual_model_recipe *recipe = plan->recipes + row;
            uint64_t pool = 0, resource = 0;
            if (!reading && !qa_application_content_resource_id(graph, recipe->resource, &pool, &resource)) return false;
            if (!frontend_save_text(io, &recipe->path) || !recipe->path || !recipe->path[0] ||
                !qa_source_save_u64(io, &pool) || !pool || !qa_source_save_u64(io, &resource) || !resource ||
                !qa_source_save_bool(io, &recipe->known) || !qa_source_save_i64(io, &recipe->rank) ||
                recipe->rank < -1 || (!recipe->known && recipe->rank)) return false;
            const qa_resource *actual = reading ? qa_application_content_resource(graph, pool, resource) : recipe->resource;
            qa_vfs *files = qa_application_content_view(graph, plan->view);
            if (!actual || !files || qa_application_content_pool(graph, pool) != qa_vfs_resources(files)) return false;
            if (reading) { recipe->resource = actual; qa_resource_retain((qa_resource *)actual); }
        }
    }
    return true;
}
bool frontend_visual_topology_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance topology capture requires idle actual owners and empty output");
    qa_application_content_graph *graph=qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance topology requires actual content graph lease");
    size_t count=frontend_visual_owner_count(frontend);
    visual_owner_plan *plans=count?calloc(count,sizeof(*plans)):NULL;
    if (count && !plans) return frontend_fail(error,QA_ERROR_MEMORY,"Capturing appearance owner topology");
    bool ok=true;
    for (size_t i=0;ok && i<count;++i) {
        frontend_visual_owner_view view;
        ok=frontend_visual_owner_read(frontend,i,&view) && qa_application_provider_instance(frontend->application,view.owner);
        if (ok) {
            uint64_t id=qa_application_content_view_id(graph,view.mounts);
            ok=id!=0; plans[i]=(visual_owner_plan){.owner=view.owner,.family=view.family,.view=id,.present=true};
            plans[i].count = frontend_visual_model_count(frontend, i);
            plans[i].recipes = plans[i].count ? calloc(plans[i].count, sizeof(*plans[i].recipes)) : NULL;
            if (plans[i].count && !plans[i].recipes) ok = frontend_fail(error, QA_ERROR_MEMORY, "Capturing actual visual opening receipts");
            const frontend_visual_owner *owner = owner_at(frontend, i); size_t row = 0;
            for (const frontend_model *model = owner ? owner->models : NULL; ok && model; model = model->next, ++row) {
                size_t length = strlen(model->path) + 1;
                char *path = malloc(length);
                if (!path) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Capturing actual visual request path"); break; }
                memcpy(path, model->path, length);
                plans[i].recipes[row] = (visual_model_recipe){path, model->resource, model->source_rank, model->source_rank_known};
                qa_resource_retain(model->resource);
            }
        }
    }
    qa_source_save_io io={0};
    ok=ok && qa_source_save_writer(&io,qa_application_session(frontend->application),error) &&
        topology_fields(&io,frontend->application,&plans,&count) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); plans_free(plans, count);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Unqualified appearance owner topology");
    return ok;
}
bool frontend_visual_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->visuals)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance preparation requires empty isolated owner list");
    qa_application_content_graph *graph=qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance preparation requires preloaded content graph");
    visual_owner_plan *plans=NULL; size_t count=0; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,qa_application_session(frontend->application),bytes,error) &&
        topology_fields(&io,frontend->application,&plans,&count) && qa_source_save_finish(&io,NULL);
    for (size_t i=0;ok && i<count;++i) ok=frontend->order && qa_application_content_view(graph,plans[i].view);
    frontend_visual_owner **tail=&frontend->visuals;
    for (size_t i=0;ok && i<count;++i) {
        frontend_visual_owner *owner=calloc(1,sizeof(*owner));
        if (!owner) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Preparing genuine appearance owner"); break; }
        owner->owner=plans[i].owner; owner->family=plans[i].family; *tail=owner; tail=&owner->next;
        owner->frontend = frontend;
        owner->recipes = plans[i].recipes; owner->recipe_count = plans[i].count; owner->recipes_present = plans[i].present;
        plans[i].recipes = NULL; plans[i].count = 0;
        ok=qa_application_content_claim_view(graph,plans[i].view,&owner->mounts,error);
        for (frontend_visual_owner *prior=frontend->visuals;ok && prior!=owner;prior=prior->next)
            if (prior->owner==owner->owner && prior->family==owner->family &&
                qa_vfs_lookup_equal(prior->mounts,owner->mounts))
                ok=frontend_fail(error,QA_ERROR_FORMAT,"Saved appearance repeats the same provider content lookup");
        if (ok) ok=(owner->images=qa_scene_resources_create_detached(owner->mounts,error))!=NULL;
        if (ok) ok=(owner->materials=qa_material_library_create_detached(owner->images,error))!=NULL;
        if (ok) ok=(owner->media=qa_media_library_create(owner->images,error))!=NULL;
    }
    plans_free(plans, count); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid saved appearance owner topology");
    return ok;
}
bool frontend_visual_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Appearance qualification requires idle actual owners");
    size_t count=frontend_visual_owner_count(frontend);
    for (size_t i=0;i<count;++i) {
        frontend_visual_owner_view view;
        if (!frontend_visual_owner_read(frontend,i,&view) || !qa_application_provider_instance(frontend->application,view.owner))
            return frontend_fail(error,QA_ERROR_FORMAT,"Appearance owner was not bound to actual prepared source");
    }
    return true;
}
bool frontend_visual_model_receipts_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual opening completion requires its actual imported owner");
    for (const frontend_visual_owner *owner = frontend->visuals; owner; owner = owner->next) {
        if (!owner->recipes_present) continue;
        size_t row = 0;
        for (const frontend_model *model = owner->models; model; model = model->next, ++row) {
            if (row >= owner->recipe_count || model->resource != owner->recipes[row].resource ||
                strcmp(model->path, owner->recipes[row].path) || model->source_rank != owner->recipes[row].rank ||
                model->source_rank_known != owner->recipes[row].known)
                return frontend_fail(error, QA_ERROR_FORMAT, "Imported visual cache differs from its retained opening receipts");
        }
        if (row != owner->recipe_count || owner->attached != row)
            return frontend_fail(error, QA_ERROR_FORMAT, "Imported visual cache omits a genuine opening receipt consumer");
    }
    return true;
}
const qa_scene_resources *frontend_visual_images_at(qa_frontend *frontend, size_t index)
{
    if (!frontend) return NULL;
    frontend_visual_owner *owner = frontend->visuals;
    while (owner && index) { owner = owner->next; --index; }
    return owner ? owner->images : NULL;
}
bool frontend_visuals_remap(qa_frontend *frontend, const char *original, const char *replacement, float offset, qa_error *error)
{
    for (frontend_visual_owner *owner = frontend->visuals; owner; owner = owner->next)
        if (!qa_material_remap(owner->materials, original, replacement, offset, error)) return false;
    return true;
}
bool frontend_visuals_idle(const qa_frontend *frontend)
{
    if (!frontend) return false;
    for (const frontend_visual_owner *owner=frontend->visuals;owner;owner=owner->next) {
        if ((owner->materials && !qa_material_library_idle(owner->materials)) ||
            (owner->images && !qa_scene_resources_idle(owner->images)) ||
            (owner->media && !qa_media_library_idle(owner->media)) ||
            (owner->shader_movies && !frontend_material_movies_idle(owner->shader_movies))) return false;
        for (const frontend_model *model=owner->models;model;model=model->next)
            if (!qa_scene_model_idle(model->scene)) return false;
    }
    return true;
}
void frontend_visuals_destroy(qa_frontend *frontend)
{
    if (!frontend_visuals_idle(frontend)) return;
    while (frontend->visuals) {
        frontend_visual_owner *owner = frontend->visuals;
        if (owner->shader_movies && owner->media && !frontend->source_restoring) {
            frontend_material_movie_source expected = {.frontend = frontend, .files = owner->mounts,
                .images = owner->images, .materials = owner->materials, .media = owner->media,
                .context = owner, .current = visual_movie_current};
            if (!frontend_renderer_materials_adopt_movies(frontend, &expected,
                &owner->shader_movies, &owner->media, NULL)) return;
        }
        if (!frontend_material_movies_destroy(&owner->shader_movies, NULL)) return;
        frontend->visuals = owner->next;
        while (owner->models) {
            frontend_model *model = owner->models; owner->models = model->next;
            qa_scene_model_destroy(model->scene); qa_model_free(&model->owned_model);
            visual_content_release(model->replacement); visual_content_release(model->content);
            frontend_model_release(model->lease);
            qa_resource_release(model->resource); free(model);
        }
        qa_material_library_destroy(owner->materials);
        qa_media_library_destroy(owner->media);
        qa_scene_resources_destroy(owner->images); qa_vfs_destroy(owner->mounts);
        recipes_free(owner->recipes, owner->recipe_count); free(owner);
    }
}
static bool visual_owner(qa_frontend *frontend, const qa_application_visual_view *view,
    frontend_visual_owner **out, qa_error *error)
{
    if (frontend->resource_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual admission is held by the actual resource transaction");
    qa_command_context source = {.owner = view->provider, .origin = QA_COMMAND_SERVER,
        .dialect = view->family == QA_GAME_Q2 ? QA_CONSOLE_Q2 : view->family == QA_GAME_Q3 ? QA_CONSOLE_Q3 : QA_CONSOLE_Q1};
    qa_command_context captured;
    if (!qa_application_capture_command_context(frontend->application, &source, &captured, error)) return false;
    qa_vfs *files = qa_application_context_files(frontend->application, &captured, NULL);
    if (!files) return frontend_fail(error, QA_ERROR_NOT_FOUND, "appearance owner has no active content view");
    qa_scene_family family = view->family == QA_GAME_Q2 ? QA_SCENE_Q2 : view->family == QA_GAME_Q3 ? QA_SCENE_Q3 : QA_SCENE_Q1;
    for (frontend_visual_owner *owner = frontend->visuals; owner; owner = owner->next)
        if (owner->owner == view->provider && owner->family == family && qa_vfs_lookup_equal(owner->mounts,files)) {
            if (owner->construction_failed)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual resource construction retains refused cleanup");
            *out = owner; return true;
        }
    frontend_visual_owner *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "allocating appearance resources");
    owner->owner = view->provider;
    owner->frontend = frontend; owner->command = captured;
    owner->family = family;
    owner->mounts = qa_vfs_clone(files, error);
    owner->images = owner->mounts ? qa_scene_resources_create(owner->mounts, error) : NULL;
    bool configured = owner->images && frontend_image_policy_initialize(frontend, owner->images, error);
    owner->materials = configured ? qa_material_library_create(owner->images, frontend->order, error) : NULL;
    owner->media = owner->materials ? qa_media_library_create(owner->images, error) : NULL;
    qa_scene_image_options images = {.family = owner->family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .transparent_index = -1};
    frontend_material_movie_source movies = {.frontend = frontend, .files = owner->mounts,
        .images = owner->images, .materials = owner->materials, .media = owner->media,
        .context = owner, .current = visual_movie_current};
    bool ok = owner->mounts && owner->images && owner->materials && owner->media &&
        frontend_material_movies_create(&movies, &owner->shader_movies, error) &&
        qa_material_library_load_scripts(owner->materials, owner->mounts, &images, error) &&
        frontend_material_remaps(frontend, owner->materials, error);
    if (!ok) {
        if (!frontend_material_movies_destroy(&owner->shader_movies, NULL)) {
            owner->construction_failed = true;
            owner->next = frontend->visuals; frontend->visuals = owner; return false;
        }
        qa_material_library_destroy(owner->materials);
        qa_media_library_destroy(owner->media);
        qa_scene_resources_destroy(owner->images);
        qa_vfs_destroy(owner->mounts); free(owner); return false;
    }
    owner->next = frontend->visuals; frontend->visuals = owner; *out = owner;
    return true;
}
static void player_translation(uint8_t colors, uint8_t out[256])
{
    for (unsigned i = 0; i < 256; ++i) out[i] = (uint8_t)i;
    unsigned top = colors & 240u, bottom = (colors & 15u) << 4;
    for (unsigned i = 0; i < 16; ++i) {
        out[16 + i] = (uint8_t)(top < 128 ? top + i : top + 15 - i);
        out[96 + i] = (uint8_t)(bottom < 128 ? bottom + i : bottom + 15 - i);
    }
}
static bool opening_rank(const qa_vfs *files, const qa_vfs_acquisition *opening, int64_t *out, qa_error *error)
{
    if (!files || !opening || !opening->path || !opening->lookup_path || !opening->link_source || !opening->link_target)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Model opening has no actual retained recipe");
    if (opening->opening_present) {
        *out = opening->opening.rank; return true;
    }
    for (size_t i = 0; i < qa_vfs_retained_read_count(files); ++i) {
        qa_vfs_read_reference row;
        if (!qa_vfs_retained_read_at(files, i, &row) || row.mount != opening->mount ||
            qa_resource_id(row.resource) != opening->resource_id || strcmp(row.path, opening->path) ||
            strcmp(row.lookup_path, opening->lookup_path) || strcmp(row.link_source, opening->link_source) ||
            strcmp(row.link_target, opening->link_target)) continue;
        *out = row.opening.rank; return true;
    }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Model acquisition lost its genuine historical first opening");
}
static bool optional_resource(qa_vfs *files, const char *path, qa_resource **out, qa_error *error)
{
    qa_error observed = {0};
    if (qa_vfs_acquire(files, path, out, NULL, &observed)) return true;
    if (observed.code == QA_ERROR_NOT_FOUND) return true;
    if (error) *error = observed; return false;
}
static bool q1_pair_skins(qa_vfs *files, const qa_model_replacement *pair, bool *valid, qa_error *error)
{
    *valid = false;
    for (uint32_t mesh = 0; mesh < pair->mesh->mesh_count; ++mesh)
        for (uint32_t group = 0; group < pair->source->skin_group_count; ++group)
            for (uint32_t frame = 0; frame < pair->source->skin_groups[group].count; ++frame) {
                const qa_model_mesh *surface = &pair->mesh->meshes[mesh];
                if (!surface->shader_count) return true;
                qa_bytes shader = qa_model_shader_name(surface->shaders);
                if (!shader.data || memchr(shader.data, 0, shader.size) || shader.size == SIZE_MAX) return true;
                char *name = malloc(shader.size + 1), *base = NULL;
                if (!name) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining real Q1 replacement skin name");
                memcpy(name, shader.data, shader.size); name[shader.size] = 0;
                bool ok = qa_model_q1_skin_path(name, group, frame, &base, error); free(name);
                if (!ok) return false;
                size_t length = strlen(base);
                if (length > SIZE_MAX - 5) { free(base); return frontend_fail(error, QA_ERROR_MEMORY, "Replacement skin path overflows"); }
                char *path = realloc(base, length + 5);
                if (!path) { free(base); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining real Q1 indexed sidecar path"); }
                memcpy(path + length, ".lmp", 5);
                qa_resource *resource = NULL;
                ok = optional_resource(files, path, &resource, error); free(path);
                if (!ok || !resource) { qa_resource_release(resource); return ok; }
                qa_image decoded = {0}; qa_error observed = {0};
                ok = qa_image_decode_qpic(qa_resource_bytes(resource), &decoded, &observed);
                qa_image_free(&decoded); qa_resource_release(resource);
                if (!ok) {
                    if (observed.code == QA_ERROR_FORMAT) return true;
                    if (error) *error = observed; return false;
                }
            }
    *valid = true; return true;
}
static bool replacement_read(const qa_model *native, const char *requested, int64_t source_rank, qa_vfs *files,
    frontend_visual_content **out, qa_model_replacement *description, qa_error *error)
{
    qa_model_replacement_paths paths = {0}; qa_vfs_acquisition opening = {0};
    frontend_visual_content *content = calloc(1, sizeof(*content));
    if (!content) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual replacement model content");
    content->references = 1; content->files = files;
    bool q2 = native->format == QA_MODEL_MD2, present = false;
    bool ok = qa_model_md5_paths(requested, q2, &paths, error);
    qa_error observed = {0};
    if (ok && !qa_vfs_acquire_receipt(files, paths.mesh, &content->resource, &opening, &observed)) {
        if (observed.code != QA_ERROR_NOT_FOUND) { if (error) *error = observed; ok = false; }
    }
    if (ok && content->resource) {
        int64_t rank;
        ok = opening_rank(files, &opening, &rank, error);
        if (ok && qa_model_md5_replacement_allowed(source_rank, rank)) {
            ok = optional_resource(files, paths.animation, &content->animation_resource, error);
            if (ok && content->animation_resource && q2) ok = optional_resource(files, paths.scales, &content->scale_resource, error);
            if (ok && content->animation_resource) {
                observed = (qa_error){0};
                bool decoded = qa_model_load(qa_resource_bytes(content->resource), &content->model, &observed) &&
                    qa_model_animation_load(qa_resource_bytes(content->animation_resource), &content->animation, &observed);
                if (decoded && content->scale_resource)
                    decoded = qa_model_animation_scale_json(&content->animation, qa_resource_bytes(content->scale_resource), NULL, NULL, &observed);
                if (decoded) decoded = qa_model_replacement_init(native, &content->model, &content->animation, description, &observed);
                if (!decoded) {
                    if (observed.code != QA_ERROR_FORMAT && observed.code != QA_ERROR_ARGUMENT) {
                        if (error) *error = observed; ok = false;
                    }
                } else if (q2) present = true;
                else ok = q1_pair_skins(files, description, &present, error);
            }
        }
    }
    qa_vfs_acquisition_dispose(&opening); qa_model_replacement_paths_free(&paths);
    if (!ok || !present) visual_content_release(content);
    else *out = content;
    return ok;
}
static bool retained_source_rank(qa_frontend *frontend, frontend_visual_owner *owner, const char *path,
    const qa_resource *source, const qa_vfs_acquisition *receipt, int64_t *out, qa_error *error)
{
    qa_command_context request = {.owner = owner->owner, .origin = QA_COMMAND_SERVER,
        .dialect = owner->family == QA_SCENE_Q2 ? QA_CONSOLE_Q2 : owner->family == QA_SCENE_Q3 ? QA_CONSOLE_Q3 : QA_CONSOLE_Q1};
    qa_command_context captured;
    if (!qa_application_capture_command_context(frontend->application, &request, &captured, error)) return false;
    qa_vfs *files = qa_application_context_files(frontend->application, &captured, NULL);
    if (!files || !qa_vfs_lookup_equal(files, owner->mounts))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Held model source differs from its actual provider lookup");
    char *normalized = qa_vfs_normalize_path(path, error);
    if (!normalized) return false;
    bool found = false, ok = true;
    const qa_vfs *views[] = {files, owner->mounts};
    for (unsigned view = 0; ok && view < 2; ++view)
        for (size_t i = 0; ok && i < qa_vfs_retained_read_count(views[view]); ++i) {
            qa_vfs_read_reference row;
            if (!qa_vfs_retained_read_at(views[view], i, &row) || row.resource != source || strcmp(row.path, normalized)) continue;
            if (receipt) {
                if (row.mount != receipt->mount || receipt->resource_id != qa_resource_id(source) ||
                    !receipt->path || !receipt->lookup_path || !receipt->link_source || !receipt->link_target ||
                    strcmp(row.path, receipt->path) || strcmp(row.lookup_path, receipt->lookup_path) ||
                    strcmp(row.link_source, receipt->link_source) || strcmp(row.link_target, receipt->link_target)) continue;
                found = true;
                continue;
            }
            int64_t rank = row.opening.rank;
            if (ok && found && rank != *out) ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Held model has ambiguous actual opening ranks");
            if (ok) { *out = rank; found = true; }
        }
    free(normalized);
    if (ok && found && receipt) return opening_rank(owner->mounts, receipt, out, error);
    return ok && (found || frontend_fail(error, QA_ERROR_ARGUMENT, "Held alias model lacks its genuine source opening receipt"));
}
static bool model_read(qa_frontend *frontend, frontend_visual_owner *owner, const char *path, const qa_resource *source,
    const qa_vfs_acquisition *source_opening, bool colored, uint8_t colors, frontend_model **out, qa_error *error)
{
    uint8_t translation[256];
    if (colored) player_translation(colors, translation);
    for (frontend_model *model = owner->models; model; model = model->next) {
        if (strcmp(model->path, path) || (source && model->resource != source)) continue;
        const qa_scene_image_options *options = qa_scene_model_image_options(model->scene);
        bool translated = colored && model->model->format == QA_MODEL_MDL;
        if (options && options->translation.size == (translated ? sizeof(translation) : 0) &&
            (!translated || !memcmp(options->translation.data, translation, sizeof(translation)))) {
            *out = model;
            return true;
        }
    }
    size_t length = strlen(path);
    if (length > SIZE_MAX - sizeof(frontend_model) - 1)
        return frontend_fail(error, QA_ERROR_MEMORY, "appearance path exceeds native storage");
    frontend_model *model = calloc(1, sizeof(*model) + length + 1);
    if (!model) return frontend_fail(error, QA_ERROR_MEMORY, "allocating decoded appearance");
    memcpy(model->path, path, length + 1);
    qa_scene_image_options images = {.family = owner->family, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .usage = QA_IMAGE_USAGE_SKIN,
        .transparent_index = owner->family == QA_SCENE_Q1 ? 255 : -1};
    bool ok; qa_vfs_acquisition opening = {0};
    if (source) {
        ok = qa_resource_pool_find(qa_vfs_resources(owner->mounts), qa_resource_id(source)) == source;
        if (!ok) frontend_fail(error, QA_ERROR_NOT_FOUND, "Appearance model lost its actual retained content owner");
        else { model->resource = (qa_resource *)source; qa_resource_retain(model->resource); }
    } else ok = qa_vfs_acquire_receipt(owner->mounts, path, &model->resource, &opening, error);
    if (ok) {
        model->content = calloc(1, sizeof(*model->content));
        if (!model->content) ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual native parsed model");
        else {
            model->content->references = 1; model->content->files = owner->mounts;
            model->content->resource = model->resource; qa_resource_retain(model->resource);
            ok = qa_model_load(qa_resource_bytes(model->resource), &model->content->model, error);
            model->model = &model->content->model;
        }
    }
    if (ok && colored && model->model->format == QA_MODEL_MDL)
        images.translation = (qa_bytes){translation, sizeof(translation)};
    if (ok) ok=qa_scene_model_create(model->model, owner->images, owner->materials, &images, &model->scene, error);
    qa_scene_model_content_lease native = {0};
    if (ok) ok = visual_content_lease(model->content, false, &native, error) && qa_scene_model_source_bind(model->scene, &native, error);
    if (native.release) native.release(native.context);
    frontend_model_policy policy;
    if (ok) ok = frontend_model_policy_read(frontend, &policy, error);
    bool alias = ok && (model->model->format == QA_MODEL_MDL || model->model->format == QA_MODEL_MD2);
    if (alias && source && source_opening) {
        ok = source_opening->resource_id == qa_resource_id(source) && source_opening->opening_present &&
            retained_source_rank(frontend, owner, path, source, source_opening, &model->source_rank, error);
        if (!ok && (!error || error->code == QA_OK))
            frontend_fail(error, QA_ERROR_ARGUMENT, "Borrowed model lost its actual constructor opening receipt");
    } else if (alias) ok = source ? retained_source_rank(frontend, owner, path, source, NULL, &model->source_rank, error)
        : opening_rank(owner->mounts, &opening, &model->source_rank, error);
    if (ok && alias) model->source_rank_known = true;
    qa_vfs_acquisition_dispose(&opening);
    if (ok && frontend_model_policy_load(&policy, owner->family, model->model)) {
        ok = replacement_read(model->model, model->path, model->source_rank, owner->mounts,
            &model->replacement, &model->replacement_description, error);
        if (ok && model->replacement) {
            qa_scene_model_content_lease mesh = {0}, alias_source = {0}, animation = {0};
            ok = visual_content_lease(model->replacement, false, &mesh, error) &&
                visual_content_lease(model->content, false, &alias_source, error) &&
                visual_content_lease(model->replacement, true, &animation, error) &&
                qa_scene_model_replacement_prepare(model->scene, &model->replacement_description, &mesh, &alias_source, &animation, error);
            if (mesh.release) mesh.release(mesh.context);
            if (alias_source.release) alias_source.release(alias_source.context);
            if (animation.release) animation.release(animation.context);
        }
    }
    if (ok && alias) ok = qa_scene_model_replacement_policy_bind(model->scene,
        frontend_model_policy_select(&policy, owner->family, model->model, 0, true),
        frontend_model_policy_distance(&policy, model->model),
        model->replacement ? &model->replacement_description : NULL, error);
    if (!ok) {
        qa_scene_model_destroy(model->scene); qa_model_free(&model->owned_model);
        visual_content_release(model->replacement); visual_content_release(model->content);
        qa_resource_release(model->resource); free(model); return false;
    }
    model->next = owner->models; owner->models = model; *out = model; return true;
}
typedef struct visual_policy_row {
    frontend_visual_owner *owner;
    frontend_model *model;
    frontend_visual_content *original, *prepared;
    qa_model_replacement description;
} visual_policy_row;
struct frontend_visual_policy {
    qa_frontend *frontend;
    qa_application *application;
    frontend_visual_owner *owners;
    visual_policy_row *rows;
    size_t count;
    bool sealed, published;
};
static bool visual_policy_current(const frontend_visual_policy *ticket)
{
    if (!ticket || ticket->frontend->application != ticket->application ||
        ticket->frontend->visuals != ticket->owners || ticket->frontend->stepping) return false;
    size_t at = 0;
    for (frontend_visual_owner *owner = ticket->owners; owner; owner = owner->next)
        for (frontend_model *model = owner->models; model; model = model->next) {
            if (at == ticket->count || ticket->rows[at].owner != owner || ticket->rows[at].model != model ||
                (!ticket->published && ticket->rows[at].original != model->replacement)) return false;
            ++at;
        }
    return at == ticket->count;
}
static void visual_policy_dispose(frontend_visual_policy *ticket)
{
    for (size_t i = 0; i < ticket->count; ++i) visual_content_release(ticket->rows[i].prepared);
    free(ticket->rows); free(ticket);
}
bool frontend_visual_policy_prepare(qa_frontend *frontend, const frontend_model_policy *policy,
    const frontend_visual_policy_binding *bindings, size_t binding_count,
    frontend_visual_policy **out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !policy || !out || *out ||
        (binding_count && !bindings))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual policy requires its actual retained frontend and model preparations");
    frontend_visual_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining prepared visual model cache");
    ticket->frontend = frontend; ticket->application = frontend->application; ticket->owners = frontend->visuals;
    for (frontend_visual_owner *owner = frontend->visuals; owner; owner = owner->next)
        for (frontend_model *model = owner->models; model; model = model->next) {
            if (ticket->count == SIZE_MAX / sizeof(*ticket->rows)) goto memory;
            ++ticket->count;
        }
    ticket->rows = ticket->count ? calloc(ticket->count, sizeof(*ticket->rows)) : NULL;
    if (ticket->count && !ticket->rows) goto memory;
    size_t at = 0;
    for (frontend_visual_owner *owner = frontend->visuals; owner; owner = owner->next)
        for (frontend_model *model = owner->models; model; model = model->next, ++at) {
            visual_policy_row *row = ticket->rows + at;
            *row = (visual_policy_row){.owner = owner, .model = model, .original = model->replacement};
            if (model->model->format != QA_MODEL_MDL && model->model->format != QA_MODEL_MD2) continue;
            qa_scene_model_image_policy *prepared = NULL;
            for (size_t i = 0; i < binding_count; ++i) if (bindings[i].model == model->scene) {
                if (prepared) {
                    visual_policy_dispose(ticket);
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual policy repeats an actual model preparation");
                }
                prepared = bindings[i].ticket;
            }
            if (!prepared) {
                visual_policy_dispose(ticket);
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual policy omits an actual retained alias model");
            }
            const qa_model_replacement *description = model->replacement ? &model->replacement_description : NULL;
            if (!description) {
                bool configured, enabled; double cutoff; const qa_scene_model *selected = NULL;
                if (!qa_scene_model_replacement_policy_read(model->scene, &configured, &enabled, &cutoff, &selected)) {
                    visual_policy_dispose(ticket);
                    return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual policy lost the actual imported model selection");
                }
                if (configured && selected) description = qa_scene_model_replacement_description(selected);
            }
            if (!description && frontend_model_policy_load(policy, owner->family, model->model)) {
                if (!model->source_rank_known) {
                    visual_policy_dispose(ticket);
                    return frontend_fail(error, QA_ERROR_UNSUPPORTED, "Imported alias lacks its historical source acquisition rank");
                }
                if (!replacement_read(model->model, model->path, model->source_rank, owner->mounts, &row->prepared, &row->description, error)) {
                    visual_policy_dispose(ticket); return false;
                }
                if (row->prepared) {
                    qa_scene_model_content_lease mesh = {0}, source = {0}, animation = {0};
                    bool ok = visual_content_lease(row->prepared, false, &mesh, error) &&
                        frontend_scene_model_content_clone(model->scene, QA_SCENE_MODEL_CONTENT_SOURCE, &source, error) &&
                        visual_content_lease(row->prepared, true, &animation, error) &&
                        qa_scene_model_image_policy_replacement(prepared, &row->description, &mesh, &source, &animation, error);
                    if (mesh.release) mesh.release(mesh.context);
                    if (source.release) source.release(source.context);
                    if (animation.release) animation.release(animation.context);
                    if (!ok) { visual_policy_dispose(ticket); return false; }
                    description = &row->description;
                }
            }
            if (!qa_scene_model_image_policy_select(prepared,
                frontend_model_policy_select(policy, owner->family, model->model, 0, true),
                frontend_model_policy_distance(policy, model->model), description, error)) {
                visual_policy_dispose(ticket); return false;
            }
        }
    *out = ticket; return true;
memory:
    /* No child is prepared before the complete cache row allocation. */
    free(ticket->rows); free(ticket);
    return frontend_fail(error, QA_ERROR_MEMORY, "Prepared visual cache exceeds addressable storage");
}
bool frontend_visual_policy_ready(frontend_visual_policy *ticket, qa_error *error)
{
    if (!visual_policy_current(ticket) || ticket->published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Prepared visual cache lost its actual owners");
    ticket->sealed = true; return true;
}
bool frontend_visual_policy_ready_is(const frontend_visual_policy *ticket)
{ return visual_policy_current(ticket) && ticket->sealed && !ticket->published; }
void frontend_visual_policy_publish(frontend_visual_policy *ticket)
{
    if (!frontend_visual_policy_ready_is(ticket)) return;
    for (size_t i = 0; i < ticket->count; ++i) {
        visual_policy_row *row = ticket->rows + i;
        if (row->prepared) {
            frontend_visual_content *retired = row->model->replacement;
            row->model->replacement = row->prepared; row->prepared = retired;
            row->model->replacement_description = row->description;
        }
    }
    ticket->published = true;
}
bool frontend_visual_policy_finish(frontend_visual_policy **owner, qa_error *error)
{
    if (!owner || !visual_policy_current(*owner) || !(*owner)->published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual cache retirement requires its actual published preparation");
    visual_policy_dispose(*owner); *owner = NULL; return true;
}
bool frontend_visual_policy_abort(frontend_visual_policy **owner, qa_error *error)
{
    if (!owner || !visual_policy_current(*owner) || (*owner)->published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Visual cache abort requires its actual unpublished preparation");
    visual_policy_dispose(*owner); *owner = NULL; return true;
}
static bool model_opening_initialize(qa_frontend *f, qa_scene_family family, qa_vfs *files,
    const qa_resource *resource, const char *path, int64_t rank,
    const qa_model *native, qa_scene_model *root, qa_error *error)
{
    if (!f || !f->application || f->capture || f->resource_inventory || f->source_restoring ||
        !resource || !path || !*path || !files || (unsigned)family > QA_SCENE_Q3 || rank < -1 ||
        !native || !root || qa_scene_model_source(root) != native ||
        qa_scene_resources_files(qa_scene_model_resource_owner(root)) != files ||
        qa_material_library_resource_owner(qa_scene_model_material_owner(root)) != qa_scene_model_resource_owner(root) ||
        qa_resource_pool_find(qa_vfs_resources(files), qa_resource_id(resource)) != resource)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Initial model policy requires its genuine decoded parent and first opening");
    if (native->format != QA_MODEL_MDL && native->format != QA_MODEL_MD2) return true;
    frontend_model_policy policy;
    if (!frontend_model_policy_read(f, &policy, error)) return false;
    frontend_visual_content *content = NULL; qa_model_replacement replacement = {0};
    bool ok = true;
    if (frontend_model_policy_load(&policy, family, native))
        ok = replacement_read(native, path, rank, files, &content, &replacement, error);
    if (ok && content) {
        qa_scene_model_content_lease mesh = {0}, animation = {0};
        ok = visual_content_lease(content, false, &mesh, error) &&
            visual_content_lease(content, true, &animation, error) &&
            qa_scene_model_replacement_prepare_parent(root, &replacement, &mesh, &animation, error);
        if (mesh.release) mesh.release(mesh.context);
        if (animation.release) animation.release(animation.context);
    }
    if (ok) ok = qa_scene_model_replacement_policy_bind(root,
        frontend_model_policy_select(&policy, family, native, 0, true),
        frontend_model_policy_distance(&policy, native), content ? &replacement : NULL, error);
    visual_content_release(content); return ok;
}
bool frontend_visual_registered_model_initialize(qa_frontend *f, const qa_q3_model_opening *opening,
    const qa_model *native, qa_scene_model *root, qa_error *error)
{
    if (!opening || !opening->present || !opening->receipt ||
        qa_scene_model_resource_owner(root) != opening->provider.images ||
        qa_scene_model_material_owner(root) != opening->provider.materials)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Initial registry model lost its actual provider parent");
    return model_opening_initialize(f, opening->provider.family, opening->provider.mounts,
        opening->resource, opening->receipt->path, opening->rank, native, root, error);
}
bool frontend_visual_model_opening_initialize(qa_frontend *f, qa_scene_family family, qa_vfs *files,
    const qa_resource *resource, const qa_vfs_acquisition *receipt, const qa_model *native,
    qa_scene_model *root, qa_error *error)
{
    if (!files || !resource || !receipt || !receipt->path ||
        receipt->resource_id != qa_resource_id(resource) || !qa_vfs_acquisition_retained(files, receipt, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Initial native model has no actual retained acquisition");
    int64_t rank;
    if (!opening_rank(files, receipt, &rank, error)) return false;
    return model_opening_initialize(f, family, files, resource, receipt->path, rank, native, root, error);
}
static bool native_cache_policy_prepare(const frontend_model_policy *policy, qa_scene_family family,
    qa_vfs *files, const qa_resource *resource, const qa_vfs_acquisition *opening,
    const qa_model *native, const qa_scene_model *root,
    const frontend_visual_policy_binding *bindings, size_t count, bool *covered, qa_error *error)
{
    if (!native || !root || qa_scene_model_source(root) != native)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native model cache lost its actual decoded parent");
    if (native->format != QA_MODEL_MDL && native->format != QA_MODEL_MD2) return true;
    size_t index = 0;
    while (index < count && bindings[index].model != root) ++index;
    if (index == count || !opening || !resource || opening->resource_id != qa_resource_id(resource) ||
        qa_scene_resources_files(qa_scene_model_resource_owner(root)) != files)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native cache policy omits its genuine prepared root or opening");
    if (covered[index]) return true;
    int64_t rank;
    if (!opening_rank(files, opening, &rank, error)) return false;
    bool configured, enabled; double distance; const qa_scene_model *selected = NULL;
    if (!qa_scene_model_replacement_policy_read(root, &configured, &enabled, &distance, &selected))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native cache lost its retained model policy");
    const qa_model_replacement *description = configured && selected ? qa_scene_model_replacement_description(selected) : NULL;
    frontend_visual_content *content = NULL; qa_model_replacement replacement = {0};
    bool ok = true;
    if (!description && frontend_model_policy_load(policy, family, native)) {
        ok = replacement_read(native, opening->path, rank, files, &content, &replacement, error);
        if (ok && content) {
            qa_scene_model_content_lease mesh = {0}, animation = {0};
            ok = visual_content_lease(content, false, &mesh, error) &&
                visual_content_lease(content, true, &animation, error) &&
                qa_scene_model_image_policy_replacement_parent(bindings[index].ticket, &replacement, &mesh, &animation, error);
            if (mesh.release) mesh.release(mesh.context);
            if (animation.release) animation.release(animation.context);
            if (ok) description = &replacement;
        }
    }
    if (ok) ok = qa_scene_model_image_policy_select(bindings[index].ticket,
        frontend_model_policy_select(policy, family, native, 0, true), frontend_model_policy_distance(policy, native), description, error);
    visual_content_release(content);
    if (ok) covered[index] = true;
    return ok;
}
bool frontend_visual_registered_model_policy_prepare(qa_frontend *f, const frontend_model_policy *policy,
    qa_q3_presentation_assets *const *registries, size_t registry_count,
    const frontend_visual_policy_binding *bindings, size_t count, qa_error *error)
{
    if (!f || !f->application || !f->resource_inventory || !policy ||
        (registry_count && !registries) || (count && !bindings))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Registered model policy requires its actual captured registry roster");
    bool *covered = count ? calloc(count, sizeof(*covered)) : NULL;
    if (count && !covered) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual model policy coverage");
    for (frontend_visual_owner *owner = f->visuals; owner; owner = owner->next)
        for (frontend_model *model = owner->models; model; model = model->next)
            for (size_t i = 0; i < count; ++i) if (bindings[i].model == model->scene) covered[i] = true;
    bool ok = true;
    for (size_t registry = 0; ok && registry < registry_count; ++registry) {
        size_t rows = 0;
        ok = qa_q3_assets_model_count(registries[registry], &rows, error);
        for (size_t row = 0; ok && row < rows; ++row) {
            qa_q3_asset_model_holder holder;
            ok = qa_q3_assets_model_holder(registries[registry], row, &holder, error);
            for (uint32_t slot = 0; ok && holder.present && slot < 3; ++slot) {
                qa_scene_model *root = holder.scenes[slot]; const qa_model *native = holder.sources[slot];
                if (!root || !native || (native->format != QA_MODEL_MDL && native->format != QA_MODEL_MD2)) continue;
                size_t index = 0;
                while (index < count && bindings[index].model != root) ++index;
                if (index == count || qa_scene_model_source(root) != native) {
                    ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Registered native alias lost its actual prepared root"); break;
                }
                if (covered[index]) continue;
                qa_q3_model_opening opening;
                ok = qa_q3_assets_model_opening(registries[registry], row, slot, &opening, error);
                if (!ok) break;
                if (!opening.present || !opening.resource || !opening.receipt || !opening.receipt->path ||
                    !opening.provider.mounts || opening.provider.images != qa_scene_model_resource_owner(root) ||
                    opening.provider.materials != qa_scene_model_material_owner(root) ||
                    qa_resource_pool_find(qa_vfs_resources(opening.provider.mounts), qa_resource_id(opening.resource)) != opening.resource) {
                    ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Registered alias lacks its genuine first-opening recipe"); break;
                }
                bool configured, enabled; double distance; const qa_scene_model *selected = NULL;
                ok = qa_scene_model_replacement_policy_read(root, &configured, &enabled, &distance, &selected);
                const qa_model_replacement *description = ok && configured && selected ?
                    qa_scene_model_replacement_description(selected) : NULL;
                frontend_visual_content *content = NULL; qa_model_replacement replacement = {0};
                if (ok && !description && frontend_model_policy_load(policy, opening.provider.family, native)) {
                    ok = replacement_read(native, opening.receipt->path, opening.rank,
                        opening.provider.mounts, &content, &replacement, error);
                    if (ok && content) {
                        qa_scene_model_content_lease mesh = {0}, animation = {0};
                        ok = visual_content_lease(content, false, &mesh, error) &&
                            visual_content_lease(content, true, &animation, error) &&
                            qa_scene_model_image_policy_replacement_parent(bindings[index].ticket, &replacement, &mesh, &animation, error);
                        if (mesh.release) mesh.release(mesh.context);
                        if (animation.release) animation.release(animation.context);
                        if (ok) description = &replacement;
                    }
                }
                if (ok) ok = qa_scene_model_image_policy_select(bindings[index].ticket,
                    frontend_model_policy_select(policy, opening.provider.family, native, 0, true), frontend_model_policy_distance(policy, native), description, error);
                visual_content_release(content);
                if (ok) covered[index] = true;
            }
        }
    }
    for (size_t receiver = 0; ok && receiver < frontend_remote_q1_count(f); ++receiver) {
        frontend_remote_q1 *owner = frontend_remote_q1_at(f, receiver);
        frontend_remote_q1_view view;
        ok = frontend_remote_q1_metadata_read(owner, &view, error);
        for (size_t i = 0; ok && i < frontend_remote_q1_model_count(owner); ++i) {
            frontend_remote_q1_model_view model;
            ok = frontend_remote_q1_model_at(owner, i, &model, error);
            if (ok && !model.world) ok = native_cache_policy_prepare(policy, QA_SCENE_Q1, view.content.mounts, model.resource,
                model.opening, model.model, model.scene, bindings, count, covered, error);
        }
    }
    for (size_t receiver = 0; ok && receiver < frontend_remote_q2_count(f); ++receiver) {
        frontend_remote_q2 *owner = frontend_remote_q2_at(f, receiver);
        frontend_remote_q2_view view;
        ok = frontend_remote_q2_metadata_read(owner, &view, error);
        for (size_t i = 0; ok && i < frontend_remote_q2_model_count(owner); ++i) {
            frontend_remote_q2_model_view model;
            ok = frontend_remote_q2_model_at(owner, i, &model, error) &&
                native_cache_policy_prepare(policy, QA_SCENE_Q2, view.content.mounts, model.resource,
                    model.opening, model.model, model.scene, bindings, count, covered, error);
        }
    }
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_model *native = qa_scene_model_source(bindings[i].model);
        if ((native->format == QA_MODEL_MDL || native->format == QA_MODEL_MD2) && !covered[i])
            ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Complete model policy roster lacks a genuine source opening owner");
    }
    free(covered);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_ARGUMENT, "Registered model policy source changed");
    return ok;
}
static bool live_owner(qa_frontend *frontend, qa_actor_owner provider,
    qa_game_family family, frontend_visual_owner **out, qa_error *error)
{
    if (!frontend || !frontend->application || !provider || !out ||
        (family != QA_GAME_Q1 && family != QA_GAME_Q2 && family != QA_GAME_Q3) ||
        !qa_application_provider_instance(frontend->application, provider))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Live model admission requires its actual selected provider");
    qa_application_visual_view request = {.provider = provider, .family = family};
    frontend_visual_owner *owner;
    if (!visual_owner(frontend, &request, &owner, error)) return false;
    qa_scene_family expected = family == QA_GAME_Q1 ? QA_SCENE_Q1
        : family == QA_GAME_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q3;
    if (owner->family != expected || !frontend_visuals_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Live model admission differs from its idle appearance owner");
    *out = owner; return true;
}
bool frontend_visual_media_acquire(qa_frontend *frontend, qa_actor_owner provider,
    qa_game_family family, frontend_visual_owner_view *out, qa_error *error)
{
    frontend_visual_owner *owner;
    if (!out || !live_owner(frontend, provider, family, &owner, error)) return false;
    *out = (frontend_visual_owner_view){owner->owner, owner->family, owner->mounts, owner->images, owner->materials,
        owner->media, owner->shader_movies};
    return true;
}
bool frontend_visual_model_acquire(qa_frontend *frontend, qa_actor_owner provider,
    qa_game_family family, const char *path, const qa_resource *source,
    frontend_visual_model_view *out, qa_error *error)
{
    if (!path || !*path || !out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Live model admission requires its actual path and output");
    frontend_visual_owner *owner;
    if (!live_owner(frontend, provider, family, &owner, error)) return false;
    frontend_model *model;
    if (!model_read(frontend, owner, path, source, NULL, false, 0, &model, error)) return false;
    *out = (frontend_visual_model_view){model->path, model->resource, model->model, model->scene};
    return true;
}
static qa_model_transform transform(const qa_application_visual_view *view)
{
    qa_model_transform value;
    qa_model_transform_identity(&value);
    qa_vec3 axes[3]; frontend_camera_axes(view->body.angles, axes);
    value.origin[0] = view->body.origin.x; value.origin[1] = view->body.origin.y; value.origin[2] = view->body.origin.z;
    for (unsigned i = 0; i < 3; ++i) {
        value.axes[i][0] = axes[i].x; value.axes[i][1] = axes[i].y; value.axes[i][2] = axes[i].z;
        value.scale[i] = view->scale;
    }
    return value;
}
static bool flare_standard_image(const char *path)
{
    const char *names[] = {"misc/flare.tga", "sprites/psx_flare"};
    for (unsigned n = 0; n < 2; ++n) {
        size_t i = 0;
        while (names[n][i] && path[i]) {
            unsigned char c = (unsigned char)path[i];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c == '\\') c = '/';
            if (c != (unsigned char)names[n][i]) break;
            ++i;
        }
        if (!names[n][i] && (n == 1 || !path[i])) return true;
    }
    return false;
}
static bool visual_flare(qa_frontend *frontend, const qa_application_visual_view *view,
    const qa_scene_world_input *world, qa_scene_frame *frame, qa_error *error)
{
    frontend_visual_owner *owner = NULL;
    if (view->family != QA_GAME_Q2 || !view->q2_flare.image || !*view->q2_flare.image)
        return frontend_fail(error, QA_ERROR_FORMAT, "Q2 flare requires its genuine image receipt");
    if (world->legacy_policy.present && !world->legacy_policy.flares) return true;
    if (!visual_owner(frontend, view, &owner, error)) return false;
    qa_scene_image_options sampling = {.family = QA_SCENE_Q2, .wrap = QA_SCENE_CLAMP,
        .filter = QA_SCENE_LINEAR, .usage = QA_IMAGE_USAGE_SPRITE, .transparent_index = -1};
    const char *path = view->q2_flare.image;
    qa_scene_image *image = NULL;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        qa_error load = {0};
        if (qa_scene_image_load(owner->images, path, &sampling, &image, &load)) break;
        if (load.code != QA_ERROR_NOT_FOUND) { if (error) *error = load; return false; }
        if (!strcmp(path, "misc/flare.tga")) return true;
        path = "misc/flare.tga";
    }
    if (!image) return true;
    qa_scene_flare_options options = {.color = view->q2_flare.color,
        .rim_color = view->q2_flare.rim_color, .scale = view->scale != 0 ? view->scale : 1,
        .fade_start = view->q2_flare.fade_start, .fade_end = view->q2_flare.fade_end,
        .separate_rim = view->q2_flare.has_rim_color, .lock_angle = view->q2_flare.lock_angle,
        .standard_image = flare_standard_image(path)};
    bool ok = qa_scene_flare(frame, &world->view, view->body.origin, &options, image, error);
    qa_scene_image_release(image);
    return ok;
}
bool frontend_visuals_submit(qa_frontend *frontend, uint32_t seat, qa_actor_owner exclude,
    const qa_scene_world_input *world, qa_scene_frame *frame, qa_error *error)
{
    qa_actor_id local = {0};
    uint32_t launch_seat;
    if(frontend_seat_launch_id_read(frontend,seat,&launch_seat))
        (void)qa_application_player_actor(frontend->application, launch_seat, &local);
    const qa_actor_record *record; uint32_t cursor = 0;
    qa_actor_registry *actors = qa_world_actors(qa_application_world(frontend->application));
    while (qa_actors_next(actors, &cursor, &record)) {
        qa_actor_id actor = record->id;
        if (frontend_native_q3_actor_admitted(frontend, seat, actor)) continue;
        if (qa_actor_id_equal(actor, local) && !world->view.clip_enabled &&
            !qa_scene_world_q1_mirror_scope(frontend->scene_world, world, frame)) continue;
        qa_application_visual_view view; qa_error observed = {0};
        if (!qa_application_visual_read(frontend->application, actor, &view, &observed)) {
            if (observed.code == QA_ERROR_NOT_FOUND) continue;
            if (error) *error = observed;
            return false;
        }
        if (!view.visible) continue;
        if (exclude && view.provider == exclude) continue;
        if (view.q2_flare.present) {
            if (!visual_flare(frontend, &view, world, frame, error)) return false;
            continue;
        }
        if (view.family == QA_GAME_Q2 && view.scale == 0) view.scale = 1;
        if (view.alpha <= 0 || view.scale == 0) continue;
        qa_model_transform placement = transform(&view);
        qa_scene_vec4 color = {1, 1, 1, view.alpha};
        if (view.has_inline_model) {
            if (!qa_scene_world_submit_model(frontend->scene_world, view.inline_model,
                &placement, world, actor.slot, color, frame, error)) return false;
            continue;
        }
        frontend_visual_owner *owner = NULL;
        for (unsigned part = 0; part < 4; ++part) {
            const char *path = view.models[part];
            if (!path || !*path) continue;
            if (!owner && !visual_owner(frontend, &view, &owner, error)) return false;
            if (owner->shader_movies && !frontend_material_movies_frame(owner->shader_movies, frame, error)) return false;
            frontend_model *model;
            if (!model_read(frontend, owner, path, view.model_resources[part], view.model_openings[part],
                view.family == QA_GAME_Q1 && view.has_player_colors, view.player_colors, &model, error)) return false;
            qa_scene_model_input input = {.view = world->view, .transform = placement,
                .previous_origin = view.body.origin, .color = color, .family = owner->family,
                .frame = view.frame >= 0 ? (uint32_t)view.frame : 0,
                .old_frame = view.old_frame >= 0 ? (uint32_t)view.old_frame : view.frame >= 0 ? (uint32_t)view.frame : 0,
                .skin = view.skin >= 0 ? (uint32_t)view.skin : 0, .flags = view.render_flags,
                .entity = actor.slot, .identity_light = world->identity_light, .seconds = world->seconds,
                .ambient = {1, 1, 1}, .fog = world->fog, .source_path = path,
                .video_frame = frontend_material_movies_frontend_resolve, .video_context = frontend};
            if (!qa_scene_world_sample_light_input(frontend->scene_world, world, view.body.origin,
                &input.ambient, &input.directed, &input.light_direction, error)) return false;
            if (!frontend_legacy_model_input(frontend, view.content, frontend->scene_world, world, &input, error)) return false;
            if (!qa_scene_model_submit(model->scene, &input, frame, error)) return false;
        }
    }
    return true;
}
