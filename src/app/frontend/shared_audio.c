#include "shared_audio.h"
#include "capture.h"
#include "qa/text.h"

struct frontend_shared_audio {
    qa_frontend *frontend;
    qa_application *application;
    qa_audio_engine *engine;
    qa_audio_device *device;
    const qa_cvars_edit *edit;
    qa_audio_engine_gains *gains;
    qa_audio_device_selection *selection;
    qa_audio_output_format output;
    float effects,music;
    char *values[5];
    bool prepared;
};
static const char *const names[5]={"s_outputRate","s_outputBits","s_outputChannels","volume","bgmvolume"};
static void release(frontend_shared_audio *owner)
{
    for (size_t i=0;i<5;++i) free(owner->values[i]);
    free(owner);
}
static bool values_current(const frontend_shared_audio *owner)
{
    for (size_t i=0;i<5;++i) {
        const qa_cvar_view *row=qa_cvars_edit_canonical_record(owner->edit,names[i]);
        if (!row || !row->value || !owner->values[i] || strcmp(row->value,owner->values[i])) return false;
    }
    return true;
}
static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static bool number(const qa_cvars_edit *edit,const char *name,double *out,qa_error *error)
{
    const qa_cvar_view *row=qa_cvars_edit_find(edit,name);
    if (!row || !row->value || !isfinite(row->number)) {
        (void)fail(error,"Prepared audio setting has no finite canonical value");
        return false;
    }
    *out=row->number; return true;
}
bool frontend_shared_audio_configuration(const qa_cvars_edit *edit,qa_audio_output_format *output,
    float *effects,float *music,qa_error *error)
{
    if (!output || !effects || !music ||
        !qa_cvars_edit_returned_is(edit,qa_cvars_edit_registry(edit)))
        return fail(error,"Audio projection requires its returned canonical preparation");
    double rate,bits,channels,effect_gain,music_gain;
    if (!number(edit,"s_outputRate",&rate,error) || !number(edit,"s_outputBits",&bits,error) ||
        !number(edit,"s_outputChannels",&channels,error) || !number(edit,"volume",&effect_gain,error) ||
        !number(edit,"bgmvolume",&music_gain,error)) return false;
    if (rate<8000 || rate>192000 || floor(rate)!=rate ||
        (bits!=8 && bits!=16) || (channels!=1 && channels!=2))
        return fail(error,"Prepared audio output requires its actual supported integer format");
    *output=(qa_audio_output_format){(uint32_t)rate,(unsigned)channels,(unsigned)bits};
    *effects=(float)fmax(0,fmin(1,effect_gain));
    *music=(float)fmax(0,fmin(1,music_gain));
    return true;
}
static bool current(const frontend_shared_audio *owner,qa_error *error)
{
    qa_frontend *f=owner?owner->frontend:NULL;
    return (f && f->application==owner->application && f->audio==owner->engine &&
        f->device==owner->device && !f->capture && !f->source_restoring &&
        frontend_seat_callbacks_returned(f)) ||
        fail(error,"Prepared audio lost its actual returned engine/device parents");
}
bool frontend_shared_audio_prepare(qa_frontend *f,const qa_cvars_edit *edit,
    frontend_shared_audio **out,qa_error *error)
{
    if (!f || !f->application || !edit || !out || *out ||
        qa_cvars_edit_registry(edit)!=qa_application_cvars(f->application) ||
        f->capture || f->source_restoring || !frontend_seat_callbacks_returned(f))
        return fail(error,"Audio preparation requires its actual canonical ENGINE and returned source boundary");
    qa_audio_output_format output; float effects,music;
    if (!frontend_shared_audio_configuration(edit,&output,&effects,&music,error)) return false;
    if (!f->audio) return !f->device || fail(error,"Native output has no retained actual audio engine");
    frontend_shared_audio *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining prepared shared audio");
    owner->frontend=f; owner->application=f->application; owner->engine=f->audio;
    owner->device=f->device; owner->edit=edit; owner->output=output;
    owner->effects=effects; owner->music=music;
    for (size_t i=0;i<5;++i) {
        const qa_cvar_view *row=qa_cvars_edit_canonical_record(edit,names[i]);
        if (!row || !row->value) { release(owner); return fail(error,"Prepared audio lacks its canonical scalar row"); }
        size_t size=strlen(row->value)+1;
        owner->values[i]=malloc(size);
        if (!owner->values[i]) { release(owner); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining prepared audio scalar text"); }
        memcpy(owner->values[i],row->value,size);
    }
    if (!qa_audio_engine_gains_prepare(owner->engine,effects,music,&owner->gains,error)) {
        release(owner); return false;
    }
    *out=owner;
    if (owner->device) {
        qa_audio_device_options options=qa_audio_device_requested_configuration(owner->device);
        options.format=output;
        if (!qa_audio_device_selection_prepare(owner->device,&options,&owner->selection,error)) return false;
    }
    owner->prepared=true; return true;
}
bool frontend_shared_audio_ready(frontend_shared_audio *owner,qa_error *error)
{
    if (!current(owner,error) || !owner->prepared ||
        qa_cvars_edit_registry(owner->edit)!=qa_application_cvars(owner->application))
        return fail(error,"Prepared audio has no current canonical publication");
    qa_audio_output_format output; float effects,music;
    if (!values_current(owner) || !frontend_shared_audio_configuration(owner->edit,&output,&effects,&music,error))
        return fail(error,"Canonical audio scalar text changed after preparation");
    if (output.sample_rate!=owner->output.sample_rate || output.channels!=owner->output.channels ||
        output.sample_bits!=owner->output.sample_bits || effects!=owner->effects || music!=owner->music)
        return fail(error,"Canonical audio settings changed after resource preparation");
    return qa_audio_engine_gains_ready(owner->gains,error) &&
        (!owner->selection || qa_audio_device_selection_ready(owner->selection,error));
}
bool frontend_shared_audio_ready_is(const frontend_shared_audio *owner)
{
    if (!current(owner,NULL) || !owner->prepared ||
        qa_cvars_edit_registry(owner->edit)!=qa_application_cvars(owner->application) ||
        !qa_cvars_edit_ready_is(owner->edit) || !values_current(owner)) return false;
    return qa_audio_engine_gains_ready(owner->gains,NULL) &&
        (owner->device?qa_audio_device_selection_ready_is(owner->selection,owner->device):!owner->selection);
}
qa_audio_engine_gains *frontend_shared_audio_gains(const frontend_shared_audio *owner)
{ return current(owner,NULL) && owner->prepared?owner->gains:NULL; }
bool frontend_shared_audio_parent_is(const frontend_shared_audio *owner,const qa_frontend *f,
    const qa_audio_engine *engine)
{ return owner && owner->frontend==f && owner->engine==engine && owner->gains &&
    owner->prepared && current(owner,NULL); }
bool frontend_shared_audio_music_gain(const frontend_shared_audio *owner,float *out)
{
    if (!out || !owner || !owner->prepared || !owner->gains || !current(owner,NULL)) return false;
    *out=owner->music; return true;
}
void frontend_shared_audio_publish(frontend_shared_audio **in)
{
    frontend_shared_audio *owner=*in;
    if (owner->selection) qa_audio_device_selection_publish(owner->selection);
    qa_audio_engine_gains_publish(owner->gains);
    owner->frontend->audio_output_format=owner->output;
    release(owner); *in=NULL;
}
bool frontend_shared_audio_abort(frontend_shared_audio **in,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_shared_audio *owner=*in;
    if (!current(owner,error)) return false;
    if (owner->selection && !qa_audio_device_selection_abort(owner->selection,error)) return false;
    qa_audio_engine_gains_abort(owner->gains);
    release(owner); *in=NULL; return true;
}
