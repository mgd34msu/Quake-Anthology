#include "codec_internal.h"
#include <stdio.h>

#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

static bool sample_valid(const qa_audio_sample *sample, qa_error *error) {
    size_t size;
    if (!sample || !sample->sample_rate || (sample->frame_count && !sample->samples) ||
        (sample->loop_start != QA_AUDIO_NO_LOOP && sample->loop_start >= sample->frame_count))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid PCM sample");
    return qa_audio_pcm_size(sample->frame_count, sample->channels, &size, error);
}

static qa_audio_sample *sample_allocate(uint64_t frames, unsigned channels, uint32_t rate,
                                        unsigned source_width, uint64_t loop, qa_error *error) {
    size_t size;
    if (!qa_audio_pcm_size(frames, channels, &size, error))
        return NULL;
    if (size > SIZE_MAX - sizeof(qa_audio_sample)) {
        qa_audio_codec_fail(error, QA_ERROR_MEMORY, 0, "PCM resource size overflows");
        return NULL;
    }
    qa_audio_sample *sample = malloc(sizeof(*sample) + size);
    if (!sample) {
        qa_audio_codec_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate PCM resource");
        return NULL;
    }
    sample->sample_rate = rate;
    sample->channels = channels;
    sample->source_bytes_per_sample = source_width;
    sample->frame_count = frames;
    sample->loop_start = loop;
    sample->samples = (const int16_t *)(sample + 1);
    atomic_init(&sample->references, 1);
    return sample;
}

qa_audio_sample *qa_audio_sample_retain(qa_audio_sample *sample) {
    if (!sample)
        return NULL;
    unsigned references = atomic_load_explicit(&sample->references, memory_order_relaxed);
    do {
        if (!references || references == UINT_MAX)
            return NULL;
    } while (!atomic_compare_exchange_weak_explicit(&sample->references, &references,
                                                    references + 1, memory_order_relaxed,
                                                    memory_order_relaxed));
    return sample;
}

void qa_audio_sample_release(qa_audio_sample *sample) {
    if (sample && atomic_fetch_sub_explicit(&sample->references, 1, memory_order_acq_rel) == 1)
        free(sample);
}

bool qa_audio_sample_copy(const int16_t *samples, uint64_t frames, unsigned channels, uint32_t rate,
                          uint64_t loop_start, qa_audio_sample **out, qa_error *error) {
    if (!out || !rate || (!samples && frames) ||
        (loop_start != QA_AUDIO_NO_LOOP && loop_start >= frames))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid PCM copy arguments");
    qa_audio_sample *sample = sample_allocate(frames, channels, rate, 2, loop_start, error);
    if (!sample)
        return false;
    if (frames)
        memcpy(sample + 1, samples, (size_t)frames * channels * sizeof(*samples));
    *out = sample;
    return true;
}

typedef struct wav_source {
    const uint8_t *pcm;
    uint64_t frames, loop;
    uint32_t rate;
    unsigned channels, width;
} wav_source;

static bool wav_parse(qa_bytes bytes, qa_audio_wav_policy policy, wav_source *out,
                      qa_error *error) {
    if ((!bytes.data && bytes.size) || !out ||
        (policy != QA_WAV_FORMAT && policy != QA_WAV_QUAKE && policy != QA_WAV_Q3))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid WAV arguments");
    if (bytes.size < 12)
        return qa_audio_codec_fail(error, QA_ERROR_FORMAT, 0, "Truncated RIFF/WAVE header");
    if (memcmp(bytes.data, "RIFF", 4) || memcmp(bytes.data + 8, "WAVE", 4))
        return qa_audio_codec_fail(error, QA_ERROR_FORMAT, 0, "Expected RIFF/WAVE audio");
    uint32_t riff_size = qa_load_u32le(bytes.data + 4);
    if (riff_size < 4 || riff_size > bytes.size - 8)
        return qa_audio_codec_fail(error, QA_ERROR_FORMAT, 4, "Invalid RIFF size");
    size_t end = 8 + (size_t)riff_size;
    size_t data_size = 0, data_offset = 0;
    uint64_t cue = QA_AUDIO_NO_LOOP, sampler = QA_AUDIO_NO_LOOP;
    uint32_t loop_length = 0;
    bool format_seen = false, data_seen = false, cue_seen = false, mark_seen = false;
    wav_source wav = {.loop = QA_AUDIO_NO_LOOP};
    for (size_t offset = 12; offset < end;) {
        if (end - offset < 8)
            return qa_audio_codec_fail(error, QA_ERROR_FORMAT, offset,
                                       "Truncated WAV chunk header");
        const uint8_t *id = bytes.data + offset;
        uint32_t length = qa_load_u32le(id + 4);
        size_t start = offset + 8;
        if (policy != QA_WAV_FORMAT && length > INT32_MAX)
            break;
        if (length > end - start) {
            /* Retail Quake tools leave incomplete trailing INFO metadata. */
            if (policy != QA_WAV_FORMAT && format_seen && data_seen && !memcmp(id, "LIST", 4) &&
                end - start >= 4 && !memcmp(bytes.data + start, "INFO", 4))
                break;
            return qa_audio_codec_fail(error, QA_ERROR_FORMAT, offset + 4,
                                       "WAV chunk exceeds RIFF bounds");
        }
        const uint8_t *data = bytes.data + start;
        if (!memcmp(id, "fmt ", 4) && !format_seen) {
            if (length < 16)
                return qa_audio_codec_fail(error, QA_ERROR_FORMAT, start, "Truncated WAV format");
            if (qa_load_u16le(data) != 1)
                return qa_audio_codec_fail(error, QA_ERROR_UNSUPPORTED, start,
                                           "WAV encoding is not PCM");
            wav.channels = qa_load_u16le(data + 2);
            wav.rate = qa_load_u32le(data + 4);
            unsigned bits = qa_load_u16le(data + 14);
            if (wav.channels != 1 && wav.channels != 2)
                return qa_audio_codec_fail(error, QA_ERROR_UNSUPPORTED, start + 2,
                                           "WAV must be mono or stereo");
            if (bits != 8 && bits != 16 && bits != 24)
                return qa_audio_codec_fail(error, QA_ERROR_UNSUPPORTED, start + 14,
                                           "WAV must contain 8, 16 or 24 bit PCM");
            wav.width = bits / 8;
            unsigned alignment = wav.channels * wav.width;
            if (!wav.rate || qa_load_u16le(data + 12) != alignment ||
                (uint64_t)wav.rate * alignment != qa_load_u32le(data + 8))
                return qa_audio_codec_fail(error, QA_ERROR_FORMAT, start + 4,
                                           "Invalid WAV rate or block alignment");
            format_seen = true;
        } else if (!memcmp(id, "data", 4) && !data_seen) {
            wav.pcm = data;
            data_size = length;
            data_offset = start;
            data_seen = true;
        } else if (policy != QA_WAV_Q3 && !memcmp(id, "cue ", 4)) {
            cue_seen = true;
            if (cue == QA_AUDIO_NO_LOOP) {
                if (length < 4 || qa_load_u32le(data) > (length - 4) / 24)
                    return qa_audio_codec_fail(error, QA_ERROR_FORMAT, start,
                                               "Truncated WAV cue points");
                if (qa_load_u32le(data))
                    cue = qa_load_u32le(data + 24);
            }
        } else if (policy != QA_WAV_Q3 && !memcmp(id, "smpl", 4) && sampler == QA_AUDIO_NO_LOOP) {
            if (length < 36 || qa_load_u32le(data + 28) > (length - 36) / 24)
                return qa_audio_codec_fail(error, QA_ERROR_FORMAT, start,
                                           "Truncated WAV sampler loops");
            if (qa_load_u32le(data + 28)) {
                uint32_t type = qa_load_u32le(data + 40);
                uint32_t begin = qa_load_u32le(data + 44);
                uint32_t finish = qa_load_u32le(data + 48);
                if (policy != QA_WAV_QUAKE || type != 255 || begin != UINT32_MAX ||
                    finish != UINT32_MAX)
                    sampler = begin;
            }
        } else if (policy == QA_WAV_QUAKE && cue_seen && !mark_seen && !memcmp(id, "LIST", 4) &&
                   length >= 24 && !memcmp(data + 20, "mark", 4)) {
            loop_length = qa_load_u32le(data + 16);
            mark_seen = true;
        }
        offset = start + length;
        if (offset < end)
            offset += length & 1u;
    }
    if (!format_seen || !data_seen)
        return qa_audio_codec_fail(error, QA_ERROR_FORMAT, 12, "Missing WAV format or data chunk");
    if (data_size % (wav.width * wav.channels))
        return qa_audio_codec_fail(error, QA_ERROR_FORMAT, data_offset,
                                   "Partial PCM frame in WAV data");
    wav.frames = data_size / (wav.width * wav.channels);
    wav.loop = cue != QA_AUDIO_NO_LOOP ? cue : sampler;
    if (wav.loop != QA_AUDIO_NO_LOOP) {
        if (wav.loop >= wav.frames)
            return qa_audio_codec_fail(error, QA_ERROR_FORMAT, data_offset,
                                       "WAV loop starts beyond PCM data");
        if (mark_seen) {
            if (!loop_length || loop_length > wav.frames - wav.loop)
                return qa_audio_codec_fail(error, QA_ERROR_FORMAT, data_offset,
                                           "Sound Forge loop end exceeds PCM data");
            wav.frames = wav.loop + loop_length;
        }
    }
    *out = wav;
    return true;
}

static void wav_read(const wav_source *wav, uint64_t begin, size_t frames, int16_t *out) {
    const uint8_t *data = wav->pcm + (size_t)begin * wav->channels * wav->width;
    size_t count = frames * wav->channels;
    for (size_t i = 0; i < count; ++i, data += wav->width) {
        if (wav->width == 1)
            out[i] = (int16_t)(((int)data[0] - 128) * 256);
        else
            out[i] = qa_load_i16le(data + (wav->width == 3 ? 1 : 0));
    }
}

bool qa_audio_decode_wav(qa_bytes bytes, qa_audio_wav_policy policy, qa_audio_sample **out,
                         qa_error *error) {
    if (!out)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Missing WAV output");
    wav_source wav;
    if (!wav_parse(bytes, policy, &wav, error))
        return false;
    qa_audio_sample *sample =
        sample_allocate(wav.frames, wav.channels, wav.rate, wav.width, wav.loop, error);
    if (!sample)
        return false;
    wav_read(&wav, 0, (size_t)wav.frames, (int16_t *)(sample + 1));
    *out = sample;
    return true;
}

typedef enum stream_kind { STREAM_SAMPLE, STREAM_WAV, STREAM_VORBIS } stream_kind;
typedef struct vorbis_source {
    qa_bytes bytes;
    size_t offset;
    OggVorbis_File file;
    long links;
    int16_t *scratch;
    size_t scratch_bytes;
    bool failed;
} vorbis_source;

struct qa_audio_stream {
    stream_kind kind;
    qa_bytes original;
    qa_audio_wav_policy policy;
    uint32_t rate;
    unsigned channels;
    uint64_t frames, position;
    void (*release_resource)(void *owner);
    void *resource_owner;
    qa_resource *resource;
    union {
        qa_audio_sample *sample;
        wav_source wav;
        vorbis_source vorbis;
    } source;
};

static size_t vorbis_read(void *out, size_t size, size_t count, void *source) {
    vorbis_source *memory = source;
    if (!size)
        return 0;
    size_t available = (memory->bytes.size - memory->offset) / size;
    if (count > available)
        count = available;
    if (count) {
        memcpy(out, memory->bytes.data + memory->offset, count * size);
        memory->offset += count * size;
    }
    return count;
}

static int vorbis_seek(void *source, ogg_int64_t offset, int whence) {
    vorbis_source *memory = source;
    size_t base;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = memory->offset;
        break;
    case SEEK_END:
        base = memory->bytes.size;
        break;
    default:
        return -1;
    }
    if (offset < 0) {
        uint64_t distance = (uint64_t)(-(offset + 1)) + 1;
        if (distance > base)
            return -1;
        memory->offset = base - (size_t)distance;
    } else {
        if ((uint64_t)offset > memory->bytes.size - base)
            return -1;
        memory->offset = base + (size_t)offset;
    }
    return 0;
}

static long vorbis_tell(void *source) { return (long)((vorbis_source *)source)->offset; }

static bool vorbis_open(qa_audio_stream *stream, qa_bytes bytes, qa_error *error) {
    if (bytes.size > LONG_MAX)
        return qa_audio_codec_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                   "Ogg input exceeds the native Vorbis callback seek range");
    vorbis_source *source = &stream->source.vorbis;
    source->bytes = bytes;
    const ov_callbacks callbacks = {vorbis_read, vorbis_seek, NULL, vorbis_tell};
    int result = ov_open_callbacks(source, &source->file, NULL, 0, callbacks);
    if (result) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Cannot open Ogg Vorbis stream: %d", result);
        return false;
    }
    source->links = ov_streams(&source->file);
    if (source->links < 1 || source->links > 65536) {
        qa_audio_codec_fail(error, QA_ERROR_FORMAT, 0, "Invalid Ogg logical stream count");
        goto fail;
    }
    for (long i = 0; i < source->links; ++i) {
        vorbis_info *info = ov_info(&source->file, (int)i);
        if (!info || (info->channels != 1 && info->channels != 2) || info->rate < 8000 ||
            info->rate > 192000) {
            qa_audio_codec_fail(error, QA_ERROR_UNSUPPORTED, 0, "Unsupported Ogg PCM format");
            goto fail;
        }
        if (!i) {
            stream->channels = (unsigned)info->channels;
            stream->rate = (uint32_t)info->rate;
        } else if ((unsigned)info->channels != stream->channels ||
                   (uint32_t)info->rate != stream->rate) {
            qa_audio_codec_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                "Ogg chained streams change PCM format");
            goto fail;
        }
    }
    ogg_int64_t frames = ov_pcm_total(&source->file, -1);
    if (frames < 0) {
        qa_audio_codec_fail(error, QA_ERROR_FORMAT, 0, "Invalid Ogg PCM frame count");
        goto fail;
    }
    stream->frames = (uint64_t)frames;
    if (ov_pcm_seek(&source->file, 0) != 0 && frames != 0) {
        qa_audio_codec_fail(error, QA_ERROR_FORMAT, 0, "Cannot seek to Ogg stream start");
        goto fail;
    }
    return true;
fail:
    ov_clear(&source->file);
    return false;
}

bool qa_audio_stream_open(qa_bytes bytes, qa_audio_wav_policy policy, qa_audio_stream **out,
                          qa_error *error) {
    if (!out || (!bytes.data && bytes.size) ||
        (policy != QA_WAV_FORMAT && policy != QA_WAV_QUAKE && policy != QA_WAV_Q3))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid stream arguments");
    bool ogg = bytes.size >= 4 && !memcmp(bytes.data, "OggS", 4);
    wav_source wav = {0};
    if (!ogg && !wav_parse(bytes, policy, &wav, error))
        return false;
    qa_audio_stream *stream = calloc(1, sizeof(*stream));
    if (!stream)
        return qa_audio_codec_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate PCM stream");
    stream->original = bytes; stream->policy = policy;
    if (ogg) {
        stream->kind = STREAM_VORBIS;
        if (!vorbis_open(stream, bytes, error)) {
            free(stream);
            return false;
        }
    } else {
        stream->kind = STREAM_WAV;
        stream->rate = wav.rate;
        stream->channels = wav.channels;
        stream->frames = wav.frames;
        stream->source.wav = wav;
    }
    *out = stream;
    return true;
}

bool qa_audio_stream_open_retained(qa_bytes bytes, qa_audio_wav_policy policy,
                                   void (*release)(void *owner), void *owner, qa_audio_stream **out,
                                   qa_error *error) {
    if (!out || !release)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Missing stream resource lease");
    qa_audio_stream *stream;
    if (!qa_audio_stream_open(bytes, policy, &stream, error))
        return false;
    stream->release_resource = release;
    stream->resource_owner = owner;
    *out = stream;
    return true;
}

static void release_stream_resource(void *owner) { qa_resource_release(owner); }

bool qa_audio_stream_open_resource(qa_resource *resource, qa_audio_wav_policy policy,
                                   qa_audio_stream **out, qa_error *error) {
    if (!resource || !out)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Missing stream content resource");
    qa_audio_stream *stream;
    if (!qa_audio_stream_open_retained(qa_resource_bytes(resource), policy,
                                      release_stream_resource, resource, &stream, error))
        return false;
    stream->resource = resource;
    *out = stream;
    return true;
}

bool qa_audio_stream_from_sample(qa_audio_sample *sample, qa_audio_stream **out, qa_error *error) {
    if (!out)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Missing PCM stream output");
    if (!sample_valid(sample, error))
        return false;
    qa_audio_stream *stream = calloc(1, sizeof(*stream));
    if (!stream)
        return qa_audio_codec_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate PCM stream");
    if (!qa_audio_sample_retain(sample)) {
        free(stream);
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "PCM reference count is exhausted");
    }
    stream->kind = STREAM_SAMPLE;
    stream->rate = sample->sample_rate;
    stream->channels = sample->channels;
    stream->frames = sample->frame_count;
    stream->source.sample = sample;
    *out = stream;
    return true;
}

uint32_t qa_audio_stream_rate(const qa_audio_stream *stream) { return stream ? stream->rate : 0; }
unsigned qa_audio_stream_channels(const qa_audio_stream *stream) {
    return stream ? stream->channels : 0;
}
uint64_t qa_audio_stream_frames(const qa_audio_stream *stream) {
    return stream ? stream->frames : 0;
}
uint64_t qa_audio_stream_position(const qa_audio_stream *stream) {
    return stream ? stream->position : 0;
}

static bool vorbis_read_pcm(qa_audio_stream *stream, int16_t *out, size_t count, size_t *frames,
                            qa_error *error) {
    vorbis_source *source = &stream->source.vorbis;
    if (source->failed)
        return qa_audio_codec_fail(error, QA_ERROR_FORMAT, 0,
                                   "Ogg stream state could not be restored");
    size_t bytes = count * stream->channels * sizeof(int16_t);
    if (bytes > source->scratch_bytes) {
        int16_t *scratch = realloc(source->scratch, bytes);
        if (!scratch)
            return qa_audio_codec_fail(error, QA_ERROR_MEMORY, 0,
                                       "Cannot allocate Ogg read buffer");
        source->scratch = scratch;
        source->scratch_bytes = bytes;
    }
    const uint16_t endian = 1;
    int big_endian = *(const uint8_t *)&endian == 0;
    size_t received = 0;
    size_t alignment = stream->channels * sizeof(int16_t);
    size_t maximum = (size_t)INT_MAX - (size_t)INT_MAX % alignment;
    while (received < bytes) {
        size_t request = bytes - received;
        if (request > maximum)
            request = maximum;
        int link = -1;
        long read = ov_read(&source->file, (char *)source->scratch + received, (int)request,
                            big_endian, 2, 1, &link);
        if (read < 0 || (read > 0 && ((size_t)read > request || (size_t)read % alignment ||
                                      link < 0 || link >= source->links))) {
            qa_error_set(error, QA_ERROR_FORMAT, source->offset, "Invalid Ogg PCM packet: %ld",
                         read);
            goto rollback;
        }
        if (!read) {
            qa_audio_codec_fail(error, QA_ERROR_FORMAT, source->offset,
                                "Ogg stream ended before its declared PCM length");
            goto rollback;
        }
        received += (size_t)read;
    }
    memcpy(out, source->scratch, received);
    *frames = received / alignment;
    stream->position += *frames;
    return true;
rollback:
    if (ov_pcm_seek(&source->file, (ogg_int64_t)stream->position))
        source->failed = true;
    return false;
}

bool qa_audio_stream_read(qa_audio_stream *stream, int16_t *out, size_t capacity_frames,
                          size_t *frames, qa_error *error) {
    if (!stream || !frames || !out || !capacity_frames)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid PCM read arguments");
    size_t size;
    if (!qa_audio_pcm_size(capacity_frames, stream->channels, &size, error))
        return false;
    uint64_t available = stream->frames - stream->position;
    size_t count = capacity_frames;
    if (available < count)
        count = (size_t)available;
    if (!count) {
        *frames = 0;
        return true;
    }
    switch (stream->kind) {
    case STREAM_SAMPLE:
        memcpy(out, stream->source.sample->samples + (size_t)stream->position * stream->channels,
               count * stream->channels * sizeof(*out));
        break;
    case STREAM_WAV:
        wav_read(&stream->source.wav, stream->position, count, out);
        break;
    case STREAM_VORBIS:
        return vorbis_read_pcm(stream, out, count, frames, error);
    }
    stream->position += count;
    *frames = count;
    return true;
}

bool qa_audio_stream_seek(qa_audio_stream *stream, uint64_t frame, qa_error *error) {
    if (!stream || frame > stream->frames)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "PCM seek is outside stream");
    if (stream->kind == STREAM_VORBIS) {
        vorbis_source *source = &stream->source.vorbis;
        int result = ov_pcm_seek(&source->file, (ogg_int64_t)frame);
        if (result) {
            if (ov_pcm_seek(&source->file, (ogg_int64_t)stream->position))
                source->failed = true;
            qa_error_set(error, QA_ERROR_FORMAT, source->offset, "Ogg PCM seek failed: %d", result);
            return false;
        }
        source->failed = false;
    }
    stream->position = frame;
    return true;
}

void qa_audio_stream_close(qa_audio_stream *stream) {
    if (!stream)
        return;
    if (stream->kind == STREAM_SAMPLE)
        qa_audio_sample_release(stream->source.sample);
    else if (stream->kind == STREAM_VORBIS) {
        ov_clear(&stream->source.vorbis.file);
        free(stream->source.vorbis.scratch);
    }
    if (stream->release_resource)
        stream->release_resource(stream->resource_owner);
    free(stream);
}

bool qa_audio_decode(qa_bytes bytes, qa_audio_wav_policy policy, qa_audio_sample **out,
                     qa_error *error) {
    if (!out || (!bytes.data && bytes.size))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid sound decode arguments");
    if (bytes.size < 4 || memcmp(bytes.data, "OggS", 4))
        return qa_audio_decode_wav(bytes, policy, out, error);
    qa_audio_stream *stream;
    if (!qa_audio_stream_open(bytes, policy, &stream, error))
        return false;
    qa_audio_sample *sample =
        sample_allocate(stream->frames, stream->channels, stream->rate, 2, QA_AUDIO_NO_LOOP, error);
    if (!sample) {
        qa_audio_stream_close(stream);
        return false;
    }
    uint64_t offset = 0;
    while (offset < sample->frame_count) {
        uint64_t remaining = sample->frame_count - offset;
        size_t count = remaining < 16384 ? (size_t)remaining : 16384;
        size_t received;
        if (!qa_audio_stream_read(stream,
                                  (int16_t *)(sample + 1) + (size_t)offset * sample->channels,
                                  count, &received, error)) {
            qa_audio_stream_close(stream);
            qa_audio_sample_release(sample);
            return false;
        }
        offset += received;
    }
    qa_audio_stream_close(stream);
    *out = sample;
    return true;
}

bool qa_audio_source_layout_compute(const qa_audio_sample *sample, uint32_t output_rate,
                                    qa_audio_family family, qa_audio_source_layout *out,
                                    qa_error *error) {
    if (!out || !output_rate ||
        (family != QA_AUDIO_Q1 && family != QA_AUDIO_Q2 && family != QA_AUDIO_Q3))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0,
                                   "Invalid source resampling arguments");
    if (!sample_valid(sample, error))
        return false;
    bool q3 = family == QA_AUDIO_Q3;
    float ratio = (float)sample->sample_rate / (float)output_rate;
    double count = truncf((float)sample->frame_count / ratio);
    if (!(count >= 0 && count < 0x1p64))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0,
                                   "Resampled length exceeds frame space");
    qa_audio_source_layout layout = {.frames = (uint64_t)count,
                                     .loop_start = QA_AUDIO_NO_LOOP,
                                     .step256 = (uint64_t)(ratio * 256.0f)};
    if (!q3 && sample->loop_start != QA_AUDIO_NO_LOOP) {
        double loop = truncf((float)sample->loop_start / ratio);
        if (!(loop >= 0 && loop < 0x1p64) || (uint64_t)loop >= layout.frames)
            return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0,
                                       "Resampled loop is outside PCM data");
        layout.loop_start = (uint64_t)loop;
    }
    *out = layout;
    return true;
}

bool qa_audio_resample_source(const qa_audio_sample *sample, uint32_t output_rate,
                              qa_audio_family family, qa_audio_sample **out, qa_error *error) {
    if (!out)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Missing resampled PCM output");
    qa_audio_source_layout layout;
    if (!qa_audio_source_layout_compute(sample, output_rate, family, &layout, error))
        return false;
    qa_audio_sample *result =
        sample_allocate(layout.frames, sample->channels, output_rate,
                        sample->source_bytes_per_sample, layout.loop_start, error);
    if (!result)
        return false;
    int16_t *pcm = (int16_t *)(result + 1);
    for (uint64_t frame = 0; frame < layout.frames; ++frame) {
        uint64_t source;
        bool in_bounds =
            qa_audio_source_index(&layout, frame, &source) && source < sample->frame_count;
        for (unsigned channel = 0; channel < sample->channels; ++channel)
            pcm[(size_t)frame * sample->channels + channel] =
                in_bounds ? sample->samples[(size_t)source * sample->channels + channel] : 0;
    }
    *out = result;
    return true;
}

static const int adpcm_index[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int adpcm_step[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static bool adpcm_state_valid(const qa_audio_adpcm_state *state, qa_error *error) {
    if (!state || state->predictor < INT16_MIN || state->predictor > INT16_MAX ||
        state->step_index < 0 || state->step_index > 88)
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0,
                                   "Invalid ADPCM predictor or step index");
    return true;
}

static int adpcm_clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

bool qa_audio_adpcm_decode(qa_bytes bytes, size_t samples, qa_audio_adpcm_state *state,
                           int16_t *out, size_t capacity, qa_error *error) {
    if ((!bytes.data && bytes.size) || (!out && samples) || capacity < samples ||
        samples > SIZE_MAX / sizeof(*out))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid ADPCM decode arguments");
    if (!adpcm_state_valid(state, error))
        return false;
    if (bytes.size < samples / 2 + samples % 2)
        return qa_audio_codec_fail(error, QA_ERROR_FORMAT, 0, "Truncated ADPCM input");
    int predictor = state->predictor, index = state->step_index;
    for (size_t i = 0; i < samples; ++i) {
        unsigned code = (i & 1) ? bytes.data[i / 2] & 15u : (unsigned)bytes.data[i / 2] >> 4;
        int step = adpcm_step[index];
        int difference = (step >> 3) + ((code & 4) ? step : 0) + ((code & 2) ? step >> 1 : 0) +
                         ((code & 1) ? step >> 2 : 0);
        predictor =
            adpcm_clamp(predictor + ((code & 8) ? -difference : difference), INT16_MIN, INT16_MAX);
        index = adpcm_clamp(index + adpcm_index[code], 0, 88);
        out[i] = (int16_t)predictor;
    }
    state->predictor = predictor;
    state->step_index = index;
    return true;
}

bool qa_audio_adpcm_encode(const int16_t *samples, size_t count, qa_audio_adpcm_state *state,
                           qa_buffer *out, qa_error *error) {
    if (!out || (!samples && count) || count > SIZE_MAX / sizeof(*samples))
        return qa_audio_codec_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid ADPCM encode arguments");
    if (!adpcm_state_valid(state, error))
        return false;
    size_t size = count / 2 + count % 2;
    uint8_t *data = size ? calloc(size, 1) : NULL;
    if (size && !data)
        return qa_audio_codec_fail(error, QA_ERROR_MEMORY, 0, "Cannot allocate ADPCM output");
    int predictor = state->predictor, index = state->step_index;
    for (size_t i = 0; i < count; ++i) {
        int difference = samples[i] - predictor;
        unsigned code = difference < 0 ? 8u : 0u;
        if (difference < 0)
            difference = -difference;
        int step = adpcm_step[index];
        int predicted_difference = step >> 3;
        for (unsigned bit = 4; bit; bit >>= 1, step >>= 1) {
            if (difference >= step) {
                code |= bit;
                difference -= step;
                predicted_difference += step;
            }
        }
        predictor =
            adpcm_clamp(predictor + ((code & 8) ? -predicted_difference : predicted_difference),
                        INT16_MIN, INT16_MAX);
        index = adpcm_clamp(index + adpcm_index[code], 0, 88);
        if (i & 1)
            data[i / 2] |= (uint8_t)code;
        else
            data[i / 2] = (uint8_t)(code << 4);
    }
    state->predictor = predictor;
    state->step_index = index;
    *out = (qa_buffer){data, size};
    return true;
}
