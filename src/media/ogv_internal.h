#ifndef QA_OGV_INTERNAL_H
#define QA_OGV_INTERNAL_H

#include "qa/audio.h"
#include "qa/binary.h"
#include "qa/ogv.h"
#include <ogg/ogg.h>
#include <theora/theoradec.h>

#define QA_OGV_MAX_BYTES (UINT64_C(512) * 1024 * 1024)
#define QA_OGV_MAX_PACKET (16u * 1024u * 1024u)
#define QA_OGV_PCM_FRAMES 4096u

typedef struct qa_ogv_packet {
    uint32_t offset, length;
    ogg_int64_t granule;
} qa_ogv_packet;
typedef struct qa_ogv_movie {
    qa_buffer video, audio;
    qa_ogv_packet *packets;
    size_t packet_count;
    uint64_t eos_packet;
} qa_ogv_movie;
struct qa_ogv_asset {
    size_t references;
    qa_ogv_movie movie;
    th_info video_info;
    th_setup_info *setup;
    qa_ogv_info info;
    size_t rgba_bytes;
};
struct qa_ogv_playback {
    qa_ogv_asset *asset;
    qa_ogv_options options;
    th_dec_ctx *decoder;
    qa_audio_stream *audio;
    uint8_t *pixels;
    int16_t *pcm;
    qa_media_frame frame;
    double epoch_ms;
    uint64_t loop, next_index;
    qa_media_status status;
    bool has_frame, reset_audio, faulted, dispatching;
};
bool qa_ogv_demux(qa_media_input *, qa_ogv_movie *, qa_error *);
void qa_ogv_movie_free(qa_ogv_movie *);
ogg_packet qa_ogv_native_packet(const qa_ogv_movie *, size_t index);

#endif
