#include "qa/platform_services.h"
#include "qa/audio.h"
#include "qa/audio_device_save.h"
#include "qa/source_save.h"
#include "qa/arena.h"
#include "codec_internal.h"

#include "device_backend.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { DEVICE_MIX_FRAMES = 4096 };

typedef struct device_pcm {
    qa_arena storage;
    int16_t *samples;
    size_t capacity, head, count;
} device_pcm;

/* The application serializes calls on a device. SDL owns its playback thread;
 * only the prefix named by submitted has been handed to that thread. */
struct qa_audio_device {
    audio_native_port *id;
    qa_audio_device_options options;
    audio_native_backend *backend;
    qa_error native_failure;
    size_t requested_maximum_queued_frames;
    char *name;
    device_pcm pcm;
    size_t submitted;
    qa_arena encoded_storage;
    uint8_t *encoded;
    size_t encoded_capacity;
    qa_audio_raw_stream *conversion;
    qa_audio_device_selection *selection;
    uintptr_t pump_engine_identity;
    uint32_t source_rate;
    uint64_t source_next, staged_start;
    size_t staged_frames;
    int16_t source[DEVICE_MIX_FRAMES * 2];
    float mixed[DEVICE_MIX_FRAMES * 2];
    uint64_t frequency, elapsed_ticks, playing_since;
    uint64_t previous_pump_frame;
    size_t pump_intervals[8], pump_interval_count, pump_interval_next;
    bool playing, paused, resume_on_attach;
    bool have_pump_engine, have_previous_pump, output_started, handoff_pending, pumping, capturing;
};

struct qa_audio_device_selection {
    qa_audio_device *device;
    qa_audio_device_options selected;
    size_t requested_maximum_queued_frames;
    size_t submitted;
    audio_native_port *previous, *replacement;
    char *name;
    qa_audio_raw_stream *previous_conversion, *conversion;
    device_pcm converted;
    qa_arena encoded_storage;
    uint8_t *encoded;
    size_t encoded_capacity;
    bool unchanged, ready, stopped, resume, invalidated;
};

static size_t automatic_target(const qa_audio_device_options *, const size_t *, size_t, bool);

static bool selection_blocks_mutation(qa_audio_device *device) {
    if (!device->selection) return false;
    device->selection->invalidated = true;
    return true;
}

static bool device_error(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

static audio_native_backend *backend_retain(audio_native_backend *backend) {
    if(backend) ++backend->references;
    return backend;
}
static void backend_release(audio_native_backend *backend) {
    if(backend && !--backend->references) backend->ops->destroy(backend);
}
static bool backend_open(audio_native_backend **out,qa_error *error) {
    audio_native_backend *backend=NULL;qa_error failure={0};
    if(!audio_native_sdl3_load(&backend,&failure)) {
        if(getenv("QUAKE_SDL3_LIBRARY") || failure.code==QA_ERROR_MEMORY) {
            if(error) *error=failure;
            return false;
        }
        if(!audio_native_sdl2_load(&backend,error)) return false;
    }
    if(!backend->ops->initialize(backend,error)) {backend_release(backend);return false;}
    backend->initialized=true;*out=backend;return true;
}
static void native_latch(qa_audio_device *device,const qa_error *error) {
    if(device->native_failure.code==QA_OK) device->native_failure=*error;
}
static bool native_check(qa_audio_device *device,qa_error *error) {
    if(device->native_failure.code==QA_OK) return true;
    if(error) *error=device->native_failure;
    device->native_failure=(qa_error){0};return false;
}
static bool native_pause(qa_audio_device *device,audio_native_port *port,bool paused) {
    qa_error error={0};
    if(!port || port->backend->ops->pause(port,paused,&error)) return true;
    native_latch(device,&error);return false;
}
static bool native_clear(qa_audio_device *device,audio_native_port *port) {
    qa_error error={0};
    if(!port || port->backend->ops->clear(port,&error)) return true;
    native_latch(device,&error);return false;
}
static void native_close(audio_native_port *port) {
    if(port) port->backend->ops->close(port);
}
static bool native_state(audio_native_port *port,bool playing,qa_error *error) {
    if(!port) return true;
    bool paused;
    if(!port->backend->ops->paused(port,&paused,error)) return false;
    return paused!=playing || device_error(error,QA_ERROR_IO,"Audio output changed its native playback state");
}

static size_t frame_bytes(qa_audio_output_format format) {
    return format.channels * (format.sample_bits / 8);
}

static bool copy_name(const char *name, char **out, qa_error *error) {
    if (name == NULL) {
        *out = NULL;
        return true;
    }
    size_t length = strlen(name);
    if (length == SIZE_MAX)
        return device_error(error, QA_ERROR_ARGUMENT, "Audio device name is too long");
    char *copy = malloc(length + 1);
    if (copy == NULL)
        return device_error(error, QA_ERROR_MEMORY, "Allocating audio device name");
    memcpy(copy, name, length + 1);
    *out = copy;
    return true;
}

static bool device_options(const qa_audio_device_options *input, qa_audio_device_options *out,
                           qa_error *error) {
    /* Output follows the donor's 8..192 kHz policy; source rates are resampled.
     * Zero sizes select a 1024-frame buffer and the native queue-size limit. */
    if (input == NULL || input->format.sample_rate < 8000 || input->format.sample_rate > 192000 ||
        input->format.sample_rate > INT_MAX ||
        (input->format.channels != 1 && input->format.channels != 2) ||
        (input->format.sample_bits != 8 && input->format.sample_bits != 16) ||
        (input->name != NULL && input->name[0] == '\0'))
        return device_error(error, QA_ERROR_ARGUMENT, "Invalid audio device options");
    *out = *input;
    unsigned requested = input->buffer_frames ? input->buffer_frames : 1024;
    /* SDL requests a power of two in a Uint16 field. */
    if (requested > (UINT16_MAX / 2u + 1u))
        return device_error(error, QA_ERROR_ARGUMENT, "Audio buffer exceeds SDL's sample count");
    out->buffer_frames = 1;
    while (out->buffer_frames < requested)
        out->buffer_frames *= 2;
    size_t maximum = UINT32_MAX / frame_bytes(input->format);
    if (maximum > SIZE_MAX / (2 * sizeof(int16_t)))
        maximum = SIZE_MAX / (2 * sizeof(int16_t));
    if (input->maximum_queued_frames > maximum)
        return device_error(error, QA_ERROR_ARGUMENT,
                            "Audio queue exceeds SDL's byte count or address space");
    out->maximum_queued_frames =
        input->maximum_queued_frames ? input->maximum_queued_frames : maximum;
    return true;
}

static size_t pcm_index(const device_pcm *pcm, size_t offset) {
    size_t tail = pcm->capacity - pcm->head;
    return offset < tail ? pcm->head + offset : offset - tail;
}

static void pcm_copy(const device_pcm *pcm, int16_t *out) {
    if (pcm->count == 0)
        return;
    size_t first = pcm->capacity - pcm->head;
    if (first > pcm->count)
        first = pcm->count;
    memcpy(out, pcm->samples + pcm->head * 2, first * 2 * sizeof(*out));
    if (first < pcm->count)
        memcpy(out + first * 2, pcm->samples, (pcm->count - first) * 2 * sizeof(*out));
}

static bool pcm_reserve(device_pcm *pcm, size_t frames, qa_error *error) {
    const size_t maximum = SIZE_MAX / (2 * sizeof(int16_t));
    if (frames > maximum)
        return device_error(error, QA_ERROR_MEMORY, "Retained audio queue overflows storage");
    if (frames <= pcm->capacity)
        return true;
    size_t capacity = pcm->capacity ? pcm->capacity : DEVICE_MIX_FRAMES;
    while (capacity < frames) {
        if (capacity > maximum / 2) {
            capacity = frames;
            break;
        }
        capacity *= 2;
    }
    qa_arena storage = {0};
    qa_arena_init(&storage, capacity * 2 * sizeof(int16_t));
    int16_t *samples = qa_arena_alloc(&storage, capacity * 2 * sizeof(*samples),
                                     _Alignof(int16_t), error);
    if (samples == NULL) {
        qa_arena_destroy(&storage);
        return false;
    }
    pcm_copy(pcm, samples);
    qa_arena_destroy(&pcm->storage);
    pcm->storage = storage;
    pcm->samples = samples;
    pcm->capacity = capacity;
    pcm->head = 0;
    return true;
}

static void pcm_append(device_pcm *pcm, const int16_t *samples, size_t frames) {
    if (frames == 0)
        return;
    size_t tail = pcm_index(pcm, pcm->count);
    size_t first = pcm->capacity - tail;
    if (first > frames)
        first = frames;
    memcpy(pcm->samples + tail * 2, samples, first * 2 * sizeof(*samples));
    if (first < frames)
        memcpy(pcm->samples, samples + first * 2, (frames - first) * 2 * sizeof(*samples));
    pcm->count += frames;
}

static void pcm_discard(device_pcm *pcm, size_t frames) {
    if (frames == 0)
        return;
    pcm->head = pcm_index(pcm, frames);
    pcm->count -= frames;
    if (pcm->count == 0)
        pcm->head = 0;
}

static bool encoding_reserve(qa_arena *storage, uint8_t **data, size_t *capacity, size_t bytes,
                             qa_error *error) {
    if (bytes <= *capacity)
        return true;
    qa_arena candidate = {0};
    qa_arena_init(&candidate, bytes);
    uint8_t *encoded = qa_arena_alloc(&candidate, bytes, _Alignof(uint8_t), error);
    if (encoded == NULL) {
        qa_arena_destroy(&candidate);
        return false;
    }
    qa_arena_destroy(storage);
    *storage = candidate;
    *data = encoded;
    *capacity = bytes;
    return true;
}

static bool encoded_reserve(qa_audio_device *device, size_t bytes, qa_error *error) {
    return encoding_reserve(&device->encoded_storage, &device->encoded,
                            &device->encoded_capacity, bytes, error);
}

static bool device_buffers_prepare(qa_audio_device *device, qa_error *error) {
    return pcm_reserve(&device->pcm, automatic_target(&device->options, NULL, 0, true), error) &&
        encoded_reserve(device, DEVICE_MIX_FRAMES * frame_bytes(device->options.format), error) &&
        qa_audio_raw_prepare(device->conversion, DEVICE_MIX_FRAMES, 2, error);
}

static bool pcm_replacement_prepare(device_pcm *candidate, const device_pcm *source,
                                     const qa_audio_device_options *options, qa_error *error) {
    size_t target = automatic_target(options, NULL, 0, true);
    if (source == candidate)
        return pcm_reserve(candidate, target > candidate->count ? target : candidate->count, error);
    if (target <= source->capacity)
        return true;
    if (!pcm_reserve(candidate, target, error))
        return false;
    pcm_copy(source, candidate->samples);
    candidate->count = source->count;
    return true;
}

static uint64_t playing_ticks(qa_audio_device *device) {
    if (!device->playing)
        return device->elapsed_ticks;
    uint64_t now = qa_platform_time_ns();
    if (now >= device->playing_since) {
        uint64_t elapsed = now - device->playing_since;
        device->elapsed_ticks = elapsed > UINT64_MAX - device->elapsed_ticks
                                    ? UINT64_MAX
                                    : device->elapsed_ticks + elapsed;
        device->playing_since = now;
    }
    return device->elapsed_ticks;
}

static void stop_playback(qa_audio_device *device) {
    if(!native_pause(device,device->id,true)) return;
    device->elapsed_ticks = playing_ticks(device);
    device->playing = false;
}

static void start_playback(qa_audio_device *device) {
    if (device->id == 0 || device->playing || device->paused)
        return;
    if(!native_pause(device,device->id,false)) return;
    device->playing_since = qa_platform_time_ns();
    device->playing = true;
}

static void reset_pump_history(qa_audio_device *device) {
    device->have_previous_pump = false;
    device->pump_interval_count = device->pump_interval_next = 0;
}

static void sync_consumed(qa_audio_device *device) {
    if (device->id == 0 || device->native_failure.code!=QA_OK)
        return;
    size_t bytes;qa_error error={0};
    if(!device->id->backend->ops->queued(device->id,&bytes,&error)) {native_latch(device,&error);return;}
    size_t width = frame_bytes(device->options.format);
    /* Round a partial frame upward so pending PCM never advances too far. */
    size_t queued = bytes / width + (bytes % width != 0);
    if (queued > device->submitted)
        return;
    pcm_discard(&device->pcm, device->submitted - queued);
    device->submitted = queued;
}

static bool encode_queue_buffer(qa_arena *storage, uint8_t **encoded, size_t *capacity, audio_native_port * id,
                                const int16_t *stereo, size_t frames,
                                qa_audio_output_format format, qa_error *error) {
    if (frames > UINT32_MAX / frame_bytes(format))
        return device_error(error, QA_ERROR_ARGUMENT, "Encoded audio exceeds SDL's byte count");
    size_t bytes = frames * frame_bytes(format), written;
    if (!encoding_reserve(storage, encoded, capacity, bytes, error) ||
        !qa_audio_output_encode(stereo, frames, format, *encoded, *capacity,
                                &written, error))
        return false;
    if(written && !id->backend->ops->queue(id,*encoded,written,error)) return false;
    return true;
}

static bool queue_prefix(qa_arena *storage,uint8_t **encoded,size_t *encoded_capacity,
    audio_native_port *port,const device_pcm *pcm,qa_audio_output_format format,
    size_t *submitted,qa_error *error) {
    size_t maximum=port->backend->maximum_bytes/frame_bytes(format);
    while(*submitted<pcm->count && *submitted<maximum) {
        size_t start=pcm_index(pcm,*submitted),frames=pcm->count-*submitted;
        if(frames>maximum-*submitted) frames=maximum-*submitted;
        if(frames>pcm->capacity-start) frames=pcm->capacity-start;
        if(frames>DEVICE_MIX_FRAMES) frames=DEVICE_MIX_FRAMES;
        if(!encode_queue_buffer(storage,encoded,encoded_capacity,port,pcm->samples+start*2,
            frames,format,error)) return false;
        *submitted+=frames;
    }
    return true;
}
static bool flush_pending(qa_audio_device *device, qa_error *error) {
    return !device->id || queue_prefix(&device->encoded_storage,&device->encoded,
        &device->encoded_capacity,device->id,&device->pcm,device->options.format,
        &device->submitted,error);
}

static bool open_native(audio_native_backend *backend,const qa_audio_device_options *options,
    audio_native_port **out,unsigned *buffer_frames,bool *unavailable,qa_error *error) {
    return backend->ops->open(backend,options,out,buffer_frames,unavailable,error);
}

static bool open_retained(qa_audio_device *device,const qa_audio_device_options *options,
    const device_pcm *pcm,audio_native_port **out,unsigned *buffer_frames,
    size_t *out_submitted,bool *unavailable,qa_error *error) {
    audio_native_port *id=NULL;
    if(!open_native(device->backend,options,&id,buffer_frames,unavailable,error)) return false;
    size_t submitted=0;
    if(!queue_prefix(&device->encoded_storage,&device->encoded,&device->encoded_capacity,
        id,pcm,options->format,&submitted,error)) {native_close(id);return false;}
    *out=id;*out_submitted=submitted;return true;
}

typedef struct audio_names {
    char **values;size_t count,capacity;qa_error *error;bool failed;
} audio_names;
static void collect_name(void *context,const char *name) {
    audio_names *names=context;
    if(names->failed) return;
    if(names->count==names->capacity) {
        size_t capacity=names->capacity?names->capacity*2:8;
        if(capacity<names->capacity || capacity>SIZE_MAX/sizeof(*names->values)) {
            names->failed=true;device_error(names->error,QA_ERROR_MEMORY,"Audio device names exceed storage");return;
        }
        char **values=realloc(names->values,capacity*sizeof(*values));
        if(!values) {names->failed=true;device_error(names->error,QA_ERROR_MEMORY,"Allocating audio device names");return;}
        names->values=values;names->capacity=capacity;
    }
    if(!copy_name(name,names->values+names->count,names->error)) names->failed=true;
    else ++names->count;
}
bool qa_audio_device_names(qa_audio_device_name_fn callback,void *user,qa_error *error) {
    if(!callback) return device_error(error,QA_ERROR_ARGUMENT,"Missing audio device name callback");
    audio_native_backend *backend=NULL;
    if(!backend_open(&backend,error)) return false;
    audio_names names={.error=error};
    bool okay=backend->ops->names(backend,collect_name,&names,error) && !names.failed;
    backend_release(backend);
    if(okay) for(size_t i=0;i<names.count;++i) callback(user,names.values[i]);
    for(size_t i=0;i<names.count;++i) free(names.values[i]);
    free(names.values);return okay;
}

bool qa_audio_device_open(const qa_audio_device_options *options, qa_audio_device **out,
                          qa_error *error) {
    if (out == NULL)
        return device_error(error, QA_ERROR_ARGUMENT, "Missing audio device destination");
    qa_audio_device *device=calloc(1,sizeof(*device));
    if(!device) return device_error(error,QA_ERROR_MEMORY,"Allocating audio device");
    qa_audio_device_options selected;
    if(!backend_open(&device->backend,error) || !device_options(options,&selected,error))
        goto failed;
    device->options = selected;
    device->requested_maximum_queued_frames = options->maximum_queued_frames;
    bool unavailable;
    if (!copy_name(selected.name, &device->name, error) ||
        !qa_audio_raw_create(selected.format.sample_rate, &device->conversion, error))
        goto failed;
    device->options.name = device->name;
    device->frequency = UINT64_C(1000000000);
    if (!open_native(device->backend, &device->options, &device->id, &device->options.buffer_frames, &unavailable,
                     error) || !device_buffers_prepare(device, error))
        goto failed;
    *out = device;
    return true;
failed:
    qa_audio_device_close(device);
    return false;
}

void qa_audio_device_close(qa_audio_device *device) {
    if (device == NULL)
        return;
    if (selection_blocks_mutation(device)) return;
    if (device->id != 0)
        native_close(device->id);
    qa_audio_raw_destroy(device->conversion);
    qa_arena_destroy(&device->pcm.storage);
    qa_arena_destroy(&device->encoded_storage);
    free(device->name);
    backend_release(device->backend);
    free(device);
}

void qa_audio_device_pause(qa_audio_device *device, bool paused) {
    if (device == NULL)
        return;
    if (selection_blocks_mutation(device)) return;
    reset_pump_history(device);
    bool was_paused=device->paused;
    device->paused = paused;
    if (paused) {
        stop_playback(device);
        if(device->playing) {device->paused=was_paused;return;}
    } else {
        start_playback(device);
        if(device->id && !device->playing) {device->paused=was_paused;return;}
        if (device->playing)
            device->output_started = true;
    }
    device->resume_on_attach = !paused;
    sync_consumed(device);
}

void qa_audio_device_clear(qa_audio_device *device) {
    if (device == NULL)
        return;
    if (selection_blocks_mutation(device)) return;
    reset_pump_history(device);
    if(!native_clear(device,device->id)) return;
    device->pcm.head = device->pcm.count = device->submitted = 0;
    device->staged_frames = 0;
    device->source_rate = 0;
    device->source_next = 0;
    (void)qa_audio_raw_queue(device->conversion, NULL, 0, 2, device->options.format.sample_rate, 0,
                             true, NULL);
}

bool qa_audio_device_round_ready(const qa_audio_device *device, qa_error *error) {
    if (!device || !device->conversion || device->pumping || device->capturing || device->handoff_pending || device->selection)
        return device_error(error, QA_ERROR_ARGUMENT, "Audio round requires its idle published device owner");
    if(device->native_failure.code!=QA_OK) {if(error) *error=device->native_failure;return false;}
    return true;
}

bool qa_audio_device_reset_round(qa_audio_device *device, qa_error *error) {
    if (!qa_audio_device_round_ready(device, error)) return false;
    qa_audio_raw_stream *conversion = NULL;
    if (!qa_audio_raw_create(device->options.format.sample_rate, &conversion, error) ||
        !qa_audio_raw_prepare(conversion, DEVICE_MIX_FRAMES, 2, error) ||
        !pcm_reserve(&device->pcm, automatic_target(&device->options, NULL, 0, true), error) ||
        !encoded_reserve(device, DEVICE_MIX_FRAMES * frame_bytes(device->options.format), error)) {
        qa_audio_raw_destroy(conversion);
        return false;
    }
    bool playing = device->playing;
    if (device->id) stop_playback(device);
    if(!native_check(device,error)) {qa_audio_raw_destroy(conversion);return false;}
    qa_audio_device_clear(device);
    if(!native_check(device,error)) {qa_audio_raw_destroy(conversion);return false;}
    qa_audio_raw_destroy(device->conversion); device->conversion = conversion;
    device->have_pump_engine = false; device->pump_engine_identity = 0;
    device->previous_pump_frame = 0;
    memset(device->pump_intervals, 0, sizeof(device->pump_intervals));
    device->staged_start = 0;
    if (playing) start_playback(device);
    return native_check(device,error);
}

void qa_audio_device_detach(qa_audio_device *device) {
    if (device == NULL || device->id == 0)
        return;
    if (selection_blocks_mutation(device)) return;
    reset_pump_history(device);
    device->resume_on_attach = device->playing;
    stop_playback(device);
    sync_consumed(device);
    native_close(device->id);
    device->id = 0;
    device->elapsed_ticks=playing_ticks(device);
    device->playing=false;
    device->submitted = 0;
}

qa_audio_output_format qa_audio_device_format(const qa_audio_device *device) {
    return device != NULL ? device->options.format : (qa_audio_output_format){0};
}

const char *qa_audio_device_name(const qa_audio_device *device) {
    return device != NULL ? device->name : NULL;
}

qa_audio_device_state qa_audio_device_get_state(const qa_audio_device *device) {
    if (device == NULL)
        return QA_AUDIO_DEVICE_CLOSED;
    if (device->id == 0)
        return QA_AUDIO_DEVICE_DETACHED;
    return device->playing ? QA_AUDIO_DEVICE_PLAYING : QA_AUDIO_DEVICE_PAUSED;
}

qa_audio_device_options qa_audio_device_configuration(const qa_audio_device *device) {
    return device != NULL ? device->options : (qa_audio_device_options){0};
}

qa_audio_device_options qa_audio_device_requested_configuration(const qa_audio_device *device) {
    qa_audio_device_options options = qa_audio_device_configuration(device);
    if (device) options.maximum_queued_frames = device->requested_maximum_queued_frames;
    return options;
}

size_t qa_audio_device_queued(qa_audio_device *device) {
    if (device == NULL)
        return 0;
    sync_consumed(device);
    return device->pcm.count;
}

uint64_t qa_audio_device_playback(qa_audio_device *device) {
    if (device == NULL)
        return 0;
    long double frames =
        (long double)playing_ticks(device) * device->options.format.sample_rate / device->frequency;
    return frames >= (long double)UINT64_MAX ? UINT64_MAX : (uint64_t)frames;
}

bool qa_audio_device_queue(qa_audio_device *device, const int16_t *stereo, size_t frames,
                           qa_error *error) {
    if (device == NULL || (stereo == NULL && frames != 0) ||
        frames > SIZE_MAX / (2 * sizeof(*stereo)) ||
        (device->selection && device->selection->ready))
        return device_error(error, QA_ERROR_ARGUMENT, "Invalid queued audio PCM");
    sync_consumed(device);
    if(!native_check(device,error)) return false;
    if (frames > device->options.maximum_queued_frames - device->pcm.count)
        return device_error(error, QA_ERROR_ARGUMENT, "Audio queue exceeds its configured maximum");
    if (!pcm_reserve(&device->pcm, device->pcm.count + frames, error))
        return false;
    pcm_append(&device->pcm, stereo, frames);
    return flush_pending(device,error);
}

bool qa_audio_device_pending(qa_audio_device *device, qa_buffer *out, qa_error *error) {
    if (device == NULL || out == NULL)
        return device_error(error, QA_ERROR_ARGUMENT, "Invalid pending audio destination");
    sync_consumed(device);
    if(!native_check(device,error)) return false;
    size_t bytes = device->pcm.count * 2 * sizeof(int16_t);
    int16_t *samples = bytes ? malloc(bytes) : NULL;
    if (bytes != 0 && samples == NULL)
        return device_error(error, QA_ERROR_MEMORY, "Copying pending audio PCM");
    pcm_copy(&device->pcm, samples);
    *out = (qa_buffer){(uint8_t *)samples, bytes};
    return true;
}

static bool convert_pending(const device_pcm *source, uint32_t old_rate, uint32_t new_rate,
                            size_t maximum, device_pcm *out, qa_error *error) {
    /* The retained queue is bounded by SDL's Uint32 bytes and rates by int. */
    uint64_t scaled = (uint64_t)source->count * new_rate;
    uint64_t count = scaled / old_rate + (scaled % old_rate != 0);
    if (count > maximum)
        return device_error(error, QA_ERROR_ARGUMENT,
                            "Resampled pending PCM exceeds the selected queue maximum");
    if (!pcm_reserve(out, (size_t)count, error))
        return false;
    uint64_t position = 0, remainder = 0;
    for (size_t frame = 0; frame < (size_t)count; ++frame) {
        size_t index = pcm_index(source, (size_t)position);
        out->samples[frame * 2] = source->samples[index * 2];
        out->samples[frame * 2 + 1] = source->samples[index * 2 + 1];
        remainder += old_rate;
        position += remainder / new_rate;
        remainder %= new_rate;
    }
    out->count = (size_t)count;
    return true;
}

bool qa_audio_device_select(qa_audio_device *device, const qa_audio_device_options *options,
                            qa_error *error) {
    if (device == NULL || device->pumping || device->capturing || device->selection)
        return device_error(error, QA_ERROR_ARGUMENT, "Missing audio device to select");
    if(!native_check(device,error)) return false;
    qa_audio_device_options selected;
    char *name = NULL;
    if (!device_options(options, &selected, error) || !copy_name(selected.name, &name, error))
        return false;
    selected.name = name;
    bool same_name = name == NULL ? device->name == NULL
                                  : device->name != NULL && strcmp(name, device->name) == 0;
    if (device->id != 0 && same_name &&
        selected.format.sample_rate == device->options.format.sample_rate &&
        selected.format.channels == device->options.format.channels &&
        selected.format.sample_bits == device->options.format.sample_bits &&
        selected.buffer_frames == device->options.buffer_frames &&
        selected.maximum_queued_frames == device->options.maximum_queued_frames) {
        device->requested_maximum_queued_frames = options->maximum_queued_frames;
        free(name);
        return true;
    }
    bool resume = device->id != 0 ? device->playing : device->resume_on_attach;
    reset_pump_history(device);
    stop_playback(device);
    sync_consumed(device);
    if(!native_check(device,error)) {free(name);return false;}
    device_pcm converted = {0};
    audio_native_port *replacement=NULL;size_t replacement_submitted=0;
    bool restore_output = false;
    bool changed_rate = selected.format.sample_rate != device->options.format.sample_rate;
    const device_pcm *pending = &device->pcm;
    if (changed_rate) {
        if (!convert_pending(&device->pcm, device->options.format.sample_rate,
                             selected.format.sample_rate, selected.maximum_queued_frames,
                             &converted, error))
            goto failed;
        pending = &converted;
    } else if (pending->count > selected.maximum_queued_frames) {
        device_error(error, QA_ERROR_ARGUMENT, "Pending PCM exceeds the selected queue maximum");
        goto failed;
    }
    bool unavailable;
    if (!open_retained(device, &selected, pending, &replacement, &selected.buffer_frames,
                       &replacement_submitted,&unavailable, error)) {
        if (!unavailable || device->id == 0)
            goto failed;
        /* Some native drivers allow only one output at a time. */
        native_close(device->id);
        device->id = 0;
        device->submitted = 0;
        restore_output = true;
        if (!open_retained(device, &selected, pending, &replacement, &selected.buffer_frames,
                           &replacement_submitted,&unavailable, error))
            goto failed;
    }
    if (!pcm_replacement_prepare(&converted, pending, &selected, error) ||
        !encoded_reserve(device, DEVICE_MIX_FRAMES * frame_bytes(selected.format), error) ||
        !qa_audio_raw_prepare(device->conversion, DEVICE_MIX_FRAMES, 2, error))
        goto failed;
    if (device->id != 0)
        native_close(device->id);
    device->id = replacement;
    if (converted.samples) {
        qa_arena_destroy(&device->pcm.storage);
        device->pcm = converted;
        converted = (device_pcm){0};
    }
    if (changed_rate) {
        (void)qa_audio_raw_set_rate(device->conversion, selected.format.sample_rate, NULL);
    }
    free(device->name);
    device->name = name;
    device->options = selected;
    device->requested_maximum_queued_frames = options->maximum_queued_frames;
    device->submitted = replacement_submitted;
    device->resume_on_attach = resume;
    if (resume)
        start_playback(device);
    return native_check(device,error);
failed:
    if (replacement) native_close(replacement);
    if (restore_output) {
        qa_error selection_error = {0}, restore_error = {0};
        if (error != NULL)
            selection_error = *error;
        if (open_retained(device, &device->options, &device->pcm, &device->id,
                          &device->options.buffer_frames, &device->submitted,&unavailable, &restore_error)) {
            /* The original PCM ring still owns any unsubmitted remainder. */
        } else {
            qa_error_set(error, QA_ERROR_IO, 0,
                         "Audio selection failed: %.92s; restoring output failed: %.92s",
                         selection_error.message, restore_error.message);
        }
    }
    qa_arena_destroy(&converted.storage);
    free(name);
    device->resume_on_attach = resume;
    if (resume)
        start_playback(device);
    return false;
}

static bool selection_is_current(const qa_audio_device_selection *selection) {
    return selection && selection->device && selection->device->selection == selection &&
        selection->device->id == selection->previous &&
        selection->device->conversion == selection->previous_conversion &&
        !selection->device->pumping && !selection->device->capturing;
}

static bool selection_current(const qa_audio_device_selection *selection, qa_error *error) {
    if (!selection_is_current(selection))
        return device_error(error, QA_ERROR_ARGUMENT, "Audio selection lost its idle live device owner");
    return true;
}

static void selection_discard_candidate(qa_audio_device_selection *selection) {
    if (selection->replacement) native_close(selection->replacement);
    qa_audio_raw_destroy(selection->conversion);
    qa_arena_destroy(&selection->converted.storage);
    qa_arena_destroy(&selection->encoded_storage);
    free(selection->name);
    free(selection);
}

static bool selection_native_current(const qa_audio_device_selection *selection, qa_error *error) {
    const qa_audio_device *device = selection->device;
    return native_state(device->id,device->playing,error) && native_state(selection->replacement,false,error);
}

static bool selection_resume_old(qa_audio_device_selection *selection, qa_error *error) {
    qa_audio_device *device = selection->device;
    if (device->id && selection->resume) start_playback(device);
    if(!native_check(device,error)) return false;
    if (!native_state(device->id,device->playing,error)) {
        stop_playback(device);
        return device_error(error, QA_ERROR_IO, "Audio selection could not resume its retained native endpoint");
    }
    return true;
}

bool qa_audio_device_selection_prepare(qa_audio_device *device,
    const qa_audio_device_options *options, qa_audio_device_selection **out, qa_error *error) {
    if (!device || !device->conversion || !device->frequency || device->pumping || device->capturing ||
        device->selection || !out || *out)
        return device_error(error, QA_ERROR_ARGUMENT, "Audio selection requires its unclaimed idle device owner");
    if(!native_check(device,error)) return false;
    if (!options)
        return device_error(error, QA_ERROR_ARGUMENT, "Missing prepared audio device options");
    qa_audio_device_options requested = *options, selected;
    bool retained_buffer = options->buffer_frames == device->options.buffer_frames;
    if (retained_buffer) requested.buffer_frames = 1;
    if (!device_options(&requested, &selected, error)) return false;
    /* An obtained SDL buffer is an actual configuration, even when it is not
     * the power-of-two advisory request used to open a different endpoint. */
    if (retained_buffer) selected.buffer_frames = options->buffer_frames;
    bool same_name = selected.name && device->name ? !strcmp(selected.name, device->name)
                                                  : selected.name == device->name;
    bool unchanged = device->id && same_name &&
        selected.format.sample_rate == device->options.format.sample_rate &&
        selected.format.channels == device->options.format.channels &&
        selected.format.sample_bits == device->options.format.sample_bits &&
        selected.buffer_frames == device->options.buffer_frames &&
        selected.maximum_queued_frames == device->options.maximum_queued_frames;
    qa_audio_device_selection *selection = calloc(1, sizeof(*selection));
    if (!selection)
        return device_error(error, QA_ERROR_MEMORY, "Allocating audio selection ticket");
    selection->device = device;
    selection->previous = device->id;
    selection->previous_conversion = device->conversion;
    selection->unchanged = unchanged;
    selection->selected = selected;
    selection->requested_maximum_queued_frames = options->maximum_queued_frames;
    if (unchanged) {
        selection->selected.name = device->name;
    } else {
        bool unavailable = false;
        if (!copy_name(selected.name, &selection->name, error)) goto failed;
        selection->selected.name = selection->name;
        if (!open_native(device->backend, &selection->selected, &selection->replacement,
                         &selection->selected.buffer_frames, &unavailable, error)) goto failed;
    }
    device->selection = selection;
    *out = selection;
    return true;
failed:
    selection_discard_candidate(selection);
    return false;
}

bool qa_audio_device_selection_ready(qa_audio_device_selection *selection, qa_error *error) {
    if (!selection_current(selection, error)) return false;
    if(!native_check(selection->device,error)) return false;
    if (selection->invalidated)
        return device_error(error, QA_ERROR_ARGUMENT, "Audio selection refused intervening owner mutation");
    if (!selection_native_current(selection, error)) return false;
    if (selection->ready) return true;
    if (selection->unchanged) {
        selection->ready = true;
        return true;
    }
    qa_audio_device *device = selection->device;
    if(!native_clear(device,selection->replacement)) return native_check(device,error);
    if (!selection->stopped)
        selection->resume = device->id ? device->playing : device->resume_on_attach;
    selection->stopped = true;
    stop_playback(device);
    sync_consumed(device);
    if(!native_check(device,error)) goto failed;
    if (!selection_native_current(selection, error)) goto failed;
    bool changed_rate = selection->selected.format.sample_rate != device->options.format.sample_rate;
    const device_pcm *pending = &device->pcm;
    if (changed_rate) {
        if (!convert_pending(pending, device->options.format.sample_rate,
            selection->selected.format.sample_rate, selection->selected.maximum_queued_frames,
            &selection->converted, error) ||
            !qa_audio_raw_clone_rate(device->conversion, selection->selected.format.sample_rate,
                                     &selection->conversion, error)) goto failed;
        pending = &selection->converted;
    } else if (pending->count > selection->selected.maximum_queued_frames) {
        device_error(error, QA_ERROR_ARGUMENT, "Pending PCM exceeds the prepared audio queue maximum");
        goto failed;
    }
    if (!pcm_replacement_prepare(&selection->converted, pending, &selection->selected, error) ||
        !qa_audio_raw_prepare(changed_rate ? selection->conversion : device->conversion,
                              DEVICE_MIX_FRAMES, 2, error) ||
        !encoding_reserve(&selection->encoded_storage, &selection->encoded, &selection->encoded_capacity,
                          DEVICE_MIX_FRAMES * frame_bytes(selection->selected.format), error))
        goto failed;
    selection->submitted=0;
    if(!queue_prefix(&selection->encoded_storage,&selection->encoded,&selection->encoded_capacity,
        selection->replacement,pending,selection->selected.format,&selection->submitted,error)) goto failed;
    selection->ready = true;
    return true;
failed:
    (void)native_clear(device,selection->replacement);
    qa_audio_raw_destroy(selection->conversion); selection->conversion = NULL;
    qa_arena_destroy(&selection->converted.storage); selection->converted = (device_pcm){0};
    qa_error selection_error = {0}, resume_error = {0};
    if (error) selection_error = *error;
    if (selection_resume_old(selection, &resume_error)) selection->stopped = false;
    else qa_error_set(error, QA_ERROR_IO, 0, "Audio preparation failed: %.92s; resuming retained output failed: %.92s",
                      selection_error.message, resume_error.message);
    return false;
}

bool qa_audio_device_selection_ready_is(const qa_audio_device_selection *selection,
                                       const qa_audio_device *device) {
    if (!device || !selection_is_current(selection) || selection->device != device ||
        !selection->ready || selection->invalidated || !device->frequency ||
        !device->conversion || qa_audio_raw_rate(device->conversion) != device->options.format.sample_rate)
        return false;
    if (selection->unchanged)
        return device->id && !selection->replacement && !selection->stopped &&
            selection->selected.name == device->name &&
            selection->selected.format.sample_rate == device->options.format.sample_rate &&
            selection->selected.format.channels == device->options.format.channels &&
            selection->selected.format.sample_bits == device->options.format.sample_bits &&
            selection->selected.buffer_frames == device->options.buffer_frames &&
            selection->selected.maximum_queued_frames == device->options.maximum_queued_frames;
    if (!selection->replacement || selection->replacement == selection->previous ||
        !selection->stopped || device->playing || selection->selected.name != selection->name ||
        !selection->encoded ||
        selection->encoded_capacity < DEVICE_MIX_FRAMES * frame_bytes(selection->selected.format))
        return false;
    bool changed_rate = selection->selected.format.sample_rate != device->options.format.sample_rate;
    if (changed_rate != (selection->conversion != NULL) ||
        (changed_rate && qa_audio_raw_rate(selection->conversion) != selection->selected.format.sample_rate))
        return false;
    const device_pcm *pending = selection->converted.samples ? &selection->converted : &device->pcm;
    return selection->submitted<=pending->count &&
        selection->submitted<=device->backend->maximum_bytes/frame_bytes(selection->selected.format) &&
        pending->count <= selection->selected.maximum_queued_frames &&
        pending->count <= pending->capacity &&
        (!pending->count || (pending->samples && pending->head < pending->capacity));
}

static uint64_t selection_scale_frames(uint64_t frames, uint32_t old_rate, uint32_t new_rate,
                                       bool upward) {
    uint64_t whole = frames / old_rate, remainder = (frames % old_rate) * new_rate;
    uint64_t tail = remainder / old_rate + (upward && remainder % old_rate != 0);
    if (whole > (UINT64_MAX - tail) / new_rate) return UINT64_MAX;
    return whole * new_rate + tail;
}

void qa_audio_device_selection_publish(qa_audio_device_selection *selection) {
    qa_audio_device *device = selection->device;
    if (!selection->unchanged) {
        uint32_t old_rate = device->options.format.sample_rate;
        uint32_t new_rate = selection->selected.format.sample_rate;
        if (selection->previous) native_close(selection->previous);
        device->id = selection->replacement; selection->replacement = 0;
        if (selection->converted.samples) {
            qa_arena_destroy(&device->pcm.storage); device->pcm = selection->converted;
            selection->converted = (device_pcm){0};
        }
        if (old_rate != new_rate) {
            qa_audio_raw_destroy(device->conversion); device->conversion = selection->conversion;
            selection->conversion = NULL;
            device->previous_pump_frame = selection_scale_frames(device->previous_pump_frame, old_rate, new_rate, false);
            for (size_t i = 0; i < 8; ++i) {
                uint64_t interval = selection_scale_frames(device->pump_intervals[i], old_rate, new_rate, true);
                device->pump_intervals[i] = interval > SIZE_MAX ? SIZE_MAX : (size_t)interval;
            }
        }
        qa_arena_destroy(&device->encoded_storage);
        device->encoded_storage = selection->encoded_storage;
        selection->encoded_storage = (qa_arena){0};
        device->encoded = selection->encoded;
        device->encoded_capacity = selection->encoded_capacity;
        selection->encoded = NULL;
        free(device->name); device->name = selection->name; selection->name = NULL;
        device->options = selection->selected; device->options.name = device->name;
        device->submitted = selection->submitted;
        device->resume_on_attach = selection->resume;
        if (selection->resume) start_playback(device);
    }
    device->requested_maximum_queued_frames = selection->requested_maximum_queued_frames;
    device->selection = NULL;
    selection_discard_candidate(selection);
}

bool qa_audio_device_selection_abort(qa_audio_device_selection *selection, qa_error *error) {
    if (!selection_current(selection, error)) return false;
    if(!native_check(selection->device,error)) return false;
    qa_audio_device *device = selection->device;
    if (selection->stopped && !selection_resume_old(selection, error)) return false;
    device->selection = NULL;
    selection_discard_candidate(selection);
    return true;
}

static bool stage_source(qa_audio_device *device, qa_audio_engine *engine, size_t wanted,
                         qa_error *error) {
    uint32_t rate = qa_audio_engine_rate(engine);
    uint64_t clock = qa_audio_engine_clock(engine);
    if (device->source_rate != rate) {
        if (!qa_audio_raw_queue(device->conversion, NULL, 0, 2, rate, 0, true, error))
            return false;
        device->source_rate = rate;
        device->source_next = 0;
    }
    uint64_t scaled = (uint64_t)wanted * rate;
    uint32_t output_rate = device->options.format.sample_rate;
    uint64_t needed = scaled / output_rate + (scaled % output_rate != 0);
    size_t frames = needed > DEVICE_MIX_FRAMES ? DEVICE_MIX_FRAMES : (size_t)needed;
    if (frames == 0)
        frames = 1;
    if (frames > UINT64_MAX - device->source_next)
        return device_error(error, QA_ERROR_ARGUMENT, "Audio conversion clock exceeds frame space");
    /* Conversion owns a continuous source coordinate independent of engine
     * clocks. A replacement engine at the same rate retains fractional phase. */
    device->staged_start = device->source_next;
    if (!qa_audio_engine_mix(engine, device->source, frames, error)) {
        /* Preserve completed engine blocks even if a later block failed. */
        uint64_t advanced = qa_audio_engine_clock(engine) - clock;
        if (advanced <= frames)
            device->staged_frames = (size_t)advanced;
        device->source_next += device->staged_frames;
        return false;
    }
    device->staged_frames = frames;
    device->source_next += frames;
    return true;
}

static bool record_pump(qa_audio_device *device, qa_audio_engine *engine, double measured_work_ms) {
    /* Retain identity only; the previous engine may already have been freed. */
    uintptr_t identity = (uintptr_t)engine;
    if (device->have_pump_engine && device->pump_engine_identity != identity) {
        reset_pump_history(device);
        device->handoff_pending = true;
    }
    device->pump_engine_identity = identity;
    device->have_pump_engine = true;
    bool initial = device->handoff_pending ||
                   (!device->output_started && !device->playing && device->pcm.count == 0);
    uint64_t playback = qa_audio_device_playback(device);
    uint64_t interval = device->have_previous_pump && playback >= device->previous_pump_frame
                            ? playback - device->previous_pump_frame
                            : 0;
    if (!initial) {
        size_t maximum = device->options.maximum_queued_frames;
        long double work = measured_work_ms;
        long double maximum_ms = (long double)maximum * 1000 / device->options.format.sample_rate;
        size_t work_frames = maximum;
        if (work < maximum_ms) {
            work = ceill(work * device->options.format.sample_rate / 1000);
            work_frames = work >= (long double)maximum ? maximum : (size_t)work;
        }
        size_t interval_frames = interval > maximum ? maximum : (size_t)interval;
        device->pump_intervals[device->pump_interval_next] =
            interval_frames > work_frames ? interval_frames : work_frames;
        device->pump_interval_next = (device->pump_interval_next + 1) % 8;
        if (device->pump_interval_count < 8)
            ++device->pump_interval_count;
    }
    device->previous_pump_frame = playback;
    device->have_previous_pump = true;
    return initial;
}

static size_t automatic_target(const qa_audio_device_options *options, const size_t *intervals,
                                size_t interval_count, bool initial) {
    uint64_t rate = options->format.sample_rate;
    uint64_t buffer_margin = (uint64_t)options->buffer_frames * 2;
    uint64_t mixahead = (rate + 4) / 5;
    if (mixahead < buffer_margin)
        mixahead = buffer_margin;
    uint64_t target;
    if (initial) {
        target = mixahead;
    } else {
        size_t interval = 0;
        for (size_t i = 0; i < interval_count; ++i)
            if (interval < intervals[i])
                interval = intervals[i];
        target = (rate * 2 + 24) / 25;
        uint64_t recent = (uint64_t)interval + buffer_margin;
        if (target < recent)
            target = recent;
    }
    size_t maximum = options->maximum_queued_frames;
    return target > maximum ? maximum : (size_t)target;
}

static bool pump_device(qa_audio_device *device, qa_audio_engine *engine, size_t target_frames,
                        bool automatic, double measured_work_ms, size_t *mixed_frames,
                        qa_error *error) {
    *mixed_frames = 0;
    if (device == NULL || engine == NULL || qa_audio_engine_rate(engine) == 0)
        return device_error(error, QA_ERROR_ARGUMENT, "Invalid audio pump device or engine");
    if(!native_check(device,error)) return false;
    if (device->id == 0)
        return device_error(error, QA_ERROR_IO, "Audio output is detached");
    if (target_frames > device->options.maximum_queued_frames)
        return device_error(error, QA_ERROR_ARGUMENT,
                            "Audio pump target exceeds its queue maximum");
    if (device->paused)
        return true;
    sync_consumed(device);
    if(!native_check(device,error)) return false;
    if (!flush_pending(device, error))
        return false;
    bool initial = record_pump(device, engine, measured_work_ms);
    if (automatic)
        target_frames = automatic_target(&device->options, device->pump_intervals,
                                         device->pump_interval_count, initial);
    size_t needed = target_frames > device->pcm.count ? target_frames - device->pcm.count : 0;
    if (!pcm_reserve(&device->pcm, device->pcm.count + needed, error) ||
        !encoded_reserve(device, DEVICE_MIX_FRAMES * frame_bytes(device->options.format), error))
        return false;
    while (needed != 0) {
        if (device->staged_frames != 0) {
            if (!qa_audio_raw_queue(device->conversion, device->source, device->staged_frames, 2,
                                    device->source_rate, device->staged_start, false, error))
                return false;
            device->staged_frames = 0;
        }
        size_t frames = needed > DEVICE_MIX_FRAMES ? DEVICE_MIX_FRAMES : needed;
        memset(device->mixed, 0, frames * 2 * sizeof(*device->mixed));
        size_t mixed = qa_audio_raw_mix(device->conversion, device->mixed, frames, 1);
        if (mixed == 0) {
            if (!stage_source(device, engine, frames, error))
                return false;
            continue;
        }
        for (size_t frame = 0; frame < mixed; ++frame) {
            size_t index = pcm_index(&device->pcm, device->pcm.count + frame);
            device->pcm.samples[index * 2] = (int16_t)device->mixed[frame * 2];
            device->pcm.samples[index * 2 + 1] = (int16_t)device->mixed[frame * 2 + 1];
        }
        device->pcm.count += mixed;
        needed -= mixed;
        *mixed_frames += mixed;
        if (!flush_pending(device, error))
            return false;
    }
    device->resume_on_attach = true;
    start_playback(device);
    if(!native_check(device,error)) return false;
    device->output_started = true;
    device->handoff_pending = false;
    return true;
}

bool qa_audio_device_pump(qa_audio_device *device, qa_audio_engine *engine, size_t target_frames,
                          qa_error *error) {
    size_t mixed_frames;
    if (!device || device->pumping || device->capturing || (device->selection && device->selection->ready))
        return device_error(error, QA_ERROR_ARGUMENT, "Audio device pump is absent or executing");
    device->pumping = true;
    bool ok = pump_device(device, engine, target_frames, false, 0, &mixed_frames, error);
    device->pumping = false; return ok;
}

bool qa_audio_device_pump_auto(qa_audio_device *device, qa_audio_engine *engine,
                               double measured_work_ms, size_t *mixed_frames, qa_error *error) {
    if (mixed_frames == NULL)
        return device_error(error, QA_ERROR_ARGUMENT, "Missing automatic audio pump frame count");
    *mixed_frames = 0;
    if (!isfinite(measured_work_ms) || measured_work_ms < 0)
        return device_error(error, QA_ERROR_ARGUMENT, "Invalid measured audio pump work duration");
    if (!device || device->pumping || device->capturing || (device->selection && device->selection->ready))
        return device_error(error, QA_ERROR_ARGUMENT, "Audio device pump is absent or executing");
    device->pumping = true;
    bool ok = pump_device(device, engine, 0, true, measured_work_ms, mixed_frames, error);
    device->pumping = false; return ok;
}

struct qa_audio_device_restore_guard {
    qa_audio_device *active, *candidate;
    audio_native_port * endpoint;
    size_t saved_submitted;
    const qa_audio_engine *candidate_engine;
    bool saved_attached, saved_playing, prepared, transferred;
};
static bool saved_name(qa_source_save_io *io,char **name)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=!reading && *name;
    if (!qa_source_save_bool(io,&present)) return false;
    size_t size=present && !reading?strlen(*name):0;
    if (present && (!qa_source_save_count(io,&size,reading?io->input.size-io->offset:SIZE_MAX-1) || size==SIZE_MAX)) return false;
    if (!reading) return !present || qa_source_save_bytes(io,*name,size);
    if (!present) { *name=NULL; return true; }
    char *copy=malloc(size+1);
    if (!copy) return device_error(io->error,QA_ERROR_MEMORY,"Restoring the actual audio device selection name");
    if (!qa_source_save_bytes(io,copy,size) || memchr(copy,0,size)) { free(copy); return false; }
    copy[size]=0; *name=copy; return true;
}
static bool saved_blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,size);
    if (!size || size>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
static bool saved_samples(qa_source_save_io *io,int16_t *samples,size_t count)
{
    for (size_t i=0;i<count;++i) {
        uint16_t bits=0;
        if (io->direction==QA_SOURCE_SAVE_WRITE) memcpy(&bits,samples+i,sizeof(bits));
        if (!qa_source_save_u16(io,&bits)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) memcpy(samples+i,&bits,sizeof(bits));
    }
    return true;
}
static bool saved_device_fields(qa_source_save_io *io,qa_audio_device *device,
    bool *attached,bool *current_engine,qa_bytes *conversion)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','A','D','V'}; uint32_t channels=device->options.format.channels,
        bits=device->options.format.sample_bits,buffer=device->options.buffer_frames;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QADV",4) ||
        !qa_source_save_bool(io,attached) ||
        !qa_source_save_u32(io,&device->options.format.sample_rate) || !qa_source_save_u32(io,&channels) ||
        !qa_source_save_u32(io,&bits) || !qa_source_save_u32(io,&buffer) ||
        !qa_source_save_count(io,&device->options.maximum_queued_frames,SIZE_MAX) ||
        !qa_source_save_count(io,&device->requested_maximum_queued_frames,SIZE_MAX) || !saved_name(io,&device->name)) return false;
    if (channels>UINT_MAX || bits>UINT_MAX || buffer>UINT_MAX) return false;
    device->options.format.channels=(unsigned)channels; device->options.format.sample_bits=(unsigned)bits;
    device->options.buffer_frames=(unsigned)buffer; device->options.name=device->name;
    qa_audio_device_options requested=device->options,qualified;
    requested.buffer_frames=1;
    requested.maximum_queued_frames=device->requested_maximum_queued_frames;
    if (!buffer || !device_options(&requested,&qualified,io->error)) return false;
    if(qualified.maximum_queued_frames!=device->options.maximum_queued_frames) return false;
    if (!qa_source_save_count(io,&device->pcm.capacity,SIZE_MAX/(2*sizeof(int16_t))) ||
        !qa_source_save_count(io,&device->pcm.head,device->pcm.capacity) ||
        !qa_source_save_count(io,&device->pcm.count,device->options.maximum_queued_frames) ||
        !qa_source_save_count(io,&device->submitted,device->pcm.count) || device->pcm.count>device->pcm.capacity ||
        (device->pcm.capacity?device->pcm.head>=device->pcm.capacity:device->pcm.head || device->pcm.count) ||
        (!*attached && device->submitted)) return false;
    if (reading && device->pcm.capacity) {
        size_t bytes=device->pcm.capacity*2*sizeof(int16_t);
        qa_arena_init(&device->pcm.storage,bytes);
        device->pcm.samples=qa_arena_alloc(&device->pcm.storage,bytes,_Alignof(int16_t),io->error);
        if (!device->pcm.samples) return false;
        memset(device->pcm.samples,0,bytes);
    }
    if (device->pcm.count && !device->pcm.samples) return false;
    for (size_t i=0;i<device->pcm.count;++i)
        if (!saved_samples(io,device->pcm.samples+pcm_index(&device->pcm,i)*2,2)) return false;
    if (!qa_source_save_count(io,&device->encoded_capacity,UINT32_MAX) ||
        !qa_source_save_u32(io,&device->source_rate) || !qa_source_save_u64(io,&device->source_next) ||
        !qa_source_save_u64(io,&device->staged_start) ||
        !qa_source_save_count(io,&device->staged_frames,DEVICE_MIX_FRAMES) ||
        (device->source_rate && (device->source_rate<8000 || device->source_rate>192000)) ||
        (device->staged_frames && (!device->source_rate || device->staged_frames>UINT64_MAX-device->staged_start ||
            device->staged_start+device->staged_frames>device->source_next)) ||
        !saved_samples(io,device->source,device->staged_frames*2) || !saved_blob(io,conversion) ||
        !qa_source_save_u64(io,&device->frequency) || !device->frequency ||
        !qa_source_save_u64(io,&device->elapsed_ticks) || !qa_source_save_u64(io,&device->previous_pump_frame) ||
        !qa_source_save_count(io,&device->pump_interval_count,8) ||
        !qa_source_save_count(io,&device->pump_interval_next,7)) return false;
    for (size_t i=0;i<8;++i)
        if (!qa_source_save_count(io,device->pump_intervals+i,SIZE_MAX)) return false;
    if (!qa_source_save_bool(io,&device->playing) || !qa_source_save_bool(io,&device->paused) ||
        !qa_source_save_bool(io,&device->resume_on_attach) || !qa_source_save_bool(io,&device->have_pump_engine) ||
        !qa_source_save_bool(io,current_engine) || (*current_engine && !device->have_pump_engine) ||
        !qa_source_save_bool(io,&device->have_previous_pump) || !qa_source_save_bool(io,&device->output_started) ||
        !qa_source_save_bool(io,&device->handoff_pending) || (device->playing && (!*attached || device->paused))) return false;
    if (!device->have_pump_engine && device->have_previous_pump) return false;
    if (reading && device->frequency != UINT64_C(1000000000)) {
        long double duration=(long double)device->elapsed_ticks * 1000000000.0L / device->frequency;
        device->elapsed_ticks=duration >= (long double)UINT64_MAX ? UINT64_MAX : (uint64_t)duration;
        device->frequency=UINT64_C(1000000000);
    }
    return true;
}
bool qa_audio_device_checkpoint(qa_audio_device *device,const qa_audio_engine *engine,qa_buffer *out,qa_error *error)
{
    if (!device || !engine || !out || out->data || out->size || !device->conversion || device->pumping || device->capturing || device->selection)
        return device_error(error,QA_ERROR_ARGUMENT,"Audio device capture requires its genuine idle engine/endpoint owner");
    if(!native_check(device,error)) return false;
    device->capturing=true;
    if(!native_pause(device,device->id,true)) {device->capturing=false;return native_check(device,error);}
    sync_consumed(device);
    qa_audio_device state=*device; state.elapsed_ticks=playing_ticks(&state);
    qa_buffer raw={0}; qa_source_save_io io={0}; bool attached=device->id!=0;
    bool current_engine=device->have_pump_engine && device->pump_engine_identity==(uintptr_t)engine;
    bool ok=native_check(device,error) && qa_audio_raw_checkpoint(device->conversion,&raw,error);
    qa_bytes conversion={raw.data,raw.size};
    if (ok) ok=qa_source_save_writer(&io,NULL,error) && saved_device_fields(&io,&state,&attached,&current_engine,&conversion) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&raw);
    /* The actual endpoint was paused only for this capture. Preserve its
     * unpaused playback coordinate while excluding time spent in the cut. */
    device->elapsed_ticks=state.elapsed_ticks;
    if (device->playing) device->playing_since=qa_platform_time_ns();
    (void)native_pause(device,device->id,!device->playing);
    if(ok) ok=native_check(device,error);
    device->capturing=false;
    if(!ok) {qa_buffer_free(out);*out=(qa_buffer){0};}
    if (!ok && (!error || error->code==QA_OK)) device_error(error,QA_ERROR_FORMAT,"Invalid genuine audio device continuation");
    return ok;
}
static bool same_device_options(const qa_audio_device *a,const qa_audio_device *b)
{
    return a->options.format.sample_rate==b->options.format.sample_rate && a->options.format.channels==b->options.format.channels &&
        a->options.format.sample_bits==b->options.format.sample_bits && a->options.buffer_frames==b->options.buffer_frames &&
        a->options.maximum_queued_frames==b->options.maximum_queued_frames &&
        a->requested_maximum_queued_frames==b->requested_maximum_queued_frames &&
        ((a->name && b->name)?!strcmp(a->name,b->name):a->name==b->name);
}
bool qa_audio_device_restore(qa_bytes bytes,const qa_audio_device *active,qa_audio_engine *engine,
    qa_audio_device **out,qa_audio_device_restore_guard **guard_out,qa_error *error)
{
    if (!active || active->pumping || active->capturing || active->selection || !engine || !out || *out || !guard_out || *guard_out)
        return device_error(error,QA_ERROR_ARGUMENT,"Audio device restoration requires separate genuine active/candidate owners");
    if(active->native_failure.code!=QA_OK) {if(error) *error=active->native_failure;return false;}
    qa_audio_device *candidate=calloc(1,sizeof(*candidate));
    qa_audio_device_restore_guard *guard=calloc(1,sizeof(*guard));
    if (!candidate || !guard) {
        free(candidate); free(guard); return device_error(error,QA_ERROR_MEMORY,"Allocating detached audio device continuation");
    }
    candidate->backend=backend_retain(active->backend);
    qa_source_save_io io={0}; qa_bytes conversion={0}; bool attached=false,current_engine=false;
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && saved_device_fields(&io,candidate,&attached,&current_engine,&conversion) &&
        qa_source_save_finish(&io,NULL) && same_device_options(candidate,active) && attached==(active->id!=0) &&
        candidate->frequency==active->frequency;
    if (ok && candidate->encoded_capacity) {
        size_t capacity=candidate->encoded_capacity;
        candidate->encoded_capacity=0;
        ok=encoded_reserve(candidate,capacity,error);
    }
    if (ok) ok=qa_audio_raw_restore(conversion,candidate->options.format.sample_rate,&candidate->conversion,error) &&
        qa_audio_raw_prepare(candidate->conversion,DEVICE_MIX_FRAMES,2,error);
    if (ok) {
        /* Only equality with the current mixer is observable. Zero retains a
         * previous-engine mismatch without retaining a retired raw pointer. */
        candidate->pump_engine_identity=current_engine?(uintptr_t)engine:0;
        *guard=(qa_audio_device_restore_guard){.active=(qa_audio_device *)active,.candidate=candidate,
            .endpoint=active->id,.saved_submitted=candidate->submitted<active->backend->maximum_bytes/frame_bytes(candidate->options.format)?
                candidate->submitted:active->backend->maximum_bytes/frame_bytes(candidate->options.format),
            .candidate_engine=engine,
            .saved_attached=attached,.saved_playing=candidate->playing};
        /* A detached owner has submitted no PCM to a native endpoint. Its
         * saved private queue/conversion/pump fields otherwise remain exact. */
        candidate->submitted=0; candidate->playing=false;
        *out=candidate; *guard_out=guard; candidate=NULL; guard=NULL;
    }
    qa_source_save_dispose(&io); qa_audio_device_close(candidate); free(guard);
    if (!ok && (!error || error->code==QA_OK)) device_error(error,QA_ERROR_FORMAT,"Saved audio device differs from its genuine native output owner");
    return ok;
}
bool qa_audio_device_create_detached(const qa_audio_device *active,qa_audio_engine *engine,
    qa_audio_device **out,qa_audio_device_restore_guard **guard_out,qa_error *error)
{
    if (!active || active->pumping || active->capturing || active->selection || !active->conversion || !active->frequency ||
        !engine || !out || *out || !guard_out || *guard_out)
        return device_error(error,QA_ERROR_ARGUMENT,"Fresh detached audio requires genuine idle native/mixer owners");
    if(active->native_failure.code!=QA_OK) {if(error) *error=active->native_failure;return false;}
    qa_audio_device *candidate=calloc(1,sizeof(*candidate));
    qa_audio_device_restore_guard *guard=calloc(1,sizeof(*guard));
    if (!candidate || !guard) {
        free(candidate); free(guard); return device_error(error,QA_ERROR_MEMORY,"Allocating fresh detached audio owner");
    }
    candidate->backend=backend_retain(active->backend);
    candidate->options=active->options; candidate->frequency=active->frequency;
    candidate->requested_maximum_queued_frames=active->requested_maximum_queued_frames;
    bool ok=copy_name(active->name,&candidate->name,error) &&
        qa_audio_raw_create(candidate->options.format.sample_rate,&candidate->conversion,error) &&
        device_buffers_prepare(candidate,error);
    candidate->options.name=candidate->name;
    if (!ok) { qa_audio_device_close(candidate); free(guard); return false; }
    /* Normal graphical construction requests playback after native open. The
     * fresh owner carries that intent until its prepared endpoint is published. */
    *guard=(qa_audio_device_restore_guard){.active=(qa_audio_device *)active,.candidate=candidate,
        .endpoint=active->id,.candidate_engine=engine,.saved_attached=active->id!=0,.saved_playing=active->id!=0};
    *out=candidate; *guard_out=guard; return true;
}
static bool device_guard_ready(const qa_audio_device_restore_guard *guard,qa_error *error)
{
    if (!guard || guard->transferred || !guard->active || !guard->candidate || guard->active==guard->candidate ||
        guard->active->pumping || guard->active->capturing || guard->candidate->pumping || guard->candidate->capturing ||
        guard->active->selection || guard->candidate->selection ||
        guard->active->id!=guard->endpoint || (guard->endpoint!=0)!=guard->saved_attached ||
        (guard->prepared ? (guard->candidate->id!=0)!=guard->saved_attached ||
            guard->candidate->submitted!=guard->saved_submitted : guard->candidate->id || guard->candidate->submitted) ||
        guard->candidate->playing || !guard->candidate->conversion ||
        !same_device_options(guard->active,guard->candidate) || guard->active->frequency!=guard->candidate->frequency)
        return device_error(error,QA_ERROR_ARGUMENT,"Audio native handoff lost its genuine qualified endpoint/device cut");
    if(guard->active->native_failure.code!=QA_OK) {if(error) *error=guard->active->native_failure;return false;}
    if(guard->candidate->native_failure.code!=QA_OK) {if(error) *error=guard->candidate->native_failure;return false;}
    return true;
}
bool qa_audio_device_handoff_ready(const qa_audio_device_restore_guard *guard,qa_error *error)
{
    if (!device_guard_ready(guard,error)) return false;
    if (!guard->prepared)
        return device_error(error,QA_ERROR_ARGUMENT,"Audio endpoint publication requires its prepared saved PCM prefix");
    return true;
}
bool qa_audio_device_handoff_prepare(qa_audio_device_restore_guard *guard,qa_error *error)
{
    if (!device_guard_ready(guard,error)) return false;
    if (guard->prepared) return true;
    if (guard->saved_attached) {
        qa_audio_device *candidate=guard->candidate;
        device_pcm prefix=candidate->pcm; prefix.count=guard->saved_submitted;
        /* Native encoding scratch belongs to this preparation, so opening an
         * endpoint never changes the saved candidate allocation continuation. */
        qa_audio_device encoder={.backend=candidate->backend}; audio_native_port *endpoint=0;
        unsigned buffer=candidate->options.buffer_frames; bool unavailable=false;size_t submitted=0;
        bool ok=open_retained(&encoder,&candidate->options,&prefix,&endpoint,&buffer,&submitted,&unavailable,error);
        qa_arena_destroy(&encoder.encoded_storage);
        if (!ok) return false;
        if (buffer!=candidate->options.buffer_frames) {
            native_close(endpoint);
            return device_error(error,QA_ERROR_UNSUPPORTED,"Prepared audio endpoint changed the saved native buffer size");
        }
        candidate->id=endpoint; candidate->submitted=submitted;
    }
    guard->prepared=true;
    return true;
}
bool qa_audio_device_restore_checkpoint(const qa_audio_device_restore_guard *guard,qa_buffer *out,qa_error *error)
{
    if (!out || out->data || out->size || !device_guard_ready(guard,error)) return false;
    qa_audio_device state=*guard->candidate;
    state.submitted=guard->saved_submitted; state.playing=guard->saved_playing;
    bool attached=guard->saved_attached;
    bool current_engine=state.have_pump_engine && state.pump_engine_identity==(uintptr_t)guard->candidate_engine;
    qa_buffer raw={0}; qa_source_save_io io={0};
    bool ok=qa_audio_raw_checkpoint(state.conversion,&raw,error);
    qa_bytes conversion={raw.data,raw.size};
    if (ok) ok=qa_source_save_writer(&io,NULL,error) && saved_device_fields(&io,&state,&attached,&current_engine,&conversion) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&raw);
    return ok;
}
void qa_audio_device_handoff(qa_audio_device_restore_guard *guard)
{
    qa_audio_device *active=guard->active,*candidate=guard->candidate;
    if(!native_pause(active,guard->endpoint,true)) {
        native_latch(candidate,&active->native_failure);
        native_close(active->id);active->id=NULL;
    }
    active->playing=false;
    candidate->playing=false;
    if(native_pause(candidate,candidate->id,!guard->saved_playing)) candidate->playing=guard->saved_playing;
    candidate->playing_since=candidate->playing?qa_platform_time_ns():0;
    guard->transferred=true;
}
void qa_audio_device_restore_guard_destroy(qa_audio_device_restore_guard *guard)
{ free(guard); }
