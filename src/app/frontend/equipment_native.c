#include "equipment_native.h"
#include "save_private.h"

typedef struct equipment_native {
    qa_frontend *frontend;
    frontend_native_q3 *row;
    frontend_native_q3_view owners;
    frontend_equipment_source *source;
    qa_application_q3_equipment_services services;
    qa_application_q3_equipment_draw draw;
    const q3n_frame *frame;
    bool entered, hud_requested, view_requested;
} equipment_native;

static bool current(const equipment_native *owner, const q3n_frame *frame)
{
    return owner && owner->entered && owner->frame == frame && frame &&
        frame->application == owner->frontend->application &&
        frame->assets == owner->owners.assets && frame->presentation == owner->owners.presentation &&
        frame->reader == owner->owners.reader && frame->seat == owner->owners.launch_seat &&
        frame->physical_presentation_seat == owner->owners.seat &&
        frame->viewing_client == owner->owners.physical_client &&
        qa_actor_id_equal(frame->viewing_actor, owner->owners.actor) &&
        qa_application_native_q3_presentation_current(frame->application, &frame->source);
}
static bool borrow(void *row, qa_application_q3_client_context *out, qa_error *error)
{ return frontend_native_q3_borrow(row, out, error); }
static bool context_current(void *row, const qa_application_q3_client_context *client)
{ return frontend_native_q3_context_current(row, client); }
static void release(void *row)
{ frontend_native_q3_release(row); }
static bool requests(void *context, bool *hud, bool *view, qa_error *error)
{
    const equipment_native *owner = context;
    if (!owner || !hud || !view)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native equipment requests require their real composition owner");
    *hud = owner->hud_requested; *view = owner->view_requested; return true;
}
static bool warning(void *context, const q3n_frame *frame, q3n_weapon_hud *out, qa_error *error)
{
    equipment_native *owner = context;
    if (!out || !current(owner, frame) ||
        !owner->services.current(owner->services.context, &owner->draw))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native weapon HUD lost its real admitted frame and receiver");
    *out = (q3n_weapon_hud){.selected = owner->draw.selected, .warning = owner->draw.warning};
    return true;
}
static bool begin(void *context, const q3n_frame *frame, qa_error *error)
{
    equipment_native *owner = context;
    if (!owner || owner->entered || !frontend_equipment_source_idle(owner->source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native selected frame requires its returned receiver");
    owner->entered = true; owner->frame = frame;
    owner->hud_requested = owner->view_requested = false;
    if (!current(owner, frame) || !owner->services.prepare(owner->services.context,
            owner->owners.receiver, owner->owners.launch_seat, &owner->draw, error)) return false;
    owner->hud_requested = owner->draw.selected;
    return true;
}
static void end(void *context)
{
    equipment_native *owner = context;
    if (!owner || !owner->entered) return;
    owner->services.release_draw(owner->services.context);
    owner->entered = false; owner->frame = NULL;
}
static void clear(void *context)
{
    equipment_native *owner = context;
    if (owner) frontend_equipment_source_clear(owner->source);
}
static bool view_weapon(void *context, const q3n_frame *frame, const qa_q3_player *player,
    bool *consumed, qa_error *error)
{
    equipment_native *owner = context;
    if (!player || !consumed || !current(owner, frame) || !frame->weapon_settings ||
        player != &frame->local_player)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native view replacement requires its finished source camera and raw PS");
    owner->view_requested = frame->weapon_settings->draw_gun && !frame->third_person;
    return frontend_equipment_source_native_view(owner->source, frame->refdef.fov_x, consumed, error);
}
static bool held_actor(equipment_native *owner, const q3n_frame *frame, qa_actor_id actor,
    const qa_q3_presentation_assets *parent_assets, const qa_q3_ref_entity *parent,
    int32_t powerups, bool *suppressed, qa_error *error)
{
    if (suppressed) *suppressed = false;
    if (!suppressed || !parent_assets || !parent || !current(owner, frame))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native held replacement lost its actual source frame");
    uint32_t physical;
    qa_application_native_q3_entity actual;
    if (!qa_q3_source_actor_slot(frame->source.source_game, actor, &physical, error) ||
        !qa_application_native_q3_presentation_entity(frame->application, &frame->source,
            physical, &actual, error)) return false;
    if (!actual.present || !qa_actor_id_equal(actual.binding.actor, actor) ||
        actual.state.powerups != powerups)
        return frontend_fail(error, QA_ERROR_FORMAT, "Native held parent has no physical full actor binding");
    qa_application_equipment_view source;
    if (!qa_application_equipment_read(frame->application, actual.binding.actor, &source, error)) return false;
    if (!source.selected) return true;
    if (source.family == QA_GAME_Q3) {
        bool authored = false;
        if (!frontend_equipment_source_native_held_from(owner->source, actual.binding.actor,
                parent_assets, parent, powerups, (parent->flags & 2) != 0,
                &authored, suppressed, error)) return false;
        if (!authored) return true;
    }
    void *token = NULL; bool admitted = false;
    if (!frontend_equipment_source_held_begin_from(owner->source, actual.binding.actor, parent_assets, parent,
            &token, &admitted, error)) return false;
    if (!admitted) return true;
    const q3n_media_view *media = q3n_media_read(frame->media);
    bool okay = media != NULL;
    qa_q3_ref_entity pass = *parent;
    pass.shader_time = 0;
    memset(pass.color, 255, sizeof(pass.color));
    if (okay && (powerups & 16)) {
        pass.custom_shader = media->graphics[Q3N_G_INVIS];
        okay = owner->services.held_pass(owner->services.context, token, &pass, error);
    } else if (okay) {
        pass.custom_shader = 0;
        okay = owner->services.held_pass(owner->services.context, token, &pass, error);
        if (okay && (powerups & 4)) {
            pass.custom_shader = media->graphics[Q3N_G_BATTLE_WEAPON];
            okay = owner->services.held_pass(owner->services.context, token, &pass, error);
        }
        if (okay && (powerups & 2)) {
            pass.custom_shader = media->graphics[Q3N_G_QUAD_WEAPON];
            okay = owner->services.held_pass(owner->services.context, token, &pass, error);
        }
    }
    if (okay) okay = owner->services.held_submit(owner->services.context, token, error);
    owner->services.held_release(owner->services.context, token);
    if (!okay && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "Native held replacement lost its actual powerup media");
    if (okay) *suppressed = true;
    return okay;
}
static bool held(void *context, const q3n_frame *frame, const qa_q3_entity *state,
    const qa_q3_ref_entity *parent, bool *suppressed, qa_error *error)
{
    equipment_native *owner = context;
    if (suppressed) *suppressed = false;
    if (!state || state->number < 0 || !current(owner, frame))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native held replacement lost its physical source entity");
    qa_application_native_q3_entity actual;
    if (!qa_application_native_q3_presentation_entity(frame->application, &frame->source,
            (uint32_t)state->number, &actual, error)) return false;
    if (!actual.present || actual.state.number != state->number)
        return frontend_fail(error, QA_ERROR_FORMAT, "Native held parent lost its source entity row");
    return held_actor(owner, frame, actual.binding.actor, frame->assets, parent,
        state->powerups, suppressed, error);
}

bool frontend_equipment_native_character_held(void *context, const q3n_frame *frame,
    qa_actor_id actor, const qa_q3_presentation_assets *parent_assets,
    const qa_q3_ref_entity *parent, int32_t source_powerups,
    bool *suppressed, qa_error *error)
{
    return held_actor(context, frame, actor, parent_assets, parent, source_powerups, suppressed, error);
}
static bool prepare_view(void *context, const qa_q3_refdef *definition,
    qa_q3_scene_options *options, qa_error *error)
{
    equipment_native *owner = context;
    return owner && frontend_equipment_source_prepare_view(owner->source, definition, options, error);
}
static bool submit_view(void *context, const qa_q3_scene_options *options,
    qa_scene_frame *frame, qa_error *error)
{
    equipment_native *owner = context;
    return owner && frontend_equipment_source_submit(owner->source, options, frame, error);
}
static bool idle(const void *context)
{
    const equipment_native *owner = context;
    return owner && !owner->entered && !owner->frame && frontend_equipment_source_idle(owner->source);
}
static bool destroy(void *context, qa_error *error)
{
    equipment_native *owner = context;
    if (!idle(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Native equipment retains its actual frame scope");
    if (!frontend_equipment_source_destroy(owner->source, error)) return false;
    free(owner); return true;
}
static bool rebind_ready(const void *context, const qa_frontend *frontend, qa_error *error)
{
    const equipment_native *owner = context;
    return owner && owner->frontend == frontend && idle(owner) &&
        frontend_equipment_source_rebind_ready(owner->source, frontend, error);
}
static void rebind(void *context, qa_frontend *frontend)
{
    equipment_native *owner = context;
    frontend_equipment_source_rebind(owner->source, frontend); owner->frontend = frontend;
}
static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t count = io->direction == QA_SOURCE_SAVE_WRITE ? bytes->size : 0;
    if (!qa_source_save_count(io, &count, 64u * 1024u * 1024u)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)bytes->data, count);
    if (io->offset > io->input.size || count > io->input.size - io->offset) return false;
    *bytes = (qa_bytes){io->input.data + io->offset, count}; io->offset += count;
    return true;
}
static bool checkpoint(const void *context, qa_buffer *out, qa_error *error)
{
    const equipment_native *owner = context;
    if (!idle(owner) || !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native equipment capture requires its actual returned owner");
    qa_buffer source = {0};
    if (!frontend_equipment_source_checkpoint(owner->source, &source, error)) return false;
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','F','E','N'}; uint32_t version = 1;
    bool hud = owner->hud_requested, view = owner->view_requested;
    qa_bytes bytes = {source.data, source.size};
    bool okay = qa_source_save_writer(&io, qa_application_session(owner->frontend->application), error) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) && qa_source_save_u32(&io, &version) &&
        qa_source_save_bool(&io, &hud) && qa_source_save_bool(&io, &view) &&
        blob(&io, &bytes) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&source); return okay;
}
static bool restore(void *context, const qa_application_q3_client_context *client,
    qa_bytes bytes, qa_error *error)
{
    equipment_native *owner = context;
    if (!idle(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Native equipment import requires its fresh empty owner");
    qa_source_save_io io = {0}; uint8_t magic[4]; uint32_t version;
    bool hud = false, view = false; qa_bytes source = {0};
    bool okay = qa_source_save_reader(&io, qa_application_session(owner->frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) && !memcmp(magic, "QFEN", 4) &&
        qa_source_save_u32(&io, &version) && version == 1 && qa_source_save_bool(&io, &hud) &&
        qa_source_save_bool(&io, &view) && blob(&io, &source) && qa_source_save_finish(&io, NULL);
    if (okay) okay = frontend_equipment_source_restore(owner->source, client, source, error);
    if (okay) { owner->hud_requested = hud; owner->view_requested = view; }
    qa_source_save_dispose(&io);
    if (!okay && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "Invalid actual native equipment continuation");
    return okay;
}
bool frontend_equipment_native_weapon(const void *context, qa_application_equipment_view *out,
    bool *requested, qa_error *error)
{
    const equipment_native *owner = context;
    return owner && frontend_equipment_source_weapon(owner->source, out, requested, error);
}
bool frontend_equipment_native_compose(void *context, frontend_native_q3 *row,
    frontend_native_q3_composition *out, qa_error *error)
{
    qa_frontend *frontend = context; frontend_native_q3_view view;
    qa_native_q3_wire_basis basis; qa_q3_presentation_binding backend;
    if (!frontend || !out || !frontend_native_q3_factory_view(row, &view, error) ||
        view.seat >= frontend->options.seats || !view.receiver ||
        !qa_native_q3_wire_reader_basis(view.reader, &basis, error) || basis.application != frontend->application ||
        !qa_q3_presentation_binding_read(view.presentation, &backend, error) || backend.frame != &frontend->frame)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native equipment constructor lacks its actual frontend owners");
    equipment_native *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining native equipment composition");
    owner->frontend = frontend; owner->row = row; owner->owners = view;
    frontend_equipment_source_options options = {.frontend = frontend, .receiver = view.receiver,
        .seat = view.launch_seat, .physical_seat = view.seat, .assets = view.assets,
        .presentation = view.presentation, .lease = row, .borrow = borrow, .current = context_current,
        .release = release, .requests = requests, .requests_context = owner};
    if (!frontend_equipment_source_create(&options, &owner->source, error)) { free(owner); return false; }
    frontend_equipment_source_services(owner->source, &owner->services);
    *out = (frontend_native_q3_composition){.context = owner, .idle = idle, .destroy = destroy,
        .rebind_ready = rebind_ready, .rebind = rebind, .checkpoint = checkpoint, .restore = restore,
        .begin_frame = begin, .end_frame = end, .held_weapon = held, .view_weapon = view_weapon,
        .weapon_warning = warning,
        .scene_cleared = clear, .prepare_view = prepare_view, .submit_view = submit_view};
    return true;
}
