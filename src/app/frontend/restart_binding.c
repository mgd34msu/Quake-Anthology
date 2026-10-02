#include "restart_binding.h"
#include "restart.h"
#include "config_store.h"
#include "video_guests.h"

static bool current(void *context,const qa_command_context *command,qa_error *error)
{
    qa_frontend *f=context;
    return (f && f->application && command && command->origin!=QA_COMMAND_REMOTE &&
        qa_application_command_context_active(f->application,command)) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Restart request lost its actual application command owner");
}
static bool stage_input(void *context,const qa_command_invocation *command,bool *staged,qa_error *error)
{
    qa_frontend *f=context;
    return frontend_config_store_stage_input(f->config_store,command->console,command,staged,error);
}
static bool command_fields(void *context,qa_source_save_io *io,qa_command_context *command)
{
    qa_frontend *f=context;
    uint64_t registry=qa_actors_identity(qa_session_actors(qa_application_session(f->application)));
    uint32_t dialect=command->dialect,origin=command->origin;
    bool ok=qa_source_save_u64(io,&registry) && registry &&
        qa_source_save_u64(io,&command->session) && qa_source_save_u64(io,&command->owner) &&
        qa_source_save_u64(io,&command->client) && qa_source_save_u32(io,&command->seat) &&
        qa_source_save_u32(io,&dialect) && dialect<=QA_CONSOLE_Q3 &&
        qa_source_save_u32(io,&origin) && origin<=QA_COMMAND_REMOTE &&
        qa_source_save_bool(io,&command->direct) && qa_source_save_bool(io,&command->console_text) &&
        qa_source_save_u64(io,&command->registry) && qa_source_save_u64(io,&command->generation) &&
        qa_source_save_actor(io,&command->actor) && qa_source_save_text(io,&command->script);
    if (ok && io->direction==QA_SOURCE_SAVE_READ) {
        command->dialect=(qa_console_dialect)dialect; command->origin=(qa_command_origin)origin;
        if (command->registry==registry)
            command->registry=qa_actors_identity(qa_session_actors(qa_application_session(f->application)));
        else if (command->registry) return frontend_fail(io->error,QA_ERROR_FORMAT,"Restart command registry differs from its captured session");
    }
    return ok;
}
static bool prepare_video(void *context,void **out,qa_error *error)
{
    frontend_video_guests *ticket=NULL;
    bool ok=frontend_video_guests_prepare(context,&ticket,error);
    *out=ticket; return ok;
}
static bool validate_video(void *context,void *ticket,qa_error *error)
{ (void)context; return frontend_video_guests_current(ticket,error); }
static bool reopen_video(void *context,void *ticket,qa_error *error)
{ (void)context; return frontend_video_guests_reopen(ticket,error); }
static bool finish_video(void *context,void **slot,qa_error *error)
{
    (void)context; frontend_video_guests *ticket=*slot;
    bool ok=frontend_video_guests_finish(&ticket,error); *slot=ticket; return ok;
}
static bool abort_video(void *context,void **slot,qa_error *error)
{
    (void)context; frontend_video_guests *ticket=*slot;
    bool ok=frontend_video_guests_abort(&ticket,error); *slot=ticket; return ok;
}
static frontend_restart *create(qa_frontend *f,qa_error *error)
{
    static const char *const input_latched[]={"in_joystick","in_joystickProfile"};
    /* Renderer/color initialization applies its authentic latched inventory
     * when the new physical owner initializes. Device formats are immediate. */
    frontend_restart_options options={.frontend=f,.cvars=qa_application_cvars(f->application),
        .latched={[1]=input_latched},.latched_count={[1]=sizeof(input_latched)/sizeof(*input_latched)},
        .context=f,.current=current,.stage_input=stage_input,.save_context=command_fields,
        .prepare_video=prepare_video,.validate_video=validate_video,.reopen_video=reopen_video,
        .finish_video=finish_video,.abort_video=abort_video};
    return frontend_restart_create(&options,error);
}
bool frontend_restart_binding_create(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || f->restart)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restart controls already have their physical frontend");
    f->restart=create(f,error);
    return f->restart && frontend_restart_register(f->restart,qa_application_console(f->application),error);
}
bool frontend_restart_binding_destroy(qa_frontend *f,qa_error *error)
{
    if (!f) return true;
    if (!frontend_restart_destroy(f->restart,error)) return false;
    f->restart=NULL; return true;
}
bool frontend_restart_binding_checkpoint(const qa_frontend *f,qa_buffer *out,qa_error *error)
{ return f && frontend_restart_checkpoint(f->restart,out,error); }
bool frontend_restart_binding_restore(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    if (!f || !f->application || f->restart)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Restart import requires its detached physical frontend");
    f->restart=create(f,error);
    return f->restart && frontend_restart_restore(f->restart,bytes,error) &&
        frontend_restart_register(f->restart,qa_application_console(f->application),error);
}
void frontend_restart_binding_rebind(qa_frontend *f)
{ if (f) frontend_restart_rebind(f->restart,f,f); }
