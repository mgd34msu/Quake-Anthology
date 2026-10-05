#include "shared_resource_policy.h"
#include "qa/text.h"
#include "capture.h"
#include "visual_access.h"
#include "resource_bindings.h"
#include "material_movies_prepare.h"
#include "shared_settings.h"
#include "q3_render_policy.h"
#include "q3_color_policy.h"
#include "shared_render_controls.h"
#include "video_guests.h"
#include "q1_sky.h"
#include "remote_q1_sky_policy.h"
#include "remote_q2_policy.h"
#include "qa/scene_model_save.h"
#include "qa/scene_world_save.h"
#include "qa/material_library_save.h"
#include "qa/font_save.h"
#include "qa/q3_assets_save.h"
#include "qa/render_controls.h"
#include "qa/console_cvar_observer.h"
#include "qa/application_engine_shutdown.h"
#include "qa/source_save.h"
#include "qa/q3_cinematic_handles.h"

static bool policy_fail(qa_error *error, const char *text)
{ return frontend_fail(error, QA_ERROR_ARGUMENT, text); }
bool frontend_resource_policy_admission_edit(const qa_frontend *frontend, const qa_cvars_edit **out, qa_error *error)
{
    if (!frontend || !frontend->application || !out)
        return policy_fail(error, "Resource admission requires its actual frontend ENGINE parent");
    *out = NULL;
    const qa_launch_snapshot *candidate = qa_application_startup_candidate(frontend->application);
    frontend_shared_settings *shared = frontend_config_store_shared(frontend->config_store,
        frontend->application, candidate);
    if (!shared)
        return !frontend_config_store_shared_pending(frontend->config_store) ||
            policy_fail(error, "Resource admission cannot replace unavailable candidate values with published ENGINE values");
    *out = frontend_shared_values_prepared(frontend_shared_settings_values(shared));
    return (*out && qa_cvars_edit_registry(*out) == qa_application_cvars(frontend->application)) ||
        policy_fail(error, "Resource admission lost its actual candidate ENGINE edit");
}

static bool model_policy_rows(const qa_cvar_view *const rows[5], frontend_model_policy *out, qa_error *error)
{
    for (unsigned i = 0; i < 5; ++i)
        if (!rows[i]) return policy_fail(error, "Model policy lacks an actual shared ENGINE declaration");
    frontend_model_policy policy = {.q1_enhanced = rows[0]->number != 0,
        .q2_load = rows[1]->number != 0, .q2_use = rows[2]->number != 0,
        .q2_distance = rows[3]->number};
    const unsigned char *text = (const unsigned char *)rows[4]->value;
    size_t length = strlen(rows[4]->value);
    while (length && (*text == ' ' || (*text >= '\t' && *text <= '\r'))) {
        ++text; --length;
    }
    while (length && (text[length - 1] == ' ' ||
        (text[length - 1] >= '\t' && text[length - 1] <= '\r'))) --length;
    static const char source[] = "source";
    policy.source_distance = length == sizeof(source) - 1;
    for (size_t i = 0; policy.source_distance && i < length; ++i) {
        unsigned char c = text[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)source[i]) policy.source_distance = false;
    }
    if (!policy.source_distance) {
        if (!qa_parse_number((qa_bytes){text, length}, &policy.distance, error) || !isfinite(policy.distance))
            return policy_fail(error, "r_model_distance must be source or a finite number");
    }
    *out = policy; return true;
}

bool frontend_model_policy_edit_read(const qa_cvars_edit *edit, frontend_model_policy *out, qa_error *error)
{
    static const char *const names[] = {"r_enhancedmodels", "gl_md5_load", "gl_md5_use", "gl_md5_distance", "r_model_distance"};
    if (!edit || !out) return policy_fail(error, "Model policy requires the actual canonical scalar ticket");
    const qa_cvar_view *rows[5];
    for (unsigned i = 0; i < 5; ++i) rows[i] = qa_cvars_edit_find(edit, names[i]);
    return model_policy_rows(rows, out, error);
}

bool frontend_model_policy_read(const qa_frontend *frontend, frontend_model_policy *out, qa_error *error)
{
    static const char *const names[] = {"r_enhancedmodels", "gl_md5_load", "gl_md5_use", "gl_md5_distance", "r_model_distance"};
    if (!frontend || !frontend->application || !out)
        return policy_fail(error, "Model policy requires its actual frontend ENGINE registry");
    const qa_cvars_edit *edit = NULL;
    if (!frontend_resource_policy_admission_edit(frontend, &edit, error)) return false;
    if (edit) return frontend_model_policy_edit_read(edit, out, error);
    const qa_cvars *registry = qa_application_cvars(frontend->application);
    const qa_cvar_view *rows[5];
    for (unsigned i = 0; i < 5; ++i) rows[i] = qa_cvars_find(registry, names[i]);
    return model_policy_rows(rows, out, error);
}

bool frontend_model_policy_load(const frontend_model_policy *policy, qa_scene_family family, const qa_model *model)
{
    return policy && model && ((family == QA_SCENE_Q1 && model->format == QA_MODEL_MDL && policy->q1_enhanced) ||
        (family == QA_SCENE_Q2 && model->format == QA_MODEL_MD2 && policy->q2_load));
}

bool frontend_model_policy_select(const frontend_model_policy *policy, qa_scene_family family, const qa_model *model,
    double distance, bool shadow)
{
    if (!frontend_model_policy_load(policy, family, model) || (model->format == QA_MODEL_MD2 && !policy->q2_use)) return false;
    if (shadow) return true;
    double cutoff = frontend_model_policy_distance(policy, model);
    return cutoff <= 0 || !(distance > cutoff);
}
double frontend_model_policy_distance(const frontend_model_policy *policy, const qa_model *model)
{ return policy->source_distance ? model->format == QA_MODEL_MDL ? 0 : policy->q2_distance : policy->distance; }
static bool model_policy_sync(qa_frontend *f, const frontend_model_policy *policy, qa_error *error)
{
    frontend_resource_inventory *inventory = NULL;
    if (!frontend_resource_inventory_collect(f, f->application, NULL, &inventory, error)) return false;
    bool ok = true;
    for (size_t i = 0; ok; ++i) {
        const qa_scene_model *model = frontend_resource_inventory_model_at(inventory, i);
        if (!model) break;
        const qa_model *source = qa_scene_model_source(model);
        if (!source || (source->format != QA_MODEL_MDL && source->format != QA_MODEL_MD2) ||
            qa_scene_model_replacement_description(model)) continue;
        const qa_scene_image_options *options = qa_scene_model_image_options(model);
        if (!((options->family == QA_SCENE_Q1 && source->format == QA_MODEL_MDL) ||
            (options->family == QA_SCENE_Q2 && source->format == QA_MODEL_MD2))) continue;
        bool configured, enabled; double distance; const qa_scene_model *selected;
        if (!qa_scene_model_replacement_policy_read(model, &configured, &enabled, &distance, &selected)) {
            ok = policy_fail(error, "Model use refresh lost its actual replacement root"); break;
        }
        if (!configured || !selected) continue;
        bool next_enabled = frontend_model_policy_select(policy, options->family, source, 0, true);
        double next_distance = frontend_model_policy_distance(policy, source);
        if (enabled != next_enabled || !(distance == next_distance || (isnan(distance) && isnan(next_distance))))
            ok = qa_scene_model_replacement_policy_update((qa_scene_model *)model, next_enabled, next_distance, error);
    }
    if (ok && !frontend_resource_inventory_current(inventory))
        ok = policy_fail(error, "Model use refresh changed its retained resource roster");
    qa_error release_error = {0};
    if (!frontend_resource_inventory_release(&inventory, &release_error)) {
        if (ok && error) *error = release_error;
        return false;
    }
    return ok;
}
static bool image_policy_rows(const qa_cvar_view *const rows[3], qa_scene_image_policy out[3], qa_error *error)
{
    for (unsigned i = 0; i < 3; ++i)
        if (!rows[i]) return policy_fail(error, "Image policy lacks an actual shared ENGINE declaration");
    double level = rows[0]->number;
    int32_t priority = level > 1 ? 2 : level >= 1 ? 1 : 0;
    qa_scene_image_policy policy;
    if (!qa_scene_image_policy_controls(priority, (uint32_t)rows[1]->integer, rows[2]->value, &policy, error)) return false;
    for (unsigned i = 0; i < 3; ++i) out[i] = policy;
    return true;
}
bool frontend_image_policy_edit_read(const qa_cvars_edit *edit, qa_scene_image_policy out[3], qa_error *error)
{
    if (!edit || !out) return policy_fail(error, "Image policy requires its actual canonical scalar ticket");
    const qa_cvar_view *rows[] = {qa_cvars_edit_find(edit, "r_override_textures"),
        qa_cvars_edit_find(edit, "r_texture_overrides"), qa_cvars_edit_find(edit, "r_texture_formats")};
    return image_policy_rows(rows, out, error);
}
bool frontend_image_policy_read(const qa_frontend *frontend, qa_scene_image_policy out[3], qa_error *error)
{
    if (!frontend || !frontend->application || !out) return policy_fail(error, "Image policy requires its actual ENGINE registry");
    const qa_cvars *registry = qa_application_cvars(frontend->application);
    const qa_cvar_view *rows[] = {qa_cvars_find(registry, "r_override_textures"),
        qa_cvars_find(registry, "r_texture_overrides"), qa_cvars_find(registry, "r_texture_formats")};
    return image_policy_rows(rows, out, error);
}
bool frontend_image_policy_initialize(qa_frontend *frontend, qa_scene_resources *images, qa_error *error)
{
    if (!frontend || !frontend->application || !images)
        return policy_fail(error, "Image admission requires its actual frontend and fresh resource bank");
    const qa_cvars_edit *edit = NULL;
    if (!frontend_resource_policy_admission_edit(frontend, &edit, error)) return false;
    qa_scene_image_policy policies[3];
    if (!(edit ? frontend_image_policy_edit_read(edit, policies, error) :
        frontend_image_policy_read(frontend, policies, error))) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (!qa_scene_resources_set_image_policy(images, (qa_scene_family)i, policies + i, error)) return false;
    return true;
}

typedef struct policy_material {
    qa_material_library *owner;
    qa_scene_material_image_policy *ticket;
    frontend_material_movies *movies;
    frontend_material_movies_policy *movie_ticket;
} policy_material;
typedef struct policy_font {
    qa_font_library *owner;
    qa_font_resource_policy *ticket;
} policy_font;
typedef struct policy_world {
    qa_scene_world *owner;
    qa_scene_world_image_policy *ticket;
    qa_scene_material_image_policy *materials;
} policy_world;
typedef struct policy_scalar {
    char *value;
    uint32_t number;
} policy_scalar;
static const char *const policy_names[] = {"r_override_textures", "r_texture_overrides", "r_texture_formats",
    "r_enhancedmodels", "gl_md5_load", "gl_md5_use", "gl_md5_distance", "r_model_distance",
    "r_detailtextures", "r_vertexLight", "r_uifullscreen", "r_ignoreFastPath",
    "r_allowExtensions", "r_ext_multitexture", "r_ext_texture_env_add"};
enum { BASE_POLICY_VALUES = 8 };
enum { POLICY_VALUES = sizeof(policy_names) / sizeof(policy_names[0]) };
struct frontend_live_resource_policy {
    qa_frontend *frontend;
    qa_application *application;
    qa_cvars *registry;
    frontend_shared_resource_policy *pending;
    policy_scalar applied[POLICY_VALUES];
    size_t applied_count;
    frontend_model_policy model_use;
    bool model_use_ready;
    bool busy;
};
static bool live_owner(qa_frontend *f, qa_error *error)
{
    if (f->live_resource_policy)
        return (f->live_resource_policy->frontend == f && f->live_resource_policy->application == f->application &&
            f->live_resource_policy->registry == qa_application_cvars(f->application)) ||
            policy_fail(error, "Live resource policy lost its actual ENGINE parent");
    struct frontend_live_resource_policy *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining live resource publication history");
    owner->frontend = f; owner->application = f->application; owner->registry = qa_application_cvars(f->application);
    f->live_resource_policy = owner; return true;
}
struct frontend_shared_resource_policy {
    qa_frontend *frontend;
    qa_application *application;
    qa_cvars *registry;
    struct frontend_live_resource_policy *recipe_owner;
    const qa_cvars_edit *edit;
    const qa_application_client_preparation *client;
    const frontend_video_guests *video;
    qa_display *display;
    qa_cpu_renderer *cpu;
    qa_gl_renderer *gl;
    frontend_q3_color *color;
    qa_q3_image_upload_options restart_upload;
    frontend_resource_inventory *inventory;
    qa_scene_resource_policy **banks;
    qa_material_order_image_policy **orders;
    policy_material *materials;
    policy_font *fonts;
    policy_world *worlds;
    frontend_visual_policy_binding *models;
    qa_q3_presentation_assets **assets;
    size_t bank_count, order_count, material_count, font_count, world_count, model_count, asset_count, assets_held;
    frontend_visual_policy *visuals;
    frontend_native_q2_image_policy *q2;
    frontend_event_image_policy *events;
    frontend_remote_q1_sky_policy *remote_q1_sky;
    frontend_remote_q2_image_policy *remote_q2_images;
    frontend_q1_sky *sky_owner;
    frontend_q1_sky_policy *sky;
    qa_render_controls *image_controls;
    qa_render_source_images_ticket *image_admissions;
    qa_q3_cinematic_handles *cinematic_owner;
    qa_q3_cinematic_handles_stage *cinematics;
    bool image_no_bind;
    qa_render_source_restart_values image_restart;
    policy_scalar scalars[sizeof(policy_names) / sizeof(policy_names[0])];
    size_t scalar_count;
    bool live, begun, source_profile, source_restart, children_entered, children_prepared, sealed, published, render_published;
};
static bool scalar_current(const frontend_shared_resource_policy *ticket, bool sealed)
{
    if (!ticket || ticket->frontend->application != ticket->application ||
        ticket->frontend->live_resource_policy != ticket->recipe_owner ||
        ticket->frontend->q1_sky != ticket->sky_owner ||
        ticket->frontend->source_cinematics != ticket->cinematic_owner ||
        (ticket->client && !qa_application_client_prepare_associated(ticket->application, ticket->client)) ||
        ((ticket->video || ticket->live) ? ((ticket->video && !frontend_video_guests_parent_is(ticket->frontend, ticket->video)) ||
            qa_application_cvars(ticket->application) != ticket->registry ||
            ticket->frontend->display != ticket->display || ticket->frontend->cpu != ticket->cpu ||
            ticket->frontend->gl != ticket->gl || ticket->frontend->source_color != ticket->color) :
            (qa_cvars_edit_registry(ticket->edit) != ticket->registry ||
            !(sealed ? qa_cvars_edit_ready_is(ticket->edit) :
                qa_cvars_edit_returned_is(ticket->edit, ticket->registry))))) return false;
    for (size_t i = 0; i < ticket->scalar_count; ++i) {
        const qa_cvar_view *row = (ticket->video || ticket->live) ?
            (i < BASE_POLICY_VALUES ? qa_cvars_find(ticket->registry, policy_names[i]) :
                frontend_render_control_record(ticket->registry, policy_names[i])) :
            (i < BASE_POLICY_VALUES ? qa_cvars_edit_find(ticket->edit, policy_names[i]) :
                qa_cvars_edit_canonical_record(ticket->edit, policy_names[i]));
        uint32_t number = 0;
        if (!row || !ticket->scalars[i].value) return false;
        memcpy(&number, &row->number, sizeof(number));
        if (number != ticket->scalars[i].number || strcmp(row->value, ticket->scalars[i].value)) return false;
    }
    return true;
}
qa_scene_resource_policy *frontend_shared_resource_policy_images(
    const frontend_shared_resource_policy *ticket, const qa_scene_resources *owner)
{
    if (!ticket || !owner || ticket->published) return NULL;
    for (size_t i = 0; i < ticket->bank_count; ++i)
        if (qa_scene_resource_policy_source(ticket->banks[i]) == owner) return ticket->banks[i];
    return NULL;
}
qa_font_resource_policy *frontend_shared_resource_policy_fonts(
    const frontend_shared_resource_policy *ticket, const qa_font_library *owner)
{
    if (!ticket || !owner || ticket->published) return NULL;
    for (size_t i = 0; i < ticket->font_count; ++i)
        if (ticket->fonts[i].owner == owner) return ticket->fonts[i].ticket;
    return NULL;
}
static qa_scene_material_image_policy *policy_library(const frontend_shared_resource_policy *ticket,
    const qa_material_library *owner)
{
    for (size_t i = 0; i < ticket->material_count; ++i)
        if (ticket->materials[i].owner == owner) return ticket->materials[i].ticket;
    return NULL;
}
static qa_material_order_image_policy *policy_order(const frontend_shared_resource_policy *ticket,
    const qa_material_order *owner)
{
    for (size_t i = 0; i < ticket->order_count; ++i)
        if (qa_material_order_image_policy_source(ticket->orders[i]) == owner) return ticket->orders[i];
    return NULL;
}
static bool policy_cleanup(frontend_shared_resource_policy **address, bool published, qa_error *error)
{
    if (!address || !*address) return true;
    frontend_shared_resource_policy *ticket = *address;
    if (ticket->published != published)
        return policy_fail(error, "Resource cleanup cannot change its actual publication branch");
    if (published && ticket->image_admissions && !ticket->render_published)
        return policy_fail(error, "Resource finish requires the actual native image publication");
    if (ticket->image_admissions && !(published ?
        qa_render_controls_source_images_finish(&ticket->image_admissions, error) :
        qa_render_controls_source_images_abort(&ticket->image_admissions, error))) return false;
    if (ticket->sky && !(published ? frontend_q1_sky_policy_finish(&ticket->sky, error) :
        frontend_q1_sky_policy_abort(&ticket->sky, error))) return false;
    if (ticket->remote_q1_sky && !(published ? frontend_remote_q1_sky_policy_finish(&ticket->remote_q1_sky, error) :
        frontend_remote_q1_sky_policy_abort(&ticket->remote_q1_sky, error))) return false;
    if (ticket->remote_q2_images && !(published ? frontend_remote_q2_image_policy_finish(&ticket->remote_q2_images, error) :
        frontend_remote_q2_image_policy_abort(&ticket->remote_q2_images, error))) return false;
    if (ticket->events && !(published ? frontend_event_image_policy_finish(&ticket->events, error) :
        frontend_event_image_policy_abort(&ticket->events, error))) return false;
    if (ticket->q2 && !(published ? frontend_native_q2_image_policy_finish(&ticket->q2, error) :
        frontend_native_q2_image_policy_abort(&ticket->q2, error))) return false;
    if (ticket->visuals && !(published ? frontend_visual_policy_finish(&ticket->visuals, error) :
        frontend_visual_policy_abort(&ticket->visuals, error))) return false;
    for (size_t i = ticket->models ? ticket->model_count : 0; i > 0; --i)
        if (ticket->models[i - 1].ticket && !(published ? qa_scene_model_image_policy_finish(&ticket->models[i - 1].ticket, error) :
            qa_scene_model_image_policy_abort(&ticket->models[i - 1].ticket, error))) return false;
    for (size_t i = ticket->worlds ? ticket->world_count : 0; i > 0; --i)
        if (ticket->worlds[i - 1].ticket && !(published ? qa_scene_world_image_policy_finish(&ticket->worlds[i - 1].ticket, error) :
            qa_scene_world_image_policy_abort(&ticket->worlds[i - 1].ticket, error))) return false;
    /* Order tickets borrow the private destination orders. Close them before
     * the library children release those actual owners. */
    for (size_t i = ticket->orders ? ticket->order_count : 0; i > 0; --i)
        if (ticket->orders[i - 1] && !(published ? qa_material_order_image_policy_finish(&ticket->orders[i - 1], error) :
            qa_material_order_image_policy_abort(&ticket->orders[i - 1], error))) return false;
    for (size_t i = ticket->materials ? ticket->material_count : 0; i > 0; --i)
        if (ticket->materials[i - 1].ticket && !(published ? qa_scene_material_image_policy_finish(&ticket->materials[i - 1].ticket, error) :
            qa_scene_material_image_policy_abort(&ticket->materials[i - 1].ticket, error))) return false;
    if (ticket->cinematics && !(published ? qa_q3_cinematic_handles_stage_finish(&ticket->cinematics, error) :
        qa_q3_cinematic_handles_stage_abort(&ticket->cinematics, error))) return false;
    if (ticket->inventory && !frontend_resource_inventory_cinematics(ticket->inventory, NULL, error)) return false;
    for (size_t i = ticket->materials ? ticket->material_count : 0; i > 0; --i)
        if (ticket->materials[i - 1].movie_ticket && !(published ?
            frontend_material_movies_policy_finish(&ticket->materials[i - 1].movie_ticket, error) :
            frontend_material_movies_policy_abort(&ticket->materials[i - 1].movie_ticket, error))) return false;
    for (size_t i = ticket->fonts ? ticket->font_count : 0; i > 0; --i)
        if (ticket->fonts[i - 1].ticket && !(published ? qa_font_resource_policy_finish(&ticket->fonts[i - 1].ticket, error) :
            qa_font_resource_policy_abort(&ticket->fonts[i - 1].ticket, error))) return false;
    for (size_t i = ticket->banks ? ticket->bank_count : 0; i > 0; --i)
        if (ticket->banks[i - 1] && !(published ? qa_scene_resource_policy_finish(&ticket->banks[i - 1], error) :
            qa_scene_resource_policy_abort(&ticket->banks[i - 1], error))) return false;
    while (ticket->assets_held) {
        qa_q3_presentation_assets *assets = ticket->assets[ticket->assets_held - 1]; size_t count = 0;
        if (!qa_q3_assets_model_count(assets, &count, error)) return false;
        qa_q3_assets_capture_end(assets);
        if (!qa_q3_assets_idle(assets)) return policy_fail(error, "Resource retirement retains its actual Q3 asset lease");
        --ticket->assets_held;
    }
    if (!frontend_resource_inventory_release(&ticket->inventory, error)) return false;
    for (size_t i = 0; i < sizeof(policy_names) / sizeof(policy_names[0]); ++i) free(ticket->scalars[i].value);
    free(ticket->assets); free(ticket->banks); free(ticket->orders); free(ticket->materials); free(ticket->fonts);
    free(ticket->worlds); free(ticket->models); free(ticket); *address = NULL; return true;
}
static bool policy_allocate(frontend_shared_resource_policy *ticket, qa_error *error)
{
    while (frontend_resource_inventory_images_at(ticket->inventory, ticket->bank_count)) ++ticket->bank_count;
    while (frontend_resource_inventory_order_at(ticket->inventory, ticket->order_count)) ++ticket->order_count;
    while (frontend_resource_inventory_library_at(ticket->inventory, ticket->material_count)) ++ticket->material_count;
    while (frontend_resource_inventory_fonts_at(ticket->inventory, ticket->font_count)) ++ticket->font_count;
    while (frontend_resource_inventory_world_at(ticket->inventory, ticket->world_count)) ++ticket->world_count;
    while (frontend_resource_inventory_model_at(ticket->inventory, ticket->model_count)) ++ticket->model_count;
    while (frontend_resource_inventory_assets_at(ticket->inventory, ticket->asset_count)) ++ticket->asset_count;
    if (ticket->bank_count > SIZE_MAX / sizeof(*ticket->banks) ||
        ticket->order_count > SIZE_MAX / sizeof(*ticket->orders) ||
        ticket->material_count > SIZE_MAX / sizeof(*ticket->materials) ||
        ticket->font_count > SIZE_MAX / sizeof(*ticket->fonts) ||
        ticket->world_count > SIZE_MAX / sizeof(*ticket->worlds) ||
        ticket->model_count > SIZE_MAX / sizeof(*ticket->models) ||
        ticket->asset_count > SIZE_MAX / sizeof(*ticket->assets))
        return frontend_fail(error, QA_ERROR_MEMORY, "Complete resource roster exceeds addressable storage");
    ticket->banks = ticket->bank_count ? calloc(ticket->bank_count, sizeof(*ticket->banks)) : NULL;
    ticket->orders = ticket->order_count ? calloc(ticket->order_count, sizeof(*ticket->orders)) : NULL;
    ticket->materials = ticket->material_count ? calloc(ticket->material_count, sizeof(*ticket->materials)) : NULL;
    ticket->fonts = ticket->font_count ? calloc(ticket->font_count, sizeof(*ticket->fonts)) : NULL;
    ticket->worlds = ticket->world_count ? calloc(ticket->world_count, sizeof(*ticket->worlds)) : NULL;
    ticket->models = ticket->model_count ? calloc(ticket->model_count, sizeof(*ticket->models)) : NULL;
    ticket->assets = ticket->asset_count ? calloc(ticket->asset_count, sizeof(*ticket->assets)) : NULL;
    if ((ticket->bank_count && !ticket->banks) || (ticket->order_count && !ticket->orders) || (ticket->material_count && !ticket->materials) ||
        (ticket->font_count && !ticket->fonts) || (ticket->world_count && !ticket->worlds) ||
        (ticket->model_count && !ticket->models) || (ticket->asset_count && !ticket->assets))
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining the complete prepared resource roster");
    return true;
}
static bool policy_begin(qa_frontend *f, const qa_launch_snapshot *candidate,
    const qa_application_client_preparation *client, const qa_cvars_edit *edit,
    frontend_shared_resource_policy **out, qa_error *error)
{
    if (!f || !f->application || !out || *out || !edit ||
        qa_cvars_edit_registry(edit) != qa_application_cvars(f->application) ||
        !qa_cvars_edit_returned_is(edit, qa_application_cvars(f->application)))
        return policy_fail(error, "Resource preparation requires its actual canonical ENGINE ticket");
    if (client && (candidate || !qa_application_client_prepare_associated(f->application, client) ||
        !qa_application_client_prepare_entered(client, QA_CLIENT_PREPARE_RESOURCES)))
        return policy_fail(error, "CLIENT resource preparation requires its genuine entered resource token");
    if (!live_owner(f, error)) return false;
    frontend_shared_resource_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining shared resource preparation");
    ticket->frontend = f; ticket->application = f->application;
    ticket->registry = qa_cvars_edit_registry(edit); ticket->edit = edit;
    ticket->recipe_owner = f->live_resource_policy;
    ticket->client = client;
    ticket->sky_owner = f->q1_sky; ticket->cinematic_owner = f->source_cinematics;
    ticket->scalar_count = BASE_POLICY_VALUES;
    *out = ticket;
    qa_scene_image_policy images[3]; frontend_model_policy models;
    bool ok = frontend_image_policy_edit_read(edit, images, error) &&
        frontend_model_policy_edit_read(edit, &models, error);
    for (size_t i = 0; ok && i < ticket->scalar_count; ++i) {
        const qa_cvar_view *row = qa_cvars_edit_find(edit, policy_names[i]);
        size_t size = strlen(row->value) + 1;
        ticket->scalars[i].value = malloc(size);
        if (!ticket->scalars[i].value) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual staged resource policy values"); break; }
        memcpy(ticket->scalars[i].value, row->value, size);
        memcpy(&ticket->scalars[i].number, &row->number, sizeof(row->number));
    }
    if (ok) ok = (client ? frontend_resource_inventory_collect_client(f, client, &ticket->inventory, error) :
        frontend_resource_inventory_collect(f, f->application, candidate, &ticket->inventory, error)) && policy_allocate(ticket, error);
    for (size_t i = 0; ok && i < ticket->material_count; ++i)
        if (qa_material_library_has_source_profile(frontend_resource_inventory_library_at(ticket->inventory, i)))
            ticket->source_profile = true;
    if (ok && ticket->source_profile) {
        ticket->scalar_count = sizeof(policy_names) / sizeof(policy_names[0]);
        for (size_t i = BASE_POLICY_VALUES; ok && i < ticket->scalar_count; ++i) {
            const qa_cvar_view *row = qa_cvars_edit_canonical_record(edit, policy_names[i]);
            if (!row || !row->value) { ok = policy_fail(error, "Source profile lost its actual candidate row"); break; }
            size_t size = strlen(row->value) + 1;
            ticket->scalars[i].value = malloc(size);
            if (!ticket->scalars[i].value) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining Source profile values"); break; }
            memcpy(ticket->scalars[i].value, row->value, size);
            memcpy(&ticket->scalars[i].number, &row->number, sizeof(row->number));
        }
    }
    if (ok) ticket->begun = true;
    else {
        qa_error cleanup = {0};
        if (!policy_cleanup(out, false, &cleanup) && error) *error = cleanup;
    }
    return ok;
}
bool frontend_shared_resource_policy_begin(qa_frontend *f, const qa_launch_snapshot *candidate,
    const qa_cvars_edit *edit, frontend_shared_resource_policy **out, qa_error *error)
{ return policy_begin(f, candidate, NULL, edit, out, error); }
bool frontend_shared_resource_policy_begin_client(qa_frontend *f, const qa_application_client_preparation *client,
    const qa_cvars_edit *edit, frontend_shared_resource_policy **out, qa_error *error)
{
    if (!client) return policy_fail(error, "CLIENT resource preparation requires its actual token");
    return policy_begin(f, NULL, client, edit, out, error);
}
static bool policy_committed_prepare(qa_frontend *f, const frontend_video_guests *video,
    frontend_shared_resource_policy **out, qa_error *error)
{
    if (!f || !f->application || !out || *out ||
        (video ? !frontend_video_guests_parent_is(f, video) :
            (f->stepping || f->preparing || f->capture || f->source_restoring || f->round)) ||
        frontend_config_store_shared_pending(f->config_store) ||
        !qa_cvars_observer_idle(qa_application_cvars(f->application)))
        return policy_fail(error, "Committed resource refresh requires its actual returned ENGINE parent");
    if (!live_owner(f, error)) return false;
    frontend_shared_resource_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining renderer restart resources");
    ticket->frontend = f; ticket->application = f->application;
    ticket->registry = qa_application_cvars(f->application); ticket->sky_owner = f->q1_sky;
    ticket->recipe_owner = f->live_resource_policy; ticket->live = !video;
    ticket->cinematic_owner = f->source_cinematics;
    ticket->video = video; ticket->display = f->display; ticket->cpu = f->cpu; ticket->gl = f->gl;
    ticket->color = f->source_color; ticket->scalar_count = BASE_POLICY_VALUES;
    *out = ticket;
    bool ok = (video ? frontend_resource_inventory_collect_video(f, video, &ticket->inventory, error) :
        frontend_resource_inventory_collect(f, f->application, NULL, &ticket->inventory, error)) && policy_allocate(ticket, error);
    for (size_t i = 0; ok && i < ticket->material_count; ++i)
        if (qa_material_library_has_source_profile(frontend_resource_inventory_library_at(ticket->inventory, i)))
            ticket->source_profile = true;
    for (size_t i = 0; video && ok && i < ticket->bank_count; ++i)
        if (qa_scene_source_q3_white(frontend_resource_inventory_images_at(ticket->inventory, i)))
            ticket->source_restart = true;
    ticket->source_restart = video && (ticket->source_restart || ticket->source_profile || f->source_color != NULL);
    if (ok && ticket->source_restart)
        ok = frontend_q3_source_upload_read(f, true, true, &ticket->restart_upload, error);
    if (ticket->source_profile) ticket->scalar_count = sizeof(policy_names) / sizeof(policy_names[0]);
    for (size_t i = 0; ok && i < ticket->scalar_count; ++i) {
        const qa_cvar_view *row = i < BASE_POLICY_VALUES ? qa_cvars_find(ticket->registry, policy_names[i]) :
            frontend_render_control_record(ticket->registry, policy_names[i]);
        if (!row || !row->value) { ok = policy_fail(error, "Source restart lost its real canonical resource policy"); break; }
        size_t size = strlen(row->value) + 1;
        ticket->scalars[i].value = malloc(size);
        if (!ticket->scalars[i].value) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining restart ENGINE rows"); break; }
        memcpy(ticket->scalars[i].value, row->value, size);
        memcpy(&ticket->scalars[i].number, &row->number, sizeof(row->number));
    }
    if (ok) { ticket->begun = true; ok = frontend_shared_resource_policy_prepare_children(ticket, error); }
    if (!ok) {
        qa_error cleanup = {0};
        if (!policy_cleanup(out, false, &cleanup) && error) *error = cleanup;
    }
    return ok;
}
bool frontend_shared_resource_policy_restart_prepare(qa_frontend *f, const frontend_video_guests *video,
    frontend_shared_resource_policy **out, qa_error *error)
{
    if (!video) return policy_fail(error, "Source restart refresh requires its real video ticket");
    return policy_committed_prepare(f, video, out, error);
}
bool frontend_shared_resource_policy_prepare_children(frontend_shared_resource_policy *ticket, qa_error *error)
{
    if (!ticket || !ticket->begun || ticket->children_entered || ticket->published ||
        !scalar_current(ticket, false) || !frontend_resource_inventory_current(ticket->inventory))
        return policy_fail(error, "Resource children require their retained pre-input metadata admission");
    ticket->children_entered = true;
    qa_frontend *f = ticket->frontend;
    const qa_cvars_edit *edit = ticket->edit;
    qa_scene_image_policy images[3]; frontend_model_policy models;
    qa_material_profile profile = {0};
    bool committed = ticket->video || ticket->live;
    bool ok = (committed ? frontend_image_policy_read(f, images, error) : frontend_image_policy_edit_read(edit, images, error)) &&
        (committed ? frontend_model_policy_read(f, &models, error) : frontend_model_policy_edit_read(edit, &models, error)) &&
        (!ticket->source_profile || frontend_q3_material_profile_read(f, edit, &profile, error));
    for (size_t i = 0; ok && i < ticket->asset_count; ++i) {
        ticket->assets[i] = frontend_resource_inventory_assets_at(ticket->inventory, i);
        ok = qa_q3_assets_capture_begin(ticket->assets[i], error);
        if (ok) ++ticket->assets_held;
    }
    for (size_t i = 0; ok && i < ticket->order_count; ++i)
        ok = qa_material_order_image_policy_prepare((qa_material_order *)frontend_resource_inventory_order_at(ticket->inventory, i),
            &ticket->orders[i], error);
    for (size_t i = 0; ok && i < ticket->bank_count; ++i)
        ok = ticket->video && ticket->source_restart ?
            qa_scene_resource_policy_prepare_source_restart((qa_scene_resources *)frontend_resource_inventory_images_at(ticket->inventory, i),
                &ticket->restart_upload, &ticket->banks[i], error) :
            qa_scene_resource_policy_prepare((qa_scene_resources *)frontend_resource_inventory_images_at(ticket->inventory, i),
                images, &ticket->banks[i], error);
    for (size_t i = 0; ok && i < ticket->bank_count; ++i)
        ok = qa_scene_resource_policy_dependencies(ticket->banks[i], ticket->banks, ticket->bank_count, error);
    if (ok && ticket->cinematic_owner) {
        qa_q3_cinematic_handles_options options;
        ok = qa_q3_cinematic_handles_read(ticket->cinematic_owner, &options) &&
            qa_q3_cinematic_handles_stage_prepare(ticket->cinematic_owner,
                frontend_shared_resource_policy_images(ticket, options.images), &ticket->cinematics, error) &&
            frontend_resource_inventory_cinematics(ticket->inventory, ticket->cinematics, error);
        if (!ok && error && error->code == QA_OK)
            policy_fail(error, "Cinematic preparation lost its actual global scratch bank");
    }
    for (size_t i = 0; ok && i < ticket->font_count; ++i) {
        policy_font *font = ticket->fonts + i;
        font->owner = (qa_font_library *)frontend_resource_inventory_fonts_at(ticket->inventory, i);
        ok = qa_font_resource_policy_prepare(font->owner, frontend_shared_resource_policy_images(ticket,
            qa_font_library_resource_owner(font->owner)), &font->ticket, error);
    }
    for (size_t i = 0; ok && i < ticket->world_count; ++i) {
        policy_world *world = ticket->worlds + i;
        world->owner = (qa_scene_world *)frontend_resource_inventory_world_at(ticket->inventory, i);
        ok = qa_scene_world_image_policy_prepare(world->owner, frontend_shared_resource_policy_images(ticket,
            qa_scene_world_resource_owner(world->owner)), &world->ticket, error);
    }
    qa_scene_world_image_policy **worlds = ok && ticket->world_count ? malloc(ticket->world_count * sizeof(*worlds)) : NULL;
    if (ok && ticket->world_count && !worlds) ok = frontend_fail(error, QA_ERROR_MEMORY, "Preparing actual material brush receipts");
    for (size_t i = 0; ok && i < ticket->world_count; ++i) worlds[i] = ticket->worlds[i].ticket;
    for (size_t i = 0; ok && i < ticket->material_count; ++i) {
        policy_material *material = ticket->materials + i;
        material->owner = (qa_material_library *)frontend_resource_inventory_library_at(ticket->inventory, i);
        ok = qa_scene_material_image_policy_prepare_profile(material->owner, frontend_shared_resource_policy_images(ticket,
            qa_material_library_resource_owner(material->owner)), worlds, ticket->world_count,
            policy_order(ticket, qa_material_library_order_owner(material->owner)),
            qa_material_library_has_source_profile(material->owner) ? &profile : NULL, &material->ticket, error);
    }
    free(worlds);
    for (size_t i = 0; ok && i < ticket->material_count; ++i) {
        policy_material *material = ticket->materials + i;
        const qa_scene_image *(*start)(void *, const char *, qa_error *) = NULL;
        void *context = NULL;
        ok = qa_material_library_video_start_read(material->owner, &start, &context);
        if (ok && start) {
            qa_q3_cinematic_source *cinematic_source = NULL;
            ok = frontend_material_movies_library_owner(material->owner, &material->movies, error) &&
                frontend_material_movies_cinematic_read(material->movies, &cinematic_source, error) &&
                frontend_material_movies_policy_prepare(material->movies, frontend_shared_resource_policy_images(ticket,
                    qa_material_library_resource_owner(material->owner)), material->ticket,
                    cinematic_source ? ticket->cinematics : NULL, &material->movie_ticket, error);
        }
    }
    for (size_t i = 0; ok && i < ticket->world_count; ++i)
        ticket->worlds[i].materials = policy_library(ticket, qa_scene_world_material_owner(ticket->worlds[i].owner));
    for (size_t i = 0; ok && i < ticket->model_count; ++i) {
        frontend_visual_policy_binding *model = ticket->models + i;
        model->model = (qa_scene_model *)frontend_resource_inventory_model_at(ticket->inventory, i);
        ok = qa_scene_model_image_policy_prepare(model->model, frontend_shared_resource_policy_images(ticket,
            qa_scene_model_resource_owner(model->model)), &model->ticket, error);
        const qa_material_library *materials = qa_scene_model_material_owner(model->model);
        if (ok && materials) ok = qa_scene_model_image_policy_materials(model->ticket, policy_library(ticket, materials), error);
    }
    const char *failure = "Prepared resource source or canonical values changed";
    if (ok) {
        failure = "Prepared visual resource policy failed";
        ok = frontend_visual_policy_prepare(f, &models, ticket->models, ticket->model_count, &ticket->visuals, error);
    }
    if (ok) {
        failure = "Prepared registered-model resource policy failed";
        ok = frontend_visual_registered_model_policy_prepare(f, &models, ticket->assets, ticket->asset_count,
            ticket->models, ticket->model_count, error);
    }
    if (ok) {
        failure = "Prepared native Q2 image policy failed";
        ok = frontend_native_q2_image_policy_prepare(f, ticket->banks, ticket->bank_count, &ticket->q2, error);
    }
    if (ok) {
        failure = "Prepared event image policy failed";
        ok = frontend_event_image_policy_prepare(f, ticket->banks, ticket->bank_count, &ticket->events, error);
    }
    if (ok) {
        failure = "Prepared remote Q1 sky policy failed";
        ok = frontend_remote_q1_sky_policy_prepare(f, ticket->banks, ticket->bank_count, &ticket->remote_q1_sky, error);
    }
    if (ok) {
        failure = "Prepared remote Q2 image policy failed";
        ok = frontend_remote_q2_image_policy_prepare(f, ticket->banks, ticket->bank_count, &ticket->remote_q2_images, error);
    }
    if (ok) {
        failure = "Prepared Q1 sky policy failed";
        ok = !ticket->sky_owner || frontend_q1_sky_policy_prepare(ticket->sky_owner, ticket->banks,
            ticket->bank_count, &ticket->sky, error);
    }
    if (ok) {
        failure = "Prepared resource canonical values changed";
        ok = scalar_current(ticket, false);
    }
    if (ok) {
        failure = "Prepared resource owner inventory changed";
        ok = frontend_resource_inventory_current(ticket->inventory);
    }
    if (ok) ticket->children_prepared = true;
    else if (error && error->code == QA_OK) policy_fail(error, failure);
    return ok;
}
bool frontend_shared_resource_policy_prepare(qa_frontend *f, const qa_launch_snapshot *candidate,
    const qa_cvars_edit *edit, frontend_shared_resource_policy **out, qa_error *error)
{
    bool ok = frontend_shared_resource_policy_begin(f, candidate, edit, out, error) &&
        frontend_shared_resource_policy_prepare_children(*out, error);
    if (!ok && out && *out) {
        qa_error cleanup = {0};
        if (!policy_cleanup(out, false, &cleanup) && error) *error = cleanup;
    }
    return ok;
}
bool frontend_shared_resource_policy_ready(frontend_shared_resource_policy *ticket, qa_error *error)
{
    if (!scalar_current(ticket, true) || !ticket->children_prepared || ticket->published || !frontend_resource_inventory_current(ticket->inventory))
        return policy_fail(error, "Resource seal requires its actual final canonical values and complete roster");
    if (ticket->sealed) return frontend_shared_resource_policy_ready_is(ticket);
    for (size_t i = 0; i < ticket->font_count; ++i)
        if (!qa_font_resource_policy_ready(ticket->fonts[i].ticket, error)) return false;
    for (size_t i = 0; i < ticket->material_count; ++i)
        if (ticket->materials[i].movie_ticket &&
            !frontend_material_movies_policy_ready(ticket->materials[i].movie_ticket, error)) return false;
    if (ticket->cinematics && !qa_q3_cinematic_handles_stage_ready(ticket->cinematics, error)) return false;
    for (size_t i = 0; i < ticket->material_count; ++i)
        if (!qa_scene_material_image_policy_ready(ticket->materials[i].ticket, error)) return false;
    for (size_t i = 0; i < ticket->order_count; ++i)
        if (!qa_material_order_image_policy_ready(ticket->orders[i], error)) return false;
    for (size_t i = 0; i < ticket->world_count; ++i)
        if (!qa_scene_world_image_policy_ready(ticket->worlds[i].ticket, ticket->worlds[i].materials, error)) return false;
    for (size_t i = 0; i < ticket->model_count; ++i)
        if (!qa_scene_model_image_policy_ready(ticket->models[i].ticket, error)) return false;
    if (!frontend_visual_policy_ready(ticket->visuals, error) ||
        !frontend_native_q2_image_policy_ready(ticket->q2, error) ||
        !frontend_event_image_policy_ready(ticket->events, error) ||
        !frontend_remote_q1_sky_policy_ready(ticket->remote_q1_sky, error) ||
        !frontend_remote_q2_image_policy_ready(ticket->remote_q2_images, error) ||
        (ticket->sky && !frontend_q1_sky_policy_ready(ticket->sky, error))) return false;
    for (size_t i = 0; i < ticket->bank_count; ++i)
        if (!qa_scene_resource_policy_ready(ticket->banks[i], error)) return false;
    bool new_source_images = false;
    for (size_t i = 0; i < ticket->bank_count; ++i) {
        size_t count = 0;
        if (!qa_scene_resource_policy_source_image_count(ticket->banks[i], &count, error)) return false;
        if (count) new_source_images = true;
    }
    if (new_source_images || (ticket->video && ticket->source_restart)) {
        ticket->image_controls = ticket->frontend->gl ? qa_gl_render_controls(ticket->frontend->gl) :
            qa_cpu_render_controls(ticket->frontend->cpu);
        bool no_bind = false;
        if (!frontend_q3_source_no_bind_read(ticket->frontend, ticket->edit, &no_bind, error)) return false;
        if (ticket->image_admissions && ticket->image_no_bind != no_bind)
            return policy_fail(error, "Prepared image admission lost its actual candidate bind policy");
        qa_render_source_restart_values restart = {0};
        if (ticket->video && !frontend_q3_source_restart_read(ticket->frontend, ticket->edit, &restart, error)) return false;
        if (ticket->image_admissions && ticket->video &&
            (ticket->image_restart.max_polys != restart.max_polys ||
             ticket->image_restart.max_polyverts != restart.max_polyverts || ticket->image_restart.filter != restart.filter))
            return policy_fail(error, "Prepared image restart lost its actual signed limits or filter");
        ticket->image_no_bind = no_bind;
        ticket->image_restart = restart;
        if ((!ticket->image_admissions && !qa_render_controls_source_images_prepare(ticket->image_controls,
                ticket->banks, ticket->bank_count, no_bind, ticket->video ? &ticket->image_restart : NULL,
                &ticket->image_admissions, error)) ||
            !qa_render_controls_source_images_ready(ticket->image_admissions, error)) return false;
    }
    if (!frontend_resource_inventory_seal(ticket->inventory, error)) return false;
    ticket->sealed = true; return frontend_shared_resource_policy_ready_is(ticket);
}
static bool policy_ready_is(const frontend_shared_resource_policy *ticket, bool consuming)
{
    if (!ticket || !ticket->sealed || ticket->published || !scalar_current(ticket, true) ||
        !(consuming ? frontend_resource_inventory_consume_ready_is(ticket->inventory) :
            frontend_resource_inventory_ready_is(ticket->inventory))) return false;
    if (ticket->image_admissions &&
        ((ticket->frontend->gl ? qa_gl_render_controls(ticket->frontend->gl) :
            qa_cpu_render_controls(ticket->frontend->cpu)) != ticket->image_controls ||
         !qa_render_controls_source_images_ready_is(ticket->image_admissions))) return false;
    if (ticket->cinematics && !qa_q3_cinematic_handles_stage_ready_is(ticket->cinematics)) return false;
    if (ticket->image_admissions) {
        bool no_bind = false;
        if (!frontend_q3_source_no_bind_read(ticket->frontend, ticket->edit, &no_bind, NULL) ||
            no_bind != ticket->image_no_bind) return false;
        if (ticket->video) {
            qa_render_source_restart_values restart = {0};
            if (!frontend_q3_source_restart_read(ticket->frontend, ticket->edit, &restart, NULL) ||
                ticket->image_restart.max_polys != restart.max_polys ||
                ticket->image_restart.max_polyverts != restart.max_polyverts ||
                ticket->image_restart.filter != restart.filter) return false;
        }
    }
    for (size_t i = 0; i < ticket->bank_count; ++i) if (!qa_scene_resource_policy_ready_is(ticket->banks[i])) return false;
    for (size_t i = 0; i < ticket->order_count; ++i) if (!qa_material_order_image_policy_ready_is(ticket->orders[i])) return false;
    for (size_t i = 0; i < ticket->font_count; ++i) if (!qa_font_resource_policy_ready_is(ticket->fonts[i].ticket)) return false;
    for (size_t i = 0; i < ticket->material_count; ++i) if (!qa_scene_material_image_policy_ready_is(ticket->materials[i].ticket)) return false;
    for (size_t i = 0; i < ticket->material_count; ++i)
        if (ticket->materials[i].movie_ticket &&
            (!frontend_material_movies_policy_current(ticket->materials[i].movie_ticket,
                ticket->materials[i].movies, frontend_shared_resource_policy_images(ticket,
                    qa_material_library_resource_owner(ticket->materials[i].owner)), ticket->materials[i].ticket) ||
             !frontend_material_movies_policy_ready_is(ticket->materials[i].movie_ticket))) return false;
    for (size_t i = 0; i < ticket->world_count; ++i) if (!qa_scene_world_image_policy_ready_is(ticket->worlds[i].ticket)) return false;
    for (size_t i = 0; i < ticket->model_count; ++i) if (!qa_scene_model_image_policy_ready_is(ticket->models[i].ticket)) return false;
    return frontend_visual_policy_ready_is(ticket->visuals) && frontend_native_q2_image_policy_ready_is(ticket->q2) &&
        frontend_event_image_policy_ready_is(ticket->events) &&
        frontend_remote_q1_sky_policy_ready_is(ticket->remote_q1_sky) &&
        frontend_remote_q2_image_policy_ready_is(ticket->remote_q2_images) &&
        (!ticket->sky || frontend_q1_sky_policy_ready_is(ticket->sky));
}
bool frontend_shared_resource_policy_ready_is(const frontend_shared_resource_policy *ticket)
{ return policy_ready_is(ticket, false); }
bool frontend_shared_resource_policy_consume_ready_is(const frontend_shared_resource_policy *ticket)
{ return policy_ready_is(ticket, true); }
static void policy_publish(frontend_shared_resource_policy *ticket)
{
    for (size_t i = 0; i < ticket->bank_count; ++i) qa_scene_resource_policy_publish(ticket->banks[i]);
    for (size_t i = 0; i < ticket->order_count; ++i) qa_material_order_image_policy_publish(ticket->orders[i]);
    for (size_t i = 0; i < ticket->material_count; ++i) qa_scene_material_image_policy_publish(ticket->materials[i].ticket);
    for (size_t i = 0; i < ticket->material_count; ++i)
        if (ticket->materials[i].movie_ticket)
            frontend_material_movies_policy_publish(ticket->materials[i].movie_ticket);
    if (ticket->cinematics) qa_q3_cinematic_handles_stage_publish(ticket->cinematics);
    for (size_t i = 0; i < ticket->world_count; ++i) qa_scene_world_image_policy_publish(ticket->worlds[i].ticket);
    for (size_t i = 0; i < ticket->model_count; ++i) qa_scene_model_image_policy_publish(ticket->models[i].ticket);
    for (size_t i = 0; i < ticket->font_count; ++i) qa_font_resource_policy_publish(ticket->fonts[i].ticket);
    frontend_visual_policy_publish(ticket->visuals);
    frontend_native_q2_image_policy_publish(ticket->q2); frontend_event_image_policy_publish(ticket->events);
    frontend_remote_q1_sky_policy_publish(ticket->remote_q1_sky);
    frontend_remote_q2_image_policy_publish(ticket->remote_q2_images);
    if (ticket->sky) frontend_q1_sky_policy_publish(ticket->sky);
    ticket->published = true;
    struct frontend_live_resource_policy *owner = ticket->recipe_owner;
    for (size_t i = 0; i < POLICY_VALUES; ++i) {
        free(owner->applied[i].value);
        owner->applied[i] = ticket->scalars[i]; ticket->scalars[i] = (policy_scalar){0};
    }
    owner->applied_count = ticket->scalar_count;
    owner->model_use_ready = false;
}
void frontend_shared_resource_policy_publish(frontend_shared_resource_policy *ticket)
{
    if (frontend_shared_resource_policy_ready_is(ticket)) policy_publish(ticket);
}
void frontend_shared_resource_policy_consume(frontend_shared_resource_policy *ticket)
{
    if (frontend_shared_resource_policy_consume_ready_is(ticket)) policy_publish(ticket);
}
void frontend_shared_resource_policy_render_publish(frontend_shared_resource_policy *ticket)
{
    if (!ticket || !ticket->published || ticket->render_published) return;
    if (ticket->image_admissions) qa_render_controls_source_images_publish(ticket->image_admissions);
    ticket->render_published = true;
}
bool frontend_shared_resource_policy_finish(frontend_shared_resource_policy **ticket, qa_error *error)
{ return policy_cleanup(ticket, true, error); }
bool frontend_shared_resource_policy_abort(frontend_shared_resource_policy **ticket, qa_error *error)
{ return policy_cleanup(ticket, false, error); }

bool frontend_shared_resource_policy_live_retire(qa_frontend *f, qa_error *error)
{
    if (!f || !f->live_resource_policy) return true;
    struct frontend_live_resource_policy *owner = f->live_resource_policy;
    qa_cvars *registry = qa_application_cvars(f->application);
    if (!registry && f->engine_shutdown) {
        qa_console *console = NULL;
        if (qa_application_engine_shutdown_owner(f->engine_shutdown) != f->application ||
            !qa_application_engine_shutdown_read(f->engine_shutdown, &console, &registry, error))
            return policy_fail(error, "Live resource retirement lost its actual ENGINE shutdown loan");
    }
    if (owner->frontend != f || owner->application != f->application || !registry ||
        owner->registry != registry || owner->busy)
        return policy_fail(error, "Live resource retirement requires its returned actual owner");
    return !owner->pending || policy_cleanup(&owner->pending, owner->pending->published, error);
}
bool frontend_shared_resource_policy_live_destroy(qa_frontend *f, qa_error *error)
{
    if (!frontend_shared_resource_policy_live_retire(f, error)) return false;
    if (!f || !f->live_resource_policy) return true;
    struct frontend_live_resource_policy *owner = f->live_resource_policy;
    for (size_t i = 0; i < POLICY_VALUES; ++i) free(owner->applied[i].value);
    free(owner); f->live_resource_policy = NULL; return true;
}
static bool live_recipe_changed(const struct frontend_live_resource_policy *owner, bool *changed, qa_error *error)
{
    *changed = !owner->applied_count;
    if (*changed) return true;
    for (size_t i = 0; i < owner->applied_count; ++i) {
        /* Use and distance update the selected model without reopening assets. */
        if (i >= 5 && i < BASE_POLICY_VALUES) continue;
        const qa_cvar_view *row = i < BASE_POLICY_VALUES ? qa_cvars_find(owner->registry, policy_names[i]) :
            frontend_render_control_record(owner->registry, policy_names[i]);
        if (!row || !row->value || !owner->applied[i].value)
            return policy_fail(error, "Live resource refresh lost its canonical Source declaration");
        if (i == 3 || i == 4) {
            float previous;
            memcpy(&previous, &owner->applied[i].number, sizeof(previous));
            *changed = (previous != 0) != (row->number != 0);
        } else {
            uint32_t number;
            memcpy(&number, &row->number, sizeof(number));
            *changed = number != owner->applied[i].number || strcmp(row->value, owner->applied[i].value);
        }
        if (*changed) break;
    }
    return true;
}
bool frontend_shared_resource_policy_live_sync(qa_frontend *f, qa_error *error)
{
    if (!f || !f->application || f->stepping || f->preparing || f->capture || f->round || f->source_restoring)
        return policy_fail(error, "Live resource refresh requires its idle physical frontend");
    if (!frontend_shared_resource_policy_live_retire(f, error)) return false;
    if (f->options.dedicated || qa_application_startup_pending(f->application) ||
        frontend_config_store_shared_pending(f->config_store)) return true;
    if (!qa_cvars_observer_idle(qa_application_cvars(f->application)) || !live_owner(f, error))
        return policy_fail(error, "Live resource refresh requires returned ENGINE publications");
    struct frontend_live_resource_policy *owner = f->live_resource_policy;
    bool changed;
    if (!live_recipe_changed(owner, &changed, error)) return false;
    if (!changed) {
        frontend_model_policy policy;
        if (!frontend_model_policy_read(f, &policy, error)) return false;
        const frontend_model_policy *previous = &owner->model_use;
        if (owner->model_use_ready && previous->q1_enhanced == policy.q1_enhanced &&
            previous->q2_load == policy.q2_load && previous->q2_use == policy.q2_use &&
            previous->source_distance == policy.source_distance && previous->distance == policy.distance &&
            (previous->q2_distance == policy.q2_distance ||
             (isnan(previous->q2_distance) && isnan(policy.q2_distance)))) return true;
        if (!model_policy_sync(f, &policy, error)) return false;
        owner->model_use = policy;
        owner->model_use_ready = true;
        return true;
    }
    owner->busy = true;
    bool ok = policy_committed_prepare(f, NULL, &owner->pending, error) &&
        frontend_shared_resource_policy_ready(owner->pending, error);
    if (ok) {
        frontend_shared_resource_policy_publish(owner->pending);
        frontend_shared_resource_policy_render_publish(owner->pending);
        ok = owner->pending->published && owner->pending->render_published;
        if (!ok) policy_fail(error, "Live resource refresh lost its prepared publication");
    }
    owner->busy = false;
    qa_error cleanup = {0};
    if (!frontend_shared_resource_policy_live_retire(f, &cleanup)) {
        if (ok && error) *error = cleanup;
        return false;
    }
    return ok;
}
static bool recipe_fields(qa_source_save_io *io, policy_scalar values[POLICY_VALUES], size_t *count)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[5] = {'Q','F','R','P',1};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QFRP\1", sizeof(magic)) ||
        !qa_source_save_count(io, count, POLICY_VALUES) ||
        (*count != 0 && *count != BASE_POLICY_VALUES && *count != POLICY_VALUES)) return false;
    for (size_t i = 0; i < *count; ++i) {
        size_t size = reading ? 0 : strlen(values[i].value);
        if (!qa_source_save_u32(io, &values[i].number) || !qa_source_save_count(io, &size, SIZE_MAX)) return false;
        if (reading) {
            if (io->offset > io->input.size || size > io->input.size - io->offset || size == SIZE_MAX) return false;
            values[i].value = malloc(size + 1);
            if (!values[i].value) return frontend_fail(io->error, QA_ERROR_MEMORY, "Decoding published resource recipe");
        }
        if (!qa_source_save_bytes(io, values[i].value, size) || memchr(values[i].value, 0, size)) return false;
        if (reading) values[i].value[size] = 0;
    }
    if (*count) {
        qa_cvar_view views[BASE_POLICY_VALUES] = {0};
        for (size_t i = 0; i < BASE_POLICY_VALUES; ++i) {
            views[i].value = values[i].value;
            memcpy(&views[i].number, &values[i].number, sizeof(values[i].number));
        }
        const qa_cvar_view *image_rows[] = {views, views + 1, views + 2};
        const qa_cvar_view *model_rows[] = {views + 3, views + 4, views + 5, views + 6, views + 7};
        qa_scene_image_policy images[3]; frontend_model_policy models;
        if (!image_policy_rows(image_rows, images, io->error) || !model_policy_rows(model_rows, &models, io->error)) return false;
    }
    return true;
}
bool frontend_shared_resource_policy_live_checkpoint(const qa_frontend *f, qa_buffer *out, qa_error *error)
{
    const struct frontend_live_resource_policy *owner = f ? f->live_resource_policy : NULL;
    if (!f || !f->application || !out || out->data || out->size ||
        (owner && (owner->frontend != f || owner->application != f->application ||
            owner->registry != qa_application_cvars(f->application) || owner->busy || owner->pending)))
        return policy_fail(error, "Resource recipe capture requires its returned publication owner");
    policy_scalar absent[POLICY_VALUES] = {0}; size_t count = owner ? owner->applied_count : 0;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, NULL, error)) return false;
    bool ok = recipe_fields(&io, owner ? (policy_scalar *)owner->applied : absent, &count) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_shared_resource_policy_live_restore(qa_frontend *f, qa_bytes bytes, qa_error *error)
{
    if (!f || !f->application || f->live_resource_policy || !f->source_restoring)
        return policy_fail(error, "Resource recipe import requires its actual detached frontend");
    struct frontend_live_resource_policy *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining decoded resource recipe");
    qa_source_save_io io;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error);
    if (ok) ok = recipe_fields(&io, owner->applied, &owner->applied_count) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        owner->frontend = f; owner->application = f->application; owner->registry = qa_application_cvars(f->application);
        f->live_resource_policy = owner;
    } else {
        for (size_t i = 0; i < POLICY_VALUES; ++i) free(owner->applied[i].value);
        free(owner);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid published resource recipe");
    }
    return ok;
}
