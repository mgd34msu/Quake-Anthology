#include "remote_q1_private.h"
#include "internal.h"
#include "legacy_render_policy.h"
#include "remote_q1_effects.h"
#include "remote_q1_skins.h"
#include "view_settings.h"
#include "qa/material.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool frontend_remote_q1_initial_clear(qa_frontend *frontend, uint32_t seat,
    bool *active, bool *clear, qa_error *error)
{
    if (!frontend || !active || !clear || seat >= frontend->options.seats) return false;
    *active = false; *clear = true;
    frontend_remote_q1 *selected = NULL;
    for (frontend_remote_q1 *row = frontend->remote_q1; row; row = row->next) {
        if (row->retired || row->options.domain.physical_seat != seat) continue;
        if (selected) return remote_q1_fail(error, QA_ERROR_ARGUMENT, "Legacy clear has multiple actual Q1 receivers");
        selected = row;
    }
    if (!selected || !selected->bound || !selected->world) return true;
    frontend_remote_q1_player_view player; bool present;
    if (!frontend_remote_q1_player_read(selected, &player, &present, error)) return false;
    if (!present) return true;
    const qa_product *product = qa_catalog_product(selected->content.catalog, selected->content.product);
    frontend_legacy_render_policy policy;
    if (!frontend_legacy_render_policy_read_registry(selected->options.domain.cvars, product, &policy, error) ||
        !remote_q1_live(selected, error)) return false;
    *active = true; *clear = policy.lighting.clear;
    return true;
}

bool remote_q1_model_lighting(frontend_remote_q1 *row,
    const qa_scene_world_input *world, qa_scene_model_input *input, qa_error *error)
{
    qa_vec3 ambient, directed, direction;
    if (!qa_scene_world_sample_light_input(row->world, world, input->previous_origin,
        &ambient, &directed, &direction, error)) return false;
    ambient = qa_vec_add(ambient, directed);
    float dynamic = 0;
    for (size_t i = 0; i < world->light_count; ++i) {
        float added = world->lights[i].radius - qa_vec_length(qa_vec_sub(input->previous_origin, world->lights[i].origin));
        if (added > 0) dynamic += added;
    }
    float values[] = {ambient.x, ambient.y, ambient.z};
    for (unsigned i = 0; i < 3; ++i) {
        float base = values[i] * 255;
        if (input->view_model) base = fmaxf(base, 24);
        float total = base + dynamic, clamped = fminf(total, 128);
        float shade = fminf(total, 192 - clamped);
        if ((input->player || !strcmp(input->source_path, "progs/player.mdl")) && clamped < 8) shade = 8;
        if (!strcmp(input->source_path, "progs/flame.mdl") || !strcmp(input->source_path, "progs/flame2.mdl")) shade = 256;
        values[i] = shade / 200 * 2;
    }
    input->alias_lighting = QA_ALIAS_PREPARED_LIGHT;
    input->alias_light = qa_v3(values[0], values[1], values[2]);
    return true;
}
static bool model_submit(frontend_remote_q1 *row, const frontend_remote_q1_entity_view *entity,
    const qa_scene_view *view, const qa_scene_world_input *world, qa_error *error)
{
    if (!entity->entity.model || !entity->visible) return true;
    qa_model_transform transform; qa_model_transform_identity(&transform);
    qa_vec3 origin = qa_v3(entity->entity.origin[0], entity->entity.origin[1], entity->entity.origin[2]);
    qa_vec3 angles = qa_v3(entity->entity.angles[0], entity->entity.angles[1], entity->entity.angles[2]), axes[3];
    frontend_camera_axes(angles, axes);
    transform.origin[0] = origin.x; transform.origin[1] = origin.y; transform.origin[2] = origin.z;
    for (unsigned i = 0; i < 3; ++i) {
        transform.axes[i][0] = axes[i].x; transform.axes[i][1] = axes[i].y; transform.axes[i][2] = axes[i].z;
        transform.scale[i] = entity->scale;
    }
    qa_scene_vec4 color = {1, 1, 1, entity->alpha};
    if (entity->model[0] == '*') {
        char *end; unsigned long number = strtoul(entity->model + 1, &end, 10);
        if (end == entity->model + 1 || *end || number > UINT32_MAX)
            return remote_q1_fail(error, QA_ERROR_FORMAT, "Q1 inline model has no genuine received model index");
        return qa_scene_world_submit_model(row->world, (uint32_t)number, &transform, world,
            entity->entity.number, color, &row->frontend->frame, error);
    }
    remote_q1_model *model = NULL;
    if (!remote_q1_model_read(row, entity, &model, error)) return false;
    if (model->world) return qa_scene_world_submit_model(model->world, 0, &transform, world,
        entity->entity.number, color, &row->frontend->frame, error);
    qa_scene_model_input input = {.view = *view, .transform = transform, .previous_origin = origin,
        .color = color, .family = QA_SCENE_Q1, .frame = entity->entity.frame, .old_frame = entity->entity.frame,
        .skin = entity->entity.skin, .entity = entity->entity.number, .seconds = world->seconds,
        .view_model = entity->view_weapon, .player = entity->has_colors,
        .material_library = row->materials, .source_path = model->path, .identity_light = 1};
    qa_scene_model_indexed_skin indexed = {0};
    if (row->skins && !strcmp(model->path, "progs/player.mdl") &&
        entity->entity.number >= 1 && entity->entity.number <= 32) {
        const qa_actor_record *actor = qa_actors_get(row->options.domain.actors, entity->actor);
        if (!actor || actor->owner != row->options.domain.actor_owner || !actor->has_source ||
            actor->source_slot != entity->entity.number)
            return remote_q1_fail(error, QA_ERROR_ARGUMENT, "QW skin lost its actual received player actor");
        frontend_remote_q1_skin skin; bool present;
        if (!frontend_remote_q1_skins_at(row->skins, actor->source_slot - 1, &skin, &present, error)) return false;
        if (present) {
            indexed = (qa_scene_model_indexed_skin){.name = skin.name, .width = skin.width,
                .height = skin.height, .indices = skin.indices};
            input.indexed_skin = &indexed;
        }
    }
    const qa_product *product=qa_catalog_product(row->content.catalog,row->content.product);
    return frontend_legacy_model_input_product(row->frontend,product,row->world,world,&input,error) &&
        remote_q1_model_lighting(row, world, &input, error) &&
        qa_scene_model_submit(model->scene, &input, &row->frontend->frame, error);
}
static bool sky_environment(frontend_remote_q1 *row,qa_scene_q1_sky_environment *out,qa_error *error)
{
    const qa_cvars *registry=qa_application_cvars(row->options.domain.application);
    const qa_cvar_view *fast=qa_cvars_find(registry,"r_fastsky"),*quality=qa_cvars_find(registry,"r_sky_quality"),
        *alpha=qa_cvars_find(registry,"r_skyalpha"),*fog=qa_cvars_find(registry,"r_skyfog"),
        *far_clip=qa_cvars_find(registry,"gl_farclip");
    if(!fast || !quality || !alpha || !fog || !far_clip || !isfinite(fast->number) ||
        !isfinite(quality->number) || !isfinite(alpha->number) || !isfinite(fog->number) ||
        !isfinite(far_clip->number) || far_clip->number<=4)
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Remote Q1 sky lost its actual canonical render controls");
    *out=(qa_scene_q1_sky_environment){.boxed=row->sky_found!=0,.fast=fast->number!=0,
        .quality=fmaxf(1,truncf(quality->number)),.alpha=fminf(1,fmaxf(0,alpha->number)),
        .fog=fog->number,.far_clip=far_clip->number};
    if(out->boxed) memcpy(out->images,row->sky_images,sizeof(out->images));
    return true;
}
typedef struct remote_scene {
    frontend_remote_q1 *row;
    frontend_remote_q1_entity_view *entities;
    size_t count;
    uint64_t revision;
    qa_vec3 viewer_origin;
} remote_scene;
static bool scene_current(void *context)
{
    const remote_scene *scene=context;
    return scene->row->busy==1 && scene->row->revision==scene->revision &&
        remote_q1_mutable(scene->row) && remote_q1_live(scene->row,NULL);
}
static bool scene_view_blend(void *context,const qa_scene_world_input *world,qa_scene_vec4 *out,qa_error *error)
{
    remote_scene *scene=context;
    return scene_current(scene) && remote_q1_camera_contents_blend(scene->row,&world->view,out,error) && scene_current(scene);
}
static bool scene_visuals(void *context,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *error)
{
    remote_scene *scene=context;
    frontend_remote_q1 *row=scene->row;
    if(frame!=&row->frontend->frame || !scene_current(scene)) return false;
    bool mirror=qa_scene_world_q1_mirror_scope(row->world,world,frame);
    for(size_t i=0;i<scene->count;++i) {
        frontend_remote_q1_entity_view entity=scene->entities[i];
        if(mirror) {
            if(entity.view_weapon) continue;
            if(entity.entity.number==row->view_entity) entity.visible=true;
        }
        if(!model_submit(row,&entity,&world->view,world,error) || !scene_current(scene)) return false;
    }
    return remote_q1_effects_models(row,&world->view,world,scene->viewer_origin,error);
}
static bool scene_particles(void *context,const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *error)
{
    remote_scene *scene=context;
    return frame==&scene->row->frontend->frame && scene_current(scene) &&
        remote_q1_effects_draw(scene->row,&world->view,world,error);
}
static bool scene_blend(void *context,const qa_scene_world_input *world,qa_scene_vec4 blend,qa_error *error)
{
    remote_scene *scene=context;
    return scene_current(scene) && remote_q1_effects_blend(scene->row,&world->view,world->seconds,blend,error);
}
static bool scene_policy(void *context, const qa_product *product,
    frontend_legacy_render_policy *out, qa_error *error)
{
    remote_scene *scene = context;
    frontend_remote_q1 *row = scene->row;
    if (!scene_current(scene) || product != qa_catalog_product(row->content.catalog, row->content.product) ||
        !frontend_legacy_render_policy_read_registry(row->options.domain.cvars, product, out, error)) return false;
    if (row->max_clients > 1) out->lighting.fullbright = false;
    return scene_current(scene);
}
bool frontend_remote_q1_draw(frontend_remote_q1 *row, const qa_scene_view *view,
    qa_audio_listener *listener, bool *rendered, qa_error *error)
{
    if (!remote_q1_mutable(row) || !view || !listener || !rendered || row->busy || !remote_q1_live(row, error) ||
        view->seat != row->options.domain.physical_seat || !view->viewport.width || !view->viewport.height) return false;
    *rendered = true;
    frontend_remote_q1_player_view player; bool present;
    if (!frontend_remote_q1_player_read(row, &player, &present, error)) return false;
    *listener = (qa_audio_listener){.seat = view->seat, .actor = QA_AUDIO_NO_ACTOR};
    if (!present || !row->world) return true;
    uint64_t actor = frontend_audio_actor(row->frontend, player.actor, error);
    if (actor == QA_AUDIO_NO_ACTOR) return false;
    *listener = (qa_audio_listener){.seat = view->seat, .actor = actor, .origin = view->origin, .gain = 1};
    for (unsigned i = 0; i < 3; ++i) listener->axis[i] = view->axis[i];
    double seconds = row->previous_seconds + (row->seconds - row->previous_seconds) * row->fraction;
    float styles[256];
    for (unsigned i = 0; i < 256; ++i) {
        const char *pattern = frontend_remote_q1_light_style(row, i); size_t count = strlen(pattern);
        double ordinal = fmod(floor(seconds * 10), (double)(count ? count : 1));
        if (ordinal < 0) ordinal += (double)count;
        styles[i] = count ? (float)((unsigned char)pattern[(size_t)ordinal] - 97) * 22 : 256;
    }
    qa_scene_world_input world = {.view = *view, .seconds = seconds,
        .q1_styles = styles, .style_count = 256, .identity_light = 1, .curve_error = 4,
        .override_sky = row->sky_found != 0, .sky_axis = {0, 0, 1}};
    for (unsigned i = 0; i < 6; ++i) world.sky_images[i] = row->sky_images[i];
    qa_scene_q1_sky_environment sky;
    if(!sky_environment(row,&sky,error)) return false;
    world.q1_sky_environment=&sky;
    frontend_q1_view_settings settings;
    if(!frontend_view_settings_q1_sample(row->frontend->view_settings,
        qa_q1_is_qw(row->options.domain.protocol)?QA_CONSOLE_QW:QA_CONSOLE_Q1,&settings,error)) return false;
    remote_scene scene={.row=row,.count=frontend_remote_q1_entity_count(row),.revision=row->revision,
        .viewer_origin=player.origin};
    if(scene.count>SIZE_MAX/sizeof(*scene.entities)) return false;
    scene.entities=scene.count?malloc(scene.count*sizeof(*scene.entities)):NULL;
    if(scene.count && !scene.entities) return remote_q1_fail(error,QA_ERROR_MEMORY,"Retaining actual remote Q1 scene rows");
    bool ok=true;
    for(size_t i=0;ok && i<scene.count;++i) {
        frontend_remote_q1_entity_view *entity=scene.entities+i;
        ok=frontend_remote_q1_entity_at(row,i,entity,error) && row->revision==scene.revision;
        if(!ok) break;
        if(entity->view_weapon) {
            if(settings.chase) entity->visible=false;
            float height=settings.size==110?1:settings.size==100?2:settings.size==90?1:settings.size==80?.5f:0;
            entity->entity.origin[2]+=height;
        } else if(settings.chase && entity->entity.number==row->view_entity) entity->visible=true;
    }
    const qa_product *product=qa_catalog_product(row->content.catalog,row->content.product);
    frontend_legacy_scene_services services={.context=&scene,.current=scene_current,
        .view_blend=scene_view_blend,.visuals=scene_visuals,.particles=scene_particles,.blend=scene_blend,.policy=scene_policy};
    if(ok) {
        ++row->busy;
        ok=remote_q1_effects_scene(row,seconds,&world.lights,&world.light_count,error) &&
            frontend_legacy_scene_submit_product(row->frontend,row->world,product,&world,
                &row->frontend->frame,&services,error);
        --row->busy;
    }
    free(scene.entities);
    return ok && remote_q1_live(row, error);
}
