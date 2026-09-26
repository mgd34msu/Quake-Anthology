#include "internal.h"
#include <gif_lib.h>

typedef struct gif_input {
    qa_bytes bytes;
    size_t offset;
} gif_input;
static int gif_read_memory(GifFileType *file, GifByteType *data, int count) {
    gif_input *in = file->UserData;
    if (count < 0)
        return 0;
    size_t n = (size_t)count;
    if (n > in->bytes.size - in->offset)
        n = in->bytes.size - in->offset;
    if (n)
        memcpy(data, in->bytes.data + in->offset, n);
    in->offset += n;
    return (int)n;
}
static bool gif_palette(const ColorMapObject *map, qa_buffer *out, qa_error *e) {
    if (!map || map->ColorCount < 1 || map->ColorCount > 256)
        return qa_img_fail(e, QA_ERROR_FORMAT, 0, "Invalid GIF palette");
    if (!qa_img_alloc(out, (size_t)map->ColorCount * 3, e))
        return false;
    for (int i = 0; i < map->ColorCount; i++) {
        out->data[(size_t)i * 3] = map->Colors[i].Red;
        out->data[(size_t)i * 3 + 1] = map->Colors[i].Green;
        out->data[(size_t)i * 3 + 2] = map->Colors[i].Blue;
    }
    return true;
}
static void gif_loop_count(ExtensionBlock *blocks, int count, int *loop) {
    for (int i = 0; i + 1 < count; i++) {
        ExtensionBlock *b = &blocks[i];
        if (b->Function == APPLICATION_EXT_FUNC_CODE && b->ByteCount == 11 &&
            (!memcmp(b->Bytes, "NETSCAPE2.0", 11) || !memcmp(b->Bytes, "ANIMEXTS1.0", 11))) {
            uint8_t payload[3];
            size_t written = 0;
            for (int j = i + 1; j < count && written < sizeof(payload); j++) {
                const ExtensionBlock *part = &blocks[j];
                if (part->Function != CONTINUE_EXT_FUNC_CODE)
                    break;
                for (int k = 0; k < part->ByteCount && written < sizeof(payload); k++)
                    payload[written++] = part->Bytes[k];
            }
            if (written == sizeof(payload) && payload[0] == 1)
                *loop = qa_load_u16le(payload + 1);
        }
    }
}
static bool gif_control(ExtensionBlock *blocks, int count, qa_gif_frame *frame, qa_error *e) {
    for (int i = 0; i < count; i++) {
        const ExtensionBlock *block = &blocks[i];
        if (block->Function != GRAPHICS_EXT_FUNC_CODE)
            continue;
        if (block->ByteCount != 4 ||
            (i + 1 < count && blocks[i + 1].Function == CONTINUE_EXT_FUNC_CODE))
            return qa_img_fail(e, QA_ERROR_FORMAT, 0, "Invalid GIF graphic control extension");
        if (frame) {
            frame->transparent_index = block->Bytes[0] & 1 ? block->Bytes[3] : -1;
            frame->delay_centiseconds = qa_load_u16le(block->Bytes + 1);
            frame->disposal = (block->Bytes[0] >> 2) & 7;
        }
    }
    return true;
}
void qa_gif_free(qa_gif *gif) {
    if (!gif)
        return;
    for (size_t i = 0; i < gif->frame_count; i++) {
        qa_image_free(&gif->frames[i].image);
        qa_buffer_free(&gif->frames[i].indices);
        qa_buffer_free(&gif->frames[i].palette_rgb);
    }
    free(gif->frames);
    qa_buffer_free(&gif->global_palette_rgb);
    *gif = (qa_gif){0};
}
bool qa_image_decode_gif(qa_bytes b, qa_gif *out, qa_error *e) {
    if (!qa_img_input(b, 13, out, e))
        return false;
    if (memcmp(b.data, "GIF87a", 6) && memcmp(b.data, "GIF89a", 6))
        return qa_img_fail(e, QA_ERROR_FORMAT, 0, "Invalid GIF signature");
    gif_input input = {b, 0};
    int code = 0;
    GifFileType *file = DGifOpen(&input, gif_read_memory, &code);
    if (!file) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "GIF: %s",
                     GifErrorString(code) ? GifErrorString(code) : "open failed");
        return false;
    }
    qa_gif result = {.width = (uint32_t)file->SWidth,
                     .height = (uint32_t)file->SHeight,
                     .background_index = (unsigned)file->SBackGroundColor,
                     .loop_count = -1};
    qa_buffer canvas = {0}, previous = {0};
    size_t size;
    if (!qa_img_size(result.width, result.height, 4, &size, e))
        goto fail;
    if (DGifSlurp(file) != GIF_OK) {
        qa_error_set(e, QA_ERROR_FORMAT, input.offset, "GIF: %s",
                     GifErrorString(file->Error) ? GifErrorString(file->Error) : "decode failed");
        goto fail;
    }
    if (file->ImageCount <= 0 || (size_t)file->ImageCount > SIZE_MAX / sizeof(*result.frames)) {
        qa_img_fail(e, QA_ERROR_FORMAT, input.offset,
                    "GIF has no frames or overflowing frame count");
        goto fail;
    }
    if (file->SColorMap && !gif_palette(file->SColorMap, &result.global_palette_rgb, e))
        goto fail;
    if (!qa_img_alloc(&canvas, size, e))
        goto fail;
    memset(canvas.data, 0, size);
    result.frames = calloc((size_t)file->ImageCount, sizeof(*result.frames));
    if (!result.frames) {
        qa_img_fail(e, QA_ERROR_MEMORY, 0, "GIF frame allocation failed");
        goto fail;
    }
    result.frame_count = (size_t)file->ImageCount;
    for (size_t i = 0; i < result.frame_count; i++) {
        SavedImage *s = &file->SavedImages[i];
        GifImageDesc *d = &s->ImageDesc;
        qa_gif_frame *f = &result.frames[i];
        if (d->Left < 0 || d->Top < 0 || d->Width <= 0 || d->Height <= 0 ||
            (uint64_t)d->Left + (unsigned)d->Width > result.width ||
            (uint64_t)d->Top + (unsigned)d->Height > result.height) {
            qa_img_fail(e, QA_ERROR_FORMAT, input.offset, "GIF frame exceeds logical screen");
            goto fail;
        }
        f->x = (uint32_t)d->Left;
        f->y = (uint32_t)d->Top;
        f->width = (uint32_t)d->Width;
        f->height = (uint32_t)d->Height;
        f->transparent_index = -1;
        if (!gif_control(s->ExtensionBlocks, s->ExtensionBlockCount, f, e))
            goto fail;
        gif_loop_count(s->ExtensionBlocks, s->ExtensionBlockCount, &result.loop_count);
        const ColorMapObject *map = d->ColorMap ? d->ColorMap : file->SColorMap;
        if (!gif_palette(map, &f->palette_rgb, e) ||
            !qa_img_copy((qa_bytes){s->RasterBits, (size_t)f->width * f->height}, &f->indices, e))
            goto fail;
        if (f->disposal == 3) {
            if (!previous.data && !qa_img_alloc(&previous, size, e))
                goto fail;
            memcpy(previous.data, canvas.data, size);
        }
        for (uint32_t y = 0; y < f->height; y++)
            for (uint32_t x = 0; x < f->width; x++) {
                unsigned index = f->indices.data[(size_t)y * f->width + x];
                if (index >= (unsigned)map->ColorCount) {
                    qa_img_fail(e, QA_ERROR_FORMAT, input.offset, "GIF palette index out of range");
                    goto fail;
                }
                if ((int)index == f->transparent_index)
                    continue;
                uint8_t *pixel = canvas.data + ((size_t)(f->y + y) * result.width + f->x + x) * 4;
                memcpy(pixel, f->palette_rgb.data + index * 3, 3);
                pixel[3] = 255;
            }
        f->image.width = result.width;
        f->image.height = result.height;
        f->image.srgb_intent = -1;
        if (!qa_img_copy((qa_bytes){canvas.data, size}, &f->image.rgba, e))
            goto fail;
        /* Anthology's rerelease composition clears disposal 2 to transparency. */
        if (f->disposal == 2)
            for (uint32_t y = 0; y < f->height; y++)
                memset(canvas.data + ((size_t)(f->y + y) * result.width + f->x) * 4, 0,
                       (size_t)f->width * 4);
        else if (f->disposal == 3)
            memcpy(canvas.data, previous.data, size);
    }
    if (!gif_control(file->ExtensionBlocks, file->ExtensionBlockCount, NULL, e))
        goto fail;
    gif_loop_count(file->ExtensionBlocks, file->ExtensionBlockCount, &result.loop_count);
    DGifCloseFile(file, &code);
    qa_buffer_free(&canvas);
    qa_buffer_free(&previous);
    *out = result;
    return true;
fail:
    DGifCloseFile(file, &code);
    qa_buffer_free(&canvas);
    qa_buffer_free(&previous);
    qa_gif_free(&result);
    return false;
}
