#include "qa/q3_color.h"
#include "internal.h"

bool qa_q3_image_upload_options_valid(const qa_q3_image_upload_options *options, qa_error *error)
{
    if (!options || options->picmip < 0 || options->picmip > 16 ||
        options->maximum_texture_size > INT32_MAX)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source upload requires its actual texture limit and source picmip");
    qa_q3_color_mappings mappings;
    return qa_q3_color_mappings_create(&options->color, &mappings, error);
}

bool qa_q3_image_upload_options_equal(const qa_q3_image_upload_options *a,
    const qa_q3_image_upload_options *b)
{
    return a && b && a->color.device.hardware_gamma == b->color.device.hardware_gamma &&
        a->color.device.fullscreen == b->color.device.fullscreen &&
        a->color.device.color_bits == b->color.device.color_bits &&
        a->color.requested_overbright_bits == b->color.requested_overbright_bits &&
        a->color.gamma == b->color.gamma && a->color.intensity == b->color.intensity &&
        a->picmip == b->picmip && a->maximum_texture_size == b->maximum_texture_size &&
        a->round_down == b->round_down && a->simple_mips == b->simple_mips &&
        a->color_mips == b->color_mips && a->allow_picmip == b->allow_picmip && a->mipmap == b->mipmap;
}

static bool power_of_two(uint32_t value, bool down, uint32_t *out, qa_error *error)
{
    uint32_t result = 1;
    while (result < value) {
        if (result > (uint32_t)INT32_MAX / 2)
            return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source image power-of-two rounding exceeds signed int32");
        result *= 2;
    }
    *out = down && result > value ? result / 2 : result;
    return true;
}

static bool source_mip(const qa_image *in, bool simple, qa_image *out, qa_error *error)
{
    if (!simple) return qa_image_mip(in, QA_MIP_Q3_WEIGHTED, out, error);
    if (in->width != 1 && in->height != 1) return qa_image_mip(in, QA_MIP_BOX, out, error);
    qa_image next;
    if (!qa_img_new(in->width > 1 ? in->width / 2 : 1,
        in->height > 1 ? in->height / 2 : 1, &next, error)) return false;
    size_t pixels = (size_t)next.width * next.height;
    for (size_t i = 0; i < pixels; ++i)
        for (size_t c = 0; c < 4; ++c)
            next.rgba.data[i * 4 + c] = (uint8_t)(((unsigned)in->rgba.data[i * 8 + c] +
                in->rgba.data[i * 8 + 4 + c]) / 2);
    *out = next;
    return true;
}

static void tint_mip(qa_image *image, size_t level)
{
    unsigned selected = (unsigned)((level - 1) % 3);
    for (size_t i = 0; i < image->rgba.size; i += 4)
        for (unsigned c = 0; c < 3; ++c)
            image->rgba.data[i + c] = (uint8_t)(((unsigned)image->rgba.data[i + c] * 127 +
                (c == selected ? 255U * 128 : 0)) >> 9);
}

bool qa_q3_image_upload(const qa_image *source, const qa_q3_image_upload_options *options,
    qa_mip_chain *out, qa_error *error)
{
    if (!out) return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source upload requires an output");
    if (!qa_q3_image_upload_options_valid(options, error)) return false;
    if (!qa_img_rgba(source, error)) return false;
    if (source->width > INT32_MAX || source->height > INT32_MAX ||
        source->rgba.size > INT32_MAX)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source image allocation exceeds signed int32");
    qa_q3_color_mappings mappings;
    if (!qa_q3_color_mappings_create(&options->color, &mappings, error)) return false;
    uint32_t width, height;
    if (!power_of_two(source->width, options->round_down, &width, error) ||
        !power_of_two(source->height, options->round_down, &height, error)) return false;
    qa_image work = {0};
    if (width != source->width || height != source->height) {
        if (width > 2048 || source->width > (uint32_t)INT32_MAX / 65536 ||
            (uint64_t)width * height * 4 > INT32_MAX)
            return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source resample exceeds its defined width or allocation domain");
        if (!qa_image_resample(source, width, height, QA_RESAMPLE_Q2_Q3, &work, error)) return false;
    } else {
        if (!qa_img_new(width, height, &work, error)) return false;
        memcpy(work.rgba.data, source->rgba.data, source->rgba.size);
    }
    uint32_t target_width = width, target_height = height;
    if (options->allow_picmip) {
        target_width >>= options->picmip; target_height >>= options->picmip;
    }
    if (!target_width) target_width = 1;
    if (!target_height) target_height = 1;
    while (options->maximum_texture_size && (target_width > options->maximum_texture_size ||
        target_height > options->maximum_texture_size)) {
        target_width >>= 1; target_height >>= 1;
    }
    if (!target_width || !target_height) {
        qa_image_free(&work);
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source hardware texture reduction produced a zero dimension");
    }
    /* Upload32 takes this bypass after its power-of-two resample. */
    bool direct = !options->mipmap && target_width == width && target_height == height;
    bool ok = true;
    while (ok && (work.width > target_width || work.height > target_height)) {
        qa_image reduced = {0};
        ok = source_mip(&work, options->simple_mips, &reduced, error);
        if (ok) { qa_image_free(&work); work = reduced; }
    }
    if (ok && !direct) {
        qa_image corrected = {0};
        ok = qa_q3_color_image(&work, &mappings, options->mipmap, &corrected, error);
        if (ok) { qa_image_free(&work); work = corrected; }
    }
    size_t count = 1;
    if (options->mipmap)
        for (uint32_t w = work.width, h = work.height; w > 1 || h > 1; ++count) {
            w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
        }
    qa_mip_chain next = {0};
    if (ok && options->color_mips && count > 16)
        ok = qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source color mip level exceeds the authored 16-row table");
    if (ok) {
        next.levels = calloc(count, sizeof(*next.levels));
        if (!next.levels) ok = qa_img_fail(error, QA_ERROR_MEMORY, 0, "Allocating Source upload levels");
    }
    if (ok) { next.levels[0] = work; work = (qa_image){0}; next.count = 1; }
    for (size_t i = 1; ok && i < count; ++i) {
        ok = source_mip(&next.levels[i - 1], options->simple_mips, &next.levels[i], error);
        if (ok) {
            ++next.count;
            if (options->color_mips) tint_mip(&next.levels[i], i);
        }
    }
    qa_image_free(&work);
    if (!ok) { qa_mip_chain_free(&next); return false; }
    *out = next;
    return true;
}
