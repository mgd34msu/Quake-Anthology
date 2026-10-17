#include "device_backend.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

typedef struct sdl2_backend {
    audio_native_backend base;
    void (SDLCALL *SDL_SetMainReady)(void);
    int (SDLCALL *SDL_InitSubSystem)(Uint32);
    void (SDLCALL *SDL_QuitSubSystem)(Uint32);
    SDL_bool (SDLCALL *SDL_SetHint)(const char *,const char *);
    const char *(SDLCALL *SDL_GetHint)(const char *);
    const char *(SDLCALL *SDL_GetError)(void);
    int (SDLCALL *SDL_GetNumAudioDevices)(int);
    const char *(SDLCALL *SDL_GetAudioDeviceName)(int,int);
    SDL_AudioDeviceID (SDLCALL *SDL_OpenAudioDevice)(const char *,int,const SDL_AudioSpec *,SDL_AudioSpec *,int);
    int (SDLCALL *SDL_QueueAudio)(SDL_AudioDeviceID,const void *,Uint32);
    Uint32 (SDLCALL *SDL_GetQueuedAudioSize)(SDL_AudioDeviceID);
    void (SDLCALL *SDL_ClearQueuedAudio)(SDL_AudioDeviceID);
    void (SDLCALL *SDL_PauseAudioDevice)(SDL_AudioDeviceID,int);
    SDL_AudioStatus (SDLCALL *SDL_GetAudioDeviceStatus)(SDL_AudioDeviceID);
    void (SDLCALL *SDL_CloseAudioDevice)(SDL_AudioDeviceID);
} sdl2_backend;

static bool failure(sdl2_backend *b,const char *operation,qa_error *e)
{
    qa_error_set(e,QA_ERROR_IO,0,"%s: %s",operation,b->SDL_GetError());return false;
}
static bool initialize(audio_native_backend *base,qa_error *e)
{
    sdl2_backend *b=(sdl2_backend *)base;b->SDL_SetMainReady();
    if(!b->SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS,"1")) {
        const char *current=b->SDL_GetHint(SDL_HINT_NO_SIGNAL_HANDLERS);
        if(!current || strcmp(current,"1")) return failure(b,SDL_HINT_NO_SIGNAL_HANDLERS,e);
    }
    return b->SDL_InitSubSystem(SDL_INIT_AUDIO)>=0 || failure(b,"SDL_InitSubSystem AUDIO",e);
}
static bool names(audio_native_backend *base,qa_audio_device_name_fn callback,void *context,qa_error *e)
{
    sdl2_backend *b=(sdl2_backend *)base;int count=b->SDL_GetNumAudioDevices(0);
    /* SDL2 drivers without enumeration can still open their default output. */
    for(int i=0;i<count;++i) {
        const char *name=b->SDL_GetAudioDeviceName(i,0);
        if(!name) return failure(b,"SDL_GetAudioDeviceName",e);
        callback(context,name);
    }
    return true;
}
static bool open_port(audio_native_backend *base,const qa_audio_device_options *options,
    audio_native_port **out,unsigned *buffer_frames,bool *unavailable,qa_error *e)
{
    sdl2_backend *b=(sdl2_backend *)base;*unavailable=false;
    SDL_AudioSpec desired={0},obtained={0};
    desired.freq=(int)options->format.sample_rate;
    desired.format=(SDL_AudioFormat)(options->format.sample_bits==16?AUDIO_S16SYS:AUDIO_U8);
    desired.channels=(Uint8)options->format.channels;desired.samples=(Uint16)options->buffer_frames;
    if(options->name) (void)b->SDL_GetNumAudioDevices(0);
    SDL_AudioDeviceID id=b->SDL_OpenAudioDevice(options->name,0,&desired,&obtained,0);
    if(!id) {*unavailable=true;return failure(b,"SDL_OpenAudioDevice",e);}
    if(obtained.freq!=desired.freq || obtained.format!=desired.format || obtained.channels!=desired.channels ||
       !obtained.samples || obtained.size!=(Uint32)obtained.samples*options->format.channels*(options->format.sample_bits/8) ||
       obtained.silence!=(options->format.sample_bits==8?128:0)) {
        b->SDL_CloseAudioDevice(id);qa_error_set(e,QA_ERROR_UNSUPPORTED,0,"SDL changed the requested audio input format");return false;
    }
    audio_native_port *port=malloc(sizeof(*port));
    if(!port) {b->SDL_CloseAudioDevice(id);qa_error_set(e,QA_ERROR_MEMORY,0,"Allocating native audio port");return false;}
    *port=(audio_native_port){.backend=base,.handle.device=id};*out=port;*buffer_frames=obtained.samples;return true;
}
static bool queued(const audio_native_port *port,size_t *out,qa_error *e)
{
    sdl2_backend *b=(sdl2_backend *)port->backend;(void)e;
    *out=b->SDL_GetQueuedAudioSize(port->handle.device);return true;
}
static bool queue(audio_native_port *port,const void *data,size_t bytes,qa_error *e)
{
    sdl2_backend *b=(sdl2_backend *)port->backend;
    return b->SDL_QueueAudio(port->handle.device,data,(Uint32)bytes)>=0 || failure(b,"SDL_QueueAudio",e);
}
static bool clear(audio_native_port *port,qa_error *e)
{
    sdl2_backend *b=(sdl2_backend *)port->backend;(void)e;b->SDL_ClearQueuedAudio(port->handle.device);return true;
}
static bool pause_port(audio_native_port *port,bool paused,qa_error *e)
{
    sdl2_backend *b=(sdl2_backend *)port->backend;(void)e;b->SDL_PauseAudioDevice(port->handle.device,paused?1:0);return true;
}
static bool paused(const audio_native_port *port,bool *out,qa_error *e)
{
    sdl2_backend *b=(sdl2_backend *)port->backend;SDL_AudioStatus status=b->SDL_GetAudioDeviceStatus(port->handle.device);
    if(status==SDL_AUDIO_STOPPED) return failure(b,"SDL_GetAudioDeviceStatus",e);
    *out=status==SDL_AUDIO_PAUSED;return true;
}
static void close_port(audio_native_port *port)
{
    sdl2_backend *b=(sdl2_backend *)port->backend;b->SDL_CloseAudioDevice(port->handle.device);free(port);
}
static void destroy(audio_native_backend *base)
{
    sdl2_backend *b=(sdl2_backend *)base;
    if(base->initialized) b->SDL_QuitSubSystem(SDL_INIT_AUDIO);
    qa_platform_library_close(base->library);free(b);
}
static const audio_native_ops operations={initialize,names,open_port,queued,queue,clear,pause_port,paused,close_port,destroy};
static bool bind_api(qa_platform_library *library,void *context,qa_error *error)
{
    sdl2_backend *api=context;
#define B(name) QA_PLATFORM_LIBRARY_BIND(library,api,name,error)
    B(SDL_SetMainReady);B(SDL_InitSubSystem);B(SDL_QuitSubSystem);B(SDL_SetHint);B(SDL_GetHint);B(SDL_GetError);
    B(SDL_GetNumAudioDevices);B(SDL_GetAudioDeviceName);B(SDL_OpenAudioDevice);B(SDL_QueueAudio);
    B(SDL_GetQueuedAudioSize);B(SDL_ClearQueuedAudio);B(SDL_PauseAudioDevice);B(SDL_GetAudioDeviceStatus);B(SDL_CloseAudioDevice);
#undef B
    return true;
}
bool audio_native_sdl2_load(audio_native_backend **out,qa_error *e)
{
    sdl2_backend *b=calloc(1,sizeof(*b));
    if(!b) {qa_error_set(e,QA_ERROR_MEMORY,0,"Allocating SDL2 audio API");return false;}
    b->base=(audio_native_backend){.ops=&operations,.references=1,.maximum_bytes=UINT32_MAX};
    if(!qa_platform_library_open(QA_PLATFORM_LIBRARY_SDL2,bind_api,b,&b->base.library,e)) {free(b);return false;}
    *out=&b->base;return true;
}
