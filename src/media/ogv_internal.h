#ifndef QA_OGV_INTERNAL_H
#define QA_OGV_INTERNAL_H

#include "qa/audio.h"
#include "qa/binary.h"
#include "qa/ogv.h"
#include "../platform/native_libraries.h"
#include <ogg/ogg.h>
#include <theora/theoradec.h>

#define QA_OGV_MAX_BYTES (UINT64_C(512) * 1024 * 1024)
#define QA_OGV_MAX_PACKET (16u * 1024u * 1024u)
#define QA_OGV_PCM_FRAMES 4096u

typedef struct qa_theora_api {
    void (*th_info_init)(th_info *);
    void (*th_info_clear)(th_info *);
    void (*th_comment_init)(th_comment *);
    void (*th_comment_clear)(th_comment *);
    int (*th_decode_headerin)(th_info *, th_comment *, th_setup_info **, ogg_packet *);
    th_dec_ctx *(*th_decode_alloc)(const th_info *, const th_setup_info *);
    void (*th_setup_free)(th_setup_info *);
    void (*th_decode_free)(th_dec_ctx *);
    int (*th_decode_packetin)(th_dec_ctx *, const ogg_packet *, ogg_int64_t *);
    int (*th_decode_ycbcr_out)(th_dec_ctx *, th_ycbcr_buffer);
} qa_theora_api;

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
    qa_platform_library *library;
    qa_theora_api api;
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
