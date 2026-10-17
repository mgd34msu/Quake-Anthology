#ifndef QA_AUDIO_DEVICE_BACKEND_H
#define QA_AUDIO_DEVICE_BACKEND_H

#include "qa/audio.h"
#include "../platform/native_libraries.h"

typedef struct audio_native_backend audio_native_backend;
typedef struct audio_native_port {
    audio_native_backend *backend;
    union { uint32_t device; void *stream; } handle;
} audio_native_port;

typedef struct audio_native_ops {
    bool (*initialize)(audio_native_backend *,qa_error *);
    bool (*names)(audio_native_backend *,qa_audio_device_name_fn,void *,qa_error *);
    bool (*open)(audio_native_backend *,const qa_audio_device_options *,audio_native_port **,
        unsigned *,bool *,qa_error *);
    bool (*queued)(const audio_native_port *,size_t *,qa_error *);
    bool (*queue)(audio_native_port *,const void *,size_t,qa_error *);
    bool (*clear)(audio_native_port *,qa_error *);
    bool (*pause)(audio_native_port *,bool,qa_error *);
    bool (*paused)(const audio_native_port *,bool *,qa_error *);
    void (*close)(audio_native_port *);
    void (*destroy)(audio_native_backend *);
} audio_native_ops;

struct audio_native_backend {
    const audio_native_ops *ops;
    qa_platform_library *library;
    size_t references,maximum_bytes;
    bool initialized;
};

/* Loading only binds the complete native API. Device/init failures never
 * select a different backend. The device owns initialization and references. */
bool audio_native_sdl2_load(audio_native_backend **,qa_error *);
bool audio_native_sdl3_load(audio_native_backend **,qa_error *);

#endif
