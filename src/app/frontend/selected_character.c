#include "selected_character_private.h"
#include "../../presentation/q3_native/attachments.h"
#include "../../presentation/q3_native/body.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct character_admission {
    frontend_selected_character *owner;
    const qa_application_selected_q3_character *source;
    void *context;
    bool (*current)(void *);
} character_admission;

static bool current(const frontend_selected_character *owner,
    const qa_application_selected_q3_character *source)
{
    const qa_native_q3_character_selection *selection = &owner->view.selection;
    qa_application_q3_asset_selection appearance; bool found;
    return source && source->launch && source->provider == selection->owner && source->product == selection->product &&
        source->launch->storage == selection->launch->storage &&
        selection->current(selection->lifetime, selection) &&
        qa_application_q3_asset_selection_read(owner->frontend->application, source->actor,
            QA_ROLE_SKIN, &appearance, &found, NULL) && found &&
        appearance.provider == owner->view.appearance.provider &&
        appearance.content == owner->view.appearance.content &&
        appearance.launch->storage == owner->view.appearance_launch->storage &&
        appearance.product == owner->view.appearance.product &&
        appearance.publication_generation == owner->view.appearance.publication_generation &&
        qa_application_selected_q3_character_current(owner->frontend->application, source);
}
static bool admission_current(const character_admission *call, qa_error *error)
{
    return call->current(call->context) && current(call->owner, call->source) ? true :
        frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character resource admission lost its genuine actor or recipient");
}
static bool component(const char *text, bool empty, bool star, qa_error *error)
{
    if (!text) return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character declaration is absent");
    if (empty && !*text) return true;
    if (star && *text == '*') ++text;
    if (*text && strcmp(text, ".") && strcmp(text, "..") && !strpbrk(text, "/\\")) return true;
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character declaration has an invalid resource component");
}
static char *path(qa_error *error, const char *format, ...)
{
    va_list args, copy; va_start(args, format); va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy); va_end(copy);
    char *result = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (!result) frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected character resource path");
    else vsnprintf(result, (size_t)length + 1, format, args);
    va_end(args); return result;
}
static void clear_resource(frontend_selected_character *owner, uint32_t slot)
{
    qa_resource_release(owner->resources[slot]); owner->resources[slot] = NULL;
    qa_vfs_acquisition_dispose(&owner->receipts[slot]);
}
/* Absence and an empty actual file try the next authored path. Other VFS
 * failures retain their real error; no guessed default model is admitted. */
static bool probe(character_admission *call, uint32_t slot, const char *name,
    bool *found, qa_error *error)
{
    *found = false;
    if (!admission_current(call, error)) return false;
    qa_resource *resource = NULL; qa_vfs_acquisition receipt = {0}; qa_error observed = {0};
    bool okay = qa_vfs_acquire_receipt(call->owner->view.content.mounts, name, &resource, &receipt, &observed);
    if (!okay && observed.code == QA_ERROR_NOT_FOUND) okay = true;
    if (okay) okay = admission_current(call, error);
    else if (error) *error = observed;
    if (okay && resource && qa_resource_bytes(resource).size) {
        clear_resource(call->owner, slot);
        call->owner->resources[slot] = resource; call->owner->receipts[slot] = receipt;
        resource = NULL; receipt = (qa_vfs_acquisition){0}; *found = true;
    }
    qa_resource_release(resource); qa_vfs_acquisition_dispose(&receipt); return okay;
}
static bool body_model(character_admission *call, uint32_t slot, const char *base, qa_error *error)
{
    const char *model = call->owner->view.selection.model;
    const char *folders[] = {"", "characters/"}; bool found = false;
    for (unsigned i = 0; i < 2 && !found; ++i) {
        char *name = path(error, "models/players/%s%s/%s", folders[i], model, base);
        bool okay = name && probe(call, slot, name, &found, error); free(name);
        if (!okay) return false;
    }
    return found || frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected character body or animation resource is missing");
}
static bool head_model(character_admission *call, qa_error *error)
{
    const qa_native_q3_character_selection *s = &call->owner->view.selection;
    const char *head = *s->head_model ? s->head_model : s->model;
    bool star = *head == '*', found = false; if (star) ++head;
    if (!star) {
        char *name = path(error, "models/players/%s/head.md3", head);
        bool okay = name && probe(call, 2, name, &found, error); free(name);
        if (!okay) return false;
    }
    if (!found) {
        char *name = path(error, "models/players/heads/%s/%s.md3", head, head);
        bool okay = name && probe(call, 2, name, &found, error); free(name);
        if (!okay) return false;
    }
    return found || frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected character head model is missing");
}
static bool body_skin(character_admission *call, uint32_t slot, const char *base, qa_error *error)
{
    const qa_native_q3_character_selection *s = &call->owner->view.selection;
    const char *folders[] = {"", "characters/"}; bool found = false;
    for (unsigned i = 0; i < 2 && !found; ++i) for (unsigned variant = 0; variant < 2 && !found; ++variant) {
        char name[64];
        if (!variant) snprintf(name, sizeof(name), "models/players/%s%s/%s_%s_default.skin", folders[i], s->model, base, s->skin);
        else snprintf(name, sizeof(name), "models/players/%s%s/%s_%s.skin", folders[i], s->model, base, s->skin);
        if (!probe(call, slot, name, &found, error)) return false;
    }
    return found || frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected character body skin is missing");
}
static bool head_file(character_admission *call, uint32_t slot, const char *base,
    const char *extension, bool required, qa_error *error)
{
    const qa_native_q3_character_selection *s = &call->owner->view.selection;
    const char *head = *s->head_model ? s->head_model : s->model;
    bool star = *head == '*', found = false; if (star) ++head;
    const char *folders[] = {"", "heads/"};
    for (unsigned i = star ? 1 : 0; i < 2 && !found; ++i) for (unsigned variant = 0; variant < 2 && !found; ++variant) {
        char name[128]; size_t extent = required ? 64 : sizeof(name);
        if (!variant) snprintf(name, extent, "models/players/%s%s/%s/%s_default.%s", folders[i], head, s->head_skin, base, extension);
        else snprintf(name, extent, "models/players/%s%s/%s_%s.%s", folders[i], head, base, s->head_skin, extension);
        if (!probe(call, slot, name, &found, error)) return false;
    }
    return !required || found || frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected character head skin is missing");
}
static bool resource_bundle(character_admission *call, qa_error *error)
{
    frontend_selected_character *owner = call->owner;
    const qa_native_q3_character_selection *s = &owner->view.selection;
    if (!component(s->model, false, false, error) || !component(s->skin, false, false, error) ||
        !component(s->head_model, true, true, error) || !component(s->head_skin, false, false, error) ||
        !body_model(call, 0, "lower.md3", error) || !body_model(call, 1, "upper.md3", error) ||
        !head_model(call, error) || !body_model(call, 6, "animation.cfg", error) ||
        !body_skin(call, 3, "lower", error) || !body_skin(call, 4, "upper", error) ||
        !head_file(call, 5, "head", "skin", true, error) ||
        !head_file(call, 7, "icon", "skin", false, error)) return false;
    if (!owner->resources[7] && !head_file(call, 7, "icon", "tga", false, error)) return false;
    if (!q3n_selected_animation_parse(qa_resource_bytes(owner->resources[6]),
        owner->receipts[6].path, &owner->animation, error)) return false;
    for (unsigned i = 0; i < 3; ++i) {
        if (!admission_current(call, error) ||
            !qa_q3_register_model(owner->view.assets, owner->receipts[i].path, &owner->view.models[i], error) ||
            !admission_current(call, error)) return false;
        if (!owner->view.models[i])
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected character model registration returned no real model");
        if (!qa_q3_register_skin(owner->view.assets, owner->receipts[i + 3].path, &owner->view.skins[i], error) ||
            !admission_current(call, error)) return false;
        if (!owner->view.skins[i])
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected character skin registration returned no real skin");
        qa_q3_asset_model_holder model; qa_q3_asset_skin_holder skin;
        if (!qa_q3_assets_model_holder(owner->view.assets, (size_t)owner->view.models[i] - 1, &model, error) ||
            !qa_q3_assets_skin_holder(owner->view.assets, (size_t)owner->view.skins[i] - 1, &skin, error) ||
            model.resource != owner->resources[i] || skin.resource != owner->resources[i + 3] ||
            !model.sources[0] || model.sources[0]->format != QA_MODEL_MD3)
            return frontend_fail(error, QA_ERROR_FORMAT, "Selected character registration changed its real MD3 or skin holder");
    }
    for (unsigned i = 0; i < 8; ++i) {
        owner->view.resources[i] = owner->resources[i]; owner->view.receipts[i] = &owner->receipts[i];
    }
    owner->view.animation = &owner->animation; return admission_current(call, error);
}

void frontend_selected_character_dispose(frontend_selected_character *owner)
{
    if (!owner) return;
    while (owner->poses) { frontend_selected_character_pose *next = owner->poses->next; free(owner->poses); owner->poses = next; }
    qa_q3_presentation_assets_destroy(owner->view.assets);
    for (unsigned i = 0; i < 8; ++i) clear_resource(owner, i);
    qa_launch_instance_lease_release(owner->appearance_lease);
    if (owner->view.selection.release) owner->view.selection.release(owner->view.selection.lifetime);
    free(owner);
}

bool frontend_selected_character_prepare(qa_frontend *frontend, uint32_t launch_seat,
    uint32_t physical_seat, const qa_application_selected_q3_character *source,
    void *context, bool (*recipient_current)(void *), frontend_selected_character_pose **out,
    bool *admitted, qa_error *error)
{
    if (!frontend || !source || !out || !admitted || !recipient_current ||
        physical_seat >= frontend->options.seats || !recipient_current(context) ||
        !qa_application_selected_q3_character_current(frontend->application, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character requires its actual full actor and display recipient");
    *admitted = false;
    frontend_selected_character *owner = frontend->selected_characters;
    while (owner && (owner->view.launch_seat != launch_seat || !current(owner, source))) owner = owner->next;
    bool fresh = !owner;
    if (fresh) {
        qa_native_q3_character_selection selection = {0}; bool found;
        if (!qa_application_character_selection_read(frontend->application, launch_seat, &selection, &found, error)) return false;
        if (!found) return true;
        const qa_product *product = qa_catalog_product(qa_launch_instance_catalog(selection.launch), selection.product);
        if (!product || product->family != QA_GAME_Q3 || selection.owner != source->provider ||
            selection.launch->storage != source->launch->storage) {
            selection.release(selection.lifetime); return true;
        }
        qa_application_q3_asset_selection appearance;
        bool appearance_found;
        if (!qa_application_q3_asset_selection_read(frontend->application, source->actor, QA_ROLE_SKIN,
            &appearance, &appearance_found, error)) { selection.release(selection.lifetime); return false; }
        if (!appearance_found || appearance.family != QA_GAME_Q3) { selection.release(selection.lifetime); return true; }
        owner = calloc(1, sizeof(*owner));
        if (!owner) { selection.release(selection.lifetime); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected character resource owner"); }
        owner->frontend = frontend; owner->view.launch_seat = launch_seat; owner->view.selection = selection;
        owner->view.appearance = appearance;
        owner->admitting = true;
        bool okay = qa_launch_instance_retain_metadata(appearance.launch, &owner->appearance_lease, error);
        if (okay) owner->view.appearance_launch = qa_launch_instance_lease_view(owner->appearance_lease);
        if (okay) okay = frontend_visual_media_acquire(frontend, appearance.provider, QA_GAME_Q3, &owner->view.content, error);
        qa_q3_presentation_asset_options assets = {.provider = {owner->view.content.mounts,
            owner->view.content.images, owner->view.content.materials, QA_SCENE_Q3}};
        if (okay) okay = qa_q3_presentation_assets_create(&assets, &owner->view.assets, error);
        character_admission call = {owner, source, context, recipient_current};
        if (okay) okay = resource_bundle(&call, error);
        owner->admitting = false;
        if (!okay) { frontend_selected_character_dispose(owner); return false; }
    }
    if (owner->admitting || owner->restoring || !qa_q3_assets_idle(owner->view.assets)) {
        if (fresh) frontend_selected_character_dispose(owner);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character resource owner is busy");
    }
    frontend_selected_character_pose *pose = owner->poses;
    while (pose && (pose->physical_seat != physical_seat || !qa_actor_id_equal(pose->actor, source->actor))) pose = pose->next;
    if (!pose) {
        pose = calloc(1, sizeof(*pose));
        if (!pose) { if (fresh) frontend_selected_character_dispose(owner); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected character actor and view pose"); }
        pose->owner = owner; pose->actor = source->actor; pose->physical_seat = physical_seat;
        pose->reset = true; pose->next = owner->poses; owner->poses = pose;
    }
    if (fresh) { owner->next = frontend->selected_characters; frontend->selected_characters = owner; }
    *out = pose; *admitted = true; return true;
}

bool frontend_selected_character_animation(const frontend_selected_character_pose *pose,
    q3n_selected_animation *out, qa_error *error)
{
    const frontend_selected_character *owner = pose ? pose->owner : NULL;
    if (!owner || !out || owner->admitting || owner->restoring || !owner->resources[6])
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character animation requires its genuine immutable bundle");
    *out = (q3n_selected_animation){owner->view.content.mounts, owner->resources[6], &owner->receipts[6], &owner->animation};
    return true;
}

static bool attach_part(frontend_selected_character *owner, qa_q3_ref_entity *child,
    const qa_q3_ref_entity *parent, const char *name, bool *found, qa_error *error)
{
    qa_model_tag tag;
    volatile float fraction = 1.0f - parent->back_lerp;
    if (!qa_q3_presentation_tag(owner->view.assets, parent->model, name, parent->old_frame,
        parent->frame, fraction, &tag, found, error)) return false;
    return !*found || q3n_attach(owner->view.assets, child, parent, name, true, error);
}

bool frontend_selected_character_build(frontend_selected_character_pose *pose,
    const qa_application_selected_q3_character *source, const frontend_selected_character_frame *frame,
    frontend_selected_character_output **out, qa_error *error)
{
    frontend_selected_character *owner = pose ? pose->owner : NULL;
    if (!owner || !frame || !out || *out || !current(owner, source) ||
        !qa_actor_id_equal(pose->actor, source->actor) || pose->users || owner->admitting || owner->restoring ||
        !qa_q3_assets_idle(owner->view.assets) || !isfinite(source->scale) || !isfinite(source->opacity) ||
        source->opacity < 0 || source->opacity > 1 || frame->frame_milliseconds < 0 ||
        !isfinite(frame->shader_time) || (frame->has_shadow_plane && !isfinite(frame->shadow_plane)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character build requires its genuine idle actor pose and frame");
    frontend_selected_character_output *output = calloc(1, sizeof(*output));
    if (!output) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining selected character body output");
    output->pose = pose; output->source = *source;
    q3n_player_pose next = pose->pose;
    bool okay = true;
    if (source->present) {
        if (pose->reset) {
            okay = q3n_lerp_clear(&owner->animation, &next.legs.animation, source->legs_animation, frame->time, error) &&
                q3n_lerp_clear(&owner->animation, &next.torso.animation, source->torso_animation, frame->time, error);
            if (okay) q3n_player_reset(&next, source->view_angles);
        }
        q3n_pose_entity entity = {.flags = source->source_flags, .velocity = source->body.velocity,
            .movement_direction = source->movement_direction, .legs_animation = source->legs_animation,
            .torso_animation = source->torso_animation};
        q3n_pose_axes axes;
        if (okay) okay = q3n_player_angles_pose(&next, &owner->animation, &entity, source->view_angles,
            frame->time, frame->frame_milliseconds, frame->swing_speed, &axes, error);
        int32_t legs = next.legs.yawing && (source->legs_animation & ~128) == 22 ? 24 : source->legs_animation;
        if (okay) okay = q3n_lerp_run(&owner->animation, &next.legs.animation, legs, frame->time,
            1, frame->no_player_animations, error) && q3n_lerp_run(&owner->animation, &next.torso.animation,
            source->torso_animation, frame->time, 1, frame->no_player_animations, error);
        if (okay) {
            for (unsigned i = 0; i < 3; ++i) {
                qa_q3_ref_entity *part = output->parts + i;
                part->kind = QA_Q3_REF_MODEL; part->model = owner->view.models[i];
                part->custom_skin = owner->view.skins[i];
                part->lighting_origin = source->body.origin;
                part->flags = 128 | (frame->personal_model ? 2 : 0) | (frame->has_shadow_plane ? 64 : 0);
                part->shadow_plane = frame->has_shadow_plane ? frame->shadow_plane : 0;
                part->shader_time = frame->shader_time;
                memset(part->color, 255, sizeof(part->color));
                part->non_normalized_axes = source->scale != 1;
            }
            memcpy(output->parts[0].axis, axes.legs, sizeof(axes.legs));
            memcpy(output->parts[1].axis, axes.torso, sizeof(axes.torso));
            memcpy(output->parts[2].axis, axes.head, sizeof(axes.head));
            for (unsigned i = 0; i < 3; ++i) {
                output->parts[0].axis[i].x *= source->scale;
                output->parts[0].axis[i].y *= source->scale;
                output->parts[0].axis[i].z *= source->scale;
            }
            output->parts[0].origin = output->parts[0].old_origin = source->body.origin;
            output->parts[0].frame = next.legs.animation.frame;
            output->parts[0].old_frame = next.legs.animation.old_frame;
            output->parts[0].back_lerp = next.legs.animation.back_lerp;
            output->parts[1].frame = next.torso.animation.frame;
            output->parts[1].old_frame = next.torso.animation.old_frame;
            output->parts[1].back_lerp = next.torso.animation.back_lerp;
            output->count = 1;
            bool found = false;
            okay = attach_part(owner, output->parts + 1, output->parts, "tag_torso", &found, error);
            if (okay && found) {
                output->parts[1].old_origin = output->parts[1].origin; output->count = 2;
                okay = attach_part(owner, output->parts + 2, output->parts + 1, "tag_head", &found, error);
                if (okay && found) { output->parts[2].old_origin = output->parts[2].origin; output->count = 3; }
            }
        }
    }
    if (okay) okay = current(owner, source);
    if (!okay) { free(output); return false; }
    if (source->present) { pose->pose = next; pose->reset = false; }
    ++pose->users; *out = output; return true;
}

size_t frontend_selected_character_output_count(const frontend_selected_character_output *output)
{ return output ? output->count : 0; }
const qa_q3_ref_entity *frontend_selected_character_output_part(
    const frontend_selected_character_output *output, uint32_t part)
{ return output && part < output->count ? output->parts + part : NULL; }
const qa_q3_presentation_assets *frontend_selected_character_output_assets(
    const frontend_selected_character_output *output)
{ return output ? output->pose->owner->view.assets : NULL; }
bool frontend_selected_character_output_current(const frontend_selected_character_output *output)
{ return output && current(output->pose->owner, &output->source); }

bool frontend_selected_character_output_passes(frontend_selected_character_output *output,
    qa_q3_presentation *presentation, const qa_q3_presentation_assets *primary,
    const frontend_selected_character_pass *passes, size_t count,
    const qa_q3_scene_options *options, uint32_t first_order, qa_scene_frame *frame, qa_error *error)
{
    if (!output || !presentation || !options || !frame || !primary || (count && !passes) ||
        !frontend_selected_character_output_current(output))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected body materials require their actual retained output");
    bool captured[3] = {false};
    for (size_t i = 0; i < count; ++i) {
        if (passes[i].part > 3)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected body material has no genuine body part");
        for (size_t j = 0; j < i; ++j)
            if (passes[j].helper == passes[i].helper && passes[j].part != passes[i].part)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected body helper changed its actual source part");
        if (passes[i].part == 3) memset(captured, 1, sizeof(captured));
        else captured[passes[i].part] = true;
    }
    if (!output->count) return true;
    size_t commands = frame->command_count, groups = frame->group_count;
    bool opacity = output->source.opacity != 1;
    qa_scene_command scope = {.kind = QA_SCENE_COMMAND_OPACITY_BEGIN, .data.opacity.value = output->source.opacity};
    bool okay = !opacity || qa_scene_frame_emit(frame, &scope, error);
    for (uint32_t part = 0; okay && part < output->count; ++part)
        if (!captured[part])
            okay = qa_q3_presentation_selected_registered(presentation, output->pose->owner->view.assets,
                output->parts + part, options, first_order + part, frame, error) &&
                frontend_selected_character_output_current(output);
    for (size_t i = 0; okay && i < count; ++i) {
        uint32_t first = passes[i].part == 3 ? 0 : passes[i].part;
        size_t end = passes[i].part == 3 ? output->count : (size_t)first + 1;
        for (uint32_t part = first; okay && part < end && part < output->count; ++part) {
            size_t ordinal = 0; bool equal = false, duplicate = false;
            for (size_t j = 0; okay && j < i; ++j) {
                okay = qa_q3_presentation_body_material_equal(presentation, primary,
                    &passes[j].material, &passes[i].material, &equal, error);
                if (okay && equal && passes[j].helper == passes[i].helper) ++ordinal;
            }
            for (size_t j = 0; okay && !duplicate && j < i; ++j) {
                if (passes[j].helper == passes[i].helper || (passes[j].part != 3 && passes[j].part != part)) continue;
                okay = qa_q3_presentation_body_material_equal(presentation, primary,
                    &passes[j].material, &passes[i].material, &equal, error);
                if (!okay || !equal) continue;
                size_t earlier_ordinal = 0;
                for (size_t k = 0; okay && k < j; ++k) {
                    okay = qa_q3_presentation_body_material_equal(presentation, primary,
                        &passes[k].material, &passes[j].material, &equal, error);
                    if (okay && equal && passes[k].helper == passes[j].helper) ++earlier_ordinal;
                }
                duplicate = okay && earlier_ordinal == ordinal;
            }
            if (!okay || duplicate) continue;
            okay = qa_q3_presentation_selected_body_pass(presentation, output->pose->owner->view.assets,
                output->parts + part, primary, &passes[i].material, options, first_order + part, frame, error) &&
                frontend_selected_character_output_current(output);
        }
    }
    scope.kind = QA_SCENE_COMMAND_OPACITY_END;
    if (okay && opacity) okay = qa_scene_frame_emit(frame, &scope, error);
    if (!okay) { frame->command_count = commands; frame->group_count = groups; }
    return okay;
}

bool frontend_selected_character_output_submit(frontend_selected_character_output *output,
    qa_q3_presentation *presentation, const qa_q3_scene_options *options, uint32_t first_order,
    qa_scene_frame *frame, qa_error *error)
{
    if (!output || !presentation || !options || !frame || !current(output->pose->owner, &output->source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character submission lost its genuine actor output");
    if (!output->count) return true;
    size_t commands = frame->command_count, groups = frame->group_count;
    bool opacity = output->source.opacity != 1;
    qa_scene_command scope = {.kind = QA_SCENE_COMMAND_OPACITY_BEGIN, .data.opacity.value = output->source.opacity};
    bool okay = !opacity || qa_scene_frame_emit(frame, &scope, error);
    for (size_t i = 0; okay && i < output->count; ++i)
        okay = qa_q3_presentation_selected_registered(presentation, output->pose->owner->view.assets,
            output->parts + i, options, first_order + (uint32_t)i, frame, error) &&
            current(output->pose->owner, &output->source);
    scope.kind = QA_SCENE_COMMAND_OPACITY_END;
    if (okay && opacity) okay = qa_scene_frame_emit(frame, &scope, error);
    if (!okay) { frame->command_count = commands; frame->group_count = groups; }
    return okay;
}

void frontend_selected_character_output_destroy(frontend_selected_character_output *output)
{
    if (!output) return;
    --output->pose->users; free(output);
}

bool frontend_selected_character_idle(const qa_frontend *frontend)
{
    if (!frontend) return false;
    for (const frontend_selected_character *owner = frontend->selected_characters; owner; owner = owner->next) {
        if (owner->admitting || !qa_q3_assets_idle(owner->view.assets)) return false;
        for (const frontend_selected_character_pose *pose = owner->poses; pose; pose = pose->next)
            if (pose->users) return false;
    }
    return true;
}

bool frontend_selected_character_retire(qa_frontend *frontend, qa_error *error)
{
    if (!frontend_selected_character_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character retirement requires every actual pose and registry to be idle");
    while (frontend->selected_characters) {
        frontend_selected_character *owner = frontend->selected_characters;
        frontend->selected_characters = owner->next;
        frontend_selected_character_dispose(owner);
    }
    return true;
}

size_t frontend_selected_character_count(const qa_frontend *frontend)
{
    size_t count = 0;
    for (const frontend_selected_character *owner = frontend ? frontend->selected_characters : NULL;
        owner; owner = owner->next) ++count;
    return count;
}

bool frontend_selected_character_at(const qa_frontend *frontend, size_t ordinal,
    frontend_selected_character_view *out, qa_error *error)
{
    const frontend_selected_character *owner = frontend ? frontend->selected_characters : NULL;
    while (owner && ordinal) { owner = owner->next; --ordinal; }
    if (!owner || !out || owner->admitting)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character inventory lacks its actual physical resource owner");
    *out = owner->view; return true;
}

bool frontend_selected_character_content_visit(const qa_frontend *frontend,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!frontend || !visitor || !visitor->catalog || !visitor->view)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character content visitor requires its actual retained graph");
    for (const frontend_selected_character *owner = frontend->selected_characters; owner; owner = owner->next) {
        if (owner->admitting || owner->restoring)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected character content owner is busy");
        if (!visitor->catalog(visitor->context, qa_launch_instance_catalog(owner->view.selection.launch), error) ||
            !visitor->catalog(visitor->context, qa_launch_instance_catalog(owner->view.appearance_launch), error) ||
            !visitor->view(visitor->context, owner->view.selection.content, error) ||
            !visitor->view(visitor->context, owner->view.appearance.content, error) ||
            !visitor->view(visitor->context, owner->view.content.mounts, error)) return false;
    }
    return true;
}
