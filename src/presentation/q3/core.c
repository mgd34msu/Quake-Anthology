#include "internal.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_presentation_save.h"
#include "qa/scene_model_save.h"
#include "qa/scene_world_save.h"

bool qa_q3_presentation_idle(const qa_q3_presentation *p)
{
    return p && !p->busy && (!p->frame || !p->frame->source_pending) && qa_q3_assets_idle(p->options.assets);
}
static bool binding_observable(const qa_q3_presentation *p)
{
    const qa_q3_presentation_assets *a=p?p->options.assets:NULL;
    if (!p || p->busy || !a || !a->users || a->codec_busy || (a->busy && !a->capturing)) return false;
    if ((p->world && !qa_scene_world_observation_ready(p->world)) ||
        (a->world && !qa_scene_world_observation_ready(a->world))) return false;
    for (size_t i=0;i<a->model_count;++i) {
        const q3p_model *model=a->models[i];
        if (!model) continue;
        if (model->world && !qa_scene_world_observation_ready(model->world)) return false;
        for (unsigned j=0;j<3;++j)
            if (model->scene[j] && !qa_scene_model_observation_ready(model->scene[j])) return false;
    }
    return true;
}
bool qa_q3_presentation_binding_read(const qa_q3_presentation *p,qa_q3_presentation_binding *out,qa_error *error)
{
    if (!out || !binding_observable(p) || p->world!=p->options.assets->world ||
        p->geometry!=p->options.assets->geometry || (!p->world!=!p->geometry) ||
        (p->entity_text.size && !p->entity_text.data))
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 binding observation requires actual qualified idle owners");
    *out=(qa_q3_presentation_binding){p->options,p->frame,p->world,p->geometry,p->entity_text}; return true;
}
bool qa_q3_presentation_prepare_restored(qa_q3_presentation *p,qa_scene_frame *frame,
    qa_scene_world *world,qa_collision_geometry *geometry,qa_bytes entities,qa_error *error)
{
    qa_q3_presentation_assets *a=p?p->options.assets:NULL;
    if (!p || !a || !qa_q3_presentation_idle(p) || p->world || p->geometry || p->entity_text.data || p->entity_text.size ||
        p->world_loaded || p->material_view_valid || p->entity_count || p->entity_capacity ||
        p->polygon_count || p->polygon_capacity || p->vertex_count || p->vertex_capacity ||
        p->light_count || p->light_capacity || p->portal_capacity || p->movie_sources ||
        ((a->world || a->geometry) && (a->world!=world || a->geometry!=geometry)) ||
        a->name_count || a->name_capacity || a->model_count || a->model_capacity ||
        a->skin_count || a->skin_capacity || a->shader_count || a->shader_capacity || a->sound_count || a->sound_capacity ||
        (!world!=!geometry) || (entities.size && !entities.data) || (!world && (entities.data || entities.size)) ||
        (world && !qa_scene_world_observation_ready(world)))
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 restore binding requires genuine empty candidate owners");
    for (size_t i=0;i<16;++i) if (p->movies[i].kind!=Q3P_MOVIE_EMPTY)
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 restore binding retains an existing movie owner");
    p->frame=frame; p->world=world; p->geometry=geometry; p->entity_text=entities;
    a->world=world; a->geometry=geometry; return true;
}

bool q3p_fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message); return false;
}

bool q3p_reserve(void **data, size_t *capacity, size_t count, size_t width, qa_error *error)
{
    if (count <= *capacity) return true;
    if (count > SIZE_MAX / width) return q3p_fail(error, QA_ERROR_MEMORY, "Q3 presentation storage overflow");
    size_t next = *capacity ? *capacity : 16;
    while (next < count) {
        if (next > SIZE_MAX / width / 2) { next = count; break; }
        next *= 2;
    }
    void *grown = realloc(*data, next * width);
    if (!grown) return q3p_fail(error, QA_ERROR_MEMORY, "growing retained Q3 presentation storage");
    *data = grown; *capacity = next; return true;
}

bool q3p_begin(qa_q3_presentation *p, qa_error *error)
{
    if (!p || p->busy || !p->options.assets || p->options.assets->busy ||
        !q3p_assets_children_idle(p->options.assets))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 presentation or retained asset owner is absent or executing");
    ++p->busy; ++p->options.assets->busy; return true;
}

bool q3p_end(qa_q3_presentation *p, bool ok)
{
    --p->options.assets->busy; --p->busy; return ok;
}

bool qa_q3_presentation_round_ready(const qa_q3_presentation *p, const qa_scene_frame *frame,
    const qa_scene_world *world, const qa_collision_geometry *geometry, qa_error *error)
{
    if (!p || p->busy || !p->options.assets || p->options.assets->busy ||
        p->frame != frame || p->world != world || p->geometry != geometry ||
        p->options.assets->world != world || p->options.assets->geometry != geometry)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 round requires its idle retained scene and asset owners");
    return qa_q3_presentation_frontend_rebind_ready(p, frame, p->options.audio,
        p->options.owner, error);
}

bool qa_q3_presentation_frontend_rebind_ready(const qa_q3_presentation *p, const qa_scene_frame *current,
    qa_audio_engine *audio, uint64_t bus, qa_error *error)
{
    if (!p || p->busy || p->options.assets->busy || !q3p_assets_children_idle(p->options.assets) ||
        (p->frame && p->frame != current) ||
        (p->options.audio != NULL) != (audio != NULL))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 presentation exchange requires idle matching frame/audio owners");
    for (size_t i = 0; i < 16; ++i) {
        if (p->movies[i].kind != Q3P_MOVIE_LOCAL) continue;
        qa_cinematic *movie = p->movies[i].local;
        if (!qa_cinematic_frame_rebind_ready(movie, current, error) ||
            !qa_cinematic_audio_rebind_ready(movie, audio, bus, error)) return false;
    }
    return true;
}

void qa_q3_presentation_frontend_rebind(qa_q3_presentation *p, const qa_scene_frame *current,
    qa_scene_frame *destination, qa_audio_engine *audio, uint64_t bus)
{
    if (!p || p->busy || p->options.assets->busy || !q3p_assets_children_idle(p->options.assets)) return;
    if (p->frame) p->frame = destination;
    p->options.audio = audio;
    for (size_t i = 0; i < 16; ++i) {
        if (p->movies[i].kind != Q3P_MOVIE_LOCAL) continue;
        qa_cinematic_frame_rebind(p->movies[i].local, current, destination);
        qa_cinematic_audio_rebind(p->movies[i].local, audio, bus);
    }
}

bool qa_q3_presentation_create(const qa_q3_presentation_options *options,
                                qa_q3_presentation **out, qa_error *error)
{
    if (!options || !out || !options->assets || options->assets->busy ||
        !q3p_assets_children_idle(options->assets) || !options->clock.sample ||
        !options->owner || options->owner == QA_AUDIO_NO_OWNER || !options->viewport.width ||
        !options->viewport.height || options->seat == QA_AUDIO_WORLD ||
        !isfinite(options->near_clip) || !isfinite(options->far_clip) ||
        options->near_clip <= 0 || options->far_clip <= options->near_clip ||
        !isfinite(options->identity_light) || options->identity_light < 0 ||
        !isfinite(options->lod_scale) || !isfinite(options->lod_bias) ||
        !isfinite(options->rail_segment_length) || options->rail_segment_length <= 0 ||
        options->assets->users == UINT_MAX)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 presentation owner options");
    qa_q3_presentation *p = calloc(1, sizeof(*p));
    if (!p) return q3p_fail(error, QA_ERROR_MEMORY, "allocating Q3 presentation owner");
    p->options = *options; p->color = (qa_scene_vec4){1, 1, 1, 1};
    ++options->assets->users;
    if (!qa_common_cursor_init(&p->cursor, (qa_bytes){0}, QA_COMMON_TERMINATED, error)) {
        --options->assets->users; free(p); return false;
    }
    *out = p; return true;
}

bool qa_q3_presentation_destroy(qa_q3_presentation *p, qa_error *error)
{
    if (!p) return true;
    if (p->frame && p->frame->source_pending)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 presentation retains unfinished Source draw work");
    if (!q3p_begin(p, error)) return false;
    bool ok = true;
    for (uint32_t i = 0; i < 16; ++i) {
        qa_error local = {0};
        if (!q3p_movie_close(p, i, QA_CINEMATIC_STOPPED, &local) && ok) {
            ok = false; if (error) *error = local;
        }
    }
    if (p->options.audio) {
        qa_error local = {0};
        if (!qa_audio_engine_stop_owner(p->options.audio, p->options.owner, p->options.seat, &local) && ok) {
            ok = false; if (error) *error = local;
        }
    }
    /* A failed retirement keeps the actual seat owner for the caller's retry.
     * Closed movie slots already describe the completed portion of teardown. */
    if (!ok) return q3p_end(p,false);
    while (p->movie_sources) {
        q3p_movie_source *source = p->movie_sources;
        p->movie_sources = source->next;
        qa_cinematic_asset_release(source->asset);
        free(source->path); free(source);
    }
    q3p_end(p, true);
    qa_q3_presentation_assets_destroy(p->options.assets);
    free(p->entities); free(p->polygons); free(p->vertices); free(p->lights); free(p->portals); free(p);
    return ok;
}

qa_q3_presentation_assets *qa_q3_presentation_resources(qa_q3_presentation *p)
{
    return p ? p->options.assets : NULL;
}

bool qa_q3_presentation_frame(qa_q3_presentation *p, qa_scene_frame *frame, qa_scene_rect viewport,
                               qa_error *error)
{
    if (!p || p->busy || p->options.assets->busy || !q3p_assets_children_idle(p->options.assets) ||
        !frame || !viewport.width || !viewport.height)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 presentation frame");
    p->frame = frame; p->options.viewport = viewport; return true;
}

bool qa_q3_presentation_world(qa_q3_presentation *p, qa_scene_world *world,
                               qa_collision_geometry *geometry, qa_bytes entities, qa_error *error)
{
    if (!p || p->busy || p->options.assets->busy || !q3p_assets_children_idle(p->options.assets) ||
        (!world != !geometry) || (entities.size && !entities.data))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 presentation world");
    qa_q3_presentation_assets *assets = p->options.assets;
    if (p->world == world && p->geometry == geometry &&
        p->entity_text.data == entities.data && p->entity_text.size == entities.size) return true;
    if (assets->world && assets->world != world)
        for (size_t i = 0; i < assets->model_count; ++i)
            if (assets->models[i] && assets->models[i]->world && !assets->models[i]->owns_world)
                return q3p_fail(error, QA_ERROR_ARGUMENT, "registered Q3 inline models retain their map owner");
    p->world = world; p->geometry = geometry; p->entity_text = entities; p->world_loaded = false;
    assets->world = world; assets->geometry = geometry;
    return true;
}

bool qa_q3_presentation_retire_world(qa_q3_presentation *p, qa_error *error)
{
    if (!p || p->busy || p->options.assets->busy || !q3p_assets_children_idle(p->options.assets))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 map presentation is executing");
    if (!qa_common_cursor_init(&p->cursor, (qa_bytes){0}, QA_COMMON_TERMINATED, error)) return false;
    qa_q3_presentation_assets *assets = p->options.assets;
    for (size_t i = 0; i < assets->model_count; ++i) {
        q3p_model *model = assets->models[i];
        if (!model || !model->world || model->owns_world) continue;
        q3p_model_free(model); assets->models[i] = NULL;
    }
    for (size_t i = 0; i < assets->name_capacity; ++i) {
        q3p_name **link = &assets->names[i];
        while (*link) {
            q3p_name *entry = *link;
            bool retired = entry->kind == Q3P_MODEL &&
                (entry->name[0] == '*' || (entry->handle > 0 &&
                (size_t)entry->handle <= assets->model_count && !assets->models[entry->handle - 1]));
            if (retired) { *link = entry->next; free(entry); --assets->name_count; }
            else link = &entry->next;
        }
    }
    assets->world = NULL; assets->geometry = NULL;
    p->world = NULL; p->geometry = NULL; p->entity_text = (qa_bytes){0};
    p->entity_count = p->polygon_count = p->vertex_count = p->light_count = 0;
    p->world_loaded = p->material_view_valid = false;
    return true;
}

bool qa_q3_presentation_load_world(qa_q3_presentation *p, const char *path, qa_error *error)
{
    if (!p || p->busy || p->options.assets->busy || !q3p_assets_children_idle(p->options.assets) ||
        !path || !p->world || !p->geometry)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 renderer has no selected map");
    if (!qa_common_cursor_init(&p->cursor, p->entity_text, QA_COMMON_TERMINATED, error)) return false;
    p->world_loaded = true; return true;
}

bool qa_q3_presentation_entity_token(qa_q3_presentation *p, const char **token, bool *found, qa_error *error)
{
    if (!p || p->busy || p->options.assets->busy || !q3p_assets_children_idle(p->options.assets) || !token || !found)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 renderer entity token output");
    if (!qa_common_parse(&p->parser, &p->cursor, true, error)) return false;
    *token = p->parser.token; *found = !p->cursor.ended && p->parser.token_length;
    if (!*found) return qa_common_cursor_init(&p->cursor, p->entity_text, QA_COMMON_TERMINATED, error);
    return true;
}

bool qa_q3_presentation_in_pvs(qa_q3_presentation *p, qa_vec3 first, qa_vec3 second,
                                bool *visible, qa_error *error)
{
    if (!p || !p->world_loaded || !p->geometry || !visible)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "R_PointInLeaf: bad model");
    qa_collision_leaf a, b;
    return qa_collision_point_leaf(p->geometry, first, &a, error) &&
        qa_collision_point_leaf(p->geometry, second, &b, error) &&
        qa_collision_cluster_visible(p->geometry, (int32_t)a.cluster, (int32_t)b.cluster, false, visible, error);
}
