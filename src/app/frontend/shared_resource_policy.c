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
    qa_bytes input = {(const unsigned char *)rows[4]->value, strlen(rows[4]->value)};
    size_t cursor = 0, start = SIZE_MAX, end = 0;
    uint32_t scalar;
    while (cursor < input.size) {
        size_t begin = cursor;
        if (!qa_utf8_next(input, &cursor, &scalar))
            return policy_fail(error, "r_model_distance must contain valid UTF-8");
        if (!qa_unicode_whitespace(scalar)) {
            if (start == SIZE_MAX) start = begin;
            end = cursor;
        }
    }
    const unsigned char *text = input.data + (start == SIZE_MAX ? 0 : start);
    size_t length = start == SIZE_MAX ? 0 : end - start;
    static const char source[] = "source";
    policy.source_distance = length == sizeof(source) - 1;
    for (size_t i = 0; policy.source_distance && i < length; ++i) {
        unsigned char c = text[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)source[i]) policy.source_distance = false;
    }
    if (!policy.source_distance) {
        if (!qa_parse_ecmascript_number((qa_bytes){text, length}, &policy.distance, error) || !isfinite(policy.distance))
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
static bool image_policy_rows(const qa_cvar_view *const rows[3], qa_scene_image_policy out[3], qa_error *error)
{
    for (unsigned i = 0; i < 3; ++i)
        if (!rows[i]) return policy_fail(error, "Image policy lacks an actual shared ENGINE declaration");
    double level = rows[0]->number, mask = rows[1]->number;
    int32_t priority = level > 1 ? 2 : level >= 1 ? 1 : 0;
    double bits = isfinite(mask) && mask != 0 ? fmod(trunc(mask), 4294967296.0) : 0;
    if (bits < 0) bits += 4294967296.0;
    qa_scene_image_policy policy;
    if (!qa_scene_image_policy_controls(priority, (uint32_t)bits, rows[2]->value, &policy, error)) return false;
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
struct frontend_shared_resource_policy {
    qa_frontend *frontend;
    qa_application *application;
    qa_cvars *registry;
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
    policy_scalar scalars[sizeof(policy_names) / sizeof(policy_names[0])];
    size_t scalar_count;
    bool begun, source_profile, source_restart, children_entered, children_prepared, sealed, published;
};
static bool scalar_current(const frontend_shared_resource_policy *ticket, bool sealed)
{
    if (!ticket || ticket->frontend->application != ticket->application ||
        ticket->frontend->q1_sky != ticket->sky_owner ||
        (ticket->client && !qa_application_client_prepare_associated(ticket->application, ticket->client)) ||
        (ticket->video ? (!frontend_video_guests_parent_is(ticket->frontend, ticket->video) ||
            qa_application_cvars(ticket->application) != ticket->registry ||
            ticket->frontend->display != ticket->display || ticket->frontend->cpu != ticket->cpu ||
            ticket->frontend->gl != ticket->gl || ticket->frontend->source_color != ticket->color) :
            (qa_cvars_edit_registry(ticket->edit) != ticket->registry ||
            !(sealed ? qa_cvars_edit_ready_is(ticket->edit) :
                qa_cvars_edit_returned_is(ticket->edit, ticket->registry))))) return false;
    for (size_t i = 0; i < ticket->scalar_count; ++i) {
        const qa_cvar_view *row = ticket->video ?
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
    frontend_shared_resource_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining shared resource preparation");
    ticket->frontend = f; ticket->application = f->application;
    ticket->registry = qa_cvars_edit_registry(edit); ticket->edit = edit;
    ticket->client = client;
    ticket->sky_owner = f->q1_sky;
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
bool frontend_shared_resource_policy_restart_prepare(qa_frontend *f, const frontend_video_guests *video,
    frontend_shared_resource_policy **out, qa_error *error)
{
    if (!f || !f->application || !out || *out || !frontend_video_guests_parent_is(f, video) ||
        frontend_config_store_shared_pending(f->config_store))
        return policy_fail(error, "Source restart refresh requires its real video ticket and committed ENGINE values");
    frontend_shared_resource_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining renderer restart resources");
    ticket->frontend = f; ticket->application = f->application;
    ticket->registry = qa_application_cvars(f->application); ticket->sky_owner = f->q1_sky;
    ticket->video = video; ticket->display = f->display; ticket->cpu = f->cpu; ticket->gl = f->gl;
    ticket->color = f->source_color; ticket->scalar_count = BASE_POLICY_VALUES;
    *out = ticket;
    bool ok = frontend_resource_inventory_collect_video(f, video, &ticket->inventory, error) && policy_allocate(ticket, error);
    for (size_t i = 0; ok && i < ticket->material_count; ++i)
        if (qa_material_library_has_source_profile(frontend_resource_inventory_library_at(ticket->inventory, i)))
            ticket->source_profile = true;
    for (size_t i = 0; ok && i < ticket->bank_count; ++i)
        if (qa_scene_source_q3_white(frontend_resource_inventory_images_at(ticket->inventory, i)))
            ticket->source_restart = true;
    ticket->source_restart = ticket->source_restart || ticket->source_profile || f->source_color != NULL;
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
    bool ok = (ticket->video ? frontend_image_policy_read(f, images, error) : frontend_image_policy_edit_read(edit, images, error)) &&
        (ticket->video ? frontend_model_policy_read(f, &models, error) : frontend_model_policy_edit_read(edit, &models, error)) &&
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
        if (ok && start) ok = frontend_material_movies_library_owner(material->owner, &material->movies, error) &&
            frontend_material_movies_policy_prepare(material->movies, frontend_shared_resource_policy_images(ticket,
                qa_material_library_resource_owner(material->owner)), material->ticket, &material->movie_ticket, error);
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
    if (ok) ok = frontend_visual_policy_prepare(f, &models, ticket->models, ticket->model_count, &ticket->visuals, error) &&
        frontend_visual_registered_model_policy_prepare(f, &models, ticket->assets, ticket->asset_count, ticket->models, ticket->model_count, error) &&
        frontend_native_q2_image_policy_prepare(f, ticket->banks, ticket->bank_count, &ticket->q2, error) &&
        frontend_event_image_policy_prepare(f, ticket->banks, ticket->bank_count, &ticket->events, error) &&
        frontend_remote_q1_sky_policy_prepare(f, ticket->banks, ticket->bank_count, &ticket->remote_q1_sky, error) &&
        frontend_remote_q2_image_policy_prepare(f, ticket->banks, ticket->bank_count, &ticket->remote_q2_images, error) &&
        (!ticket->sky_owner || frontend_q1_sky_policy_prepare(ticket->sky_owner, ticket->banks,
            ticket->bank_count, &ticket->sky, error)) &&
        scalar_current(ticket, false) && frontend_resource_inventory_current(ticket->inventory);
    if (ok) ticket->children_prepared = true;
    else if (error && error->code == QA_OK) policy_fail(error, "Prepared resource source or canonical values changed");
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
    if (!frontend_resource_inventory_seal(ticket->inventory, error)) return false;
    ticket->sealed = true; return frontend_shared_resource_policy_ready_is(ticket);
}
static bool policy_ready_is(const frontend_shared_resource_policy *ticket, bool consuming)
{
    if (!ticket || !ticket->sealed || ticket->published || !scalar_current(ticket, true) ||
        !(consuming ? frontend_resource_inventory_consume_ready_is(ticket->inventory) :
            frontend_resource_inventory_ready_is(ticket->inventory))) return false;
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
    for (size_t i = 0; i < ticket->world_count; ++i) qa_scene_world_image_policy_publish(ticket->worlds[i].ticket);
    for (size_t i = 0; i < ticket->model_count; ++i) qa_scene_model_image_policy_publish(ticket->models[i].ticket);
    for (size_t i = 0; i < ticket->font_count; ++i) qa_font_resource_policy_publish(ticket->fonts[i].ticket);
    frontend_visual_policy_publish(ticket->visuals);
    frontend_native_q2_image_policy_publish(ticket->q2); frontend_event_image_policy_publish(ticket->events);
    frontend_remote_q1_sky_policy_publish(ticket->remote_q1_sky);
    frontend_remote_q2_image_policy_publish(ticket->remote_q2_images);
    if (ticket->sky) frontend_q1_sky_policy_publish(ticket->sky);
    ticket->published = true;
}
void frontend_shared_resource_policy_publish(frontend_shared_resource_policy *ticket)
{
    if (frontend_shared_resource_policy_ready_is(ticket)) policy_publish(ticket);
}
void frontend_shared_resource_policy_consume(frontend_shared_resource_policy *ticket)
{
    if (frontend_shared_resource_policy_consume_ready_is(ticket)) policy_publish(ticket);
}
bool frontend_shared_resource_policy_finish(frontend_shared_resource_policy **ticket, qa_error *error)
{ return policy_cleanup(ticket, true, error); }
bool frontend_shared_resource_policy_abort(frontend_shared_resource_policy **ticket, qa_error *error)
{ return policy_cleanup(ticket, false, error); }
