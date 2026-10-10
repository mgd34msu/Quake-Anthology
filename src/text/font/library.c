#include "internal.h"
#include "qa/font_save.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct qa_font_library_capture { qa_font_library *library; };
bool qa_font_library_idle(const qa_font_library *library)
{ return library && !library->capture && !library->policy && !library->codec_active && !library->callbacks; }
bool qa_font_internal_admission_ready(const qa_font_library *library, qa_error *error)
{
    return qa_font_library_idle(library) || qa_font_fail(error,QA_ERROR_ARGUMENT,0,"Font owner callback or continuation capture is active");
}
bool qa_font_library_capture_begin(const qa_font_library *library, qa_font_library_capture **out, qa_error *error)
{
    if (!out || *out || !qa_font_library_idle(library))
        return qa_font_fail(error,QA_ERROR_ARGUMENT,0,"Font capture requires an idle real library and empty token");
    qa_font_library_capture *capture=malloc(sizeof(*capture));
    if (!capture) return qa_font_fail(error,QA_ERROR_MEMORY,0,"Retaining the font library capture lease");
    capture->library=(qa_font_library *)library; capture->library->capture=capture; *out=capture; return true;
}
void qa_font_library_capture_end(qa_font_library_capture *capture)
{
    if (!capture) return;
    if (capture->library->capture==capture) capture->library->capture=NULL;
    free(capture);
}

bool qa_font_fail(qa_error *error, qa_status status, size_t offset, const char *message) {
    qa_error_set(error, status, offset, "%s", message);
    return false;
}

char *qa_font_copy_string(const char *source, qa_error *error) {
    if (!source) {
        qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing font string");
        return NULL;
    }
    size_t length = strlen(source);
    if (length == SIZE_MAX) {
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Font string size overflow");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating font string");
        return NULL;
    }
    memcpy(copy, source, length + 1);
    return copy;
}

bool qa_font_valid_scalar(uint32_t codepoint) {
    return codepoint <= 0x10ffffu && (codepoint < 0xd800u || codepoint > 0xdfffu);
}

static bool reserve(void **items, size_t *capacity, size_t count, size_t item_size,
                    qa_error *error) {
    if (count <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 8;
    while (next < count) {
        if (next > SIZE_MAX / 2)
            return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Font collection size overflow");
        next *= 2;
    }
    if (next > SIZE_MAX / item_size)
        return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Font allocation size overflow");
    void *grown = realloc(*items, next * item_size);
    if (!grown)
        return qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating font collection");
    *items = grown;
    *capacity = next;
    return true;
}

qa_font_library *qa_font_library_create(qa_vfs *vfs, qa_scene_resources *resources,
                                        qa_error *error) {
    if (!vfs || !resources) {
        qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing font VFS or scene resources");
        return NULL;
    }
    qa_font_library *library = calloc(1, sizeof(*library));
    if (!library) {
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating font library");
        return NULL;
    }
    library->vfs = vfs;
    library->resources = resources;
    return library;
}

const qa_vfs *qa_font_library_content(const qa_font_library *library) {
    return library ? library->vfs : NULL;
}

void qa_font_internal_destroy(qa_font *font) {
    if (!font)
        return;
    for (size_t i = 0; i < font->image_count; ++i)
        qa_scene_image_release(font->images[i]);
    for (size_t i = 0; i < font->source_count; ++i)
        qa_resource_release(font->sources[i]);
    free(font->images);
    free(font->sources);
    free(font->glyphs);
    free(font->truetype_coverage);
    free(font->name);
    free(font);
}

void qa_font_library_destroy(qa_font_library *library) {
    if (!qa_font_library_idle(library))
        return;
    for (size_t i = 0; i < library->font_count; ++i)
        qa_font_internal_destroy(library->fonts[i]);
    if (library->freetype)
        FT_Done_FreeType(library->freetype);
    free(library->fonts);
    free(library);
}

qa_font *qa_font_internal_create(qa_font_library *library, qa_font_kind kind, const char *name,
                                 qa_error *error) {
    if (!library || !name) {
        qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid font construction");
        return NULL;
    }
    if (!qa_font_internal_admission_ready(library,error)) return NULL;
    qa_font *font = calloc(1, sizeof(*font));
    if (!font) {
        qa_font_fail(error, QA_ERROR_MEMORY, 0, "Allocating font");
        return NULL;
    }
    font->name = qa_font_copy_string(name, error);
    if (!font->name) {
        free(font);
        return NULL;
    }
    font->library = library;
    font->kind = kind;
    return font;
}

bool qa_font_internal_add_glyph(qa_font *font, qa_font_glyph glyph, qa_error *error) {
    if (!font || !qa_font_valid_scalar(glyph.codepoint) || !isfinite(glyph.width) ||
        !isfinite(glyph.height) || !isfinite(glyph.advance) || !isfinite(glyph.bearing_x) ||
        !isfinite(glyph.bearing_y) || glyph.width < 0 || glyph.height < 0) {
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid font glyph");
    }
    glyph.font = font;
    if (!reserve((void **)&font->glyphs, &font->glyph_capacity, font->glyph_count + 1,
                 sizeof(*font->glyphs), error))
        return false;
    font->glyphs[font->glyph_count++] = glyph;
    return true;
}

bool qa_font_internal_take_image(qa_font *font, const qa_scene_image *image, qa_error *error) {
    if (!font || !image)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing font atlas image");
    for (size_t i = 0; i < font->image_count; ++i) {
        if (font->images[i] == image) {
            qa_scene_image_release(image);
            return true;
        }
    }
    if (!reserve((void **)&font->images, &font->image_capacity, font->image_count + 1,
                 sizeof(*font->images), error)) {
        qa_scene_image_release(image);
        return false;
    }
    font->images[font->image_count++] = image;
    return true;
}

bool qa_font_internal_take_source(qa_font *font, qa_resource *resource, qa_error *error) {
    if (!font || !resource)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Missing retained font source");
    for (size_t i = 0; i < font->source_count; ++i) {
        if (font->sources[i] == resource) {
            qa_resource_release(resource);
            return true;
        }
    }
    if (!reserve((void **)&font->sources, &font->source_capacity, font->source_count + 1,
                 sizeof(*font->sources), error)) {
        qa_resource_release(resource);
        return false;
    }
    font->sources[font->source_count++] = resource;
    return true;
}

static int compare_glyph(const void *left, const void *right) {
    const qa_font_glyph *a = left, *b = right;
    return a->codepoint < b->codepoint ? -1 : a->codepoint > b->codepoint;
}

bool qa_font_internal_publish(qa_font *font, const qa_font **out, qa_error *error) {
    if (!font || !out || !(font->line_height > 0) || !isfinite(font->line_height) ||
        !isfinite(font->ascent) || !isfinite(font->descent))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Incomplete font publication");
    if (!qa_font_internal_admission_ready(font->library,error)) return false;
    qsort(font->glyphs, font->glyph_count, sizeof(*font->glyphs), compare_glyph);
    qa_font_library *library = font->library;
    if (!reserve((void **)&library->fonts, &library->font_capacity, library->font_count + 1,
                 sizeof(*library->fonts), error))
        return false;
    library->fonts[library->font_count++] = font;
    *out = font;
    return true;
}

void qa_font_internal_measure_cap_ink(qa_font *font) {
    static const uint32_t samples[] = {'H', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (!font || !(font->line_height > 0))
        return;
    uint32_t top = UINT32_MAX, bottom = 0;
    bool found = false;
    for (size_t sample = 0; sample < sizeof(samples) / sizeof(samples[0]); ++sample) {
        const qa_font_glyph *glyph = NULL;
        for (size_t i = 0; i < font->glyph_count; ++i) {
            if (font->glyphs[i].codepoint == samples[sample]) {
                glyph = &font->glyphs[i];
                break;
            }
        }
        if (!glyph || !glyph->image || !glyph->image->level_count ||
            !glyph->image->levels[0].pixels ||
            (glyph->image->kind != QA_SCENE_RGBA8 && glyph->image->kind != QA_SCENE_RGB8))
            continue;
        const qa_scene_image_level *level = &glyph->image->levels[0];
        double source_x = (double)glyph->uv.x * level->width;
        double source_y = (double)glyph->uv.y * level->height;
        double source_width = glyph->width;
        double source_height = glyph->height;
        if (!isfinite(source_x) || !isfinite(source_y) || !isfinite(source_width) ||
            !isfinite(source_height) || source_x < 0 || source_y < 0 || source_width < 0 ||
            source_height < 0 || source_x > UINT32_MAX || source_y > UINT32_MAX ||
            source_width > UINT32_MAX || source_height > UINT32_MAX)
            continue;
        uint32_t x0 = (uint32_t)llround(source_x);
        uint32_t y0 = (uint32_t)llround(source_y);
        uint32_t width = (uint32_t)llround(source_width);
        uint32_t height = (uint32_t)llround(source_height);
        if (x0 > level->width || y0 > level->height || width > level->width - x0 ||
            height > level->height - y0 ||
            (level->height && level->width > SIZE_MAX / level->height / 4u) ||
            level->bytes < (size_t)level->width * level->height * 4u)
            continue;
        const uint8_t *pixels = level->pixels;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                size_t offset = ((size_t)(y0 + y) * level->width + x0 + x) * 4u;
                if (pixels[offset + 3] <= 127)
                    continue;
                if (y < top)
                    top = y;
                if (y > bottom)
                    bottom = y;
                found = true;
            }
        }
    }
    if (found) {
        font->has_cap_ink = true;
        font->cap_top = (float)top * 8.0f / font->line_height;
        font->cap_height = (float)(bottom - top + 1u) * 8.0f / font->line_height;
    }
}

bool qa_font_internal_freetype(qa_font_library *library, FT_Library *out, qa_error *error) {
    if (!library || !out)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid FreeType request");
    if (!qa_font_internal_admission_ready(library,error)) return false;
    if (!library->freetype && FT_Init_FreeType(&library->freetype))
        return qa_font_fail(error, QA_ERROR_UNSUPPORTED, 0, "FreeType initialization failed");
    *out = library->freetype;
    return true;
}

bool qa_font_internal_picture(qa_font *font, const char *path, qa_game_family family,
                              qa_scene_filter filter, const qa_scene_image **out, qa_error *error) {
    if (!font || !path || !out)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid font picture request");
    qa_scene_image_options options = {0};
    options.family = family;
    options.wrap = QA_SCENE_CLAMP;
    options.filter = filter;
    options.usage = QA_IMAGE_USAGE_PICTURE;
    options.transparent = true;
    options.transparent_index = 255;
    qa_scene_image *image = NULL;
    if (!qa_scene_image_load(font->library->resources, path, &options, &image, error))
        return false;
    if (!qa_font_internal_take_image(font, image, error))
        return false;
    *out = image;
    return true;
}

bool qa_font_classic_create(qa_font_library *library, const char *name, const qa_scene_image *image,
                            qa_font_color_policy policy, const qa_font **out, qa_error *error) {
    if (!library || !name || !image || !out || image->logical_width == 0 ||
        image->logical_height == 0 || image->logical_width % 16 || image->logical_height % 16 ||
        (policy != QA_FONT_TINTED && policy != QA_FONT_BAKED_COLOR))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                            "Classic charset must contain 16 by 16 cells");
    qa_font *font = qa_font_internal_create(library, QA_FONT_CLASSIC, name, error);
    if (!font)
        return false;
    qa_scene_image_retain(image);
    if (!qa_font_internal_take_image(font, image, error)) {
        qa_font_internal_destroy(font);
        return false;
    }
    float width = (float)image->logical_width / 16.0f;
    float height = (float)image->logical_height / 16.0f;
    font->line_height = font->ascent = height;
    font->descent = 0;
    for (uint32_t code = 0; code < 256; ++code) {
        float x = (float)(code & 15u) * width;
        float y = (float)(code >> 4) * height;
        qa_font_glyph glyph = {
            .codepoint = code,
            .image = image,
            .uv = {x / (float)image->logical_width, y / (float)image->logical_height,
                   (x + width) / (float)image->logical_width, (y + height) / (float)image->logical_height},
            .width = width,
            .height = height,
            .advance = width,
            .bearing_y = height,
            .visible = (code & 127u) != 32u,
            .baked_color = policy == QA_FONT_BAKED_COLOR,
        };
        if (!qa_font_internal_add_glyph(font, glyph, error)) {
            qa_font_internal_destroy(font);
            return false;
        }
    }
    if (!qa_font_internal_publish(font, out, error)) {
        qa_font_internal_destroy(font);
        return false;
    }
    return true;
}

bool qa_font_describe(const qa_font *font, qa_font_info *out) {
    if (!font || !out)
        return false;
    *out = (qa_font_info){
        .kind = font->kind,
        .name = font->name,
        .line_height = font->line_height,
        .ascent = font->ascent,
        .descent = font->descent,
        .cap_top = font->cap_top,
        .cap_height = font->cap_height,
        .has_cap_ink = font->has_cap_ink,
        .glyph_count = font->glyph_count,
    };
    return true;
}

bool qa_font_atlas_create(qa_font_library *library, const char *name, const qa_scene_image *image,
    const qa_font_glyph *glyphs, size_t count, float height, const qa_font **out, qa_error *error)
{
    if (!library || !name || !image || !glyphs || !count || !out || !isfinite(height) || height <= 0)
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid authored font atlas");
    qa_font *font = qa_font_internal_create(library, QA_FONT_ATLAS, name, error);
    if (!font) return false;
    qa_scene_image_retain(image);
    bool ok = qa_font_internal_take_image(font, image, error);
    font->line_height = font->ascent = height;
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_font_glyph *glyph = glyphs + i;
        ok = glyph->image == image && glyph->uv.x >= 0 && glyph->uv.y >= 0 && glyph->uv.z <= 1 && glyph->uv.w <= 1 &&
            glyph->uv.x <= glyph->uv.z && glyph->uv.y <= glyph->uv.w;
        for (size_t j = 0; ok && j < i; ++j) ok = glyphs[j].codepoint != glyph->codepoint;
        if (ok) ok = qa_font_internal_add_glyph(font, *glyph, error);
    }
    if (ok) { qa_font_internal_measure_cap_ink(font); ok = qa_font_internal_publish(font, out, error); }
    if (!ok) { qa_font_internal_destroy(font); if (error && error->code == QA_OK) qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid authored atlas metrics"); }
    return ok;
}

bool qa_font_find_glyph(const qa_font *font, uint32_t codepoint, qa_font_glyph *out) {
    if (!font || !out)
        return false;
    size_t low = 0, high = font->glyph_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        uint32_t found = font->glyphs[middle].codepoint;
        if (found < codepoint)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == font->glyph_count || font->glyphs[low].codepoint != codepoint)
        return false;
    *out = font->glyphs[low];
    return true;
}

bool qa_font_selection_init(qa_font_selection *selection, uint32_t seat, const qa_font *classic,
                            const qa_font *primary, const qa_font *const *fallbacks,
                            size_t fallback_count, qa_error *error) {
    if (!selection || !classic || classic->kind != QA_FONT_CLASSIC ||
        (fallback_count && !fallbacks))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0, "Invalid font selection");
    qa_font_library *library = classic->library;
    if ((primary && primary->library != library))
        return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                            "Font selection crosses library ownership");
    qa_font_selection result = {0};
    result.seat = seat;
    result.classic = classic;
    result.primary = primary;
    for (size_t i = 0; i < fallback_count; ++i) {
        if (!fallbacks[i] || fallbacks[i]->library != library)
            return qa_font_fail(error, QA_ERROR_ARGUMENT, 0,
                                "Font fallback crosses library ownership");
        ++result.fallback_count;
    }
    result.fallbacks = fallbacks;
    *selection = result;
    return true;
}

static bool usable_glyph(const qa_font_glyph *glyph) {
    if (!glyph || !glyph->font)
        return false;
    if (glyph->font->kind == QA_FONT_TRUETYPE || glyph->font->kind == QA_FONT_CLASSIC)
        return true;
    return glyph->width > 0 || glyph->advance != 0;
}

bool qa_font_resolve(const qa_font_selection *selection, uint32_t codepoint, bool alternate,
                     qa_font_glyph *out) {
    if (!selection || !selection->classic || !out || !qa_font_valid_scalar(codepoint) ||
        (selection->fallback_count && !selection->fallbacks))
        return false;
    if (!selection->primary && codepoint <= 255u) {
        uint32_t classic = codepoint | (alternate ? 128u : 0u);
        if (qa_font_find_glyph(selection->classic, classic & 255u, out)) {
            out->visible = (codepoint & 127u) != 32u;
            return true;
        }
    }
    if (selection->primary && qa_font_find_glyph(selection->primary, codepoint, out) &&
        usable_glyph(out))
        return true;
    for (size_t i = 0; i < selection->fallback_count; ++i) {
        if (qa_font_find_glyph(selection->fallbacks[i], codepoint, out) && usable_glyph(out))
            return true;
    }
    if (codepoint <= 255u) {
        uint32_t classic = codepoint | (alternate ? 128u : 0u);
        if (qa_font_find_glyph(selection->classic, classic & 255u, out)) {
            out->visible = (codepoint & 127u) != 32u;
            return true;
        }
    }
    if (selection->primary && qa_font_find_glyph(selection->primary, '?', out) && usable_glyph(out))
        return true;
    for (size_t i = 0; i < selection->fallback_count; ++i) {
        if (qa_font_find_glyph(selection->fallbacks[i], '?', out) && usable_glyph(out))
            return true;
    }
    return qa_font_find_glyph(selection->classic, '?', out);
}

struct qa_font_resource_policy {
    qa_font_library *source,*destination;
    qa_scene_resource_policy *images;
    qa_font **original,*original_state,*staged_state,**published;
    size_t original_count,staged_count,published_capacity;
    bool ready,published_state;
};
static bool policy_current(const qa_font_resource_policy *owner)
{
    if (!owner || !owner->source || owner->source->policy!=owner || !owner->destination ||
        owner->source->capture || owner->source->codec_active || owner->source->callbacks ||
        owner->destination->capture || owner->destination->codec_active || owner->destination->callbacks)
        return false;
    if (owner->published_state) return true;
    if (owner->source->font_count!=owner->original_count ||
        qa_scene_resource_policy_source(owner->images)!=owner->source->resources ||
        qa_scene_resource_policy_destination(owner->images)!=owner->destination->resources) return false;
    for (size_t i=0;i<owner->original_count;++i)
        if (owner->source->fonts[i]!=owner->original[i] ||
            memcmp(owner->original[i],&owner->original_state[i],sizeof(qa_font))) return false;
    return true;
}
static qa_font *policy_clone(qa_font_library *destination,const qa_font *source,
    qa_scene_resource_policy *images,qa_error *error)
{
    qa_font *font=calloc(1,sizeof(*font));
    if (!font) { qa_font_fail(error,QA_ERROR_MEMORY,0,"Retaining prepared font bindings"); return NULL; }
    *font=*source; font->library=destination; font->name=NULL;
    font->glyphs=NULL; font->images=NULL; font->sources=NULL; font->truetype_coverage=NULL;
    font->image_count=font->source_count=0;
    font->name=qa_font_copy_string(source->name,error);
    font->glyphs=source->glyph_capacity?calloc(source->glyph_capacity,sizeof(*font->glyphs)):NULL;
    font->images=source->image_capacity?calloc(source->image_capacity,sizeof(*font->images)):NULL;
    font->sources=source->source_capacity?calloc(source->source_capacity,sizeof(*font->sources)):NULL;
    font->truetype_coverage=source->truetype_coverage_count?
        malloc(source->truetype_coverage_count*sizeof(*font->truetype_coverage)):NULL;
    const qa_scene_image **mapped=source->image_count?
        calloc(source->image_count,sizeof(*mapped)):NULL;
    if (!font->name || (source->glyph_capacity && !font->glyphs) ||
        (source->image_capacity && !font->images) || (source->source_capacity && !font->sources) ||
        (source->truetype_coverage_count && !font->truetype_coverage) ||
        (source->image_count && !mapped)) {
        qa_font_fail(error,QA_ERROR_MEMORY,0,"Preparing actual font collections");
        free(mapped); qa_font_internal_destroy(font); return NULL;
    }
    for (size_t i=0;i<source->source_count;++i) {
        qa_resource_retain(source->sources[i]); font->sources[font->source_count++]=source->sources[i];
    }
    if (source->truetype_coverage_count) memcpy(font->truetype_coverage,source->truetype_coverage,
        source->truetype_coverage_count*sizeof(*font->truetype_coverage));
    for (size_t i=0;i<source->image_count;++i) {
        qa_scene_image *image=NULL;
        if (!qa_scene_resource_policy_image(images,source->images[i],&image,error)) {
            free(mapped); qa_font_internal_destroy(font); return NULL;
        }
        mapped[i]=image;
        if (!qa_font_internal_take_image(font,image,error)) {
            free(mapped); qa_font_internal_destroy(font); return NULL;
        }
    }
    for (size_t i=0;i<source->glyph_count;++i) {
        qa_font_glyph glyph=source->glyphs[i];
        size_t image=0;
        while (image<source->image_count && source->images[image]!=glyph.image) ++image;
        if (image==source->image_count && glyph.image) {
            qa_font_fail(error,QA_ERROR_ARGUMENT,i,"Font glyph lost its actual atlas owner");
            free(mapped); qa_font_internal_destroy(font); return NULL;
        }
        if (glyph.image) {
            const qa_scene_image *old=glyph.image,*next=mapped[image];
            glyph.image=next;
            if (font->kind==QA_FONT_CLASSIC) {
                if (!next->logical_width || !next->logical_height ||
                    next->logical_width%16 || next->logical_height%16) {
                    qa_font_fail(error,QA_ERROR_FORMAT,i,"Prepared classic font has invalid cell dimensions");
                    free(mapped); qa_font_internal_destroy(font); return NULL;
                }
                glyph.width=glyph.advance=(float)next->logical_width/16;
                glyph.height=glyph.bearing_y=(float)next->logical_height/16;
                float x=(float)(glyph.codepoint&15u)*glyph.width;
                float y=(float)(glyph.codepoint>>4)*glyph.height;
                glyph.uv=(qa_vec4){x/(float)next->logical_width,y/(float)next->logical_height,
                    (x+glyph.width)/(float)next->logical_width,(y+glyph.height)/(float)next->logical_height};
                font->line_height=font->ascent=glyph.height; font->descent=0;
            } else if (font->kind==QA_FONT_KFONT) {
                if (!next->logical_width || !next->logical_height) {
                    qa_font_fail(error,QA_ERROR_FORMAT,i,"Prepared KFONT atlas has no logical extent");
                    free(mapped); qa_font_internal_destroy(font); return NULL;
                }
                glyph.uv.x*= (float)old->logical_width/(float)next->logical_width;
                glyph.uv.z*= (float)old->logical_width/(float)next->logical_width;
                glyph.uv.y*= (float)old->logical_height/(float)next->logical_height;
                glyph.uv.w*= (float)old->logical_height/(float)next->logical_height;
                if (glyph.uv.x<0 || glyph.uv.y<0 || glyph.uv.z>1 || glyph.uv.w>1) {
                    qa_font_fail(error,QA_ERROR_FORMAT,i,"Prepared KFONT glyph exceeds its actual atlas");
                    free(mapped); qa_font_internal_destroy(font); return NULL;
                }
            }
        }
        glyph.font=font; font->glyphs[i]=glyph;
    }
    if (font->kind==QA_FONT_KFONT || font->kind==QA_FONT_ATLAS) {
        font->has_cap_ink=false; font->cap_top=font->cap_height=0;
        qa_font_internal_measure_cap_ink(font);
    }
    size_t capacity=font->image_count?8:0;
    while (capacity<font->image_count) capacity*=2;
    font->image_capacity=capacity;
    free(mapped);
    return font;
}
bool qa_font_resource_policy_prepare(qa_font_library *source,qa_scene_resource_policy *images,
    qa_font_resource_policy **out,qa_error *error)
{
    if (!out || *out)
        return qa_font_fail(error,QA_ERROR_ARGUMENT,0,"Font policy requires an empty actual handoff destination");
    if (!qa_font_library_idle(source)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,
            "Font policy requires its returned library: present=%u capture=%u policy=%u codec=%u callbacks=%u",
            source!=NULL,source && source->capture!=NULL,source && source->policy!=NULL,
            source && source->codec_active,source?source->callbacks:0);
        return false;
    }
    qa_scene_resources *bank=qa_scene_resource_policy_source(images);
    qa_scene_resources *destination=qa_scene_resource_policy_destination(images);
    if (bank!=source->resources || !destination) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,
            "Font policy requires its prepared resource bank: font=%p supplied=%p destination=%p",
            (void *)source->resources,(void *)bank,(void *)destination);
        return false;
    }
    qa_font_resource_policy *owner=calloc(1,sizeof(*owner));
    if (!owner) return qa_font_fail(error,QA_ERROR_MEMORY,0,"Retaining font resource handoff");
    owner->source=source; owner->images=images; owner->original_count=source->font_count;
    owner->original=source->font_count?calloc(source->font_count,sizeof(*owner->original)):NULL;
    owner->original_state=source->font_count?calloc(source->font_count,sizeof(*owner->original_state)):NULL;
    owner->destination=qa_font_library_create(source->vfs,qa_scene_resource_policy_destination(images),error);
    if (!owner->destination || (source->font_count && (!owner->original || !owner->original_state))) {
        qa_font_library_destroy(owner->destination); free(owner->original); free(owner->original_state); free(owner);
        return qa_font_fail(error,QA_ERROR_MEMORY,0,"Preparing private font destination");
    }
    if (!reserve((void **)&owner->destination->fonts,&owner->destination->font_capacity,
        source->font_count,sizeof(*source->fonts),error)) {
        qa_font_library_destroy(owner->destination); free(owner->original); free(owner->original_state); free(owner); return false;
    }
    source->policy=owner; *out=owner;
    for (size_t i=0;i<source->font_count;++i) {
        owner->original[i]=source->fonts[i]; owner->original_state[i]=*source->fonts[i];
    }
    for (size_t i=0;i<source->font_count;++i) {
        qa_font *font=policy_clone(owner->destination,source->fonts[i],images,error);
        if (!font) return false;
        owner->destination->fonts[owner->destination->font_count++]=font;
    }
    return true;
}
qa_font_library *qa_font_resource_policy_source(const qa_font_resource_policy *owner)
{ return owner?owner->source:NULL; }
qa_font_library *qa_font_resource_policy_destination(const qa_font_resource_policy *owner)
{ return owner && !owner->ready && !owner->published_state?owner->destination:NULL; }
bool qa_font_resource_policy_font(const qa_font_resource_policy *owner,const qa_font *candidate,const qa_font **out)
{
    if (!out || !candidate || !policy_current(owner)) return false;
    for (size_t i=0;i<owner->destination->font_count;++i)
        if (owner->destination->fonts[i]==candidate) {
            *out=i<owner->original_count?owner->original[i]:candidate; return true;
        }
    return false;
}
bool qa_font_resource_policy_ready(qa_font_resource_policy *owner,qa_error *error)
{
    if (!policy_current(owner) || owner->published_state ||
        owner->destination->font_count<owner->original_count)
        return qa_font_fail(error,QA_ERROR_ARGUMENT,0,"Font policy lost its complete destination roster");
    if (owner->ready) return qa_font_resource_policy_ready_is(owner);
    owner->staged_count=owner->destination->font_count;
    owner->published_capacity=owner->destination->font_capacity;
    owner->published=owner->published_capacity?calloc(owner->published_capacity,sizeof(*owner->published)):NULL;
    owner->staged_state=owner->staged_count?calloc(owner->staged_count,sizeof(*owner->staged_state)):NULL;
    if ((owner->published_capacity && !owner->published) || (owner->staged_count && !owner->staged_state)) {
        free(owner->published); owner->published=NULL; free(owner->staged_state); owner->staged_state=NULL;
        return qa_font_fail(error,QA_ERROR_MEMORY,0,"Sealing font policy pointer handoff");
    }
    for (size_t i=0;i<owner->staged_count;++i) {
        owner->published[i]=i<owner->original_count?owner->original[i]:owner->destination->fonts[i];
        owner->staged_state[i]=*owner->destination->fonts[i];
    }
    owner->destination->policy=owner; owner->ready=true; return true;
}
bool qa_font_resource_policy_ready_is(const qa_font_resource_policy *owner)
{
    if (!policy_current(owner) || !owner->ready || owner->published_state ||
        owner->destination->policy!=owner || owner->destination->font_count!=owner->staged_count ||
        owner->destination->font_capacity!=owner->published_capacity) return false;
    for (size_t i=0;i<owner->staged_count;++i)
        if (memcmp(owner->destination->fonts[i],&owner->staged_state[i],sizeof(qa_font))) return false;
    return true;
}
void qa_font_resource_policy_publish(qa_font_resource_policy *owner)
{
    for (size_t i=0;i<owner->original_count;++i) {
        qa_font *stable=owner->original[i],*candidate=owner->destination->fonts[i];
        qa_font old=*stable; *stable=*candidate; *candidate=old;
    }
    for (size_t i=0;i<owner->staged_count;++i) {
        qa_font *font=owner->published[i]; font->library=owner->source;
        for (size_t j=0;j<font->glyph_count;++j) font->glyphs[j].font=font;
    }
    free(owner->source->fonts); owner->source->fonts=owner->published; owner->published=NULL;
    owner->source->font_count=owner->staged_count; owner->source->font_capacity=owner->published_capacity;
    owner->destination->font_count=owner->original_count; owner->published_state=true;
}
static bool policy_dispose(qa_font_resource_policy **in,bool published,qa_error *error)
{
    if (!in || !*in) return true;
    qa_font_resource_policy *owner=*in;
    if (!policy_current(owner) || owner->published_state!=published)
        return qa_font_fail(error,QA_ERROR_ARGUMENT,0,"Font policy cleanup retains nonterminal actual parents");
    owner->source->policy=NULL; owner->destination->policy=NULL;
    qa_font_library_destroy(owner->destination);
    free(owner->original); free(owner->original_state); free(owner->staged_state); free(owner->published);
    free(owner); *in=NULL; return true;
}
bool qa_font_resource_policy_finish(qa_font_resource_policy **owner,qa_error *error)
{ return policy_dispose(owner,true,error); }
bool qa_font_resource_policy_abort(qa_font_resource_policy **owner,qa_error *error)
{ return policy_dispose(owner,false,error); }

size_t qa_font_library_record_count(const qa_font_library *library) { return library?library->font_count:0; }
const qa_font *qa_font_library_record_at(const qa_font_library *library, size_t index)
{ return library && index<library->font_count?library->fonts[index]:NULL; }
qa_scene_resources *qa_font_library_resource_owner(const qa_font_library *library)
{ return library?library->resources:NULL; }
