#include "equipment_media_private.h"
#include "view_settings.h"
#include "equipment_held_stock.h"
#include "../../presentation/q3_native/weapon.h"
#include "equipment_icon.h"
#include "../application/equipment_runtime.h"
#include "qa/application_equipment_content.h"
#include "../application/guest_q3_components.h"
#include "shared_resource_policy.h"
#include "scene_refs.h"
#include "renderer_materials.h"
#include "qa/media_library_prepare.h"
#include "qa/q3_assets_custody.h"
#include "qa/q3_assets_save.h"
#include "qa/game_q2_items.h"
#include "qa/unified_frame_player.h"
#include <stdio.h>
#include <math.h>

typedef struct native_hud_icon_spec {
    qa_unified_inventory_icon_kind kind;
    qa_string_id path, lump;
} native_hud_icon_spec;
typedef struct native_hud_icons {
    qa_strings *strings;
    frontend_visual_owner_view owner;
    uint64_t lookup_generation;
    native_hud_icon_spec declarations[3];
    frontend_equipment_hud_icons pictures;
} native_hud_icons;

static void native_icons_release(native_hud_icons *icons)
{
    qa_material_library_destroy(icons->owner.materials);
    qa_scene_resources_destroy(icons->owner.images);
    qa_vfs_destroy(icons->owner.mounts);
    qa_strings_destroy(icons->strings);
    *icons = (native_hud_icons){0};
}

typedef struct held_attachment_source {
    qa_vfs *files;
    const char *path;
    qa_ruleset_id rules;
} held_attachment_source;
static bool held_attachment_admit(void *context, qa_scene_model *scene, qa_error *error)
{
    const held_attachment_source *carrier = context;
    qa_vfs *files = carrier->files;
    const char *model_path = carrier->path;
    frontend_model_source source;
    if (qa_scene_model_replacement_description(scene)) {
        if (!frontend_scene_model_source_read(scene, QA_SCENE_MODEL_CONTENT_SOURCE, &source))
            return frontend_held_stock_q2_attachment(carrier->path, carrier->rules, scene, error);
        files = (qa_vfs *)source.files;
        model_path = qa_resource_path(source.resource);
    }
    static const char suffix[] = ".attachment.json";
    size_t length = strlen(model_path);
    if (length > SIZE_MAX - sizeof(suffix))
        return frontend_fail(error, QA_ERROR_MEMORY, "Carrier attachment path exceeds address space");
    char *path = malloc(length + sizeof(suffix));
    if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "Preparing carrier attachment path");
    memcpy(path, model_path, length);
    memcpy(path + length, suffix, sizeof(suffix));
    qa_resource *resource = NULL;
    qa_error local = {0};
    bool ok = qa_vfs_acquire(files, path, &resource, NULL, &local);
    free(path);
    if (!ok && local.code == QA_ERROR_NOT_FOUND)
        return frontend_held_stock_q2_attachment(carrier->path, carrier->rules, scene, error);
    if (!ok) { if (error) *error = local; return false; }
    frontend_held_attachment attachment = {0};
    ok = frontend_held_attachment_read(qa_resource_bytes(resource), &attachment, error);
    if (ok) ok = qa_scene_model_grip_bind(scene, attachment.joint, attachment.reference_frame,
        attachment.joint ? NULL : attachment.vertices, &attachment.grip, error);
    frontend_held_attachment_free(&attachment);
    qa_resource_release(resource);
    return ok;
}
bool frontend_held_attachment_prepare(qa_vfs *files, qa_ruleset_id rules, const char *model_path,
    qa_scene_model *scene, qa_error *error)
{
    held_attachment_source source = {files, model_path, rules};
    return qa_scene_model_visit(scene, held_attachment_admit, &source, error);
}

static bool carrier_grip_present(void *context, qa_scene_model *model, qa_error *error)
{
    (void)error;
    bool *present = context;
    *present |= qa_scene_model_grip_defined(model);
    return true;
}

bool frontend_held_carrier_scene_prepare(qa_scene_model *model, const qa_scene_model_input *pose,
    qa_string_id preferred_tag, frontend_held_carrier *out, bool *present, qa_error *error)
{
    *out = (frontend_held_carrier){0};
    *present = false;
    if (!model) return true;
    qa_model_tag tag;
    bool tagged = preferred_tag && qa_scene_model_lerp_tag(model, preferred_tag,
        pose->old_frame, pose->frame, 1 - pose->back_lerp, &tag);
    if (!tagged && !qa_scene_model_visit(model, carrier_grip_present, present, error)) return false;
    *present |= tagged;
    if (*present) *out = (frontend_held_carrier){.model = model, .pose = *pose,
        .tag = tagged ? preferred_tag : QA_STRING_NONE};
    return true;
}

bool frontend_held_carrier_prepare(const qa_q3_presentation_assets *assets,
    const qa_q3_ref_entity *parent, double seconds, frontend_held_carrier *out,
    bool *animated, qa_error *error)
{
    *out = (frontend_held_carrier){0};
    *animated = false;
    if (parent->kind != QA_Q3_REF_MODEL || parent->model <= 0) return true;
    qa_model_tag tag;
    bool tagged;
    if (!qa_q3_presentation_tag(assets, parent->model,
        qa_q3_presentation_tag_id(assets, QA_Q3_TAG_WEAPON), parent->old_frame,
        parent->frame, 1 - parent->back_lerp, &tag, &tagged, error)) return false;
    if (tagged) return true;
    qa_q3_asset_model_holder holder;
    if (!qa_q3_assets_model_holder(assets, (size_t)parent->model - 1, &holder, error)) return false;
    if (!holder.present || !holder.scenes[0]) return true;
    qa_scene_model_input pose = {.family = holder.provider.family,
        .frame = (uint32_t)parent->frame, .old_frame = (uint32_t)parent->old_frame,
        .skin = (uint32_t)parent->skin,
        .back_lerp = parent->back_lerp, .previous_origin = parent->old_origin,
        .flags = (uint32_t)parent->flags, .seconds = seconds};
    pose.transform = qa_q3_ref_model_transform(parent);
    return frontend_held_carrier_scene_prepare(holder.scenes[0], &pose,
        QA_STRING_NONE, out, animated, error);
}

bool frontend_held_carrier_sample(const frontend_held_carrier *carrier,
    const qa_scene_view *view, qa_scene_frame *frame, qa_model_transform *out,
    bool *visible, qa_error *error)
{
    qa_scene_model_input pose = carrier->pose;
    pose.view = *view;
    qa_model_transform socket;
    if (carrier->tag) {
        qa_model_tag tag;
        *visible = qa_scene_model_lerp_tag(carrier->model, carrier->tag,
            pose.old_frame, pose.frame, 1 - pose.back_lerp, &tag);
        if (!*visible) return true;
        qa_model_transform_identity(&socket);
        memcpy(socket.origin, tag.origin, sizeof(socket.origin));
        memcpy(socket.axes, tag.axes, sizeof(socket.axes));
        qa_model_transform_compose(&pose.transform, &socket, out);
        return true;
    }
    if (!qa_scene_model_grip_sample(carrier->model, &pose, frame, &socket, visible, error)) return false;
    if (!*visible) return true;
    qa_model_transform hand = q3n_weapon_hand_grip(), alignment;
    if (!qa_model_attachment_align(&hand, &socket, &alignment))
        return frontend_fail(error, QA_ERROR_FORMAT, "Animated carrier cannot align its Q3 hand registration");
    qa_model_transform_compose(&pose.transform, &alignment, out);
    return true;
}

void frontend_held_carrier_ref(const qa_model_transform *carrier,
    const qa_q3_ref_entity *source, qa_q3_ref_entity *out)
{
    qa_model_transform local = qa_q3_ref_model_transform(source), world;
    qa_model_transform_compose(carrier, &local, &world);
    *out = *source;
    out->origin = qa_v3(world.origin[0], world.origin[1], world.origin[2]);
    out->old_origin = out->origin;
    for (size_t i = 0; i < 3; ++i) {
        out->axis[i] = qa_v3(world.axes[i][0] * world.scale[i],
            world.axes[i][1] * world.scale[i], world.axes[i][2] * world.scale[i]);
        out->non_normalized_axes |= world.scale[i] != 1;
    }
}

bool frontend_equipment_media_namespace_current(const qa_frontend *frontend,
    const frontend_equipment_media *row)
{
    if (!frontend || !frontend->application || row->strings != qa_session_strings(
        qa_application_session(frontend->application))) return false;
    if(row->source_slot) {
        application_q3_component_publication source;bool found=false;
        if(row->gear_namespace||row->gear_service_owner||
            !application_q3_components_checkpoint_publication_read(frontend->application,row->provider,&source,&found,NULL)||
            !found||source.generation!=row->source_generation||!source.product||source.product->family!=row->family||
            !qa_vfs_lookup_equal(source.content,row->owner.mounts))return false;
        for(size_t i=0;i<application_q3_component_item_definition_count(source.game);++i) {
            qa_item_admission item;qa_bytes icon,held;
            if(!application_q3_component_item_definition(source.game,i,&item,&icon,&held,NULL))return false;
            if(item.definition.item==row->item)return held.size==row->source_held.size&&
                (!held.size||!memcmp(held.data,row->source_held.data,held.size))&&
                icon.size==row->source_icon.size&&(!icon.size||!memcmp(icon.data,row->source_icon.data,icon.size));
        }
        return false;
    }
    if (!row->gear_namespace) {
        if (row->gear_service_owner) return false;
        if (row->family != QA_GAME_Q3) return true;
        const qa_q3_model_names *names;
        if (!qa_application_equipment_q3_metadata_read(
                frontend->application, row->provider, &names, NULL)) return false;
        for (unsigned weapon = 1; weapon < QA_Q3_WEAPON_COUNT; ++weapon)
            if (names->weapon_items[weapon] == row->item && names->weapons[weapon] == row->view_path) return true;
        return false;
    }
    qa_application_equipment_content content;
    if (row->family != QA_GAME_Q3 ||
        !qa_application_equipment_content_read(frontend->application, row->gear_namespace, &content, NULL) ||
        content.selected_owner != row->provider || content.service_owner != row->gear_service_owner ||
        !qa_vfs_lookup_equal(content.files, row->owner.mounts)) return false;
    application_equipment_runtime *runtime = frontend->application->equipment_runtime;
    application_equipment_runtime_source source;
    return application_equipment_runtime_source_read(runtime, content.selected_owner, &source, NULL) &&
        source.gear_owner == row->gear_namespace && source.weapon_item == row->item && source.definition &&
        source.view_model && source.view_model == row->view_path;
}

static bool movie_current(void *context,const frontend_material_movie_source *source)
{
    frontend_equipment_media *row=context;
    return row&&row->source_slot&&row->frontend&&source&&source->frontend==row->frontend&&
        source->context==row&&source->current==movie_current&&source->files==row->owner.mounts&&
        source->images==row->owner.images&&source->materials==row->owner.materials&&source->media==row->media&&
        frontend_equipment_media_namespace_current(row->frontend,row);
}

frontend_material_movie_source frontend_equipment_media_movie_source(frontend_equipment_media *row)
{
    frontend_material_movie_source source={.frontend=row->frontend,.files=row->owner.mounts,
        .images=row->owner.images,.materials=row->owner.materials,.media=row->media,
        .context=row,.current=movie_current};
    if (row->movies) source.identity=frontend_material_movies_identity(row->movies);
    else {
        source.identity=frontend_material_movie_provider_identity(row->frontend,
            FRONTEND_MOVIE_EQUIPMENT,row->provider,row->family);
        source.identity.item=row->item?qa_strings_cstr(row->strings,row->item):NULL;
        source.identity.model=row->source_slot?row->declaration.path:
            row->view_path?qa_strings_cstr(row->strings,row->view_path):NULL;
    }
    return source;
}

bool frontend_equipment_media_dispose(frontend_equipment_media *media,qa_error *error)
{
    if(media->movies) {
        frontend_material_movie_source source=frontend_equipment_media_movie_source(media);
        if(!media->frontend->source_restoring&&
            !frontend_renderer_materials_adopt_movies(media->frontend,&source,&media->movies,&media->media,error))return false;
        if(!frontend_material_movies_destroy(&media->movies,error))return false;
    }
    qa_media_library_destroy(media->media);media->media=NULL;
    qa_scene_model_destroy(media->held_scene);
    frontend_held_model_free(&media->held);
    frontend_model_release(media->held_lease);
    if(media->source_model){qa_model_free(media->source_model);free(media->source_model);}
    frontend_held_declaration_free(&media->declaration);
    qa_resource_release((qa_resource *)media->view.resource);
    qa_resource_release((qa_resource *)media->held_parent.resource);
    if(media->source_slot){
        qa_material_library_destroy(media->owner.materials);
        qa_scene_resources_destroy(media->owner.images);
        qa_vfs_destroy(media->owner.mounts);
    }
    qa_buffer_free(&media->source_held);
    qa_buffer_free(&media->source_icon);
    qa_resource_release(media->icon_source);
    qa_strings_destroy(media->strings); free(media);return true;
}

bool frontend_equipment_idle(const qa_frontend *frontend)
{
    if (!frontend || !frontend->equipment) return true;
    if (frontend->equipment->admitting) return false;
    for (const frontend_equipment_media *row = frontend->equipment->media; row; row = row->next)
        if (row->users || (row->held_scene && !qa_scene_model_idle(row->held_scene)) ||
            (row->movies&&!frontend_material_movies_idle(row->movies)) ||
            (row->media&&!qa_media_library_idle(row->media))) return false;
    return true;
}

bool frontend_equipment_retire(qa_frontend *frontend, qa_error *error)
{
    if (!frontend_equipment_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment media has an active real scene or admission");
    if (!frontend || !frontend->equipment) return true;
    frontend->equipment->admitting=true;
    while(frontend->equipment->media) {
        frontend_equipment_media *row=frontend->equipment->media,*next=row->next;
        if(!frontend_equipment_media_dispose(row,error)){frontend->equipment->admitting=false;return false;}
        frontend->equipment->media=next;
    }
    frontend->equipment->tail = NULL;
    for (size_t i = 0; i < frontend->equipment->held_lookups.count; ++i) {
        frontend_held_lookup *lookup = (frontend_held_lookup *)(uintptr_t)
            frontend->equipment->held_lookups.entries[i].value;
        qa_vfs_destroy(lookup->files);
        qa_strings_destroy(lookup->strings);
    }
    qa_id_index_destroy(&frontend->equipment->held_lookups);
    for (size_t i = 0; i < frontend->equipment->native_hud_icons.count; ++i)
        native_icons_release((native_hud_icons *)(uintptr_t)
            frontend->equipment->native_hud_icons.entries[i].value);
    qa_id_index_destroy(&frontend->equipment->native_hud_icons);
    qa_id_index_destroy(&frontend->equipment->q1_hud_pictures);
    frontend->equipment->q1_hud_strings = NULL;
    qa_arena_destroy(&frontend->equipment->held_storage);
    frontend->equipment->admitting=false;
    return true;
}

bool frontend_equipment_media_prune(qa_frontend *f,qa_error *error)
{
    if(!f||!f->application||f->source_restoring||f->capture||f->resource_inventory||
        !frontend_equipment_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment cache retirement requires its returned live media owners");
    if(!f->equipment)return true;
    f->equipment->admitting=true;
    bool okay=true;
    frontend_equipment_media **slot=&f->equipment->media;
    while(*slot) {
        frontend_equipment_media *row=*slot;
        if(!row->source_slot){slot=&row->next;continue;}
        application_q3_component_publication source;bool found=false;
        if(!application_q3_components_checkpoint_publication_read(f->application,row->provider,&source,&found,error)) {
            okay=false;break;
        }
        if(found&&source.generation==row->source_generation&&(row->bound||row->restoring)) {
            if(!frontend_equipment_media_namespace_current(f,row)) {
                okay=frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment cache changed its retained component declaration");break;
            }
            slot=&row->next;continue;
        }
        frontend_equipment_media *next=row->next;
        if(!frontend_equipment_media_dispose(row,error)){okay=false;break;}
        *slot=next;
    }
    f->equipment->tail=NULL;
    for(frontend_equipment_media *row=f->equipment->media;row;row=row->next)f->equipment->tail=row;
    f->equipment->admitting=false;return okay;
}

void frontend_equipment_destroy(qa_frontend *frontend)
{
    if (!frontend || !frontend->equipment) return;
    qa_error error = {0};
    if (!frontend_equipment_retire(frontend, &error)) return;
    free(frontend->equipment); frontend->equipment = NULL;
}

static bool held_lookup(qa_frontend *frontend, const qa_application_equipment_view *view,
    qa_vfs *files, const char **path, bool *found, qa_error *error)
{
    if (!frontend->equipment) {
        frontend->equipment = calloc(1, sizeof(*frontend->equipment));
        if (!frontend->equipment)
            return frontend_fail(error, QA_ERROR_MEMORY, "Allocating retained equipment media owner");
    }
    frontend_equipment *equipment = frontend->equipment;
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    equipment->held_lookups.storage = &equipment->held_storage;
    qa_id_index_entry *entry = qa_id_index_find(&equipment->held_lookups,
        view->view_model, view->provider);
    frontend_held_lookup *lookup = entry ? (frontend_held_lookup *)(uintptr_t)entry->value : NULL;
    if (!lookup) {
        if (!qa_id_index_reserve(&equipment->held_lookups, 1, error)) return false;
        lookup = qa_arena_alloc(&equipment->held_storage, sizeof(*lookup),
            _Alignof(frontend_held_lookup), error);
        if (!lookup) return false;
        *lookup = (frontend_held_lookup){0};
    }
    uint64_t generation = qa_vfs_lookup_generation(files);
    if (lookup->strings != strings || lookup->files != files ||
        lookup->lookup_generation != generation) {
        const char *retained_path = lookup->path;
        if (lookup->strings != strings) {
            const char *model_path = qa_strings_cstr(strings, view->view_model);
            size_t length = strlen(model_path);
            if (length > SIZE_MAX - sizeof(".held.json"))
                return frontend_fail(error, QA_ERROR_MEMORY, "Equipment declaration path exceeds address space");
            char *copy = qa_arena_alloc(&equipment->held_storage,
                length + sizeof(".held.json"), 1, error);
            if (!copy) return false;
            memcpy(copy, model_path, length);
            memcpy(copy + length, ".held.json", sizeof(".held.json"));
            retained_path = copy;
        }
        if (lookup->files != files && !qa_vfs_retain(files, error)) return false;
        if (lookup->strings != strings) {
            qa_strings_retain(strings);
            qa_strings_destroy(lookup->strings);
        }
        if (lookup->files != files) qa_vfs_destroy(lookup->files);
        bool archive_only = true;
        for (size_t i = 0; i < qa_vfs_mount_count(files); ++i) {
            qa_vfs_mount_info mount;
            qa_vfs_mount_at(files, i, &mount);
            if (!mount.is_archive) { archive_only = false; break; }
        }
        *lookup = (frontend_held_lookup){.strings=strings, .files=files,
            .path=retained_path, .lookup_generation=generation, .archive_only=archive_only};
    }
    if (!entry) qa_id_index_insert(&equipment->held_lookups, view->view_model,
        view->provider, (size_t)(uintptr_t)lookup);
    /* Loose mounts observe later file appearance without advancing the lookup
     * policy token. Only admitted archive membership permits reusing a probe. */
    if (!lookup->probed || !lookup->archive_only) {
        bool present = false;
        uint64_t size;
        if (!qa_vfs_probe(files, lookup->path, &present, &size, error)) return false;
        lookup->found = present;
        lookup->probed = true;
    }
    *path = lookup->path;
    *found = lookup->found;
    return true;
}

static bool held_declaration(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_equipment_media *row, qa_error *error)
{
    const char *model_path = qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), view->view_model);
    const char *path = NULL;
    bool found = false;
    bool ok = held_lookup(frontend, view, row->owner.mounts, &path, &found, error);
    if (ok && found) {
        qa_resource *source = NULL;
        ok = qa_vfs_acquire(row->owner.mounts, path, &source, NULL, error);
        if (ok) ok = frontend_held_declaration_read(source, &row->declaration, error);
        qa_resource_release(source);
    } else if (ok) {
        const char *item = view->item ?
            qa_strings_cstr(qa_session_strings(qa_application_session(frontend->application)), view->item) : NULL;
        ok = frontend_held_stock(view->family, row->owner.rules, model_path, item, &row->declaration, &found, error);
        if (ok && !found)
            ok = frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected equipment has no authored held declaration");
    }
    return ok;
}

static bool held_model(qa_frontend *frontend, frontend_equipment_media *row, qa_error *error)
{
    if (row->declaration.none) return true;
    const char *path = row->declaration.path;
    bool found = false;
    uint64_t size;
    if (!qa_vfs_probe(row->owner.mounts, path, &found, &size, error)) return false;
    if (!found && row->declaration.fallback) {
        path = row->declaration.fallback;
        if (!qa_vfs_probe(row->owner.mounts, path, &found, &size, error)) return false;
    }
    if (!found) return frontend_fail(error, QA_ERROR_NOT_FOUND, "Actual source held model is absent");
    qa_string_id path_name;
    if (!qa_strings_intern_cstr(row->strings,path,&path_name,error) ||
        !frontend_visual_model_acquire(frontend, row->provider, row->family, path_name, NULL,
            &row->held_parent, error)) return false;
    qa_resource_retain((qa_resource *)row->held_parent.resource);
    if (row->family == QA_GAME_Q1 && !row->declaration.source) {
        const qa_model *model = row->held_parent.model;
        if (model->format != QA_MODEL_MDL || model->mesh_count != 1 || model->frame_count != 143 ||
            model->meshes[0].vertex_count != 212 || model->meshes[0].triangle_count != 408)
            return frontend_fail(error, QA_ERROR_FORMAT, "Original held subset requires the stock Q1 player layout");
    }
    if (row->family == QA_GAME_Q2 && !row->declaration.source) {
        bool known;
        if (!frontend_held_stock_q2_grip(row->held_parent.path, row->owner.rules, row->held_parent.resource,
                row->held_parent.model,
                &row->declaration.grip, &known, error)) return false;
    }
    if (!frontend_held_model_prepare(&row->declaration, row->held_parent.path, row->held_parent.resource,
            row->held_parent.model, &row->held, error)) return false;
    const qa_scene_image_options *options = qa_scene_model_image_options(row->held_parent.scene);
    if (!options) return frontend_fail(error, QA_ERROR_FORMAT, "Held parent has no actual scene image policy");
    return qa_scene_model_create(row->held.model, row->held_parent.path, row->owner.images, row->owner.materials,
        options, row->strings, &row->held_scene, error) &&
        qa_scene_model_source_resource_bind(row->held_scene, row->held_parent.resource, error);
}

static frontend_equipment_media *media_find(const qa_frontend *frontend,
    const qa_application_equipment_view *view)
{
    for (frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next)
        if (!row->source_slot && row->provider == view->provider && row->family == view->family &&
            row->item == view->item && row->gear_namespace == view->gear_namespace &&
            row->gear_service_owner == view->gear_service_owner &&
            row->strings == qa_session_strings(qa_application_session(frontend->application)) &&
            row->view_path == view->view_model && (view->family == QA_GAME_Q3 ||
                !view->view_source || row->view.resource == view->view_source)) return row;
    return NULL;
}

static bool prepare_media(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_held_declaration *authored, frontend_equipment_media **out, qa_error *error)
{
    if (!frontend || !view || !out || !view->selected || !view->view_model || !qa_strings_text(qa_session_strings(qa_application_session(frontend->application)), view->view_model).size ||
        (view->family == QA_GAME_Q3 && (!authored || !authored->source)) ||
        !qa_application_equipment_current(frontend->application, view) ||
        (frontend->equipment && frontend->equipment->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Foreign equipment media requires its actual selected source observation");
    for (const frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next)
        if (row->held_scene && !qa_scene_model_idle(row->held_scene))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment admission requires idle physical scenes");
    if (!frontend->equipment) {
        frontend->equipment = calloc(1, sizeof(*frontend->equipment));
        if (!frontend->equipment) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating retained equipment media owner");
    }
    frontend_equipment_media *existing = media_find(frontend, view);
    if (existing) { *out = existing; return true; }
    frontend_equipment_media *row = calloc(1, sizeof(*row));
    if (!row) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating physical selected equipment media row");
    row->provider = view->provider; row->family = view->family; row->item = view->item;
    row->gear_namespace = view->gear_namespace; row->gear_service_owner = view->gear_service_owner;
    row->strings = qa_session_strings(qa_application_session(frontend->application));
    qa_strings_retain(row->strings);
    row->view_path = view->view_model;
    frontend->equipment->admitting = true;
    bool ok = frontend_visual_media_acquire(frontend, view->provider, view->family, &row->owner, error);
    if (ok && view->family != QA_GAME_Q3)
        ok = frontend_visual_model_acquire(frontend, view->provider, view->family,
            view->view_model,
            view->view_source, &row->view, error);
    if (ok) {
        qa_resource_retain((qa_resource *)row->view.resource);
        if (authored) { row->declaration = *authored; *authored = (frontend_held_declaration){0}; }
        else ok = held_declaration(frontend, view, row, error);
        if (ok) ok = held_model(frontend, row, error);
    }
    if (ok && (!qa_application_equipment_current(frontend->application, view) ||
        !frontend_equipment_media_namespace_current(frontend, row)))
        ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment admission lost its actual selected source owner");
    frontend->equipment->admitting = false;
    if (!ok) { (void)frontend_equipment_media_dispose(row,error); return false; }
    row->bound = true;
    if (frontend->equipment->tail) frontend->equipment->tail->next = row;
    else frontend->equipment->media = row;
    frontend->equipment->tail = row;
    *out = row;
    return true;
}

bool frontend_equipment_media_prepare(qa_frontend *frontend, const qa_application_equipment_view *view,
    frontend_equipment_media **out, qa_error *error)
{ return prepare_media(frontend, view, NULL, out, error); }

static frontend_equipment_media *source_media_find(const qa_frontend *f,
    const qa_application_equipment_view *view)
{
    for (frontend_equipment_media *row = f->equipment ? f->equipment->media : NULL;
            row; row = row->next)
        if (row->source_slot && row->provider == view->provider && row->family == view->family &&
            row->item == view->item && row->source_generation == view->source_generation &&
            row->source_held.size == view->source_held.size &&
            (!view->source_held.size || !memcmp(row->source_held.data, view->source_held.data, view->source_held.size)) &&
            row->source_icon.size == view->source_icon.size &&
            (!view->source_icon.size || !memcmp(row->source_icon.data, view->source_icon.data, view->source_icon.size)))
            return row;
    return NULL;
}

static bool source_media_prepare(qa_frontend *f,const qa_application_equipment_view *view,
    frontend_equipment_media **out,bool *authored,qa_error *error)
{
    if(!f||!view||!out||*out||!authored||!view->source_slot||!view->selected||
        !qa_application_equipment_current(f->application,view)||f->resource_inventory||
        (f->equipment&&f->equipment->admitting))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source held admission requires its actual component binding");
    *authored=false;
    if(!view->source_held.size&&!view->source_icon.size)return true;
    for(frontend_equipment_media *row=f->equipment?f->equipment->media:NULL;row;row=row->next)
        if(row->held_scene&&!qa_scene_model_idle(row->held_scene))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source held admission retains an active scene operation");
    if(!f->equipment){f->equipment=calloc(1,sizeof(*f->equipment));
        if(!f->equipment)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining source held media owner");}
    frontend_equipment_media *existing = source_media_find(f, view);
    if (existing) {
        if (!existing->bound || !frontend_equipment_media_namespace_current(f, existing))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Retained held model lost its actual source declaration");
        *out=existing;*authored=true;return true;
    }
    frontend_equipment_media *row=calloc(1,sizeof(*row));
    if(!row)return frontend_fail(error,QA_ERROR_MEMORY,"Retaining declared component held model");
    row->source_slot=true;row->source_generation=view->source_generation;
    row->frontend=f;
    row->strings=qa_session_strings(qa_application_session(f->application));qa_strings_retain(row->strings);
    row->provider=view->provider;row->family=view->family;row->item=view->item;
    f->equipment->admitting=true;
    bool ok=true;
    if(view->source_held.size){row->source_held.data=malloc(view->source_held.size);
        ok=row->source_held.data!=NULL;
        if(ok){row->source_held.size=view->source_held.size;memcpy(row->source_held.data,view->source_held.data,view->source_held.size);
            ok=frontend_held_declaration_value(view->source_held,&row->declaration,error);}
        else frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source held declaration");}
    else row->declaration.none=true;
    if(ok&&view->source_icon.size){row->source_icon.data=malloc(view->source_icon.size);
        ok=row->source_icon.data!=NULL;
        if(ok){row->source_icon.size=view->source_icon.size;memcpy(row->source_icon.data,view->source_icon.data,view->source_icon.size);}
        else frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source icon declaration");}
    const char *instance = qa_application_provider_instance(f->application, view->provider);
    const qa_launch_instance *source_launch = instance ? qa_launch_snapshot_find(
        qa_application_launch(f->application), instance) : NULL;
    qa_ruleset_id rules = source_launch ? source_launch->selection.clock.kind : QA_RULESET_NETQUAKE;
    if (ok && !source_launch) {
        application_q3_component_publication component = {0};
        bool present = false;
        ok = application_q3_components_checkpoint_publication_read(f->application,
            view->provider, &component, &present, error);
        if (ok && present) rules = qa_product_ruleset(component.product, view->family);
    }
    row->owner=(frontend_visual_owner_view){.owner=view->provider,.family=view->family,.rules=rules};
    if(ok)row->owner.mounts=qa_vfs_clone(view->view_content,error);
    if(ok)row->owner.images=row->owner.mounts?qa_scene_resources_create(row->owner.mounts,error):NULL;
    if(ok)ok=row->owner.images&&frontend_image_policy_initialize(f,row->owner.images,error);
    if(ok)row->owner.materials=qa_material_library_create(row->owner.images,f->order,error);
    if(ok)ok=row->owner.materials!=NULL;
    if(ok)row->media=qa_media_library_create(row->owner.images,error);
    if(ok)ok=row->media!=NULL;
    if(ok){frontend_material_movie_source source=frontend_equipment_media_movie_source(row);
        ok=frontend_material_movies_create(&source,&row->movies,error);}
    qa_scene_image_options images={.family=row->owner.family,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.usage=QA_IMAGE_USAGE_SKIN,.transparent_index=-1};
    if(ok)ok=qa_material_library_load_scripts(row->owner.materials,row->owner.mounts,&images,error)&&
        frontend_material_remaps(f,row->owner.materials,error);
    if(ok&&view->source_icon.size)ok=frontend_equipment_icon_load(view->source_icon,row->owner.family,
        row->owner.mounts,row->owner.images,row->owner.materials,&row->icon,&row->icon_source,error);
    if(ok&&!row->declaration.none) {
        const char *path=row->declaration.path;bool found=false;uint64_t size;
        ok=qa_vfs_probe(row->owner.mounts,path,&found,&size,error);
        if(ok&&!found&&row->declaration.fallback){path=row->declaration.fallback;ok=qa_vfs_probe(row->owner.mounts,path,&found,&size,error);}
        qa_resource *resource=NULL;
        if(ok&&!found)ok=frontend_fail(error,QA_ERROR_NOT_FOUND,"Actual component held model is absent");
        if(ok)ok=qa_vfs_acquire(row->owner.mounts,path,&resource,NULL,error);
        row->held_parent.resource=resource;
        if(ok){row->source_model=calloc(1,sizeof(*row->source_model));
            if(!row->source_model)ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining component held model arrays");}
        if(ok)ok=qa_model_load(qa_resource_bytes(resource),row->source_model,error);
        qa_string_id name=QA_STRING_NONE;
        if(ok)ok=qa_strings_intern_cstr(row->strings,path,&name,error);
        row->held_parent.path=qa_strings_cstr(row->strings,name);row->held_parent.model=row->source_model;
        if(ok)ok=frontend_held_model_prepare(&row->declaration,path,resource,row->source_model,&row->held,error)&&
            qa_scene_model_create(row->held.model,path,row->owner.images,row->owner.materials,&images,row->strings,&row->held_scene,error)&&
            qa_scene_model_source_resource_bind(row->held_scene,resource,error);
    }
    if(ok)ok=qa_application_equipment_current(f->application,view)&&frontend_equipment_media_namespace_current(f,row);
    f->equipment->admitting=false;
    if(!ok){
        if(!frontend_equipment_media_dispose(row,error)) {
            if(f->equipment->tail)f->equipment->tail->next=row;else f->equipment->media=row;
            f->equipment->tail=row;
        }
        return false;
    }
    row->bound=true;
    if(f->equipment->tail)f->equipment->tail->next=row;else f->equipment->media=row;
    f->equipment->tail=row;*out=row;*authored=true;return true;
}

bool frontend_equipment_media_prepare_source_icon(qa_frontend *f,const qa_application_equipment_view *view,
    frontend_equipment_media **out,const qa_material **icon,qa_error *error)
{
    bool present=false;
    if(!icon||*icon||!source_media_prepare(f,view,out,&present,error))return false;
    *icon=present?(*out)->icon:NULL;return true;
}

typedef struct q1_hud_picture {
    const char *item, *classic, *wheel, *ammo;
} q1_hud_picture;
/* Anthology's common HUD uses actual id1/mission-pack Sbar and wwheel assets. */
static const q1_hud_picture q1_hud_pictures[] = {
    {"q1:weapon/axe", NULL, "axe", NULL},
    {"q1:weapon/shotgun", "shotgun", "shotgun1", "sb_shells"},
    {"q1:weapon/supershotgun", "sshotgun", "shotgun2", "sb_shells"},
    {"q1:weapon/nailgun", "nailgun", "nail1", "sb_nails"},
    {"q1:weapon/supernailgun", "snailgun", "nail2", "sb_nails"},
    {"q1:weapon/grenadelauncher", "rlaunch", "rocket1", "sb_rocket"},
    {"q1:weapon/rocketlauncher", "srlaunch", "rocket2", "sb_rocket"},
    {"q1:weapon/lightning", "lightng", "light", "sb_cells"},
    {"q1:weapon/hipnotic:laser", "laser", "ui_h_weapon_laser", "sb_cells"},
    {"q1:weapon/hipnotic:mjolnir", "mjolnir", "ui_h_weapon_mjolnir", "sb_cells"},
    {"q1:weapon/hipnotic:proximity", "prox", "ui_h_weapon_gren", "sb_rocket"},
    {"q1:weapon/rogue:lava-nailgun", "r_lava", "ui_r_weapon_lava", "r_ammolava"},
    {"q1:weapon/rogue:lava-supernailgun", "r_superlava", "ui_r_weapon_superlava", "r_ammolava"},
    {"q1:weapon/rogue:multi-grenade", "r_gren", "ui_r_weapon_gren", "r_ammomulti"},
    {"q1:weapon/rogue:multi-rocket", "r_multirock", "ui_r_weapon_multirock", "r_ammomulti"},
    {"q1:weapon/rogue:plasma", "r_plasma", "ui_r_weapon_plasma", "r_ammoplasma"},
    {"q1:weapon/mg3:laser", "laser", "ui_h_weapon_laser", "sb_cells"},
    {"q1:weapon/mg3:mjolnir", NULL, "axe", "sb_cells"}
};
static bool q1_hud_pictures_bind(qa_frontend *f, qa_error *error)
{
    if (!f->equipment) {
        f->equipment = calloc(1, sizeof(*f->equipment));
        if (!f->equipment)
            return frontend_fail(error, QA_ERROR_MEMORY, "Allocating retained equipment media owner");
    }
    frontend_equipment *equipment = f->equipment;
    qa_strings *strings = qa_session_strings(qa_application_session(f->application));
    if (equipment->q1_hud_strings == strings) return true;
    qa_id_index *pictures = &equipment->q1_hud_pictures;
    qa_id_index_clear(pictures);
    equipment->q1_hud_strings = NULL;
    pictures->storage = &equipment->held_storage;
    size_t count = sizeof(q1_hud_pictures) / sizeof(*q1_hud_pictures);
    if (!qa_id_index_reserve(pictures, count, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_string_id item;
        if (!qa_strings_intern_cstr(strings, q1_hud_pictures[i].item, &item, error)) return false;
        qa_id_index_insert(pictures, item, 0, i);
    }
    equipment->q1_hud_strings = strings;
    return true;
}
static native_hud_icons *native_icons_find(const qa_frontend *f, qa_actor_owner provider, qa_item_id item)
{
    qa_id_index_entry *entry = f->equipment ?
        qa_id_index_find(&f->equipment->native_hud_icons, item, provider) : NULL;
    return entry ? (native_hud_icons *)(uintptr_t)entry->value : NULL;
}
static bool native_icons_current(const qa_frontend *f, const native_hud_icons *icons,
    const frontend_visual_owner_view *owner)
{
    return icons && icons->strings == qa_session_strings(qa_application_session(f->application)) &&
        icons->owner.owner == owner->owner && icons->owner.family == owner->family &&
        icons->owner.mounts == owner->mounts && icons->owner.images == owner->images &&
        icons->owner.materials == owner->materials &&
        icons->lookup_generation == qa_vfs_lookup_generation(owner->mounts);
}
static bool native_icon_bind(qa_strings *strings, qa_unified_inventory_icon_kind kind,
    const char *path, const char *lump, native_hud_icon_spec *out, qa_error *error)
{
    *out = (native_hud_icon_spec){0};
    if (!path || !*path) return true;
    out->kind = kind;
    return qa_strings_intern_cstr(strings, path, &out->path, error) &&
        (!lump || !*lump || qa_strings_intern_cstr(strings, lump, &out->lump, error));
}
static bool native_icon_load(qa_strings *strings, const frontend_visual_owner_view *owner,
    const native_hud_icon_spec *spec, const qa_material **out, qa_error *error)
{
    if (!spec->path) return true;
    const char *path = qa_strings_cstr(strings, spec->path);
    const char *lump = spec->lump ? qa_strings_cstr(strings, spec->lump) : "";
    size_t a = strlen(path), b = strlen(lump);
    if (b > SIZE_MAX - 2 || a > SIZE_MAX - b - 2)
        return frontend_fail(error, QA_ERROR_MEMORY, "Native icon identity exceeds address space");
    qa_arena storage = {0};
    size_t key_bytes = a + b + 2;
    char *key = qa_arena_alloc(&storage, key_bytes, 1, error);
    bool okay = key && frontend_equipment_icon_key(path, lump, key, key_bytes, error);
    const qa_material *material = okay ? qa_material_find(owner->materials, key) : NULL;
    if (okay && !material) {
        const char *format = spec->kind == QA_UNIFIED_INVENTORY_ICON_SHADER ?
            "{\"kind\":\"shader\",\"name\":\"%s\"}" :
            spec->kind == QA_UNIFIED_INVENTORY_ICON_WAD_PICTURE ?
                "{\"kind\":\"wad-picture\",\"path\":\"%s\",\"lump\":\"%s\"}" :
                "{\"kind\":\"image\",\"path\":\"%s\"}";
        size_t fixed = strlen(format) + 1;
        if (a > SIZE_MAX - b || a + b > SIZE_MAX - fixed) {
            okay = frontend_fail(error, QA_ERROR_MEMORY, "Native icon declaration exceeds address space");
        } else {
            size_t bytes = a + b + fixed;
            char *declaration = qa_arena_alloc(&storage, bytes, 1, error);
            okay = declaration != NULL;
            if (okay) {
                if (spec->kind == QA_UNIFIED_INVENTORY_ICON_SHADER)
                    snprintf(declaration, bytes, "{\"kind\":\"shader\",\"name\":\"%s\"}", path);
                else if (spec->kind == QA_UNIFIED_INVENTORY_ICON_WAD_PICTURE)
                    snprintf(declaration, bytes, "{\"kind\":\"wad-picture\",\"path\":\"%s\",\"lump\":\"%s\"}", path, lump);
                else snprintf(declaration, bytes, "{\"kind\":\"image\",\"path\":\"%s\"}", path);
                qa_resource *source = NULL;
                qa_error local = {0};
                okay = frontend_equipment_icon_load(
                    (qa_bytes){(const uint8_t *)declaration, strlen(declaration)}, owner->family,
                    owner->mounts, owner->images, owner->materials, &material, &source, &local);
                qa_resource_release(source);
                if (!okay && local.code == QA_ERROR_NOT_FOUND) okay = true;
                else if (!okay && error) *error = local;
            }
        }
    }
    qa_arena_destroy(&storage);
    if (okay) *out = material;
    return okay;
}
static bool native_icons_admit(qa_frontend *f, const frontend_visual_owner_view *owner,
    qa_actor_owner provider, qa_item_id item, const native_hud_icon_spec declarations[3], qa_error *error)
{
    native_hud_icons *icons = native_icons_find(f, provider, item);
    if (native_icons_current(f, icons, owner)) return true;
    qa_id_index *index = &f->equipment->native_hud_icons;
    index->storage = &f->equipment->held_storage;
    if (!icons) {
        if (!qa_id_index_reserve(index, 1, error)) return false;
        icons = qa_arena_alloc(&f->equipment->held_storage, sizeof(*icons), _Alignof(native_hud_icons), error);
        if (!icons) return false;
        *icons = (native_hud_icons){0};
    }
    native_hud_icons candidate = {.lookup_generation = qa_vfs_lookup_generation(owner->mounts)};
    if (!qa_vfs_retain(owner->mounts, error)) return false;
    candidate.owner.mounts = owner->mounts;
    if (!qa_scene_resources_retain(owner->images, error)) goto failed;
    candidate.owner.images = owner->images;
    if (!qa_material_library_retain(owner->materials, error)) goto failed;
    candidate.owner = *owner;
    candidate.strings = qa_session_strings(qa_application_session(f->application));
    qa_strings_retain(candidate.strings);
    memcpy(candidate.declarations, declarations, sizeof(candidate.declarations));
    if (!native_icon_load(candidate.strings, owner, declarations, &candidate.pictures.weapon, error) ||
        !native_icon_load(candidate.strings, owner, declarations + 1, &candidate.pictures.selected_weapon, error) ||
        !native_icon_load(candidate.strings, owner, declarations + 2, &candidate.pictures.ammo, error)) goto failed;
    if (owner->family == QA_GAME_Q1 && !candidate.pictures.selected_weapon)
        candidate.pictures.selected_weapon = candidate.pictures.ammo;
    native_icons_release(icons);
    *icons = candidate;
    qa_id_index_entry *entry = qa_id_index_find(index, item, provider);
    if (entry) entry->value = (size_t)(uintptr_t)icons;
    else qa_id_index_insert(index, item, provider, (size_t)(uintptr_t)icons);
    return true;
failed:
    native_icons_release(&candidate);
    return false;
}
static bool q1_native_icons(qa_frontend *f, const frontend_visual_owner_view *owner,
    qa_actor_owner provider, qa_item_id item, const qa_product *product, qa_error *error)
{
    native_hud_icon_spec declarations[3] = {0};
    qa_strings *strings = qa_session_strings(qa_application_session(f->application));
    qa_id_index_entry *entry = qa_id_index_find(&f->equipment->q1_hud_pictures, item, 0);
    if (entry && product) {
        const q1_hud_picture *picture = q1_hud_pictures + entry->value;
        if (product->edition == QA_EDITION_RERELEASE) {
            for (size_t i = 0; i < 2; ++i) {
                char path[128];
                snprintf(path, sizeof(path), "gfx/weapons/%s%s_%u.lmp",
                    !strncmp(picture->wheel, "ui_", 3) ? "" : "ww_", picture->wheel, (unsigned)i + 1);
                if (!native_icon_bind(strings, QA_UNIFIED_INVENTORY_ICON_IMAGE, path, NULL, declarations + i, error)) return false;
            }
        } else if (picture->classic) {
            for (size_t i = 0; i < 2; ++i) {
                char lump[64];
                snprintf(lump, sizeof(lump), "%s%s", !strncmp(picture->classic, "r_", 2) ? "" :
                    i ? "inv2_" : "inv_", picture->classic);
                if (!native_icon_bind(strings, QA_UNIFIED_INVENTORY_ICON_WAD_PICTURE,
                    "gfx.wad", lump, declarations + i, error)) return false;
            }
        }
        if (picture->ammo && !native_icon_bind(strings, QA_UNIFIED_INVENTORY_ICON_WAD_PICTURE,
            "gfx.wad", picture->ammo, declarations + 2, error)) return false;
    }
    return native_icons_admit(f, owner, provider, item, declarations, error);
}
static bool q2_native_icons(qa_frontend *f, const frontend_visual_owner_view *owner,
    qa_actor_owner provider, const qa_q2_game *game, bool rerelease, qa_error *error)
{
    qa_strings *strings = qa_session_strings(qa_application_session(f->application));
    size_t count = qa_q2_item_count(game);
    for (size_t i = 0; i < count; ++i) {
        const qa_q2_item_definition *weapon = qa_q2_item_at(game, i);
        if (!weapon) continue;
        native_hud_icon_spec declarations[3] = {0};
        const qa_q2_item_definition *ammo = NULL;
        for (size_t j = 0; weapon->ammo && j < count; ++j) {
            const qa_q2_item_definition *candidate = qa_q2_item_at(game, j);
            if (candidate && candidate->item == weapon->ammo) { ammo = candidate; break; }
        }
        const qa_string_id icons[2] = {weapon->icon_id, ammo ? ammo->icon_id : QA_STRING_NONE};
        for (size_t j = 0; j < 2; ++j) {
            if (!icons[j]) continue;
            const char *name = qa_strings_cstr(strings, icons[j]);
            size_t length = strlen(name);
            if (length > SIZE_MAX - sizeof("pics/.pcx"))
                return frontend_fail(error, QA_ERROR_MEMORY, "Q2 icon path exceeds address space");
            qa_arena storage = {0};
            size_t bytes = length + sizeof("pics/.pcx");
            char *path = qa_arena_alloc(&storage, bytes, 1, error);
            bool okay = path && qa_scene_q2_picture_path(name, rerelease, path, bytes, error) &&
                native_icon_bind(strings, QA_UNIFIED_INVENTORY_ICON_IMAGE, path, NULL,
                    declarations + (j ? 2 : 0), error);
            qa_arena_destroy(&storage);
            if (!okay) return false;
        }
        declarations[1] = declarations[0];
        if (!native_icons_admit(f, owner, provider, weapon->item, declarations, error)) return false;
    }
    return true;
}
static bool q3_native_icons(qa_frontend *f, const frontend_visual_owner_view *owner,
    qa_actor_owner provider, const qa_q3_game *game, qa_error *error)
{
    const qa_q3_model_names *names;
    if (!qa_application_equipment_q3_metadata_read(f->application, provider, &names, NULL)) return true;
    size_t count;
    const qa_q3_item *items = qa_q3_items(names->product, &count);
    qa_strings *strings = qa_session_strings(qa_application_session(f->application));
    for (size_t i = 0; i < count; ++i) {
        const qa_q3_item *weapon = items + i;
        qa_item_id item = weapon->kind == QA_Q3_ITEM_WEAPON ? names->weapon_items[weapon->tag] :
            game ? qa_q3_item_identity(game, (uint32_t)i) : QA_STRING_NONE;
        if (!item) continue;
        native_hud_icon_spec declarations[3] = {0};
        if (!native_icon_bind(strings, QA_UNIFIED_INVENTORY_ICON_SHADER, weapon->icon, NULL, declarations, error)) return false;
        declarations[1] = declarations[0];
        for (size_t j = 0; weapon->kind == QA_Q3_ITEM_WEAPON && j < count; ++j) {
            const qa_q3_item *ammo = items + j;
            if (ammo->kind != QA_Q3_ITEM_AMMO || ammo->tag != weapon->tag) continue;
            if (!native_icon_bind(strings, QA_UNIFIED_INVENTORY_ICON_SHADER, ammo->icon, NULL, declarations + 2, error)) return false;
            break;
        }
        if (!native_icons_admit(f, owner, provider, item, declarations, error)) return false;
    }
    return true;
}
bool frontend_equipment_media_native_icons_prepare(qa_frontend *f,
    const qa_application_equipment_view *view, frontend_equipment_hud_icons *out, qa_error *error)
{
    if (!out || !f || !view || view->source_slot || !qa_application_equipment_current(f->application, view))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native HUD icons require their actual equipment source");
    *out = (frontend_equipment_hud_icons){0};
    if (!view->item) return true;
    if (!q1_hud_pictures_bind(f, error)) return false;
    frontend_visual_owner_view owner;
    if (!frontend_visual_media_acquire(f, view->provider, view->family, &owner, error)) return false;
    native_hud_icons *icons = native_icons_find(f, view->provider, view->item);
    if (!native_icons_current(f, icons, &owner)) {
        qa_id_index_cursor cursor = application_provider_lookup_begin(f->application,
            view->provider, APPLICATION_PROVIDER_PUBLISHED);
        application_provider *source = application_provider_lookup_next(f->application, &cursor);
        bool okay = true;
        if (view->family == QA_GAME_Q1)
            okay = q1_native_icons(f, &owner, view->provider, view->item, source ? source->product : NULL, error);
        else if (view->family == QA_GAME_Q2 && source && source->kind == APPLICATION_PROVIDER_Q2)
            okay = q2_native_icons(f, &owner, view->provider, source->state.q2,
                source->launch->selection.clock.kind == QA_RULESET_Q2_RERELEASE, error);
        else if (view->family == QA_GAME_Q3 && source && source->product && source->product->family == QA_GAME_Q3)
            okay = q3_native_icons(f, &owner, view->provider,
                source->kind == APPLICATION_PROVIDER_Q3 ? source->state.q3 : NULL, error);
        if (!okay) return false;
        icons = native_icons_find(f, view->provider, view->item);
        if (!native_icons_current(f, icons, &owner)) {
            const native_hud_icon_spec empty[3] = {0};
            if (!native_icons_admit(f, &owner, view->provider, view->item, empty, error)) return false;
            icons = native_icons_find(f, view->provider, view->item);
        }
    }
    if (!qa_application_equipment_current(f->application, view) ||
        !qa_vfs_lookup_equal(qa_application_provider_files(f->application, view->provider), owner.mounts))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native HUD icons changed their actual equipment or media owner");
    *out = icons->pictures;
    return true;
}
bool frontend_equipment_media_native_icons_read(qa_frontend *f,
    const qa_application_equipment_view *view, frontend_equipment_hud_icons *out, qa_error *error)
{
    if (!out || !f || !view || view->source_slot || !qa_application_equipment_current(f->application, view))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native HUD icons require their actual equipment source");
    *out = (frontend_equipment_hud_icons){0};
    if (!view->item) return true;
    frontend_visual_owner_view owner;
    if (!frontend_visual_media_read(f, view->provider, view->family, &owner, error)) return false;
    const native_hud_icons *icons = native_icons_find(f, view->provider, view->item);
    if (native_icons_current(f, icons, &owner)) *out = icons->pictures;
    return true;
}
bool frontend_equipment_media_native_item_icons_read(const qa_frontend *f,
    qa_actor_owner provider, qa_item_id item, frontend_equipment_hud_icons *out, qa_error *error)
{
    (void)error;
    if (!f || !out) return false;
    *out = (frontend_equipment_hud_icons){0};
    const native_hud_icons *icons = native_icons_find(f, provider, item);
    if (!icons || icons->strings != qa_session_strings(qa_application_session(f->application))) return true;
    frontend_visual_owner_view owner;
    if (frontend_visual_media_read(f, provider, icons->owner.family, &owner, NULL) &&
        native_icons_current(f, icons, &owner)) *out = icons->pictures;
    return true;
}

qa_hud_q1_variant frontend_q1_hud_variant(const qa_product *product)
{
    return product && product->campaign_id == QA_CAMPAIGN_ROGUE ? QA_HUD_Q1_ROGUE :
        product && product->campaign_id == QA_CAMPAIGN_HIPNOTIC ? QA_HUD_Q1_HIPNOTIC : QA_HUD_Q1_BASE;
}
static bool q1_hud_lump_prepare(qa_vfs *files, qa_scene_resources *images,
    qa_material_library *materials, const char *lump, qa_error *error)
{
    char key[64], declaration[128];
    bool file=strchr(lump,'/')!=NULL;
    if (!file && !frontend_equipment_icon_key("gfx.wad", lump, key, sizeof(key), error)) return false;
    if (qa_material_find(materials, file?lump:key)) return true;
    if(file)snprintf(declaration,sizeof(declaration),"{\"kind\":\"image\",\"path\":\"%s\"}",lump);
    else snprintf(declaration, sizeof(declaration),
        "{\"kind\":\"wad-picture\",\"path\":\"gfx.wad\",\"lump\":\"%s\"}", lump);
    const qa_material *material = NULL; qa_resource *resource = NULL;
    bool okay = frontend_equipment_icon_load((qa_bytes){(const uint8_t *)declaration, strlen(declaration)},
        QA_GAME_Q1, files, images, materials, &material, &resource, error);
    qa_resource_release(resource); return okay;
}
bool frontend_q1_hud_prepare(qa_vfs *files, qa_scene_resources *images,
    qa_material_library *materials, qa_hud_q1_variant variant, qa_error *error)
{
    static const char *const common[] = {"face1", "face_p1", "face2", "face_p2", "face3", "face_p3",
        "face4", "face_p4", "face5", "face_p5", "face_invis", "face_invul2", "face_inv2", "face_quad",
        "sbar", "ibar", "scorebar", "backtile", "disc", "gfx/complete.lmp", "gfx/inter.lmp", "num_minus", "anum_minus", "num_colon", "num_slash",
        "sb_shells", "sb_nails", "sb_rocket", "sb_cells", "sb_armor1", "sb_armor2", "sb_armor3",
        "sb_key1", "sb_key2", "sb_invis", "sb_invuln", "sb_suit", "sb_quad",
        "sb_sigil1", "sb_sigil2", "sb_sigil3", "sb_sigil4"};
    if ((unsigned)variant > QA_HUD_Q1_ROGUE)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Unknown actual Q1 HUD variant");
    for (size_t i = 0; i < sizeof(common) / sizeof(*common); ++i)
        if (!q1_hud_lump_prepare(files, images, materials, common[i], error)) return false;
    for (unsigned i = 0; i < 10; ++i) for (unsigned alternate = 0; alternate < 2; ++alternate) {
        char name[32]; snprintf(name, sizeof(name), "%s_%u", alternate ? "anum" : "num", i);
        if (!q1_hud_lump_prepare(files, images, materials, name, error)) return false;
    }
    static const char *const weapons[] = {"shotgun", "sshotgun", "nailgun", "snailgun", "rlaunch", "srlaunch", "lightng"};
    static const char *const hipnotic[] = {"laser", "mjolnir", "gren_prox", "prox_gren", "prox"};
    for (unsigned variant_row = 0; variant_row < (variant == QA_HUD_Q1_HIPNOTIC ? 2u : 1u); ++variant_row)
        for (unsigned i = 0; i < (variant_row ? 5u : 7u); ++i) for (unsigned animation = 0; animation < 7; ++animation) {
            char name[48]; const char *weapon = variant_row ? hipnotic[i] : weapons[i];
            if (animation < 2) snprintf(name, sizeof(name), "%s_%s", animation ? "inv2" : "inv", weapon);
            else snprintf(name, sizeof(name), "inva%u_%s", animation - 1, weapon);
            if (!q1_hud_lump_prepare(files, images, materials, name, error)) return false;
        }
    static const char *const extra_hipnotic[] = {"sb_wsuit", "sb_eshld"};
    static const char *const extra_rogue[] = {"r_invbar1", "r_invbar2", "r_lava", "r_superlava", "r_gren",
        "r_multirock", "r_plasma", "r_ammolava", "r_ammomulti", "r_ammoplasma", "r_teambord", "r_shield1", "r_agrav1"};
    const char *const *extra = variant == QA_HUD_Q1_ROGUE ? extra_rogue : extra_hipnotic;
    size_t count = variant == QA_HUD_Q1_BASE ? 0 : variant == QA_HUD_Q1_ROGUE ?
        sizeof(extra_rogue) / sizeof(*extra_rogue) : sizeof(extra_hipnotic) / sizeof(*extra_hipnotic);
    for (size_t i = 0; i < count; ++i)
        if (!q1_hud_lump_prepare(files, images, materials, extra[i], error)) return false;
    return true;
}
static const qa_scene_image *q1_hud_picture_read(void *context, const char *lump, qa_error *error)
{
    const qa_scene_image *image = NULL;
    return frontend_q1_face_read(context, lump, &image, error) ? image : NULL;
}
bool frontend_q1_hud_read(qa_material_library *materials, const qa_q1_clientdata *client,
    const qa_product *product, const qa_cvars *registry, const qa_hud_cvar_handles *refs,
    frontend_view_settings *settings,
    qa_ruleset_id rules, double seconds, const qa_scene_image *face, qa_hud_q1_status *out, qa_error *error)
{
    const qa_cvar_view *deathmatch = qa_cvars_read(registry, refs->deathmatch),
        *swap = qa_cvars_read(registry, refs->swap);
    if (!materials || !client || !product || product->family != QA_GAME_Q1 || !out || !face ||
        !isfinite(seconds))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q1 stock HUD lost its actual Source values, settings or media");
    frontend_q1_view_settings view;
    if (!frontend_view_settings_q1_sample(settings, rules,
        &view, error)) return false;
    *out = (qa_hud_q1_status){.picture_context = materials, .picture = q1_hud_picture_read, .face = face,
        .health = client->health, .armor = client->armor, .items = client->items,
        .active_weapon = client->weapon, .ammo_count = client->ammo,
        .ammunition = {client->shells, client->nails, client->rockets, client->cells},
        .seconds = seconds, .view_size = view.size, .variant = frontend_q1_hud_variant(product),
        .present = true, .deathmatch = deathmatch && deathmatch->number != 0, .quakeworld = rules == QA_RULESET_QUAKEWORLD,
        .overlay_status = view.overlay_status, .hud_swap = swap && swap->number != 0};
    return true;
}
bool frontend_q1_face_read(const qa_material_library *materials, const char *lump,
    const qa_scene_image **out, qa_error *error)
{
    char key[64];
    bool file=strchr(lump,'/')!=NULL;
    if (!file && !frontend_equipment_icon_key("gfx.wad",lump,key,sizeof(key),error)) return false;
    const qa_material *material=qa_material_find(materials,file?lump:key);
    if (!material || !material->stage_count || !material->stages[0].image_count ||
        !material->stages[0].images[0])
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 face has not completed its actual media preparation");
    *out=material->stages[0].images[0]; return true;
}
bool frontend_equipment_media_q1_hud_prepare(qa_frontend *f, qa_actor_owner provider,
    qa_hud_q1_variant variant, qa_error *error)
{
    frontend_visual_owner_view media;
    return frontend_visual_media_acquire(f,provider,QA_GAME_Q1,&media,error) &&
        frontend_q1_hud_prepare(media.mounts,media.images,media.materials,variant,error);
}
bool frontend_equipment_media_q1_hud_read(qa_frontend *f, qa_actor_owner provider,
    const qa_q1_clientdata *client, const qa_product *product, const qa_cvars *registry,
    const qa_hud_cvar_handles *refs,
    qa_ruleset_id rules, double seconds, const qa_scene_image *face, qa_hud_q1_status *out, qa_error *error)
{
    frontend_visual_owner_view media;
    return frontend_visual_media_read(f, provider, QA_GAME_Q1, &media, error) &&
        frontend_q1_hud_read(media.materials, client, product, registry, refs, f->view_settings,
            rules, seconds, face, out, error);
}
bool frontend_equipment_media_q1_face_read(qa_frontend *f, qa_actor_owner provider,
    const char *lump, const qa_scene_image **out, qa_error *error)
{
    frontend_visual_owner_view media;
    return frontend_visual_media_read(f,provider,QA_GAME_Q1,&media,error) &&
        frontend_q1_face_read(media.materials,lump,out,error);
}
bool frontend_q1_team_face_read(qa_scene_resources *images,const qa_material_library *materials,
    uint8_t colors,int32_t score,qa_hud_team_face *out,qa_error *error)
{
    qa_bytes palette={0};
    const qa_scene_image *border=NULL;
    if (!frontend_q1_face_read(materials,"r_teambord",&border,error)) return false;
    if (!qa_scene_resources_palette_read(images,QA_GAME_Q1,&palette) || palette.size!=768)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Rogue team face lost its admitted Source palette");
    unsigned top=(colors&0xf0u)+8u,bottom=((colors&15u)<<4)+8u;
    *out=(qa_hud_team_face){.border=border,.score=score,.alternate_digits=top==8,
        .top={palette.data[top*3]/255.0f,palette.data[top*3+1]/255.0f,palette.data[top*3+2]/255.0f,1},
        .bottom={palette.data[bottom*3]/255.0f,palette.data[bottom*3+1]/255.0f,palette.data[bottom*3+2]/255.0f,1}};
    return true;
}
bool frontend_equipment_media_q1_team_face_read(qa_frontend *f,qa_actor_owner provider,
    uint8_t colors,int32_t score,qa_hud_team_face *out,qa_error *error)
{
    frontend_visual_owner_view media;
    return frontend_visual_media_read(f,provider,QA_GAME_Q1,&media,error) &&
        frontend_q1_team_face_read(media.images,media.materials,colors,score,out,error);
}

bool frontend_equipment_media_source_icon_read(const qa_frontend *f,
    const qa_application_equipment_view *view,const qa_material **icon,qa_error *error)
{
    if(!f||!view||!icon||*icon||!view->source_slot||!view->selected||
        !qa_application_equipment_current(f->application,view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source icon read requires its actual selected component binding");
    if(!view->source_icon.size)return true;
    const frontend_equipment_media *row = source_media_find(f, view);
    if (row) {
        if(!row->bound||!row->icon||!frontend_equipment_media_namespace_current(f,row)||
            !qa_application_equipment_current(f->application,view))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source icon lost its actual prepared media receipt");
        *icon=row->icon;return true;
    }
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Source icon has not completed its actual media preparation");
}

frontend_equipment_media *frontend_equipment_media_held_read(const qa_frontend *frontend,
    const qa_application_equipment_view *view, const qa_q3_presentation_assets *recipient)
{
    frontend_equipment_media *row = view->source_slot ? source_media_find(frontend, view) : media_find(frontend, view);
    if (!row || !row->bound || (view->source_slot && !view->source_held.size) ||
        (!view->source_slot && view->family == QA_GAME_Q3 && !row->declaration.source)) return NULL;
    if (!row->declaration.none && qa_scene_model_image_options(row->held_scene)->family == QA_GAME_Q3) {
        qa_q3_presentation_provider provider;
        if (!qa_q3_assets_provider_at(recipient, 0, &provider) ||
            !qa_scene_model_materials_ready(row->held_scene, provider.materials)) return NULL;
    }
    return row;
}

static bool q3_held_prepare(qa_frontend *frontend,
    const qa_application_equipment_view *view, frontend_equipment_media **out,
    bool *authored, qa_error *error)
{
    if (!frontend || !view || !out || *out || !authored || view->family != QA_GAME_Q3 ||
        !view->selected || !view->view_model || !qa_strings_text(qa_session_strings(qa_application_session(frontend->application)), view->view_model).size ||
        !qa_application_equipment_current(frontend->application, view) ||
        (frontend->equipment && frontend->equipment->admitting))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held admission requires its actual selected source and empty output");
    *authored = false;
    for (const frontend_equipment_media *row = frontend->equipment ? frontend->equipment->media : NULL;
            row; row = row->next)
        if (row->held_scene && !qa_scene_model_idle(row->held_scene))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held admission requires idle physical scenes");
    frontend_equipment_media *row = media_find(frontend, view);
    if (row) {
        if (!row->bound || !row->declaration.source)
            return frontend_fail(error, QA_ERROR_FORMAT, "Q3 held media lost its retained authored declaration");
        *out = row; *authored = true; return true;
    }
    frontend_visual_owner_view owner;
    if (!frontend_visual_media_acquire(frontend, view->provider, view->family, &owner, error)) return false;
    const char *path = NULL;
    bool found = false;
    bool okay = held_lookup(frontend, view, owner.mounts, &path, &found, error);
    qa_resource *resource = NULL; frontend_held_declaration declaration = {0};
    if (okay && found) okay = qa_vfs_acquire(owner.mounts, path, &resource, NULL, error) &&
        frontend_held_declaration_read(resource, &declaration, error);
    qa_resource_release(resource);
    if (okay && found) okay = prepare_media(frontend, view, &declaration, out, error);
    frontend_held_declaration_free(&declaration);
    if (okay && !qa_application_equipment_current(frontend->application, view))
        okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Q3 held probe retired its actual selected source");
    if (okay) *authored = found;
    return okay;
}

bool frontend_equipment_media_held_prepare(qa_frontend *frontend,
    const qa_application_equipment_view *view, const qa_q3_presentation_assets *recipient,
    frontend_equipment_media **out, bool *authored, qa_error *error)
{
    bool okay;
    if (view->source_slot) {
        bool present = false;
        okay = source_media_prepare(frontend, view, out, &present, error);
        *authored = present && view->source_held.size != 0;
    } else if (view->family == QA_GAME_Q3)
        okay = q3_held_prepare(frontend, view, out, authored, error);
    else {
        okay = prepare_media(frontend, view, NULL, out, error);
        *authored = okay;
    }
    if (!okay || !*authored || (*out)->declaration.none) return okay;
    const qa_scene_image_options *images = qa_scene_model_image_options((*out)->held_scene);
    if (images->family != QA_GAME_Q3) return true;
    qa_q3_presentation_provider provider;
    return qa_q3_assets_provider_at(recipient, 0, &provider) &&
        qa_scene_model_materials_admit((*out)->held_scene, NULL, provider.materials, error);
}

bool frontend_equipment_media_read(const frontend_equipment_media *row,
    frontend_equipment_media_view *out)
{
    if (!row || !out) return false;
    *out = (frontend_equipment_media_view){row->provider, row->family, row->item,
        row->view_path, row->strings, row->owner, row->view, row->held_parent, &row->declaration,
        &row->held, row->held_scene, row->gear_namespace, row->gear_service_owner,
        row->source_slot,row->source_generation,{row->source_held.data,row->source_held.size},
        {row->source_icon.data,row->source_icon.size},row->icon,row->media,row->movies};
    return true;
}

bool frontend_equipment_media_retain(frontend_equipment_media *row, qa_error *error)
{
    if (!row || row->users == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Equipment media retain exceeds its actual holder lifetime");
    ++row->users;
    return true;
}

void frontend_equipment_media_release(frontend_equipment_media *row)
{
    if (row) --row->users;
}

size_t frontend_equipment_media_count(const qa_frontend *frontend)
{
    size_t count = 0;
    if (frontend && frontend->equipment)
        for (const frontend_equipment_media *row = frontend->equipment->media; row; row = row->next) ++count;
    return count;
}

bool frontend_equipment_media_at(const qa_frontend *frontend, size_t ordinal,
    frontend_equipment_media_view *out)
{
    const frontend_equipment_media *row = frontend && frontend->equipment ? frontend->equipment->media : NULL;
    while (row && ordinal) { row = row->next; --ordinal; }
    return frontend_equipment_media_read(row, out);
}

size_t frontend_equipment_movie_count(const qa_frontend *f)
{
    size_t count=0;
    for(const frontend_equipment_media *row=f&&f->equipment?f->equipment->media:NULL;row;row=row->next)
        count+=row->source_slot;
    return count;
}
bool frontend_equipment_movie_at(const qa_frontend *f,size_t index,size_t *physical)
{
    size_t ordinal=0;
    for(const frontend_equipment_media *row=f&&f->equipment?f->equipment->media:NULL;row;row=row->next,++ordinal)
        if(row->source_slot){if(!index){if(!physical)return false;*physical=ordinal;return true;}--index;}
    return false;
}
bool frontend_equipment_movie_source_read(qa_frontend *f,size_t ordinal,
    frontend_material_movie_source *out,qa_error *error)
{
    frontend_equipment_media *row=f&&f->equipment?f->equipment->media:NULL;
    while(row&&ordinal){row=row->next;--ordinal;}
    if(!row||!out||!row->source_slot||row->frontend!=f||!row->media||
        !frontend_equipment_media_namespace_current(f,row))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment shader movie lost its actual component media owner");
    *out=frontend_equipment_media_movie_source(row);
    return (f->source_restoring&&row->restoring&&!row->movies)||
        (row->movies&&frontend_material_movies_current(row->movies));
}
