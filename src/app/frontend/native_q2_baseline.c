#include "internal.h"
#include "native_q2_baseline.h"

typedef struct baseline_print {
    struct baseline_print *next;
    qa_native_host_print print;
    char text[];
} baseline_print;
typedef struct native_baseline {
    qa_frontend frontend;
    baseline_print *prints, **tail;
    char *clipboard;
    qa_error output_error;
} native_baseline;
static void retain_print(void *context, const qa_native_host_print *print)
{
    native_baseline *baseline=context;
    if (!print || !print->text || baseline->output_error.code!=QA_OK) return;
    size_t length=strlen(print->text);
    if (length>SIZE_MAX-sizeof(baseline_print)-1) {
        qa_error_set(&baseline->output_error,QA_ERROR_MEMORY,0,"Scratch native Q2 diagnostic extent overflow"); return;
    }
    baseline_print *item=malloc(sizeof(*item)+length+1);
    if (!item) { qa_error_set(&baseline->output_error,QA_ERROR_MEMORY,0,"Retaining scratch native Q2 diagnostic"); return; }
    item->next=NULL; item->print=*print; memcpy(item->text,print->text,length+1); item->print.text=item->text;
    *baseline->tail=item; baseline->tail=&item->next;
}
static void console_print(void *context, const qa_command_context *command, const char *text)
{
    qa_frontend *frontend=context;
    qa_native_host_print print={.kind=QA_NATIVE_HOST_PRINT_DEBUG,.client=command?command->actor:(qa_actor_id){0},.text=text};
    retain_print(frontend->native_output_context,&print);
}
static bool retain_clipboard(void *context, const char *text, qa_error *error)
{
    native_baseline *baseline=context; size_t length=strlen(text);
    if (length==SIZE_MAX) return frontend_fail(error,QA_ERROR_MEMORY,"scratch clipboard extent overflow");
    char *copy=malloc(length+1);
    if (!copy) return frontend_fail(error,QA_ERROR_MEMORY,"retaining scratch native Q2 clipboard");
    memcpy(copy,text,length+1); free(baseline->clipboard); baseline->clipboard=copy; return true;
}
bool frontend_clipboard_write(qa_frontend *frontend, const char *text, qa_error *error)
{
    if (!frontend || !text) return frontend_fail(error,QA_ERROR_ARGUMENT,"clipboard requires a frontend and text");
    if (frontend->native_clipboard) return frontend->native_clipboard(frontend->native_output_context,text,error);
    if (SDL_SetClipboardText(text)==0) return true;
    qa_error_set(error,QA_ERROR_IO,0,"writing clipboard: %s",SDL_GetError()); return false;
}
static bool services(void *context, qa_application *application, qa_actor_owner owner,
    qa_native_profile profile, qa_native_host_engine_services *engine,
    qa_native_host_q2_application_fn *out, void **application_context, qa_error *error)
{
    qa_frontend *frontend=context;
    if (!frontend || !application || !engine ||
        (profile!=QA_NATIVE_Q2_GAME_API3 && profile!=QA_NATIVE_Q2_GAME_API2023) ||
        (frontend->application && frontend->application!=application))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"scratch frontend binds one actual native Q2 GAME application");
    frontend->application=application;
    if (!frontend->tools && !frontend_tools_create_diagnostics(frontend,engine->content_files,error)) return false;
    return frontend_native_q2_services(frontend,application,owner,profile,engine,out,application_context,error);
}
bool frontend_native_q2_baseline_create(void *context, const qa_application_options *supplied,
    qa_application_options *out, void **owned_context, qa_error *error)
{
    qa_frontend *active=context;
    if (!active || active->stepping || !supplied || !out || !owned_context || *owned_context)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"scratch native Q2 frontend requires idle supplied services and empty output");
    native_baseline *baseline=calloc(1,sizeof(*baseline));
    if (!baseline) return frontend_fail(error,QA_ERROR_MEMORY,"allocating scratch native Q2 frontend");
    baseline->tail=&baseline->prints;
    qa_frontend *frontend=&baseline->frontend;
    frontend->options=active->options; frontend->options.application=*supplied;
    frontend->time_ns=active->time_ns; frontend->frame_number=active->frame_number;
    frontend->width=active->width; frontend->height=active->height;
    frontend->native_output_context=baseline; frontend->native_print=retain_print; frontend->native_clipboard=retain_clipboard;
    qa_scene_frame_init(&frontend->frame,QA_FRONTEND_COMMAND_OWNER);
    qa_audio_engine_options audio={.sample_rate=48000,.output_channels=2,.mix_frames=1024,.initial_voices=128};
    if (!qa_audio_engine_create(&audio,&frontend->audio,error)) {
        qa_scene_frame_destroy(&frontend->frame); free(baseline); return false;
    }
    qa_application_options options=*supplied;
    options.guest_context=frontend; options.native_q2_services=services; options.console_print=console_print;
    options.q3_services=NULL; options.q3_client_effect=NULL; options.q3_campaign_command=NULL;
    options.world_change_ready=NULL; options.before_world_change=NULL; options.world_retired=NULL;
    frontend->options.application=options;
    *out=options; *owned_context=baseline; return true;
}
bool frontend_native_q2_baseline_ready(void *context, qa_error *error)
{
    native_baseline *baseline=context;
    if (!baseline || !frontend_native_q2_callbacks_idle(&baseline->frontend) ||
        !qa_tools_callbacks_idle(frontend_tools_owner(&baseline->frontend)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"scratch native Q2 platform imports require returned callbacks");
    if (baseline->output_error.code!=QA_OK) { if (error) *error=baseline->output_error; return false; }
    return true;
}
bool frontend_native_q2_baseline_prepare(void *context, qa_application *candidate, qa_actor_owner owner,
    qa_application_native_baseline_services *services_out, qa_error *error)
{
    (void)context;
    qa_frontend *frontend=services_out?services_out->options.guest_context:NULL;
    if (!candidate || !owner || !services_out || services_out->context || !frontend || frontend->application!=candidate)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"native Q2 scratch imports require the candidate's real frontend service graph");
    qa_application_options options; void *owned=NULL;
    if (!frontend_native_q2_baseline_create(frontend,&services_out->options,&options,&owned,error)) return false;
    services_out->options=options; services_out->context=owned;
    services_out->ready=frontend_native_q2_baseline_ready; services_out->destroy=frontend_native_q2_baseline_destroy;
    return true;
}
bool frontend_native_q2_baseline_destroy(void *context, qa_error *error)
{
    native_baseline *baseline=context; if (!baseline) return true;
    qa_frontend *frontend=&baseline->frontend;
    if (frontend->stepping || frontend->native_q2 || frontend->sources ||
        !qa_tools_callbacks_idle(frontend_tools_owner(frontend)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"scratch native Q2 source leases must retire before platform consumers");
    if (!frontend_tools_destroy(frontend,error)) return false;
    qa_audio_engine_destroy(frontend->audio); qa_scene_frame_destroy(&frontend->frame);
    while (baseline->prints) { baseline_print *next=baseline->prints->next; free(baseline->prints); baseline->prints=next; }
    free(baseline->clipboard); free(frontend->audio_ids); free(baseline); return true;
}
