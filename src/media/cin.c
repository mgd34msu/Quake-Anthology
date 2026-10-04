#include "qa/media.h"
#include "qa/binary.h"

#include <stdlib.h>
#include <string.h>

#define CIN_HEADER_BYTES UINT64_C(65556)
#define CIN_COMPRESSED_LIMIT 131072u

struct qa_cin_asset {
    size_t references;
    qa_media_input *input;
    qa_cin_info info;
    uint16_t roots[256], children[256][255][2];
};
struct qa_cin_decoder {
    qa_cin_asset *asset;
    uint64_t offset, index;
    uint8_t palette[768], compressed[CIN_COMPRESSED_LIMIT];
    uint8_t *pixels, *audio;
    size_t audio_capacity;
    bool ended;
};

static bool fail(qa_error *error, const char *text) { qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", text); return false; }
static int smallest(uint32_t *weights, bool *used, unsigned limit) {
    uint32_t best = UINT32_MAX;
    int selected = -1;
    for (unsigned i = 0; i < limit; ++i) if (!used[i] && weights[i] && weights[i] < best) {
        best = weights[i]; selected = (int)i;
    }
    if (selected >= 0) used[selected] = true;
    return selected;
}
bool qa_cin_asset_load(qa_media_input *input, qa_cin_asset **out, qa_error *error) {
    if (!input || !out) return fail(error, "Missing CIN source or output");
    uint8_t header[20];
    if (!qa_media_input_read(input, 0, header, sizeof(header), error)) return false;
    int32_t width = qa_load_i32le(header), height = qa_load_i32le(header + 4);
    int32_t rate = qa_load_i32le(header + 8), bytes = qa_load_i32le(header + 12), channels = qa_load_i32le(header + 16);
    if (width <= 0 || height <= 0 || (uint64_t)(uint32_t)width * (uint32_t)height > UINT64_C(0x1000000))
        return fail(error, "Invalid CIN dimensions");
    if (!(rate == 0 && bytes == 0 && channels == 0) &&
        !(rate > 0 && (bytes == 1 || bytes == 2) && (channels == 1 || channels == 2)))
        return fail(error, "Invalid CIN audio format");
    qa_cin_asset *asset = calloc(1, sizeof(*asset));
    uint8_t *counts = malloc(65536);
    if (!asset || !counts) { free(asset); free(counts); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CIN codebooks"); return false; }
    if (!qa_media_input_read(input, 20, counts, 65536, error)) { free(counts); free(asset); return false; }
    for (unsigned context = 0; context < 256; ++context) {
        uint32_t weights[512] = {0}; bool used[512] = {0};
        for (unsigned i = 0; i < 256; ++i) weights[i] = counts[context * 256 + i];
        unsigned count = 256;
        while (count != 511) {
            int left = smallest(weights, used, count);
            if (left < 0) break;
            int right = smallest(weights, used, count);
            if (right < 0) break;
            asset->children[context][count - 256][0] = (uint16_t)left;
            asset->children[context][count - 256][1] = (uint16_t)right;
            weights[count++] = weights[left] + weights[right];
        }
        asset->roots[context] = (uint16_t)(count - 1);
    }
    free(counts);
    asset->references = 1; asset->input = input; qa_media_input_retain(input);
    asset->info = (qa_cin_info){(uint32_t)width, (uint32_t)height, (uint32_t)rate, (uint8_t)channels, (uint8_t)bytes};
    *out = asset; return true;
}
void qa_cin_asset_retain(qa_cin_asset *asset) { if (asset) ++asset->references; }
void qa_cin_asset_release(qa_cin_asset *asset) {
    if (!asset || --asset->references) return;
    qa_media_input_release(asset->input); free(asset);
}
qa_cin_info qa_cin_asset_info(const qa_cin_asset *asset) { return asset->info; }
bool qa_cin_decoder_create(qa_cin_asset *asset, qa_cin_decoder **out, qa_error *error) {
    if (!asset || !out) return fail(error, "Missing CIN asset or output");
    qa_cin_decoder *decoder = calloc(1, sizeof(*decoder));
    if (!decoder) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CIN decoder"); return false; }
    decoder->pixels = malloc((size_t)asset->info.width * asset->info.height);
    if (!decoder->pixels) { free(decoder); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CIN pixels"); return false; }
    decoder->asset = asset; qa_cin_asset_retain(asset);
    decoder->offset = CIN_HEADER_BYTES; *out = decoder; return true;
}
void qa_cin_decoder_destroy(qa_cin_decoder *decoder) {
    if (!decoder) return;
    qa_cin_asset_release(decoder->asset); free(decoder->pixels); free(decoder->audio); free(decoder);
}
bool qa_cin_sample_range(uint64_t frame, uint32_t rate, uint64_t *start, uint64_t *end, qa_error *error) {
    if (!start || !end || frame == UINT64_MAX || (rate && frame + 1 > UINT64_MAX / rate))
        return fail(error, "CIN sample position overflow");
    *start = frame * rate / 14; *end = (frame + 1) * rate / 14;
    return true;
}
static bool read_at(qa_cin_decoder *decoder, uint64_t *offset, void *data, size_t length, qa_error *error) {
    if (!qa_media_input_read(decoder->asset->input, *offset, data, length, error)) return false;
    *offset += length; return true;
}
static bool pixels_decode(qa_cin_decoder *decoder, size_t size, qa_error *error) {
    size_t count = (size_t)decoder->asset->info.width * decoder->asset->info.height;
    if (qa_load_u32le(decoder->compressed) != count) return fail(error, "CIN frame pixel count differs from dimensions");
    unsigned context = 0, bits = 0, value = 0;
    size_t offset = 4;
    for (size_t i = 0; i < count; ++i) {
        unsigned node = decoder->asset->roots[context];
        while (node >= 256) {
            if (!bits) {
                if (offset == size) return fail(error, "Truncated CIN Huffman frame");
                value = decoder->compressed[offset++]; bits = 8;
            }
            unsigned child = decoder->asset->children[context][node - 256][value & 1];
            if (child >= node) return fail(error, "Invalid CIN Huffman branch");
            node = child; value >>= 1; --bits;
        }
        decoder->pixels[i] = (uint8_t)node; context = node;
    }
    return true;
}
bool qa_cin_decoder_next(qa_cin_decoder *decoder, qa_cin_frame *out, qa_error *error) {
    if (!decoder || !out) return fail(error, "Missing CIN decoder or output");
    qa_cin_info info = decoder->asset->info;
    if (decoder->ended) { *out = (qa_cin_frame){.ended = true, .info = info}; return true; }
    uint64_t offset = decoder->offset;
    uint8_t word[4], palette[768];
    if (!read_at(decoder, &offset, word, sizeof(word), error)) return false;
    uint32_t command = qa_load_u32le(word);
    if (command == 2) { decoder->offset = offset; decoder->ended = true; *out = (qa_cin_frame){.ended = true, .info = info}; return true; }
    memcpy(palette, decoder->palette, sizeof(palette));
    if (command == 1 && !read_at(decoder, &offset, palette, sizeof(palette), error)) return false;
    if (!read_at(decoder, &offset, word, sizeof(word), error)) return false;
    uint32_t size = qa_load_u32le(word);
    if (size < 4 || size > CIN_COMPRESSED_LIMIT) return fail(error, "Invalid CIN compressed frame size");
    if (!read_at(decoder, &offset, decoder->compressed, size, error)) return false;
    uint64_t start = 0, end = 0;
    size_t audio_bytes = 0;
    if (info.channels) {
        if (!qa_cin_sample_range(decoder->index, info.sample_rate, &start, &end, error)) return false;
        uint64_t bytes = (end - start) * info.channels * info.sample_bytes;
        if (bytes > SIZE_MAX || offset > qa_media_input_size(decoder->asset->input) ||
            bytes > qa_media_input_size(decoder->asset->input) - offset) return fail(error, "Truncated CIN audio");
        audio_bytes = (size_t)bytes;
        if (audio_bytes > decoder->audio_capacity) {
            uint8_t *grown = realloc(decoder->audio, audio_bytes);
            if (!grown) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating CIN PCM scratch"); return false; }
            decoder->audio = grown; decoder->audio_capacity = audio_bytes;
        }
        if (!read_at(decoder, &offset, decoder->audio, audio_bytes, error)) return false;
    }
    if (decoder->index == UINT64_MAX) return fail(error, "CIN frame index overflow");
    if (!pixels_decode(decoder, size, error)) return false;
    memcpy(decoder->palette, palette, sizeof(palette)); decoder->offset = offset;
    *out = (qa_cin_frame){.index = decoder->index++, .source_sample = start, .pixels = decoder->pixels,
        .palette = decoder->palette, .audio = {decoder->audio, audio_bytes}, .info = info};
    return true;
}
void qa_cin_decoder_rewind(qa_cin_decoder *decoder) {
    decoder->offset = CIN_HEADER_BYTES; decoder->index = 0; decoder->ended = false;
    memset(decoder->palette, 0, sizeof(decoder->palette));
}
const uint8_t *qa_cin_decoder_palette(const qa_cin_decoder *decoder) { return decoder->palette; }
uint64_t qa_cin_decoder_index(const qa_cin_decoder *decoder) { return decoder->index; }
bool qa_cin_decoder_capture(qa_cin_decoder *decoder, qa_cin_checkpoint *out, qa_error *error) {
    if (!decoder || !out) return fail(error, "Missing CIN checkpoint output");
    qa_cin_checkpoint result = {.input_size = qa_media_input_size(decoder->asset->input),
        .offset = decoder->offset, .next_frame = decoder->index, .ended = decoder->ended};
    memcpy(result.palette, decoder->palette, sizeof(result.palette)); *out = result; return true;
}
bool qa_cin_decoder_restore(qa_cin_decoder *decoder, const qa_cin_checkpoint *saved, qa_error *error) {
    if (!decoder || !saved || saved->input_size != qa_media_input_size(decoder->asset->input) ||
        saved->offset < CIN_HEADER_BYTES || saved->offset > saved->input_size) return fail(error, "Invalid CIN checkpoint position");
    decoder->offset = saved->offset; decoder->index = saved->next_frame; decoder->ended = saved->ended;
    memcpy(decoder->palette, saved->palette, sizeof(decoder->palette)); return true;
}
bool qa_cin_rgba(qa_bytes pixels, qa_bytes palette, void *rgba, size_t capacity, qa_error *error) {
    if ((pixels.size && (!pixels.data || !rgba)) || !palette.data || palette.size != 768 ||
        pixels.size > capacity / 4) return fail(error, "Invalid CIN palette or RGBA destination");
    uint8_t *out = rgba;
    for (size_t i = 0; i < pixels.size; ++i) {
        size_t color = (size_t)pixels.data[i] * 3;
        out[i * 4] = palette.data[color]; out[i * 4 + 1] = palette.data[color + 1];
        out[i * 4 + 2] = palette.data[color + 2]; out[i * 4 + 3] = 255;
    }
    return true;
}
