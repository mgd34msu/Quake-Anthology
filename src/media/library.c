#include "library_internal.h"
#include "qa/media_resource.h"
#include "qa/media_library_prepare.h"
#include "qa/binary.h"

#include <stdlib.h>
#include <string.h>

qa_media_library *qa_media_library_create(qa_scene_resources *resources, qa_error *error) {
    if (!resources) {
        cinematic_fail(error, "Media library requires shared scene resources");
        return NULL;
    }
    qa_media_library *library = calloc(1, sizeof(*library));
    if (!library) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating media asset library");
        return NULL;
    }
    library->resources = resources;
    return library;
}
void qa_cinematic_asset_retain(qa_cinematic_asset *asset) {
    if (asset)
        ++asset->references;
}
void qa_cinematic_asset_release(qa_cinematic_asset *asset) {
    if (!asset || --asset->references)
        return;
    switch (asset->source.format) {
    case QA_CINEMATIC_CIN:
        qa_cin_asset_release(asset->source.data.cin);
        break;
    case QA_CINEMATIC_ROQ:
        qa_media_input_release(asset->source.data.roq);
        break;
    case QA_CINEMATIC_OGV:
        qa_ogv_asset_release(asset->source.data.ogv);
        break;
    case QA_CINEMATIC_IMAGE:
        qa_scene_image_release(asset->source.data.image);
        break;
    }
    qa_resource_release(asset->source_record);
    free(asset->name);
    free(asset);
}
void qa_media_library_destroy(qa_media_library *library) {
    if (!library || !qa_media_library_idle(library))
        return;
    while (library->assets) {
        qa_cinematic_asset *next = library->assets->next;
        library->assets->next = NULL;
        qa_cinematic_asset_release(library->assets);
        library->assets = next;
    }
    free(library);
}
void qa_media_library_trim(qa_media_library *library) {
    if (!library || !qa_media_library_idle(library))
        return;
    qa_cinematic_asset **link = &library->assets;
    while (*link) {
        qa_cinematic_asset *asset = *link;
        if (asset->references == 1) {
            *link = asset->next;
            asset->next = NULL;
            qa_cinematic_asset_release(asset);
        } else
            link = &asset->next;
    }
}
qa_cinematic_source qa_cinematic_asset_source(const qa_cinematic_asset *asset) {
    return asset->source;
}
void qa_cinematic_asset_dimensions(const qa_cinematic_asset *asset, uint32_t *width,
                                   uint32_t *height) {
    *width = asset->width;
    *height = asset->height;
}
bool qa_media_asset_format(const char *path, qa_cinematic_format *out, qa_error *error) {
    const char *extension = strrchr(path, '.');
    if (!extension || strlen(extension) != 4)
        return cinematic_fail(error, "Movie requires a CIN, RoQ, OGV, or PCX extension");
    char folded[4];
    for (size_t i = 0; i < 4; ++i) {
        unsigned c = (unsigned char)extension[i];
        folded[i] = (char)(c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c);
    }
    if (!memcmp(folded, ".cin", 4))
        *out = QA_CINEMATIC_CIN;
    else if (!memcmp(folded, ".roq", 4))
        *out = QA_CINEMATIC_ROQ;
    else if (!memcmp(folded, ".ogv", 4))
        *out = QA_CINEMATIC_OGV;
    else if (!memcmp(folded, ".pcx", 4))
        *out = QA_CINEMATIC_IMAGE;
    else
        return cinematic_fail(error, "Unsupported cinematic extension");
    return true;
}
static bool roq_info(qa_cinematic_asset *asset, qa_error *error) {
    qa_roq_decoder *decoder;
    qa_roq_decoder_options options = {.end_policy = QA_ROQ_CINEMATIC, .silent = true};
    if (!qa_roq_decoder_create(asset->source.data.roq, &options, &decoder, error))
        return false;
    bool ok;
    qa_roq_event event;
    do {
        ok = qa_roq_decoder_chunk(decoder, NULL, &event, error);
    } while (ok && event.kind != QA_ROQ_INFO && event.kind != QA_ROQ_END);
    if (ok && event.kind == QA_ROQ_END)
        ok = cinematic_fail(error, "RoQ has no video dimensions");
    if (ok) {
        asset->width = event.data.info.width;
        asset->height = event.data.info.height;
    }
    qa_roq_decoder_destroy(decoder);
    return ok;
}
bool qa_media_asset_load(qa_media_library *library, qa_resource *resource, qa_cinematic_asset *asset,
                 qa_error *error) {
    qa_media_input *input;
    if (!qa_media_input_resource(resource, &input, error))
        return false;
    bool ok = false;
    switch (asset->source.format) {
    case QA_CINEMATIC_CIN:
        ok = qa_cin_asset_load(input, &asset->source.data.cin, error);
        if (ok) {
            qa_cin_info info = qa_cin_asset_info(asset->source.data.cin);
            asset->width = info.width;
            asset->height = info.height;
        }
        break;
    case QA_CINEMATIC_ROQ:
        qa_media_input_retain(input);
        asset->source.data.roq = input;
        if (asset->source_roq) {
            qa_bytes bytes=qa_resource_bytes(resource);
            ok=bytes.size>=2 && qa_load_u16le(bytes.data)==UINT16_C(0x1084);
            if (!ok) cinematic_fail(error,"Original cinematic requires its retained RoQ header magic");
        } else ok = roq_info(asset, error);
        break;
    case QA_CINEMATIC_OGV:
        ok = qa_ogv_asset_load(input, &asset->source.data.ogv, error);
        if (ok) {
            qa_ogv_info info = qa_ogv_asset_info(asset->source.data.ogv);
            asset->width = info.width;
            asset->height = info.height;
        }
        break;
    case QA_CINEMATIC_IMAGE: {
        qa_image image = {0};
        ok = qa_image_decode_pcx(qa_resource_bytes(resource), QA_IMAGE_FORMAT, &image, error);
        if (ok && (!image.palette.data || !image.rgba.data))
            ok = cinematic_fail(error, "Cinematic PCX requires its own palette");
        if (ok) {
            qa_scene_image *shared;
            qa_scene_image_level level = {image.width, image.height, image.rgba.data,
                                          image.rgba.size};
            ok = qa_scene_image_create(library->resources, asset->name, QA_SCENE_RGBA8, &level, 1,
                                       QA_SCENE_CLAMP, QA_SCENE_LINEAR, (qa_scene_vec4){0, 0, 0, 1},
                                       &shared, error);
            if (ok) {
                asset->source.data.image = shared;
                asset->width = image.width;
                asset->height = image.height;
            }
        }
        qa_image_free(&image);
        break;
    }
    }
    qa_media_input_release(input);
    return ok;
}
static bool library_asset(qa_media_library *library, const char *path, qa_cinematic_format kind, bool source_roq,
    qa_resource *resource, qa_cinematic_asset **out, qa_error *error) {
    const qa_sha256_digest *digest = qa_resource_digest(resource);
    for (const qa_media_library *owner = library; owner; owner = owner->parent)
        for (qa_cinematic_asset *asset = owner->assets; asset; asset = asset->next)
            if (asset->source.format == kind && asset->source_roq==source_roq && qa_sha256_equal(&asset->digest, digest)) {
                qa_cinematic_asset_retain(asset); qa_resource_release(resource);
                *out = asset; return true;
            }
    qa_cinematic_asset *asset = calloc(1, sizeof(*asset));
    if (!asset) {
        qa_resource_release(resource);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating cinematic asset");
        return false;
    }
    asset->references = 1;
    asset->source.asset = asset;
    asset->source.format = kind;
    asset->source_roq = source_roq;
    asset->digest = *digest;
    size_t length = strlen(path);
    asset->name = malloc(length + 1);
    if (!asset->name) {
        qa_resource_release(resource);
        qa_cinematic_asset_release(asset);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating cinematic asset name");
        return false;
    }
    memcpy(asset->name, path, length + 1);
    asset->source.name = asset->name;
    bool ok = qa_media_asset_load(library, resource, asset, error);
    if (ok) { qa_resource_retain(resource); asset->source_record = resource; }
    qa_resource_release(resource);
    if (!ok) {
        qa_cinematic_asset_release(asset);
        return false;
    }
    asset->next = library->assets;
    library->assets = asset;
    qa_cinematic_asset_retain(asset);
    *out = asset;
    return true;
}
bool qa_media_library_load(qa_media_library *library, qa_vfs *view, const char *path,
    qa_cinematic_asset **out, qa_error *error)
{
    if (!library || !view || !path || !out || !qa_media_library_idle(library))
        return cinematic_fail(error, "Invalid media library request");
    qa_cinematic_format kind;
    if (!qa_media_asset_format(path, &kind, error)) return false;
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(view, path, &resource, NULL, error)) return false;
    return library_asset(library, path, kind, false, resource, out, error);
}
bool qa_media_library_load_shader(qa_media_library *library, qa_vfs *view, const char *path,
    qa_cinematic_asset **out, qa_error *error)
{
    if (!library || !view || !path || !out || !qa_media_library_idle(library))
        return cinematic_fail(error, "Invalid shader media library request");
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(view, path, &resource, NULL, error)) return false;
    qa_cinematic_format kind;
    if (!qa_media_asset_format(path, &kind, error) || kind == QA_CINEMATIC_IMAGE) {
        qa_resource_release(resource);
        if (error && error->code == QA_OK) cinematic_fail(error, "Shader movie requires RoQ, CIN or OGV content");
        return false;
    }
    return library_asset(library, path, kind, false, resource, out, error);
}

bool qa_media_library_load_source_roq(qa_media_library *library, qa_vfs *view, const char *path,
    qa_cinematic_asset **out, qa_error *error)
{
    if (!library || !view || !path || !out || !qa_media_library_idle(library))
    {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Invalid original cinematic library request");
        return false;
    }
    qa_resource *resource=NULL;
    if (!qa_vfs_acquire(view,path,&resource,NULL,error)) return false;
    qa_bytes bytes=qa_resource_bytes(resource);
    if (!bytes.size) {
        qa_resource_release(resource);
        qa_error_set(error,QA_ERROR_NOT_FOUND,0,"Original cinematic file is empty: %s",path); return false;
    }
    if (bytes.size<2 || qa_load_u16le(bytes.data)!=UINT16_C(0x1084)) {
        qa_resource_release(resource);
        qa_error_set(error,QA_ERROR_FORMAT,0,"Original cinematic requires the actual RoQ header magic");
        return false;
    }
    return library_asset(library,path,QA_CINEMATIC_ROQ,true,resource,out,error);
}
const qa_resource *qa_cinematic_asset_resource(const qa_cinematic_asset *asset)
{
    return asset ? asset->source_record : NULL;
}
