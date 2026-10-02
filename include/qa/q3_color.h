#ifndef QA_Q3_COLOR_H
#define QA_Q3_COLOR_H

#include "qa/image.h"

/* These are the selected physical renderer's observed values. Gamma support
 * means the native gamma API accepted the retained display's original ramp. */
typedef struct qa_q3_color_device {
    bool hardware_gamma, fullscreen;
    int32_t color_bits;
} qa_q3_color_device;
typedef struct qa_q3_color_inputs {
    qa_q3_color_device device;
    int32_t requested_overbright_bits;
    float gamma, intensity;
} qa_q3_color_inputs;
typedef struct qa_q3_color_lighting {
    uint32_t overbright_bits;
    float identity_light;
    uint8_t identity_light_byte;
} qa_q3_color_lighting;
typedef struct qa_q3_color_mappings {
    qa_q3_color_inputs inputs;
    qa_q3_color_lighting lighting;
    uint8_t gamma[256], intensity[256];
} qa_q3_color_mappings;

/* Scalar publication precedes the original renderer's cvar clamp callbacks.
 * Table construction requires those callbacks to have clamped the inputs. */
bool qa_q3_color_lighting_read(const qa_q3_color_device *, int32_t requested_bits,
    qa_q3_color_lighting *, qa_error *);
bool qa_q3_color_mappings_create(const qa_q3_color_inputs *, qa_q3_color_mappings *, qa_error *);
bool qa_q3_color_map_shift(int32_t requested_map_bits, const qa_q3_color_lighting *,
    uint32_t *, qa_error *);
/* R_LightScaleTexture's normal upload tail. Same-size, nonmipmapped Upload32
 * bypasses this operation. Alpha is preserved; generated mips follow it. */
bool qa_q3_color_image(const qa_image *, const qa_q3_color_mappings *, bool mipmap,
    qa_image *, qa_error *);

typedef enum qa_q3_texture_format {
    QA_Q3_TEXTURE_RGB, QA_Q3_TEXTURE_RGBA, QA_Q3_TEXTURE_RGB5,
    QA_Q3_TEXTURE_RGBA4, QA_Q3_TEXTURE_RGB8, QA_Q3_TEXTURE_RGBA8,
    QA_Q3_TEXTURE_RGB4_S3TC
} qa_q3_texture_format;
typedef struct qa_q3_image_upload_options {
    qa_q3_color_inputs color;
    int32_t picmip;
    uint32_t maximum_texture_size; /* Zero: this actual backend has no texture-size limit. */
    bool round_down, simple_mips, color_mips, allow_picmip, mipmap;
    int32_t texture_bits;
    bool s3tc, lightmap;
} qa_q3_image_upload_options;
bool qa_q3_image_upload_options_valid(const qa_q3_image_upload_options *, qa_error *);
bool qa_q3_image_upload_options_equal(const qa_q3_image_upload_options *, const qa_q3_image_upload_options *);
struct qa_source_save_io;
bool qa_q3_image_upload_options_codec(struct qa_source_save_io *, qa_q3_image_upload_options *);
/* Extended callers gate this complete layout with their own saved version.
 * The historical codec above keeps its exact original byte layout. */
bool qa_q3_image_upload_options_precision_codec(struct qa_source_save_io *, qa_q3_image_upload_options *);
/* Complete owned upload levels, including the first level. Uses the original
 * power-of-two resample, reduction, light-scale and descendant-mip order. */
bool qa_q3_image_upload(const qa_image *, const qa_q3_image_upload_options *,
    qa_mip_chain *, qa_error *);
/* Select the native storage format after POT resampling, before reduction.
 * Every returned level uses that format; native storage converts its bytes. */
bool qa_q3_image_upload_format(const qa_image *, const qa_q3_image_upload_options *,
    qa_mip_chain *, qa_q3_texture_format *, qa_error *);

#endif
