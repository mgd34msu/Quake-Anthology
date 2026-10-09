#include "q1_sky_private.h"
#include "qa/scene_resource_save.h"
#include "qa/text.h"
#include "resource_bindings.h"
#include "qc_rerelease_events.h"
#include <math.h>
#include <stdio.h>

static const char *const suffixes[6] = {"rt", "lf", "bk", "ft", "up", "dn"};
void frontend_q1_sky_controls_bind(const qa_cvars *registry, frontend_q1_sky_controls *out)
{
    *out = (frontend_q1_sky_controls){
        .fast = qa_cvars_resolve(registry, "r_fastsky"),
        .quality = qa_cvars_resolve(registry, "r_sky_quality"),
        .alpha = qa_cvars_resolve(registry, "r_skyalpha"),
        .fog = qa_cvars_resolve(registry, "r_skyfog"),
        .far_clip = qa_cvars_resolve(registry, "gl_farclip")};
}
bool frontend_q1_sky_controls_read(const qa_cvars *registry, const frontend_q1_sky_controls *controls,
    qa_scene_q1_sky_environment *out, uint64_t *fog_modification)
{
    const qa_cvar_view *fast = qa_cvars_read(registry, controls->fast),
        *quality = qa_cvars_read(registry, controls->quality),
        *alpha = qa_cvars_read(registry, controls->alpha),
        *fog = qa_cvars_read(registry, controls->fog),
        *far_clip = qa_cvars_read(registry, controls->far_clip);
    if (!fast || !quality || !alpha || !fog || !far_clip || !isfinite(fast->number) || !isfinite(quality->number) ||
        !isfinite(alpha->number) || !isfinite(fog->number) || !isfinite(far_clip->number)) return false;
    *out = (qa_scene_q1_sky_environment){.fast = fast->number != 0, .quality = fmaxf(1, truncf(quality->number)),
        .alpha = fminf(1, fmaxf(0, alpha->number)), .fog = fog->number, .far_clip = far_clip->number};
    if (fog_modification) *fog_modification = fog->modification_count;
    return true;
}
bool frontend_q1_sky_current(const frontend_q1_sky *owner)
{
    if (!owner || !owner->frontend || owner->frontend->application != owner->application) return false;
    if (!owner->map) return !owner->frontend->map_resource && !owner->map_bank && !owner->map_revision && !owner->baseline;
    return owner->frontend->map_resource == owner->map && owner->frontend->images == owner->map_bank &&
        owner->frontend->map_revision == owner->map_revision;
}
bool frontend_q1_sky_idle(const frontend_q1_sky *owner)
{ return owner && !owner->busy && !owner->pending; }
bool frontend_q1_sky_selection_current(const frontend_q1_sky *owner, const frontend_q1_sky_selection *selection)
{
    if (!selection || !selection->bank || !selection->name) return false;
    if (!selection->provider) return selection->bank == owner->map_bank;
    qa_scene_resources *images = NULL; qa_vfs *files = NULL;
    return frontend_event_q1_images_read(owner->frontend, selection->provider, &images, &files, NULL) &&
        images == selection->bank && files == qa_scene_resources_files(images);
}
void frontend_q1_sky_selection_free(frontend_q1_sky_selection *selection)
{
    if (!selection) return;
    for (unsigned i = 0; i < 6; ++i) qa_scene_image_release(selection->images[i]);
    free(selection->name); free(selection);
}
static void clear(frontend_q1_sky *owner)
{
    frontend_q1_sky_selection_free(owner->baseline); owner->baseline = NULL;
    while (owner->retained) {
        frontend_q1_sky_selection *row = owner->retained;
        owner->retained = row->next; frontend_q1_sky_selection_free(row);
    }
    qa_resource_release(owner->map); owner->map = NULL; owner->map_bank = NULL;
    owner->map_revision = 0; owner->q1_map = false; owner->map_fog = false;
}
static void prune(frontend_q1_sky *owner)
{
    frontend_q1_sky_selection **link = &owner->retained;
    const qa_actor_registry *actors = qa_world_actors(qa_application_world(owner->application));
    while (*link) {
        frontend_q1_sky_selection *row = *link;
        if ((row->provider && !qa_application_provider_instance(owner->application, row->provider)) ||
            (row->recipient.registry && !qa_actors_get(actors, row->recipient))) {
            *link = row->next; frontend_q1_sky_selection_free(row);
        } else link = &row->next;
    }
}
static frontend_q1_sky_selection *selection_create(const char *name, qa_scene_resources *bank, qa_error *error)
{
    if (!name || !bank) { frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky requires an actual name and image owner"); return NULL; }
    size_t size = strlen(name);
    if (size == SIZE_MAX) { frontend_fail(error, QA_ERROR_MEMORY, "Q1 sky name exceeds storage"); return NULL; }
    frontend_q1_sky_selection *row = calloc(1, sizeof(*row));
    if (row) row->name = malloc(size + 1);
    if (!row || !row->name) {
        frontend_q1_sky_selection_free(row);
        frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Q1 sky selection"); return NULL;
    }
    memcpy(row->name, name, size + 1); row->bank = bank; return row;
}
bool frontend_q1_sky_selection_load(frontend_q1_sky_selection *row, qa_scene_resources *bank, qa_error *error)
{
    if (!row || !row->name || !bank) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky image admission lost its actual owner");
    size_t length = strlen(row->name);
    if (!length) return true;
    if (length > SIZE_MAX - 16) return frontend_fail(error, QA_ERROR_MEMORY, "Q1 sky path exceeds storage");
    char *path = malloc(length + 16);
    if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Q1 sky face request");
    qa_scene_image_options options = {.family = QA_SCENE_Q1, .wrap = QA_SCENE_CLAMP,
        .filter = QA_SCENE_LINEAR, .usage = QA_IMAGE_USAGE_SKY, .transparent_index = -1};
    bool ok = true;
    for (unsigned i = 0; ok && i < 6; ++i) {
        qa_scene_image *image = NULL; qa_error observed = {0};
        snprintf(path, length + 16, "gfx/env/%s%s.tga", row->name, suffixes[i]);
        bool loaded = qa_scene_image_load_exact(bank, path, &options, &image, &observed);
        if (!loaded && (observed.code == QA_ERROR_NOT_FOUND || observed.code == QA_ERROR_FORMAT)) {
            observed = (qa_error){0};
            snprintf(path, length + 16, "gfx/env/%s%s.png", row->name, suffixes[i]);
            loaded = qa_scene_image_load_exact(bank, path, &options, &image, &observed);
        }
        if (loaded) { row->images[i] = image; row->found |= (uint8_t)(1u << i); }
        else if (observed.code == QA_ERROR_NOT_FOUND || observed.code == QA_ERROR_FORMAT) {
            row->images[i] = qa_scene_missing(bank); qa_scene_image_retain(row->images[i]);
        } else { if (error) *error = observed; ok = false; }
    }
    free(path);
    if (ok && !row->found) for (unsigned i = 0; i < 6; ++i) {
        qa_scene_image_release(row->images[i]); row->images[i] = NULL;
    }
    return ok;
}
bool frontend_q1_sky_create(qa_frontend *frontend, frontend_q1_sky **out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->options.dedicated || !out || *out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky requires its actual presentation frontend");
    frontend_q1_sky *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Creating Q1 sky presentation owner");
    owner->frontend = frontend; owner->application = frontend->application; owner->next_sequence = 1;
    const qa_cvars *registry = qa_application_cvars(owner->application);
    frontend_q1_sky_controls_bind(registry, &owner->controls);
    const qa_cvar_view *fog = qa_cvars_read(registry, owner->controls.fog);
    if (!fog || !isfinite(fog->number)) { free(owner); return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky lost its actual canonical fog control"); }
    owner->fog = fog->number; owner->fog_modification = fog->modification_count;
    *out = owner; return true;
}
bool frontend_q1_sky_destroy(frontend_q1_sky **out, qa_error *error)
{
    if (!out || !*out) return true;
    if (!frontend_q1_sky_idle(*out)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky retirement retains its actual prepared owner");
    clear(*out); free(*out); *out = NULL; return true;
}
bool frontend_q1_sky_map(frontend_q1_sky *owner, qa_error *error)
{
    if (!frontend_q1_sky_idle(owner) || owner->frontend->capture || owner->frontend->resource_inventory ||
        owner->frontend->source_restoring || owner->frontend->application != owner->application)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky map admission overlaps a held owner");
    qa_frontend *frontend = owner->frontend;
    prune(owner);
    if (owner->map == frontend->map_resource && owner->map_bank == frontend->images &&
        owner->map_revision == frontend->map_revision) return true;
    if (!frontend->map_resource) { clear(owner); return true; }
    if (!frontend->images || !frontend->map_revision)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky map lacks its actual global image owner");
    qa_bsp_view bsp; qa_entities entities = {0};
    if (!qa_bsp_open(qa_resource_bytes(frontend->map_resource), &bsp, error)) return false;
    const qa_cvar_view *control = qa_cvars_read(qa_application_cvars(owner->application), owner->controls.fog);
    if (!control || !isfinite(control->number)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 map sky lost its actual fog setting");
    frontend_q1_sky_selection *baseline = selection_create("", frontend->images, error);
    if (!baseline) return false;
    float fog = control->number; bool map_fog = false;
    bool ok = bsp.family != QA_BSP_Q1 || qa_entities_parse(bsp.lumps[QA_BSP_ENTITIES].bytes, QA_ENTITY_Q1, &entities, error);
    owner->busy = true;
    if (ok && entities.count) for (size_t i = 0; ok && i < entities.records[0].property_count; ++i) {
        const qa_entity_property *property = entities.properties + entities.records[0].first_property + i;
        qa_bytes key = property->key;
        if (key.size && key.data[0] == '_') ++key.data, --key.size;
        while (key.size && key.data[key.size - 1] == ' ') --key.size;
        bool sky = (key.size == 3 && !memcmp(key.data, "sky", 3)) ||
            (key.size == 7 && !memcmp(key.data, "skyname", 7)) || (key.size == 5 && !memcmp(key.data, "qlsky", 5));
        if (sky) {
            size_t length = property->value.size;
            char *name = length < SIZE_MAX ? malloc(length + 1) : NULL;
            if (!name) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored worldspawn sky"); break; }
            memcpy(name, property->value.data, length); name[length] = 0;
            frontend_q1_sky_selection *next = selection_create(name, frontend->images, error); free(name);
            ok = next && frontend_q1_sky_selection_load(next, frontend->images, error);
            if (ok) { frontend_q1_sky_selection_free(baseline); baseline = next; }
            else frontend_q1_sky_selection_free(next);
        } else if (key.size == 6 && !memcmp(key.data, "skyfog", 6)) {
            size_t length = property->value.size;
            char *value = length < SIZE_MAX ? malloc(length + 1) : NULL;
            if (!value) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored sky fog text"); break; }
            memcpy(value, property->value.data, length); value[length] = 0;
            double parsed = 0;
            ok = qa_parse_atof(value, &parsed, error);
            if (ok) { fog = (float)parsed; ok = isfinite(fog); }
            free(value); if (ok) map_fog = true;
        }
    }
    owner->busy = false; qa_entities_free(&entities);
    if (!ok) {
        frontend_q1_sky_selection_free(baseline);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid authored Q1 sky fog");
        return false;
    }
    frontend_q1_sky_selection **link = &owner->retained;
    while (*link) {
        if (!(*link)->provider || (*link)->map_revision != frontend->map_revision) {
            frontend_q1_sky_selection *row = *link; *link = row->next; frontend_q1_sky_selection_free(row);
        } else link = &(*link)->next;
    }
    frontend_q1_sky_selection_free(owner->baseline); qa_resource_release(owner->map);
    owner->map = frontend->map_resource; qa_resource_retain(owner->map);
    owner->map_revision = frontend->map_revision; owner->map_bank = frontend->images;
    owner->q1_map = bsp.family == QA_BSP_Q1; owner->baseline = baseline;
    owner->fog = fog; owner->map_fog = map_fog; owner->fog_modification = control->modification_count;
    return true;
}
static bool receive(frontend_q1_sky *owner, qa_actor_owner provider, qa_actor_id recipient,
    const char *name, qa_scene_resources *bank, qa_error *error)
{
    if (!owner->next_sequence) return frontend_fail(error, QA_ERROR_MEMORY, "Q1 sky event sequence is exhausted");
    frontend_q1_sky_selection *row = selection_create(name, bank, error);
    if (!row) return false;
    row->provider = provider; row->recipient = recipient; row->sequence = owner->next_sequence;
    qa_application_map_view map;
    if (!qa_application_map_read(owner->application, &map)) {
        frontend_q1_sky_selection_free(row);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky service has no actual map publication");
    }
    row->map_revision = map.revision;
    owner->busy = true; bool ok = frontend_q1_sky_selection_load(row, bank, error); owner->busy = false;
    if (!ok || !frontend_q1_sky_selection_current(owner, row)) {
        frontend_q1_sky_selection_free(row);
        return ok ? frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky source retired during image admission") : false;
    }
    frontend_q1_sky_selection **link = &owner->retained;
    while (*link) {
        if (qa_actor_id_equal((*link)->recipient, recipient)) {
            frontend_q1_sky_selection *old = *link; *link = old->next; frontend_q1_sky_selection_free(old); break;
        }
        link = &(*link)->next;
    }
    link = &owner->retained; while (*link) link = &(*link)->next;
    *link = row; ++owner->next_sequence; return true;
}
bool frontend_q1_sky_receive(frontend_q1_sky *owner, qa_actor_owner provider, qa_actor_id recipient,
    const char *name, qa_error *error)
{
    if (!frontend_q1_sky_idle(owner) || !frontend_q1_sky_current(owner) || !provider ||
        owner->frontend->capture || owner->frontend->resource_inventory || owner->frontend->source_restoring ||
        !qa_application_provider_instance(owner->application, provider))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky service lost its actual provider owner");
    if (recipient.registry && !qa_actors_get(qa_world_actors(qa_application_world(owner->application)), recipient))
        return true;
    qa_scene_resources *images = NULL; qa_audio_bank *sounds = NULL;
    if (!frontend_event_qc_resources(owner->frontend, provider, &images, &sounds, error)) return false;
    return receive(owner, provider, recipient, name, images, error);
}
bool frontend_q1_sky_command(frontend_q1_sky *owner, const char *name, qa_error *error)
{
    if (!frontend_q1_sky_idle(owner) || !frontend_q1_sky_current(owner) || !owner->map ||
        owner->frontend->capture || owner->frontend->resource_inventory || owner->frontend->source_restoring)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky command has no actual current renderer map");
    return receive(owner, 0, (qa_actor_id){0}, name, owner->map_bank, error);
}
const frontend_q1_sky_selection *frontend_q1_sky_selected(const frontend_q1_sky *owner, qa_actor_id actor)
{
    const frontend_q1_sky_selection *shared = owner->baseline, *private = NULL;
    for (const frontend_q1_sky_selection *row = owner->retained; row; row = row->next) {
        if (!row->recipient.registry) { shared = row; private = NULL; }
        else if (qa_actor_id_equal(row->recipient, actor)) private = row;
    }
    return private ? private : shared;
}
bool frontend_q1_sky_name(const frontend_q1_sky *owner, qa_actor_id actor, const char **out, qa_error *error)
{
    if (!frontend_q1_sky_idle(owner) || !frontend_q1_sky_current(owner) || !out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky name lost its actual installed renderer");
    const frontend_q1_sky_selection *row = frontend_q1_sky_selected(owner, actor);
    if (row && !frontend_q1_sky_selection_current(owner, row))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky name references a retired provider");
    *out = row && row->found ? row->name : ""; return true;
}
bool frontend_q1_sky_retire_provider(frontend_q1_sky *owner, qa_actor_owner provider, qa_error *error)
{
    if (!frontend_q1_sky_idle(owner) || !provider || owner->frontend->capture || owner->frontend->resource_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky source retirement retains an active owner");
    frontend_q1_sky_selection **link = &owner->retained;
    while (*link) {
        if ((*link)->provider == provider) {
            frontend_q1_sky_selection *row = *link; *link = row->next; frontend_q1_sky_selection_free(row);
        } else link = &(*link)->next;
    }
    return true;
}
bool frontend_q1_sky_retire_actor(frontend_q1_sky *owner, qa_actor_id actor, qa_error *error)
{
    if (!frontend_q1_sky_idle(owner) || !actor.registry || owner->frontend->capture || owner->frontend->resource_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky client retirement retains an active owner");
    frontend_q1_sky_selection **link = &owner->retained;
    while (*link) {
        if (qa_actor_id_equal((*link)->recipient, actor)) {
            frontend_q1_sky_selection *row = *link; *link = row->next; frontend_q1_sky_selection_free(row);
        } else link = &(*link)->next;
    }
    return true;
}
bool frontend_q1_sky_view_read(frontend_q1_sky *owner, qa_actor_id actor,
    frontend_q1_sky_view *out, qa_error *error)
{
    if (!frontend_q1_sky_idle(owner) || !frontend_q1_sky_current(owner) || !out ||
        owner->frontend->capture || owner->frontend->resource_inventory || owner->frontend->source_restoring)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky view lost its actual map owner");
    prune(owner);
    const qa_cvars *registry = qa_application_cvars(owner->application);
    qa_scene_q1_sky_environment controls; uint64_t fog_modification;
    if (!frontend_q1_sky_controls_read(registry, &owner->controls, &controls, &fog_modification) || controls.far_clip <= 0)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky view lost its canonical controls");
    if (owner->fog_modification != fog_modification) {
        owner->fog = controls.fog; owner->map_fog = false; owner->fog_modification = fog_modification;
    }
    const frontend_q1_sky_selection *row = frontend_q1_sky_selected(owner, actor);
    if (row && !frontend_q1_sky_selection_current(owner, row))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 sky view references a retired provider");
    *out = (frontend_q1_sky_view){.classic_q1 = owner->q1_map, .boxed = row && row->found,
        .fast = controls.fast, .quality = controls.quality,
        .alpha = controls.alpha, .fog = owner->fog, .far_clip = controls.far_clip};
    if (out->boxed) memcpy(out->images, row->images, sizeof(out->images));
    return true;
}
