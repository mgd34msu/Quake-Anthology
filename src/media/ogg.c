#include "ogv_internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum stream_kind { STREAM_UNKNOWN, STREAM_VIDEO, STREAM_AUDIO, STREAM_IGNORED } stream_kind;
typedef struct logical_stream {
    ogg_stream_state native;
    uint32_t serial, next_sequence;
    uint64_t packets;
    size_t pending_bytes;
    qa_buffer pages;
    size_t page_capacity;
    stream_kind kind;
    bool initialized, continued, ended;
} logical_stream;

typedef struct movie_builder {
    qa_ogv_movie movie;
    size_t packet_capacity, video_capacity;
    logical_stream streams[8];
    size_t stream_count;
    bool has_video, has_audio;
} movie_builder;

static bool append(qa_buffer *buffer, size_t *capacity, qa_bytes bytes, qa_error *error) {
    if (bytes.size > SIZE_MAX - buffer->size || buffer->size + bytes.size > QA_OGV_MAX_BYTES) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Ogg stream storage exceeds movie limit");
        return false;
    }
    size_t needed = buffer->size + bytes.size;
    if (needed > *capacity) {
        size_t grown = *capacity ? *capacity : 65536;
        while (grown < needed) {
            if (grown > (size_t)QA_OGV_MAX_BYTES / 2) {
                grown = needed;
                break;
            }
            grown *= 2;
        }
        uint8_t *data = realloc(buffer->data, grown);
        if (!data) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating compressed Ogg storage");
            return false;
        }
        buffer->data = data;
        *capacity = grown;
    }
    if (bytes.size)
        memcpy(buffer->data + buffer->size, bytes.data, bytes.size);
    buffer->size = needed;
    return true;
}

static bool save_packet(movie_builder *builder, const ogg_packet *packet, qa_error *error) {
    qa_ogv_movie *movie = &builder->movie;
    if (movie->packet_count == builder->packet_capacity) {
        size_t capacity = builder->packet_capacity ? builder->packet_capacity * 2 : 256;
        if (capacity < builder->packet_capacity || capacity > SIZE_MAX / sizeof(*movie->packets)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Ogg packet index overflow");
            return false;
        }
        qa_ogv_packet *packets = realloc(movie->packets, capacity * sizeof(*packets));
        if (!packets) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Ogg packet index");
            return false;
        }
        movie->packets = packets;
        builder->packet_capacity = capacity;
    }
    qa_ogv_packet saved = {.offset = (uint32_t)movie->video.size,
                           .length = (uint32_t)packet->bytes,
                           .granule = packet->granulepos};
    if (!append(&movie->video, &builder->video_capacity,
                (qa_bytes){packet->packet, (size_t)packet->bytes}, error))
        return false;
    if (packet->e_o_s)
        movie->eos_packet = movie->packet_count;
    movie->packets[movie->packet_count++] = saved;
    return true;
}

static bool classify(movie_builder *builder, logical_stream *stream, const ogg_packet *packet,
                     qa_error *error) {
    bool video = packet->bytes >= 7 && packet->packet[0] == 0x80 &&
                 memcmp(packet->packet + 1, "theora", 6) == 0;
    bool audio = packet->bytes >= 7 && packet->packet[0] == 1 &&
                 memcmp(packet->packet + 1, "vorbis", 6) == 0;
    if ((video && builder->has_video) || (audio && builder->has_audio)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Ogg movie has multiple %s streams",
                     video ? "Theora" : "Vorbis");
        return false;
    }
    stream->kind = video ? STREAM_VIDEO : audio ? STREAM_AUDIO : STREAM_IGNORED;
    builder->has_video |= video;
    builder->has_audio |= audio;
    if (!audio) {
        qa_buffer_free(&stream->pages);
        stream->page_capacity = 0;
    }
    return true;
}

static bool read_page(qa_media_input *input, uint64_t offset, uint64_t input_size,
                      uint8_t storage[65307], size_t *page_size, ogg_page *page, qa_error *error) {
    if (input_size - offset < 27 || !qa_media_input_read(input, offset, storage, 27, error)) {
        if (input_size - offset < 27)
            qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset, "Truncated Ogg page header");
        return false;
    }
    if (memcmp(storage, "OggS", 4) != 0 || storage[4] != 0 || (storage[5] & ~7u)) {
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset,
                     "Invalid Ogg page capture, version, or flags");
        return false;
    }
    size_t segments = storage[26], header = 27 + segments;
    if (header > input_size - offset) {
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset, "Truncated Ogg page lacing");
        return false;
    }
    if (segments && !qa_media_input_read(input, offset + 27, storage + 27, segments, error))
        return false;
    size_t payload = 0;
    for (size_t i = 0; i < segments; ++i)
        payload += storage[27 + i];
    size_t total = header + payload;
    if (total > input_size - offset) {
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset, "Truncated Ogg page payload");
        return false;
    }
    if (payload && !qa_media_input_read(input, offset + header, storage + header, payload, error))
        return false;
    *page = (ogg_page){.header = storage,
                       .header_len = (long)header,
                       .body = storage + header,
                       .body_len = (long)payload};
    uint32_t expected = qa_load_u32le(storage + 22);
    ogg_page_checksum_set(page);
    if (expected != qa_load_u32le(storage + 22)) {
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset + 22, "Ogg page checksum mismatch");
        return false;
    }
    *page_size = total;
    return true;
}

static logical_stream *admit_page(movie_builder *builder, ogg_page *page, uint64_t offset,
                                  qa_error *error) {
    const uint8_t *header = page->header;
    uint32_t serial = qa_load_u32le(header + 14), sequence = qa_load_u32le(header + 18);
    logical_stream *stream = NULL;
    for (size_t i = 0; i < builder->stream_count; ++i)
        if (builder->streams[i].serial == serial) {
            stream = &builder->streams[i];
            break;
        }
    bool first = (header[5] & 2) != 0;
    if (!stream) {
        if (!first || sequence != 0 || builder->stream_count == 8) {
            qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset,
                         "Invalid Ogg logical stream start");
            return NULL;
        }
        stream = &builder->streams[builder->stream_count++];
        stream->serial = serial;
        if (ogg_stream_init(&stream->native, qa_load_i32le(header + 14)) != 0) {
            qa_error_set(error, QA_ERROR_MEMORY, (size_t)offset, "Allocating Ogg logical stream");
            return NULL;
        }
        stream->initialized = true;
    } else if (first || stream->ended) {
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset,
                     "Ogg logical stream restarted after admission");
        return NULL;
    }
    if (sequence != stream->next_sequence || ((header[5] & 1) != 0) != stream->continued) {
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset, "Ogg packet sequence discontinuity");
        return NULL;
    }
    ++stream->next_sequence;
    for (unsigned i = 0; i < header[26]; ++i) {
        unsigned size = header[27 + i];
        stream->pending_bytes += size;
        if (stream->pending_bytes > QA_OGV_MAX_PACKET) {
            qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset, "Ogg packet exceeds 16 MiB");
            return NULL;
        }
        stream->continued = size == 255;
        if (!stream->continued)
            stream->pending_bytes = 0;
    }
    if ((header[5] & 4) && stream->continued) {
        qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset, "Ogg stream ends inside a packet");
        return NULL;
    }
    return stream;
}

bool qa_ogv_demux(qa_media_input *input, qa_ogv_movie *out, qa_error *error) {
    if (!input || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Ogg movie input or output");
        return false;
    }
    uint64_t size = qa_media_input_size(input);
    if (size > QA_OGV_MAX_BYTES) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Ogg movie exceeds 512 MiB");
        return false;
    }
    movie_builder builder = {.movie = {.eos_packet = UINT64_MAX}};
    uint8_t storage[65307];
    bool success = false;
    for (uint64_t offset = 0; offset < size;) {
        size_t page_size;
        ogg_page page;
        if (!read_page(input, offset, size, storage, &page_size, &page, error))
            goto done;
        logical_stream *stream = admit_page(&builder, &page, offset, error);
        if (!stream)
            goto done;
        if ((stream->kind == STREAM_UNKNOWN || stream->kind == STREAM_AUDIO) &&
            !append(&stream->pages, &stream->page_capacity, (qa_bytes){storage, page_size}, error))
            goto done;
        if (ogg_stream_pagein(&stream->native, &page) != 0) {
            qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset,
                         "Native Ogg page admission failed");
            goto done;
        }
        for (;;) {
            ogg_packet packet;
            int result = ogg_stream_packetout(&stream->native, &packet);
            if (!result)
                break;
            if (result < 0 || packet.bytes < 0 || packet.bytes > QA_OGV_MAX_PACKET ||
                packet.packetno < 0 || (uint64_t)packet.packetno != stream->packets) {
                qa_error_set(error, QA_ERROR_FORMAT, (size_t)offset, "Invalid Ogg packet boundary");
                goto done;
            }
            if (!stream->packets && !classify(&builder, stream, &packet, error))
                goto done;
            if (stream->kind == STREAM_VIDEO && !save_packet(&builder, &packet, error))
                goto done;
            ++stream->packets;
        }
        if (page.header[5] & 4)
            stream->ended = true;
        offset += page_size;
    }
    for (size_t i = 0; i < builder.stream_count; ++i) {
        logical_stream *stream = &builder.streams[i];
        if (!stream->ended || !stream->packets || stream->continued) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Empty or truncated Ogg logical stream");
            goto done;
        }
        if (stream->kind == STREAM_AUDIO) {
            builder.movie.audio = stream->pages;
            stream->pages = (qa_buffer){0};
        }
    }
    if (!builder.has_video || builder.movie.packet_count < 4) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Ogg movie has no complete Theora video");
        goto done;
    }
    *out = builder.movie;
    builder.movie = (qa_ogv_movie){0};
    success = true;
done:
    for (size_t i = 0; i < builder.stream_count; ++i) {
        if (builder.streams[i].initialized)
            ogg_stream_clear(&builder.streams[i].native);
        qa_buffer_free(&builder.streams[i].pages);
    }
    qa_ogv_movie_free(&builder.movie);
    return success;
}

void qa_ogv_movie_free(qa_ogv_movie *movie) {
    qa_buffer_free(&movie->video);
    qa_buffer_free(&movie->audio);
    free(movie->packets);
    *movie = (qa_ogv_movie){0};
}
ogg_packet qa_ogv_native_packet(const qa_ogv_movie *movie, size_t index) {
    const qa_ogv_packet *packet = &movie->packets[index];
    return (ogg_packet){.packet = movie->video.data + packet->offset,
                        .bytes = (long)packet->length,
                        .b_o_s = index == 0,
                        .e_o_s = index == movie->eos_packet,
                        .granulepos = packet->granule,
                        .packetno = (ogg_int64_t)index};
}
