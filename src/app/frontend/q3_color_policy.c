#include "q3_color_policy.h"
#include "shared_resource_policy.h"
#include "shared_register.h"
#include "shared_render_controls.h"
#include "q3_render_policy.h"
#include "qa/display_settings.h"
#include "qa/material_library_save.h"
#include "qa/material_source_scratch.h"
#include "qa/source_save.h"
#include <limits.h>
#include <math.h>

struct frontend_q3_color {
    qa_frontend *frontend;
    qa_application *application;
    qa_display *display;
    qa_gl_renderer *gl;
    qa_cpu_renderer *cpu;
    qa_display_gamma *gamma;
    qa_display *acquired_gamma_display;
    qa_q3_image_upload_options upload;
    qa_q3_color_lighting lighting;
    frontend_q3_color_ticket *ticket;
    bool initializing, lighting_ready, initialized;
};

struct frontend_q3_color_ticket {
    frontend_q3_color *owner;
    const qa_cvars_edit *edit;
    qa_display *target;
    qa_display_gamma_ticket *native;
    qa_display_endpoint endpoint;
    qa_q3_image_upload_options upload;
    qa_q3_color_lighting lighting;
    bool prepared, ready, published, restored, deferred_finish, deferred_abort;
};

static bool current(const frontend_q3_color *owner, qa_error *error)
{
    const qa_frontend *f = owner ? owner->frontend : NULL;
    return (f && f->source_color == owner && f->application == owner->application &&
        f->display == owner->display && f->cpu == owner->cpu && f->gl == owner->gl &&
        owner->gamma && qa_display_gamma_borrow(owner->display) == owner->gamma) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Source color lost its actual selected display and renderer");
}

static bool record(qa_frontend *f, const qa_cvars_edit *edit, const char *name,
    const qa_cvar_view **out, qa_error *error)
{
    qa_cvars *registry = f && f->application ? qa_application_cvars(f->application) : NULL;
    if (!registry || (edit && (qa_cvars_edit_registry(edit) != registry ||
        !qa_cvars_edit_returned_is(edit, registry))))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color requires its actual canonical ENGINE rows");
    *out = edit ? qa_cvars_edit_canonical_record(edit, name) : frontend_render_control_record(registry, name);
    return (*out && (*out)->value) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Source color lacks a physical canonical setting");
}

static bool gamma_acquire(frontend_q3_color *owner, const qa_cvars_edit *edit,
    qa_display *display, qa_error *error)
{
    if (owner->gamma) return true;
    const qa_cvar_view *ignore;
    if (!record(owner->frontend, edit, "r_ignorehwgamma", &ignore, error)) return false;
    owner->gamma = qa_display_gamma_borrow(display);
    if (owner->gamma) return true;
    if (!qa_display_gamma_begin(display, ignore->integer != 0, &owner->gamma, error)) return false;
    owner->acquired_gamma_display = display;
    return true;
}

static bool device_read(frontend_q3_color *owner, qa_display *display,
    qa_q3_color_device *out, uint32_t *maximum, qa_error *error)
{
    qa_display_gamma_capability gamma;
    qa_display_state state;
    if (!current(owner, error) || !qa_display_gamma_read(owner->gamma, &gamma, error) ||
        !qa_display_state_get(display, &state, error)) return false;
    uint32_t color;
    if (owner->gl) {
        const qa_gl_capabilities *caps = qa_gl_capabilities_get(owner->gl);
        if (!caps || !caps->maximum_texture_size)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color requires its queried GL framebuffer and texture limit");
        color = caps->color_bits; *maximum = caps->maximum_texture_size;
    } else {
        qa_cpu_capabilities caps;
        if (!qa_cpu_capabilities_read(owner->cpu, &caps, error)) return false;
        color = caps.color_bits; *maximum = 0;
    }
    if (color > INT32_MAX || state.backend != (owner->gl ? QA_DISPLAY_OPENGL : QA_DISPLAY_CPU))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color framebuffer differs from its selected physical renderer");
    *out = (qa_q3_color_device){.hardware_gamma = gamma.kind == QA_DISPLAY_GAMMA_ACCEPTED,
        .fullscreen = state.fullscreen != QA_DISPLAY_WINDOWED, .color_bits = (int32_t)color};
    return true;
}

static bool profile_read(frontend_q3_color *owner, const qa_cvars_edit *edit,
    qa_display *display, qa_q3_image_upload_options *out, qa_error *error)
{
    static const char *const names[] = {"r_gamma", "r_intensity", "r_overBrightBits",
        "r_picmip", "r_roundImagesDown", "r_simpleMipMaps", "r_colorMipLevels"};
    const qa_cvar_view *rows[7];
    for (size_t i = 0; i < 7; ++i)
        if (!record(owner->frontend, edit, names[i], &rows[i], error)) return false;
    qa_q3_image_upload_options next = {0};
    if (!device_read(owner, display, &next.color.device, &next.maximum_texture_size, error)) return false;
    next.color.gamma = (float)rows[0]->number; next.color.intensity = (float)rows[1]->number;
    next.color.requested_overbright_bits = rows[2]->integer;
    next.picmip = rows[3]->integer; next.round_down = rows[4]->integer != 0;
    next.simple_mips = rows[5]->integer != 0; next.color_mips = rows[6]->integer != 0;
    const qa_cvar_view *bits;
    if (!record(owner->frontend,edit,"r_texturebits",&bits,error)) return false;
    next.texture_bits=bits->integer;
    if (owner->initialized) next.s3tc=owner->upload.s3tc;
    else {
        const qa_cvar_view *compression,*extensions;
        if (!record(owner->frontend,edit,"r_ext_compressed_textures",&compression,error) ||
            !record(owner->frontend,edit,"r_allowExtensions",&extensions,error)) return false;
        const qa_gl_capabilities *caps=owner->gl?qa_gl_capabilities_get(owner->gl):NULL;
#ifdef _WIN32
        bool enabled=compression->integer!=0;
#else
        bool enabled=compression->number!=0;
#endif
        next.s3tc=caps && caps->s3tc && extensions->integer!=0 && enabled;
    }
    if (!qa_q3_image_upload_options_valid(&next, error)) return false;
    *out = next;
    return true;
}

bool frontend_q3_source_color_ensure(qa_frontend *f, qa_error *error)
{
    if (!f || !f->application || !f->display || (!!f->gl == !!f->cpu))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color initialization requires the selected physical renderer");
    if (f->source_color && f->source_color->initialized)
        return frontend_q3_source_color_publication_finish(f,error) && current(f->source_color, error);
    if (f->source_color && f->source_color->initializing)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color initialization is already entered");
    const qa_cvars_edit *edit = NULL;
    if (!frontend_resource_policy_admission_edit(f, &edit, error)) return false;
    if (!f->source_color) {
        frontend_q3_color *owner = calloc(1, sizeof(*owner));
        if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Source color authority");
        owner->frontend = f; owner->application = f->application;
        owner->display = f->display; owner->gl = f->gl; owner->cpu = f->cpu;
        f->source_color = owner;
    }
    frontend_q3_color *owner = f->source_color;
    owner->initializing = true;
    if (!frontend_source_color_register(f,edit,error) || !frontend_shared_q3_renderer_initialize(f,error)) {
        owner->initializing = false; return false;
    }
    if (!gamma_acquire(owner, edit, owner->display, error)) {
        owner->initializing = false; return false;
    }
    const qa_cvar_view *requested;
    qa_q3_color_device device; uint32_t maximum;
    bool ok = device_read(owner, owner->display, &device, &maximum, error) &&
        record(f, edit, "r_overBrightBits", &requested, error) &&
        qa_q3_color_lighting_read(&device, requested->integer, &owner->lighting, error);
    if (ok) owner->lighting_ready = true;
    if (ok) ok = frontend_source_color_clamp(f, edit, error) &&
        profile_read(owner, edit, owner->display, &owner->upload, error);
    if (ok && owner->upload.color.device.hardware_gamma)
        ok = qa_display_gamma_apply(owner->gamma, owner->upload.color.gamma, error);
    owner->initializing = false;
    owner->initialized = ok;
    return ok;
}

bool frontend_q3_source_color_lighting_read(qa_frontend *f, const qa_cvars_edit *edit,
    qa_q3_color_lighting *out, qa_error *error)
{
    if (!out || !f) return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid Source lighting observation");
    if (!f->source_color && !frontend_q3_source_color_ensure(f, error)) return false;
    frontend_q3_color *owner = f->source_color;
    if (!current(owner, error) || !owner->lighting_ready) return false;
    if (edit && owner->ticket && owner->ticket->edit == edit && owner->ticket->prepared &&
        !owner->ticket->published) { *out = owner->ticket->lighting; return true; }
    if (!edit) { *out = owner->lighting; return true; }
    const qa_cvar_view *requested; qa_q3_color_device device; uint32_t maximum;
    return record(f, edit, "r_overBrightBits", &requested, error) &&
        device_read(owner, owner->display, &device, &maximum, error) &&
        qa_q3_color_lighting_read(&device, requested->integer, out, error);
}

bool frontend_q3_source_color_device_read(qa_frontend *f, qa_q3_color_device *out, qa_error *error)
{
    if (!f || !out || !frontend_q3_source_color_ensure(f, error)) return false;
    uint32_t maximum;
    return device_read(f->source_color, f->display, out, &maximum, error);
}

static bool profile_device_current(frontend_q3_color *owner,qa_display *display,
    const qa_q3_image_upload_options *profile,qa_error *error)
{
    qa_q3_color_device device; uint32_t maximum;
    if (!device_read(owner,display,&device,&maximum,error)) return false;
    if (device.hardware_gamma!=profile->color.device.hardware_gamma ||
        (device.hardware_gamma && device.fullscreen!=profile->color.device.fullscreen) ||
        device.color_bits!=profile->color.device.color_bits || maximum!=profile->maximum_texture_size ||
        (device.hardware_gamma && !qa_display_gamma_applied_is(display)))
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Source output capability changed and requires an actual renderer restart");
    if (profile->s3tc && (!owner->gl || !qa_gl_capabilities_get(owner->gl)->s3tc))
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Source compression lost its actual initialized native capability");
    return true;
}

bool frontend_q3_source_upload_read(void *context, bool allow_picmip, bool mipmap,
    qa_q3_image_upload_options *out, qa_error *error)
{
    qa_frontend *f = context;
    if (!out || !f || !f->source_color || !f->source_color->initialized || !current(f->source_color, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source image registration lacks its initialized physical color owner");
    frontend_q3_color_ticket *ticket = f->source_color->ticket;
    bool candidate=ticket && ticket->prepared && !ticket->published;
    const qa_q3_image_upload_options *profile=candidate?&ticket->upload:&f->source_color->upload;
    if (!profile_device_current(f->source_color,candidate?ticket->target:f->display,profile,error)) return false;
    qa_q3_image_upload_options next=*profile;
    const qa_cvar_view *bits;
    if (!record(f,candidate?ticket->edit:NULL,"r_texturebits",&bits,error)) return false;
    next.texture_bits=bits->integer;
    next.allow_picmip = allow_picmip; next.mipmap = mipmap;
    *out=next;
    return true;
}

bool frontend_q3_source_output(qa_frontend *f,const qa_material_library *materials,
    qa_scene_rect rect,qa_error *error)
{
    if (!f || !qa_material_library_source_upload_is(materials,frontend_q3_source_upload_read,f) ||
        !qa_scene_resources_source_image_admit_is(qa_material_library_resource_owner(materials),
            frontend_q3_source_image_admit,f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source output lost its actual color-upload owner");
    if (!frontend_q3_source_color_ensure(f,error)) return false;
    if (!profile_device_current(f->source_color,f->display,&f->source_color->upload,error)) return false;
    return frontend_q3_source_begin_frame(f,0,error) &&
        qa_scene_frame_output_domain(&f->frame,rect,true,error);
}

bool frontend_q3_generic_overlay_begin(qa_frontend *f,qa_scene_rect rect,qa_error *error)
{
    if (!f || !rect.width || !rect.height)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Generic overlay requires its actual entered viewport");
    if ((f->cpu && f->gl) || (!f->cpu && !f->gl))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Generic overlay lost its actual physical renderer");
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):qa_gl_render_controls(f->gl);
    qa_material_source_scratch *scratch=qa_render_controls_source_scratch(controls,error);
    if (!scratch || !qa_material_source_swap_end(scratch,&f->frame,error)) return false;
    bool source=false,found=false;
    for (size_t i=f->frame.command_count;i;--i) {
        const qa_scene_command *command=f->frame.commands+i-1;
        if (command->kind!=QA_SCENE_COMMAND_OUTPUT_DOMAIN) continue;
        qa_scene_rect actual=command->data.output_domain.rect;
        if (actual.x==rect.x && actual.y==rect.y && actual.width==rect.width && actual.height==rect.height) {
            source=command->data.output_domain.source; found=true; break;
        }
    }
    if (!found && !(f->cpu?qa_cpu_output_domain_read(f->cpu,rect,&source,error):
        qa_gl_output_domain_read(f->gl,rect,&source,error))) return false;
    bool preblend=false;
    if (source) {
        if (!f->source_color || !current(f->source_color,error) ||
            !profile_device_current(f->source_color,f->display,&f->source_color->upload,error)) return false;
        preblend=!f->source_color->upload.color.device.hardware_gamma;
    }
    return qa_scene_frame_preblend_gamma(&f->frame,preblend,error);
}

bool frontend_q3_generic_overlay_end(qa_frontend *f,qa_error *error)
{
    return f && qa_scene_frame_preblend_gamma(&f->frame,false,error);
}

static bool recipient_image(void *context, const qa_scene_image *source,
    bool allow_picmip, bool mipmap, const qa_scene_image **out, qa_error *error)
{
    qa_frontend *f = context;
    if (!f || !source || !out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source recipient requires its reached image");
    qa_q3_image_upload_options upload;
    if (!frontend_q3_source_upload_read(f, allow_picmip, mipmap, &upload, error)) return false;
    qa_scene_resources *bank = qa_scene_image_resource_owner(source);
    if (!bank)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source recipient image lost its real retained bank");
    if (source->kind == QA_SCENE_DEPTH32F) { *out = source; return true; }
    if (!qa_scene_resources_set_source_image_admit(bank,frontend_q3_source_image_admit,f,error)) return false;
    qa_scene_image *mapped = NULL;
    if (!qa_scene_image_source_q3_recipient_variant(bank, source, &upload,
        frontend_q3_source_image_admit, f, &mapped, error)) return false;
    /* The bank owns the retained correspondence; the eventual draw pins this
     * borrowed immutable version independently until frame reset. */
    *out = mapped;
    qa_scene_image_release(mapped);
    return true;
}

bool frontend_q3_source_recipient(qa_frontend *f, qa_scene_world_input *input, qa_error *error)
{
    if (!f || !input || !f->source_color || !f->source_color->initialized ||
        !current(f->source_color, error) ||
        !profile_device_current(f->source_color, f->display, &f->source_color->upload, error)) return false;
    input->source_recipient_image = recipient_image;
    input->source_recipient_context = f;
    return true;
}

static bool generic_recipient_image(void *context,const qa_scene_image *source,
    bool allow_picmip,bool mipmap,const qa_scene_image **out,qa_error *error)
{
    qa_frontend *f=context;
    (void)allow_picmip;
    if(!f || !f->application || !source || !out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Generic recipient requires its actual reached image");
    if(!source->source_q3 || source->kind==QA_SCENE_DEPTH32F) { *out=source; return true; }
    qa_scene_resources *bank=qa_scene_image_resource_owner(source);
    if(!bank)return frontend_fail(error,QA_ERROR_ARGUMENT,"Generic recipient lost the admitted Source image owner");
    qa_scene_image *mapped=NULL;
    if(!qa_scene_image_generic_variant(bank,source,mipmap,&mapped,error))return false;
    *out=mapped;
    qa_scene_image_release(mapped);
    return true;
}

bool frontend_q3_generic_recipient(qa_frontend *f,qa_scene_world_input *input,qa_error *error)
{
    if(!f || !f->application || !input || input->view.seat>=f->options.seats)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Generic recipient requires its actual physical viewport");
    input->source_recipient_image=generic_recipient_image;
    input->source_recipient_context=f;
    return true;
}

bool frontend_q3_source_color_retire(qa_frontend *f, qa_error *error)
{
    if (!f) return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid Source color retirement");
    frontend_q3_color *owner = f->source_color;
    if (!owner) return true;
    if (!frontend_q3_source_color_publication_finish(f,error)) return false;
    if (owner->initializing || owner->ticket)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color retirement is entered");
    if (owner->gamma && (qa_display_gamma_parent_is(owner->gamma, owner->display) ||
        (owner->acquired_gamma_display && qa_display_gamma_parent_is(owner->gamma, owner->acquired_gamma_display))) &&
        !qa_display_gamma_release(&owner->gamma, error)) return false;
    free(owner); f->source_color = NULL;
    return true;
}

static bool ticket_current(const frontend_q3_color_ticket *ticket, bool published, qa_error *error)
{
    if (!ticket || !ticket->owner || ticket->owner->ticket != ticket ||
        ticket->published != published || !current(ticket->owner, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color ticket lost its actual parent");
    if (!published) {
        const qa_cvars_edit *actual = NULL;
        if (!frontend_resource_policy_admission_edit(ticket->owner->frontend, &actual, error)) return false;
        if (actual != ticket->edit)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color ticket lost its canonical candidate edit");
    }
    return true;
}

bool frontend_q3_source_color_prepare(qa_frontend *f, const qa_cvars_edit *edit,
    qa_display *target, frontend_q3_color_ticket **out, qa_error *error)
{
    if (f && !frontend_q3_source_color_publication_finish(f,error)) return false;
    if (!f || !target || !out || *out || !f->source_color ||
        !f->source_color->initialized || f->source_color->ticket || !current(f->source_color, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color preparation requires its initialized renderer parent");
    const qa_cvars_edit *actual = NULL;
    if (!frontend_resource_policy_admission_edit(f, &actual, error)) return false;
    if (actual != edit)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color preparation requires the real retained canonical edit");
    frontend_q3_color_ticket *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Preparing Source color authority");
    ticket->owner = f->source_color; ticket->edit = edit; ticket->target = target;
    ticket->owner->ticket = ticket; *out = ticket;
    if (!profile_read(ticket->owner, edit, target, &ticket->upload, error) ||
        !qa_q3_color_lighting_read(&ticket->upload.color.device,
            ticket->upload.color.requested_overbright_bits, &ticket->lighting, error)) return false;
    if (ticket->upload.color.device.hardware_gamma &&
        !qa_display_gamma_prepare(ticket->owner->gamma, target,
            ticket->upload.color.gamma, &ticket->native, error)) return false;
    ticket->prepared = true;
    return frontend_q3_source_color_ready(ticket, error);
}

bool frontend_q3_source_color_restore_prepare(qa_frontend *f,qa_display *target,
    frontend_q3_color_ticket **out,qa_error *error)
{
    if (f && !frontend_q3_source_color_publication_finish(f,error)) return false;
    if (!f || !target || !out || *out || !f->source_color ||
        !f->source_color->initialized || f->source_color->ticket || !current(f->source_color,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color restore publication requires its imported physical owner");
    const qa_cvars_edit *edit=NULL;
    if (!frontend_resource_policy_admission_edit(f,&edit,error)) return false;
    frontend_q3_color_ticket *ticket=calloc(1,sizeof(*ticket));
    if (!ticket) return frontend_fail(error,QA_ERROR_MEMORY,"Preparing imported Source color publication");
    ticket->owner=f->source_color; ticket->edit=edit; ticket->target=target; ticket->restored=true;
    ticket->upload=ticket->owner->upload; ticket->lighting=ticket->owner->lighting;
    ticket->owner->ticket=ticket; *out=ticket;
    if (ticket->upload.color.device.hardware_gamma &&
        !qa_display_gamma_prepare(ticket->owner->gamma,target,ticket->upload.color.gamma,&ticket->native,error)) return false;
    ticket->prepared=true;
    return frontend_q3_source_color_ready(ticket,error);
}

bool frontend_q3_source_color_ready(frontend_q3_color_ticket *ticket, qa_error *error)
{
    if (ticket) ticket->ready = false;
    if (!ticket_current(ticket, false, error) || !ticket->prepared) return false;
    qa_q3_image_upload_options actual;
    if (ticket->restored) {
        actual=ticket->upload;
        if (!profile_device_current(ticket->owner,ticket->target,&actual,error)) return false;
    } else if (!profile_read(ticket->owner, ticket->edit, ticket->target, &actual, error)) return false;
    if (!qa_q3_image_upload_options_equal(&actual, &ticket->upload))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Prepared Source color settings or native capability changed");
    if (ticket->native && !qa_display_gamma_ready(ticket->native, error)) return false;
    if (!qa_display_endpoint_read(ticket->target, &ticket->endpoint))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source color target lost its physical endpoint");
    ticket->ready = true;
    return true;
}

bool frontend_q3_source_color_ready_is(const frontend_q3_color_ticket *ticket)
{
    if (!ticket || !ticket->owner || ticket->owner->ticket != ticket || !ticket->prepared ||
        !ticket->ready || ticket->published || (ticket->edit &&
        !qa_cvars_edit_returned_is(ticket->edit, qa_application_cvars(ticket->owner->application))) ||
        !qa_display_endpoint_is(ticket->target, &ticket->endpoint)) return false;
    const qa_frontend *f = ticket->owner->frontend;
    return f && f->source_color == ticket->owner && f->application == ticket->owner->application &&
        f->display == ticket->owner->display && f->gl == ticket->owner->gl && f->cpu == ticket->owner->cpu &&
        (!ticket->native || qa_display_gamma_ready_is(ticket->native));
}

void frontend_q3_source_color_publish(frontend_q3_color_ticket *ticket)
{
    if (!ticket || !ticket->ready || ticket->published || ticket->owner->ticket != ticket) return;
    /* The enclosing video publication has already moved these actual parents. */
    qa_frontend *f = ticket->owner->frontend;
    if (f->display != ticket->target || qa_display_gamma_borrow(f->display) != ticket->owner->gamma) return;
    if (ticket->native) qa_display_gamma_publish(ticket->native);
    ticket->owner->display = f->display;
    ticket->owner->acquired_gamma_display = NULL;
    ticket->owner->upload = ticket->upload; ticket->owner->lighting = ticket->lighting;
    ticket->ready = false; ticket->published = true;
}

bool frontend_q3_source_color_abort(frontend_q3_color_ticket **out, qa_error *error)
{
    if (!out) return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid Source color abort");
    frontend_q3_color_ticket *ticket = *out;
    if (!ticket) return true;
    if (!ticket_current(ticket, false, error) ||
        !qa_display_gamma_abort(&ticket->native, error)) return false;
    ticket->owner->ticket = NULL; free(ticket); *out = NULL;
    return true;
}

bool frontend_q3_source_color_finish(frontend_q3_color_ticket **out, qa_error *error)
{
    if (!out) return frontend_fail(error, QA_ERROR_ARGUMENT, "Invalid Source color retirement");
    frontend_q3_color_ticket *ticket = *out;
    if (!ticket) return true;
    if (!ticket_current(ticket, true, error) ||
        !qa_display_gamma_finish(&ticket->native, error)) return false;
    ticket->owner->ticket = NULL; free(ticket); *out = NULL;
    return true;
}

void frontend_q3_source_color_defer_finish(frontend_q3_color_ticket **out)
{
    frontend_q3_color_ticket *ticket=out?*out:NULL;
    if (!ticket || !ticket->published || !ticket->owner || ticket->owner->ticket!=ticket) return;
    ticket->deferred_finish=true;
    *out=NULL;
}

void frontend_q3_source_color_defer_abort(frontend_q3_color_ticket **out)
{
    frontend_q3_color_ticket *ticket=out?*out:NULL;
    if (!ticket || ticket->published || !ticket->owner || ticket->owner->ticket!=ticket) return;
    ticket->deferred_abort=true;
    *out=NULL;
}

bool frontend_q3_source_color_publication_finish(qa_frontend *f,qa_error *error)
{
    if (!f) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid Source color publication retirement");
    frontend_q3_color *owner=f->source_color;
    if (!owner || !owner->ticket) return true;
    if (owner->ticket->deferred_abort) return frontend_q3_source_color_abort(&owner->ticket,error);
    if (!owner->ticket->deferred_finish) return true;
    return frontend_q3_source_color_finish(&owner->ticket,error);
}

static bool saved_fields(qa_source_save_io *io, bool *present, qa_q3_image_upload_options *upload)
{
    uint8_t magic[4]={'Q','F','C','G'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFCG",4) &&
        qa_source_save_bool(io,present) &&
        (!*present || qa_q3_image_upload_options_precision_codec(io,upload)) && (!*present || !upload->lightmap);
}
static bool saved_read(qa_bytes bytes,bool *present,qa_q3_image_upload_options *upload,qa_error *error)
{
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && saved_fields(&io,present,upload) &&
        qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_q3_source_color_checkpoint(const qa_frontend *f,qa_buffer *out,qa_error *error)
{
    if (!f || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid Source color continuation output");
    const frontend_q3_color *owner=f->source_color;
    if (owner && (!owner->initialized || owner->initializing || owner->ticket || !current(owner,error))) return false;
    bool present=owner!=NULL;
    qa_q3_image_upload_options upload=owner?owner->upload:(qa_q3_image_upload_options){0};
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && saved_fields(&io,&present,&upload) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_q3_source_color_restore_native(qa_frontend *f,qa_display *active,
    qa_bytes bytes,qa_error *error)
{
    if (!f || !f->application || f->source_color)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color native restore requires its empty candidate owner");
    bool present=false; qa_q3_image_upload_options upload={0};
    bool ok=saved_read(bytes,&present,&upload,error);
    if (!ok || !present) return ok;
    frontend_q3_color *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining restored Source color native parent");
    owner->frontend=f; owner->application=f->application; owner->upload=upload;
    f->source_color=owner;
    return gamma_acquire(owner,NULL,active,error);
}
bool frontend_q3_source_color_restore(qa_frontend *f,const qa_display_restore_guard *display_guard,
    qa_bytes bytes,qa_error *error)
{
    if (!f || !f->application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color restore requires its actual candidate owner");
    bool present=false; qa_q3_image_upload_options upload={0};
    bool ok=saved_read(bytes,&present,&upload,error);
    if (!ok) return false;
    if (!present) return !f->source_color ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Absent Source color import retains a candidate color owner");
    frontend_q3_color *owner=f->source_color;
    if (!owner || owner->frontend!=f || owner->application!=f->application ||
        owner->initialized || owner->initializing || owner->ticket ||
        !qa_q3_image_upload_options_equal(&owner->upload,&upload))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color import lacks its actual native preparation");
    qa_display_info info;
    qa_display_gamma *gamma=qa_display_gamma_borrow(f->display);
    qa_display_gamma_capability cap;
    if (!f->display || (!!f->gl==!!f->cpu))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color import lacks its actual display and single renderer");
    if (!qa_display_restore_info(display_guard,f->display,&info))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color import lost its retained native display recipe");
    if (!gamma || gamma!=owner->gamma || !qa_display_gamma_capability_read(gamma,&cap))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color import lost its actual prepared gamma capability");
    uint32_t color,maximum;
    if (f->gl) {
        const qa_gl_capabilities *actual=qa_gl_capabilities_get(f->gl);
        if (!actual) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source color import lacks its real GL capabilities");
        color=actual->color_bits; maximum=actual->maximum_texture_size;
    } else {
        qa_cpu_capabilities actual;
        if (!qa_cpu_capabilities_read(f->cpu,&actual,error)) return false;
        color=actual.color_bits; maximum=0;
    }
    if ((upload.s3tc && (!f->gl || !qa_gl_capabilities_get(f->gl)->s3tc)) ||
        color>INT32_MAX || upload.color.device.color_bits!=(int32_t)color ||
        upload.maximum_texture_size!=maximum || upload.color.device.hardware_gamma!=(cap.kind==QA_DISPLAY_GAMMA_ACCEPTED) ||
        upload.color.device.fullscreen!=(info.fullscreen!=QA_DISPLAY_WINDOWED) ||
        info.backend!=(f->gl?QA_DISPLAY_OPENGL:QA_DISPLAY_CPU))
        return frontend_fail(error,QA_ERROR_FORMAT,"Imported Source color differs from its real physical renderer capability");
    owner->display=f->display;
    owner->gl=f->gl; owner->cpu=f->cpu; owner->gamma=gamma; owner->upload=upload;
    if (!qa_q3_color_lighting_read(&upload.color.device,upload.color.requested_overbright_bits,&owner->lighting,error)) return false;
    owner->lighting_ready=owner->initialized=true;
    f->source_color=owner;
    return true;
}
