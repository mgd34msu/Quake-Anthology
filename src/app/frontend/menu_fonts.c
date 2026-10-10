#include "menu_fonts.h"
#include "ui_features_private.h"
#include "qa/image.h"
#include "qa/text.h"
#include "../../render/scene/resources_internal.h"

bool frontend_menu_font_view(qa_frontend *f, const qa_product **selected, const qa_product **typography, qa_error *error)
{
    qa_catalog *catalog = qa_application_catalog(f->application);
    *selected = f->options.game ? frontend_product_selection(catalog, f->options.game) : NULL; *typography = NULL;
    if (f->options.game && !*selected) return frontend_fail(error,QA_ERROR_NOT_FOUND,"Requested menu source product is absent from the actual catalog");
    const qa_product *q3 = NULL;
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        const qa_product *product = qa_catalog_at(catalog, i);
        if (product->availability != QA_CONTENT_INSTALLED) continue;
        if (!*selected) *selected = product;
        if (!*typography && product->edition == QA_EDITION_RERELEASE &&
            (product->family == QA_GAME_Q1 || product->family == QA_GAME_Q2)) *typography = product;
        if (!q3 && product->family == QA_GAME_Q3) q3 = product;
    }
    if (!*typography) *typography = q3;
    if (!*selected) return true;
    if ((*selected)->availability != QA_CONTENT_INSTALLED)
        return frontend_fail(error, QA_ERROR_NOT_FOUND, "Selected game has no installed source charset");
    if (!qa_catalog_open(catalog, (*selected)->id, &f->ui_mounts, error)) return false;
    if (!*typography || (*typography)->id == (*selected)->id) return true;
    qa_vfs *source = NULL;
    if (!qa_catalog_open(catalog, (*typography)->id, &source, error)) return false;
    size_t original = qa_vfs_mount_count(f->ui_mounts), extra = qa_vfs_mount_count(source);
    if (extra > SIZE_MAX - original || original + extra > SIZE_MAX / sizeof(qa_mount_id)) {
        qa_vfs_destroy(source); return frontend_fail(error, QA_ERROR_MEMORY, "Menu typography mount order exceeds memory extent");
    }
    qa_mount_id *order = malloc((original + extra) * sizeof(*order));
    if (!order && original + extra) { qa_vfs_destroy(source); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored typography search order"); }
    bool ok = true;
    for (size_t i = 0; ok && i < original; ++i) {
        qa_vfs_mount_info mount; ok = qa_vfs_mount_at(f->ui_mounts, i, &mount); if (ok) order[extra + i] = mount.id;
    }
    for (size_t i = 0; ok && i < extra; ++i) {
        qa_vfs_mount_info mount;
        ok = qa_vfs_mount_at(source, i, &mount) && qa_vfs_mount_retained(f->ui_mounts, source, mount.id,
            mount.comparison, false, order + i, error);
    }
    if (ok) ok = qa_vfs_set_prefix_order(f->ui_mounts, "fonts", order, original + extra, error) &&
        qa_vfs_set_prefix_order(f->ui_mounts, "localization", order, original + extra, error) &&
        qa_vfs_set_prefix_order(f->ui_mounts, "menu/art", order, original + extra, error);
    free(order); qa_vfs_destroy(source); return ok;
}
bool frontend_menu_charset(qa_frontend *f, const qa_product *product, qa_error *error)
{
    qa_scene_image *image = NULL; bool ok = true;
    if (product->family == QA_GAME_Q1) {
        qa_resource *wad_source = NULL, *palette = NULL; qa_wad wad = {0}; qa_image expanded = {0};
        ok = qa_vfs_acquire(f->ui_mounts, "gfx.wad", &wad_source, NULL, error) &&
            qa_vfs_acquire(f->ui_mounts, "gfx/palette.lmp", &palette, NULL, error) &&
            qa_wad_decode(qa_resource_bytes(wad_source), &wad, error);
        const qa_wad_lump *chars = NULL;
        for (size_t i = 0; ok && i < wad.count; ++i) if (!strcmp(wad.lumps[i].name, "conchars")) { chars = wad.lumps + i; break; }
        if (ok && (!chars || chars->compression || chars->bytes.size != 128 * 128 || qa_resource_bytes(palette).size != 768))
            ok = frontend_fail(error, QA_ERROR_FORMAT, "Quake source conchars or palette is malformed");
        qa_palette_options colors = {.transparent_index = 0, .fullbright_first = -1, .fullbright_last = -1, .layer = QA_PALETTE_COMBINED};
        if (ok) ok = qa_image_expand_indexed(&(qa_indexed_level){128, 128, {(uint8_t *)chars->bytes.data, chars->bytes.size}},
            qa_resource_bytes(palette), &colors, &expanded, error);
        if (ok) ok = qa_scene_image_create(f->ui_images, "frontend:source-conchars", QA_SCENE_RGBA8,
            &(qa_scene_image_level){128, 128, expanded.rgba.data, expanded.rgba.size}, 1, QA_SCENE_CLAMP,
            QA_SCENE_NEAREST, (qa_vec4){0}, &image, error);
        if (ok) {
            image_asset_recipe recipe = {.kind = 1, .level_count = 1, .source = wad_source,
                .palette_source = palette, .palette_attempted = true,
                .offsets = {chars->offset}, .widths = {128}, .heights = {128},
                .fullbright_first = 256, .fullbright_last = -1, .layer = QA_PALETTE_COMBINED,
                .options = {.family = QA_GAME_Q1, .wrap = QA_SCENE_CLAMP, .filter = QA_SCENE_NEAREST,
                    .usage = QA_IMAGE_USAGE_PICTURE, .transparent = true, .transparent_index = 0}};
            ok = scene_image_asset_copy(image, &recipe, error);
        }
        qa_image_free(&expanded); qa_wad_free(&wad); qa_resource_release(palette); qa_resource_release(wad_source);
    } else {
        qa_scene_image_options options = {.family = product->family == QA_GAME_Q2 ? QA_GAME_Q2 : QA_GAME_Q3,
            .wrap = QA_SCENE_CLAMP, .filter = QA_SCENE_NEAREST, .usage = QA_IMAGE_USAGE_PICTURE,
            .transparent = true, .transparent_index = 255};
        ok = qa_scene_image_load(f->ui_images, product->family == QA_GAME_Q2 ? "pics/conchars.pcx" : "gfx/2d/bigchars", &options, &image, error);
    }
    if (ok) ok = qa_font_classic_create(f->fonts, "source conchars", image,
        product->family == QA_GAME_Q3 ? QA_FONT_TINTED : QA_FONT_BAKED_COLOR, &f->classic, error);
    qa_scene_image_release(image);
    if (ok && product->family == QA_GAME_Q2 && product->edition == QA_EDITION_RERELEASE)
        ok = qa_font_kfont_load(f->fonts, "fonts/qconfont.kfont", &f->ui_features->console, error);
    return ok;
}
static const uint16_t prop[65][3] = {
    {0,0,8},{11,122,7},{154,181,14},{55,122,17},{79,122,18},{101,122,23},{153,122,18},{9,93,7},
    {207,122,8},{230,122,9},{177,122,18},{30,152,18},{85,181,7},{34,93,11},{110,181,6},{130,152,14},
    {22,64,17},{41,64,12},{58,64,17},{78,64,18},{98,64,19},{120,64,18},{141,64,18},{204,64,16},
    {162,64,17},{182,64,18},{59,181,7},{35,181,7},{203,152,14},{56,93,14},{228,152,14},{177,181,18},
    {28,122,22},{5,4,18},{27,4,18},{48,4,18},{69,4,17},{90,4,13},{106,4,13},{121,4,18},
    {143,4,17},{164,4,8},{175,4,16},{195,4,18},{216,4,12},{230,4,23},{6,34,18},{27,34,18},
    {48,34,18},{68,34,18},{90,34,17},{110,34,18},{130,34,14},{146,34,18},{166,34,19},{185,34,29},
    {215,34,18},{234,34,18},{5,64,14},{60,152,7},{106,151,13},{83,152,7},{128,122,17},{4,152,21},{134,181,5}
};
static const uint16_t prop_end[4][3] = {{153,152,13},{11,181,5},{180,152,13},{79,93,17}};
static bool proportional(qa_frontend *f, qa_error *error)
{
    qa_scene_image *image = NULL;
    qa_scene_image_options options = {.family = QA_GAME_Q3, .wrap = QA_SCENE_CLAMP,
        .filter = QA_SCENE_LINEAR, .usage = QA_IMAGE_USAGE_PICTURE, .transparent = true, .transparent_index = 255};
    if (!qa_scene_image_load(f->ui_images, "menu/art/font1_prop.tga", &options, &image, error)) return false;
    qa_font_glyph glyphs[95];
    for (unsigned code = 32; code < 127; ++code) {
        unsigned mapped = code >= 97 && code <= 122 ? code - 32 : code;
        const uint16_t *metric = mapped >= 123 ? prop_end[mapped - 123] : prop[mapped - 32];
        glyphs[code - 32] = (qa_font_glyph){.codepoint = code, .image = image,
            .uv = {(float)metric[0] / 256, (float)metric[1] / 256, (float)(metric[0] + metric[2]) / 256, (float)(metric[1] + 27) / 256},
            .width = metric[2], .height = 27, .advance = metric[2] + 3, .bearing_y = 27, .visible = code != 32};
    }
    bool ok = qa_font_atlas_create(f->fonts, "Q3 proportional", image, glyphs, 95, 27, &f->primary, error);
    qa_scene_image_release(image); if (ok) f->ui_features->bold = f->primary; return ok;
}
static bool coverage_add(uint32_t **points, size_t *count, size_t *capacity, uint32_t point, qa_error *error)
{
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 512;
        if (next < *capacity || next > SIZE_MAX / sizeof(**points)) return frontend_fail(error, QA_ERROR_MEMORY, "Authored font coverage overflows");
        uint32_t *grown = realloc(*points, next * sizeof(*grown));
        if (!grown) return frontend_fail(error, QA_ERROR_MEMORY, "Collecting authored localization font coverage");
        *points = grown; *capacity = next;
    }
    (*points)[(*count)++] = point; return true;
}
static int point_compare(const void *left, const void *right)
{ uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right; return a < b ? -1 : a > b; }
bool frontend_menu_typography(qa_frontend *f, const qa_product *product, qa_error *error)
{
    f->ui_features->ui_profile=product && product->family==QA_GAME_Q1 ? QA_LOCALIZATION_Q1_RERELEASE : QA_LOCALIZATION_Q2_RERELEASE;
    if (!product) { f->primary = f->classic; f->ui_features->bold = f->classic; return true; }
    if (product->family == QA_GAME_Q3) return proportional(f, error);
    uint32_t base[336];
    for (unsigned i = 0; i < 224; ++i) base[i] = i + 32;
    for (unsigned i = 0; i < 112; ++i) base[224 + i] = i + 0x2000;
    qa_font_truetype_options options = {.path = "fonts/Montserrat-Regular.ttf", .pixel_size = 48,
        .codepoints = base, .codepoint_count = 336, .atlas_width = 1024, .atlas_height = 2048};
    if (!qa_font_truetype_load(f->fonts, &options, &f->primary, error)) return false;
    options.path = "fonts/NotoSans-Bold.ttf"; options.pixel_size = 72;
    if (!qa_font_truetype_load(f->fonts, &options, &f->ui_features->bold, error)) return false;
    frontend_ui_features *owner = f->ui_features;
    owner->fallbacks = calloc(4, sizeof(*owner->fallbacks));
    if (!owner->fallbacks) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining authored font fallback roots");
    owner->fallback_capacity = 4; owner->fallbacks[owner->fallback_count++] = owner->bold;
    uint32_t *points = NULL; size_t count = 0, capacity = 0; qa_vfs_listing listing = {0}; bool ok = true;
    for (unsigned i = 0; ok && i < 336; ++i) ok = coverage_add(&points, &count, &capacity, base[i], error);
    if (ok) ok = qa_vfs_list(f->ui_mounts, "localization", ".txt", &listing, error);
    for (size_t i = 0; ok && i < listing.count; ++i) {
        size_t length = strlen(listing.names[i]);
        if (length > SIZE_MAX - 14) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Localization font path overflows"); break; }
        char *path = malloc(length + 14); qa_resource *resource = NULL;
        if (!path) { ok = frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual localization font path"); break; }
        memcpy(path, "localization/", 13); memcpy(path + 13, listing.names[i], length + 1);
        ok = qa_vfs_acquire(f->ui_mounts, path, &resource, NULL, error); free(path);
        size_t at = 0; uint32_t point;
        while (ok && qa_utf8_next(qa_resource_bytes(resource), &at, &point))
            if (point >= 32) ok = coverage_add(&points, &count, &capacity, point, error);
        qa_resource_release(resource);
    }
    qa_vfs_listing_free(&listing);
    if (ok) {
        qsort(points, count, sizeof(*points), point_compare); size_t unique = 0;
        for (size_t i = 0; i < count; ++i) if (!unique || points[i] != points[unique - 1]) points[unique++] = points[i];
        count = unique;
    }
    const char *paths[] = {"fonts/NotoSans-Bold.ttf", "fonts/NotoSansJP-Regular.otf", "fonts/NotoSansKR-Regular.otf"};
    for (unsigned i = 0; ok && i < 3; ++i) {
        size_t missing = 0;
        for (size_t j = 0; j < count; ++j) {
            qa_font_glyph glyph; bool present = qa_font_find_glyph(f->primary, points[j], &glyph);
            for (size_t k = 0; !present && k < owner->fallback_count; ++k) present = qa_font_find_glyph(owner->fallbacks[k], points[j], &glyph);
            if (!present) points[missing++] = points[j];
        }
        count = missing; if (!count) break;
        options.path = paths[i]; options.pixel_size = 48; options.codepoints = points; options.codepoint_count = count;
        const qa_font *font = NULL;
        ok = qa_font_truetype_load(f->fonts, &options, &font, error);
        if (ok) owner->fallbacks[owner->fallback_count++] = font;
    }
    free(points); return ok;
}
bool frontend_menu_font_selection(qa_frontend *f, uint32_t seat, bool bold, qa_font_selection *out, qa_error *error)
{
    frontend_ui_features *owner = f ? f->ui_features : NULL;
    if (!owner || seat >= f->options.seats) return frontend_fail(error, QA_ERROR_ARGUMENT, "Menu font selection lost its actual seat roots");
    size_t skip = bold && owner->fallback_count && owner->fallbacks[0] == owner->bold ? 1 : 0;
    return qa_font_selection_init(out, seat, f->classic, bold ? owner->bold : f->primary,
        owner->fallbacks ? owner->fallbacks + skip : NULL, owner->fallback_count - skip, error);
}
bool frontend_console_font_selection(qa_frontend *f, uint32_t seat, qa_font_selection *out, qa_error *error)
{
    if (!f || !f->ui_features) return frontend_fail(error, QA_ERROR_ARGUMENT, "Console font selection lost its actual roots");
    return qa_font_selection_init(out, seat, f->classic, f->ui_features->console, f->ui_features->fallbacks,
        f->ui_features->fallback_count, error);
}
