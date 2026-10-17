#include "device_backend.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sdl3_backend {
    audio_native_backend base;
    void (SDLCALL *SDL_SetMainReady)(void);
    bool (SDLCALL *SDL_InitSubSystem)(SDL_InitFlags);
    void (SDLCALL *SDL_QuitSubSystem)(SDL_InitFlags);
    bool (SDLCALL *SDL_SetHint)(const char *,const char *);
    const char *(SDLCALL *SDL_GetHint)(const char *);
    const char *(SDLCALL *SDL_GetError)(void);
    SDL_AudioDeviceID *(SDLCALL *SDL_GetAudioPlaybackDevices)(int *);
    const char *(SDLCALL *SDL_GetAudioDeviceName)(SDL_AudioDeviceID);
    void (SDLCALL *SDL_free)(void *);
    bool (SDLCALL *SDL_GetAudioDeviceFormat)(SDL_AudioDeviceID,SDL_AudioSpec *,int *);
    SDL_AudioDeviceID (SDLCALL *SDL_GetAudioStreamDevice)(SDL_AudioStream *);
    bool (SDLCALL *SDL_GetAudioStreamFormat)(SDL_AudioStream *,SDL_AudioSpec *,SDL_AudioSpec *);
    SDL_AudioStream *(SDLCALL *SDL_OpenAudioDeviceStream)(SDL_AudioDeviceID,const SDL_AudioSpec *,SDL_AudioStreamCallback,void *);
    int (SDLCALL *SDL_GetAudioStreamQueued)(SDL_AudioStream *);
    bool (SDLCALL *SDL_PutAudioStreamData)(SDL_AudioStream *,const void *,int);
    bool (SDLCALL *SDL_ClearAudioStream)(SDL_AudioStream *);
    bool (SDLCALL *SDL_PauseAudioStreamDevice)(SDL_AudioStream *);
    bool (SDLCALL *SDL_ResumeAudioStreamDevice)(SDL_AudioStream *);
    bool (SDLCALL *SDL_AudioDevicePaused)(SDL_AudioDeviceID);
    void (SDLCALL *SDL_DestroyAudioStream)(SDL_AudioStream *);
} sdl3_backend;

static bool failure(sdl3_backend *b,const char *operation,qa_error *e)
{
    qa_error_set(e,QA_ERROR_IO,0,"%s: %s",operation,b->SDL_GetError());return false;
}
static bool hint(sdl3_backend *b,const char *name,const char *value,qa_error *e)
{
    if(b->SDL_SetHint(name,value)) return true;
    const char *current=b->SDL_GetHint(name);
    if(current && !strcmp(current,value)) return true;
    return failure(b,name,e);
}
static bool initialize(audio_native_backend *base,qa_error *e)
{
    sdl3_backend *b=(sdl3_backend *)base;b->SDL_SetMainReady();
    if(!hint(b,SDL_HINT_NO_SIGNAL_HANDLERS,"1",e)) return false;
    const char *driver=getenv("SDL_AUDIODRIVER");
    if(!getenv("SDL_AUDIO_DRIVER") && driver && !hint(b,SDL_HINT_AUDIO_DRIVER,driver,e)) return false;
    return b->SDL_InitSubSystem(SDL_INIT_AUDIO) || failure(b,"SDL_InitSubSystem AUDIO",e);
}
static bool names(audio_native_backend *base,qa_audio_device_name_fn callback,void *context,qa_error *e)
{
    sdl3_backend *b=(sdl3_backend *)base;int count=0;
    SDL_AudioDeviceID *devices=b->SDL_GetAudioPlaybackDevices(&count);
    if(!devices) return failure(b,"SDL_GetAudioPlaybackDevices",e);
    bool okay=true;
    for(int i=0;i<count;++i) {
        const char *name=b->SDL_GetAudioDeviceName(devices[i]);
        if(!name || !*name) {okay=failure(b,"SDL_GetAudioDeviceName",e);break;}
        callback(context,name);
    }
    b->SDL_free(devices);return okay;
}
static bool open_port(audio_native_backend *base,const qa_audio_device_options *options,
    audio_native_port **out,unsigned *buffer_frames,bool *unavailable,qa_error *e)
{
    sdl3_backend *b=(sdl3_backend *)base;*unavailable=false;
    SDL_AudioDeviceID device=SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if(options->name) {
        int count=0;SDL_AudioDeviceID *devices=b->SDL_GetAudioPlaybackDevices(&count);
        if(!devices) {*unavailable=true;return failure(b,"SDL_GetAudioPlaybackDevices",e);}
        device=0;
        for(int i=0;i<count;++i) {
            const char *name=b->SDL_GetAudioDeviceName(devices[i]);
            if(name && !strcmp(name,options->name)) {device=devices[i];break;}
        }
        b->SDL_free(devices);
        if(!device) {*unavailable=true;qa_error_set(e,QA_ERROR_IO,0,"Audio output is unavailable: %s",options->name);return false;}
    }
    SDL_AudioSpec hardware={0};int frames=0;
    if(!b->SDL_GetAudioDeviceFormat(device,&hardware,&frames)) return failure(b,"SDL_GetAudioDeviceFormat preferred",e);
    if(hardware.freq<=0) {qa_error_set(e,QA_ERROR_IO,0,"SDL returned an invalid preferred audio rate");return false;}
    uint64_t requested=((uint64_t)options->buffer_frames*(uint32_t)hardware.freq+options->format.sample_rate-1)/options->format.sample_rate;
    char value[32];snprintf(value,sizeof(value),"%llu",(unsigned long long)requested);
    (void)b->SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,value);
    SDL_AudioSpec input={.format=options->format.sample_bits==16?SDL_AUDIO_S16:SDL_AUDIO_U8,
        .channels=(int)options->format.channels,.freq=(int)options->format.sample_rate};
    SDL_AudioStream *stream=b->SDL_OpenAudioDeviceStream(device,&input,NULL,NULL);
    if(!stream) {*unavailable=true;return failure(b,"SDL_OpenAudioDeviceStream",e);}
    SDL_AudioSpec actual={0};bool okay=b->SDL_GetAudioStreamFormat(stream,&actual,NULL);
    if(!okay) failure(b,"SDL_GetAudioStreamFormat",e);
    if(okay && (actual.format!=input.format || actual.channels!=input.channels || actual.freq!=input.freq)) {
        qa_error_set(e,QA_ERROR_UNSUPPORTED,0,"SDL changed the requested audio input format");okay=false;
    }
    if(okay && !b->SDL_GetAudioDeviceFormat(b->SDL_GetAudioStreamDevice(stream),&hardware,&frames))
        okay=failure(b,"SDL_GetAudioDeviceFormat opened",e);
    uint64_t converted=0;
    if(okay && (hardware.freq<=0 || frames<=0)) {
        qa_error_set(e,QA_ERROR_IO,0,"SDL returned an invalid audio buffer specification");okay=false;
    }
    if(okay) converted=((uint64_t)(unsigned)frames*options->format.sample_rate+(uint32_t)hardware.freq-1)/(uint32_t)hardware.freq;
    if(okay && converted>UINT_MAX) {qa_error_set(e,QA_ERROR_UNSUPPORTED,0,"SDL audio buffer exceeds its input frame range");okay=false;}
    uint8_t probe[32];size_t bytes=8*options->format.channels*(options->format.sample_bits/8);
    memset(probe,options->format.sample_bits==8?128:0,bytes);
    if(okay && !b->SDL_PutAudioStreamData(stream,probe,(int)bytes)) okay=failure(b,"SDL_PutAudioStreamData probe",e);
    if(okay) {
        int queued_bytes=b->SDL_GetAudioStreamQueued(stream);
        if(queued_bytes<0) okay=failure(b,"SDL_GetAudioStreamQueued probe",e);
        else if(queued_bytes!=(int)bytes) {
            qa_error_set(e,QA_ERROR_UNSUPPORTED,0,"SDL audio stream does not retain exact paused input bytes");okay=false;
        }
    }
    if(okay && !b->SDL_ClearAudioStream(stream)) okay=failure(b,"SDL_ClearAudioStream probe",e);
    audio_native_port *port=okay?malloc(sizeof(*port)):NULL;
    if(okay && !port) {qa_error_set(e,QA_ERROR_MEMORY,0,"Allocating native audio port");okay=false;}
    if(!okay) {b->SDL_DestroyAudioStream(stream);return false;}
    *port=(audio_native_port){.backend=base,.handle.stream=stream};
    *out=port;*buffer_frames=(unsigned)converted;return true;
}
static bool queued(const audio_native_port *port,size_t *out,qa_error *e)
{
    sdl3_backend *b=(sdl3_backend *)port->backend;int count=b->SDL_GetAudioStreamQueued(port->handle.stream);
    if(count<0) return failure(b,"SDL_GetAudioStreamQueued",e);
    *out=(size_t)count;return true;
}
static bool queue(audio_native_port *port,const void *data,size_t bytes,qa_error *e)
{
    sdl3_backend *b=(sdl3_backend *)port->backend;
    return b->SDL_PutAudioStreamData(port->handle.stream,data,(int)bytes) || failure(b,"SDL_PutAudioStreamData",e);
}
static bool clear(audio_native_port *port,qa_error *e)
{
    sdl3_backend *b=(sdl3_backend *)port->backend;
    return b->SDL_ClearAudioStream(port->handle.stream) || failure(b,"SDL_ClearAudioStream",e);
}
static bool pause_port(audio_native_port *port,bool paused,qa_error *e)
{
    sdl3_backend *b=(sdl3_backend *)port->backend;
    bool okay=paused?b->SDL_PauseAudioStreamDevice(port->handle.stream):b->SDL_ResumeAudioStreamDevice(port->handle.stream);
    return okay || failure(b,paused?"SDL_PauseAudioStreamDevice":"SDL_ResumeAudioStreamDevice",e);
}
static bool paused(const audio_native_port *port,bool *out,qa_error *e)
{
    sdl3_backend *b=(sdl3_backend *)port->backend;(void)e;
    *out=b->SDL_AudioDevicePaused(b->SDL_GetAudioStreamDevice(port->handle.stream));return true;
}
static void close_port(audio_native_port *port)
{
    sdl3_backend *b=(sdl3_backend *)port->backend;b->SDL_DestroyAudioStream(port->handle.stream);free(port);
}
static void destroy(audio_native_backend *base)
{
    sdl3_backend *b=(sdl3_backend *)base;
    if(base->initialized) b->SDL_QuitSubSystem(SDL_INIT_AUDIO);
    qa_platform_library_close(base->library);free(b);
}
static const audio_native_ops operations={initialize,names,open_port,queued,queue,clear,pause_port,paused,close_port,destroy};
static bool bind_api(qa_platform_library *library,void *context,qa_error *error)
{
    sdl3_backend *api=context;
#define B(name) QA_PLATFORM_LIBRARY_BIND(library,api,name,error)
    B(SDL_SetMainReady);B(SDL_InitSubSystem);B(SDL_QuitSubSystem);B(SDL_SetHint);B(SDL_GetHint);B(SDL_GetError);
    B(SDL_GetAudioPlaybackDevices);B(SDL_GetAudioDeviceName);B(SDL_free);B(SDL_GetAudioDeviceFormat);
    B(SDL_GetAudioStreamDevice);B(SDL_GetAudioStreamFormat);B(SDL_OpenAudioDeviceStream);B(SDL_GetAudioStreamQueued);
    B(SDL_PutAudioStreamData);B(SDL_ClearAudioStream);B(SDL_PauseAudioStreamDevice);B(SDL_ResumeAudioStreamDevice);
    B(SDL_AudioDevicePaused);B(SDL_DestroyAudioStream);
#undef B
    return true;
}
bool audio_native_sdl3_load(audio_native_backend **out,qa_error *e)
{
    sdl3_backend *b=calloc(1,sizeof(*b));
    if(!b) {qa_error_set(e,QA_ERROR_MEMORY,0,"Allocating SDL3 audio API");return false;}
    b->base=(audio_native_backend){.ops=&operations,.references=1,.maximum_bytes=INT_MAX};
    if(!qa_platform_library_open(QA_PLATFORM_LIBRARY_SDL3,bind_api,b,&b->base.library,e)) {free(b);return false;}
    *out=&b->base;return true;
}
