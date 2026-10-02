#include "remote_unified_material_movies_bridge.h"
#include "remote_unified_media_private.h"
#include "remote_unified_private.h"
#include "renderer_materials.h"
#include "source_cinematics.h"
#include "qa/media_library_prepare.h"
#include "qa/media_library_save.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"

static unified_media_bank *bank_at(const frontend_unified_media *owner, size_t ordinal)
{
    unified_media_bank *row = owner ? owner->banks : NULL;
    while (row && ordinal--) row = row->next;
    return row;
}
static bool shader_movies_current(void *context, const frontend_material_movie_source *source)
{
    frontend_unified_media *owner = context;
    if (!owner || !source || !owner->frontend || source->frontend != owner->frontend ||
        source->context != owner || source->current != shader_movies_current ||
        !frontend_unified_media_current(owner) ||
        (owner->importing && !owner->frontend->source_restoring)) return false;
    for (const unified_media_bank *row = owner->banks; row; row = row->next) {
        if (row->files != source->files || row->images != source->images ||
            row->materials != source->materials || row->media != source->media) continue;
        qa_vfs *admitted = NULL;
        const qa_product *product = NULL;
        return row->content && *row->content && row->files && row->images && row->materials && row->media &&
            qa_executable_recipe_content_read(owner->recipe, row->content, &admitted, &product) &&
            admitted == row->files && product == row->product &&
            qa_scene_resources_files(row->images) == row->files &&
            qa_material_library_resource_owner(row->materials) == row->images &&
            qa_media_library_resource_owner(row->media) == row->images;
    }
    return false;
}
static frontend_material_movie_source source_view(frontend_unified_media *owner, const unified_media_bank *row)
{
    return (frontend_material_movie_source){.frontend = owner ? owner->frontend : NULL,
        .files = row ? row->files : NULL, .images = row ? row->images : NULL,
        .materials = row ? row->materials : NULL, .media = row ? row->media : NULL,
        .context = owner, .current = shader_movies_current};
}
bool frontend_unified_material_movie_source_read(frontend_unified_media *owner, size_t ordinal,
    frontend_material_movie_source *out, qa_error *error)
{
    frontend_material_movie_source source = source_view(owner, bank_at(owner, ordinal));
    if (!out || !shader_movies_current(owner, &source))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified shader movies lost their retained recipe content bank");
    *out = source;
    return true;
}
bool frontend_unified_material_movies_create(frontend_unified_media *owner, size_t ordinal, qa_error *error)
{
    unified_media_bank *row = bank_at(owner, ordinal);
    if (!owner || !owner->frontend || owner->importing || owner->frontend->source_restoring ||
        owner->frontend->capture || owner->frontend->resource_inventory || !row ||
        row->media || row->shader_movies || !row->images || !row->materials)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified shader movies require their actual linked fresh content bank");
    row->media = qa_media_library_create(row->images, error);
    if (!row->media) return false;
    frontend_material_movie_source source;
    if (!frontend_unified_material_movie_source_read(owner, ordinal, &source, error) ||
        !frontend_material_movies_create(&source, &row->shader_movies, error)) return false;
    if (row->product->family == QA_GAME_Q3)
        return row->cinematic_audio_owner && owner->physical_seat < owner->frontend->options.seats &&
            frontend_source_cinematics_ensure(owner->frontend, row->images, error) &&
            frontend_material_movies_cinematic_attach(row->shader_movies, owner->frontend->source_cinematics,
                owner->physical_seat, row->cinematic_audio_owner, error);
    return true;
}
bool frontend_unified_material_cinematic_namespace_read(const frontend_unified_media *owner, size_t ordinal,
    uint32_t *seat, uint64_t *bus, bool *present, qa_error *error)
{
    const unified_media_bank *row = bank_at(owner, ordinal);
    qa_vfs *files = NULL; const qa_product *product = NULL;
    if (!owner || !owner->frontend || !row || !row->product || !seat || !bus || !present ||
        owner->physical_seat >= owner->frontend->options.seats || !row->content ||
        !qa_executable_recipe_content_read(owner->recipe, row->content, &files, &product) ||
        files != row->files || product != row->product ||
        ((row->product->family == QA_GAME_Q3) != (row->cinematic_audio_owner != 0)))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified cinematic namespace lost its real bank topology");
    *present = row->cinematic_audio_owner != 0;
    *seat = owner->physical_seat; *bus = row->cinematic_audio_owner; return true;
}
bool frontend_unified_material_movies_prepare_restored(frontend_unified_media *owner, size_t ordinal, qa_error *error)
{
    unified_media_bank *row = bank_at(owner, ordinal);
    if (!owner || !owner->frontend || !owner->importing || !owner->frontend->source_restoring ||
        !row || row->media || row->shader_movies || !row->files || !row->images || !row->materials ||
        qa_scene_resources_files(row->images) != row->files ||
        qa_material_library_resource_owner(row->materials) != row->images)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified movie cache import requires its actual isolated empty banks");
    row->media = qa_media_library_create(row->images, error);
    return row->media != NULL;
}
bool frontend_unified_material_movies_restore(frontend_unified_media *owner, size_t ordinal,
    const frontend_material_movies_refs *refs, qa_bytes bytes, qa_error *error)
{
    unified_media_bank *row = bank_at(owner, ordinal);
    frontend_material_movie_source source;
    if (!owner || !owner->frontend || !owner->importing || !owner->frontend->source_restoring ||
        !row || row->shader_movies || !frontend_unified_material_movie_source_read(owner, ordinal, &source, error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified playback import requires its genuine cold recipe bank");
    return frontend_material_movies_restore(&source, refs, bytes, &row->shader_movies, error);
}
bool frontend_unified_material_movies_restore_ready(frontend_unified_media *owner, qa_error *error)
{
    if (!owner || !owner->frontend || !owner->importing || !owner->frontend->source_restoring)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified movie readiness requires its actual importing media parent");
    for (unified_media_bank *row = owner->banks; row; row = row->next) {
        if (row->constructing || row->construction_failed || (!row->media != !row->shader_movies))
            return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified movie import retains an incomplete content bank");
        if (!row->media) {
            const qa_scene_image *(*start)(void *, const char *, qa_error *) = NULL;
            void *context = NULL;
            if (!qa_material_library_video_start_read(row->materials, &start, &context)) return false;
            if (start || context) {
                frontend_material_movies *retained = NULL;
                if (!frontend_material_movies_library_owner(row->materials, &retained, error) ||
                    !frontend_material_movies_publish_ready(retained, error)) return false;
            } else for (size_t i = 0; i < qa_material_library_record_count(row->materials); ++i)
                if (qa_material_library_video_receipt_count(row->materials, i))
                    return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified video receipts have no imported playback custodian");
            continue;
        }
        frontend_material_movie_source source, expected = source_view(owner, row);
        frontend_material_movies *installed = NULL;
        uint32_t cinematic_seat = 0; uint64_t cinematic_bus = 0; bool numeric = false;
        if (!shader_movies_current(owner, &expected) ||
            !frontend_material_movies_library_owner(row->materials, &installed, error) || installed != row->shader_movies ||
            !frontend_material_movies_source_read(row->shader_movies, &source, error) ||
            source.frontend != owner->frontend || source.files != row->files || source.images != row->images ||
            source.materials != row->materials || source.media != row->media || source.context != owner ||
            source.current != shader_movies_current ||
            !frontend_material_movies_cinematic_namespace_read(row->shader_movies,
                &cinematic_seat, &cinematic_bus, &numeric, error) ||
            numeric != (row->product->family == QA_GAME_Q3) ||
            (numeric && (cinematic_seat != owner->physical_seat || cinematic_bus != row->cinematic_audio_owner)) ||
            !frontend_material_movies_publish_ready(row->shader_movies, error))
            return (error && error->code != QA_OK) ? false :
                frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified movie import leaves its retained cache, library or playback dictionary");
    }
    return true;
}
bool frontend_unified_material_movies_idle(const frontend_unified_media *owner)
{
    for (const unified_media_bank *row = owner ? owner->banks : NULL; row; row = row->next)
        if ((row->shader_movies && !frontend_material_movies_idle(row->shader_movies)) ||
            (row->media && !qa_media_library_idle(row->media))) return false;
    return true;
}
bool frontend_unified_material_movies_frame(frontend_unified_media *owner, qa_scene_frame *frame, qa_error *error)
{
    if (!owner || !owner->frontend || frame != &owner->frontend->frame)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified movies require their actual submitted frontend frame");
    for (unified_media_bank *row = owner->banks; row; row = row->next)
        if (row->shader_movies && !frontend_material_movies_frame(row->shader_movies, frame, error)) return false;
    return true;
}
bool frontend_unified_material_movies_clear(frontend_unified_media *owner, size_t ordinal, qa_error *error)
{
    unified_media_bank *row = bank_at(owner, ordinal);
    if (!owner || !owner->frontend || !row || owner->frontend->capture ||
        (owner->frontend->resource_inventory && !owner->frontend->source_restoring) ||
        !frontend_unified_material_movies_idle(owner) ||
        (row->materials && !qa_material_library_idle(row->materials)))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified movie retirement retains an active content or policy borrow");
    if (!owner->frontend->source_restoring && row->shader_movies && row->media) {
        frontend_material_movie_source expected = source_view(owner, row);
        if (!frontend_renderer_materials_adopt_movies(owner->frontend, &expected,
            &row->shader_movies, &row->media, error)) return false;
    }
    if (!frontend_material_movies_destroy(&row->shader_movies, error)) return false;
    qa_media_library_destroy(row->media);
    row->media = NULL;
    return true;
}
size_t frontend_unified_material_movie_count(const frontend_unified_media *owner)
{
    size_t count = 0;
    for (const unified_media_bank *row = owner ? owner->banks : NULL; row; row = row->next)
        if (row->media) ++count;
    return count;
}
bool frontend_unified_material_movie_at(const frontend_unified_media *owner, size_t filtered, size_t *ordinal)
{
    if (!ordinal) return false;
    size_t physical = 0;
    for (const unified_media_bank *row = owner ? owner->banks : NULL; row; row = row->next, ++physical)
        if (row->media && filtered-- == 0) { *ordinal = physical; return true; }
    return false;
}
