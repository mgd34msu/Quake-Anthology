#include "internal.h"
void qa_wad_free(qa_wad *wad) {
    if (wad) {
        free(wad->lumps);
        *wad = (qa_wad){0};
    }
}
void qa_wad_image_free(qa_wad_image *im) {
    if (im) {
        qa_image_free(&im->image);
        qa_mip_texture_free(&im->texture);
        qa_buffer_free(&im->palette_rgb);
        *im = (qa_wad_image){0};
    }
}
bool qa_wad_decode(qa_bytes b, qa_wad *out, qa_error *e) {
    if (!qa_img_input(b, 12, out, e))
        return false;
    if (memcmp(b.data, "WAD2", 4) && memcmp(b.data, "WAD3", 4))
        return qa_img_fail(e, QA_ERROR_FORMAT, 0, "Expected WAD2 or WAD3");
    int32_t count = qa_load_i32le(b.data + 4), offset = qa_load_i32le(b.data + 8);
    if (count < 0 || offset < 0 || (size_t)count > SIZE_MAX / 32 ||
        !qa_img_range(b, (size_t)offset, (size_t)count * 32) ||
        (size_t)count > SIZE_MAX / sizeof(qa_wad_lump))
        return qa_img_fail(e, QA_ERROR_FORMAT, 4, "Invalid WAD directory");
    qa_wad wad = {.wad3 = b.data[3] == '3', .count = (size_t)count};
    wad.lumps = count ? calloc((size_t)count, sizeof(*wad.lumps)) : NULL;
    if (count && !wad.lumps)
        return qa_img_fail(e, QA_ERROR_MEMORY, 0, "WAD directory allocation failed");
    for (size_t i = 0; i < wad.count; i++) {
        const uint8_t *p = b.data + (size_t)offset + i * 32;
        int32_t start = qa_load_i32le(p), disk = qa_load_i32le(p + 4), size = qa_load_i32le(p + 8);
        if (start < 0 || disk < 0 || size < 0 || !qa_img_range(b, (size_t)start, (size_t)disk)) {
            qa_wad_free(&wad);
            return qa_img_fail(e, QA_ERROR_FORMAT, (size_t)offset + i * 32,
                               "Invalid WAD lump extent");
        }
        qa_wad_lump *l = &wad.lumps[i];
        l->offset = (uint32_t)start;
        l->disk_size = (uint32_t)disk;
        l->decoded_size = (uint32_t)size;
        l->type = p[12];
        l->compression = p[13];
        l->bytes = (qa_bytes){b.data + start, (size_t)disk};
        for (unsigned j = 0; j < 16; j++) {
            unsigned c = p[16 + j];
            l->name[j] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
    }
    *out = wad;
    return true;
}
bool qa_wad_decode_image(const qa_wad *wad, size_t index, qa_wad_image *out, qa_error *e) {
    if (!wad || !out || index >= wad->count || !wad->lumps)
        return qa_img_fail(e, QA_ERROR_ARGUMENT, index, "Invalid WAD lump index");
    const qa_wad_lump *l = &wad->lumps[index];
    qa_wad_image im = {0};
    if (l->compression)
        return qa_img_fail(e, QA_ERROR_UNSUPPORTED, l->offset, "Compressed WAD lumps unsupported");
    if (l->disk_size != l->decoded_size)
        return qa_img_fail(e, QA_ERROR_FORMAT, l->offset, "Uncompressed WAD size mismatch");
    if (l->type == 66) {
        im.kind = QA_WAD_QPIC;
        if (!qa_image_decode_qpic(l->bytes, &im.image, e))
            return false;
    } else if (l->type == 68 || (wad->wad3 && l->type == 67)) {
        im.kind = QA_WAD_MIP;
        if (!qa_image_decode_mip(l->bytes, &im.texture, e))
            return false;
        if (wad->wad3 && !im.texture.external) {
            size_t last = qa_load_u32le(l->bytes.data + 36), n = im.texture.levels[3].indices.size;
            if (last > SIZE_MAX - n || !qa_img_range(l->bytes, last + n, 770) ||
                qa_load_u16le(l->bytes.data + last + n) != 256) {
                qa_wad_image_free(&im);
                return qa_img_fail(e, QA_ERROR_FORMAT, l->offset,
                                   "WAD3 miptex requires 256 palette colors");
            }
            if (!qa_img_copy((qa_bytes){l->bytes.data + last + n + 2, 768}, &im.palette_rgb, e)) {
                qa_wad_image_free(&im);
                return false;
            }
        }
    } else if (l->type == 64 && l->bytes.size == 768) {
        im.kind = QA_WAD_PALETTE;
        if (!qa_img_copy(l->bytes, &im.palette_rgb, e))
            return false;
    } else {
        im.kind = QA_WAD_RAW;
        im.raw = l->bytes;
    }
    *out = im;
    return true;
}
