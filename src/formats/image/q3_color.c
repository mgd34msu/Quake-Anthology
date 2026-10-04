#include "qa/q3_color.h"
#include "internal.h"
#include <fenv.h>
#include <math.h>
#include "qa/source_save.h"

bool qa_q3_image_upload_options_codec(qa_source_save_io *io, qa_q3_image_upload_options *upload)
{
    if (io->direction==QA_SOURCE_SAVE_READ) {
        upload->texture_bits=0; upload->s3tc=false; upload->lightmap=false;
    }
    qa_q3_color_inputs *color=&upload->color;
    return qa_source_save_bool(io,&color->device.hardware_gamma) &&
        qa_source_save_bool(io,&color->device.fullscreen) && qa_source_save_i32(io,&color->device.color_bits) &&
        qa_source_save_i32(io,&color->requested_overbright_bits) && qa_source_save_f32(io,&color->gamma) &&
        qa_source_save_f32(io,&color->intensity) && qa_source_save_i32(io,&upload->picmip) &&
        qa_source_save_u32(io,&upload->maximum_texture_size) && qa_source_save_bool(io,&upload->round_down) &&
        qa_source_save_bool(io,&upload->simple_mips) && qa_source_save_bool(io,&upload->color_mips) &&
        qa_source_save_bool(io,&upload->allow_picmip) && qa_source_save_bool(io,&upload->mipmap) &&
        qa_q3_image_upload_options_valid(upload,io->error);
}

bool qa_q3_image_upload_options_precision_codec(qa_source_save_io *io,qa_q3_image_upload_options *upload)
{
    return qa_q3_image_upload_options_codec(io,upload) &&
        qa_source_save_i32(io,&upload->texture_bits) && qa_source_save_bool(io,&upload->s3tc) &&
        qa_source_save_bool(io,&upload->lightmap) && qa_q3_image_upload_options_valid(upload,io->error);
}

bool qa_q3_color_lighting_read(const qa_q3_color_device *device, int32_t requested,
    qa_q3_color_lighting *out, qa_error *error)
{
    if (!device || !out || device->color_bits < 0)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source color requires its actual framebuffer capabilities");
    int32_t bits = device->hardware_gamma && device->fullscreen ? requested : 0;
    int32_t maximum = device->color_bits > 16 ? 2 : 1;
    if (bits > maximum) bits = maximum;
    if (bits < 0) bits = 0;
    float identity = 1.0f / (float)(1U << bits);
    *out = (qa_q3_color_lighting){(uint32_t)bits, identity, (uint8_t)(255.0f * identity)};
    return true;
}

static bool color_mappings_evaluate(const qa_q3_color_inputs *inputs,
    qa_q3_color_mappings *out, qa_error *error)
{
    if (!inputs || !isfinite(inputs->gamma) || inputs->gamma < .5f || inputs->gamma > 3 ||
        !isfinite(inputs->intensity) || inputs->intensity < 1)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source color tables require source-clamped gamma and intensity");
    qa_q3_color_mappings next = {.inputs = *inputs};
    if (!qa_q3_color_lighting_read(&inputs->device, inputs->requested_overbright_bits,
        &next.lighting, error)) return false;
    bool tables = out != NULL || (fetestexcept(FE_INEXACT) & FE_INEXACT) == 0;
    for (unsigned i = 0; i < 256; ++i) {
        if (tables) {
            float sample = (float)i / 255.0f, exponent = 1.0f / inputs->gamma;
            double corrected = inputs->gamma == 1 ? i : floor(255.0 * pow(sample, exponent) + .5);
            unsigned shifted = (unsigned)corrected << next.lighting.overbright_bits;
            next.gamma[i] = (uint8_t)(shifted > 255 ? 255 : shifted);
        }
        float scaled = (float)i * inputs->intensity;
        if (!isfinite(scaled) || (double)scaled >= 2147483648.0)
            return qa_img_fail(error, QA_ERROR_ARGUMENT, i, "Source intensity conversion exceeds signed int32");
        int32_t intensity = (int32_t)scaled;
        next.intensity[i] = (uint8_t)(intensity > 255 ? 255 : intensity);
    }
    if (out) *out = next;
    return true;
}
bool qa_img_q3_color_valid(const qa_q3_color_inputs *inputs, qa_error *error)
{
    return color_mappings_evaluate(inputs, NULL, error);
}
bool qa_q3_color_mappings_create(const qa_q3_color_inputs *inputs,
    qa_q3_color_mappings *out, qa_error *error)
{
    if (!out)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source color tables require source-clamped gamma and intensity");
    return color_mappings_evaluate(inputs, out, error);
}

bool qa_q3_color_map_shift(int32_t requested, const qa_q3_color_lighting *lighting,
    uint32_t *out, qa_error *error)
{
    if (!lighting || !out || lighting->overbright_bits > 2)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source map color requires actual renderer overbright");
    int64_t shift = (int64_t)requested - lighting->overbright_bits;
    if (shift < 0 || shift > 15)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source map overbright shift is outside its defined 0..15 domain");
    *out = (uint32_t)shift;
    return true;
}

bool qa_q3_color_image(const qa_image *source, const qa_q3_color_mappings *mappings,
    bool mipmap, qa_image *out, qa_error *error)
{
    if (!mappings || !out)
        return qa_img_fail(error, QA_ERROR_ARGUMENT, 0, "Source upload requires its retained color mappings");
    if (!qa_img_rgba(source, error)) return false;
    qa_image next;
    if (!qa_img_new(source->width, source->height, &next, error)) return false;
    for (size_t i = 0; i < source->rgba.size; ++i) {
        uint8_t value = source->rgba.data[i];
        if (i % 4 != 3) {
            if (mipmap) value = mappings->intensity[value];
            if (!mappings->inputs.device.hardware_gamma) value = mappings->gamma[value];
        }
        next.rgba.data[i] = value;
    }
    *out = next;
    return true;
}
