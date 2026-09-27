#include "asset_internal.h"

static bool take(size_t *offset, size_t count, size_t stride, size_t length, size_t *start,
                 qa_error *e) {
    if (*offset > length || count > (length - *offset) / stride) {
        qa_error_set(e, QA_ERROR_FORMAT, *offset, "Truncated navigation record table");
        return false;
    }
    *start = *offset;
    *offset += count * stride;
    return true;
}
bool nav_kex_read(qa_bytes bytes, qa_nav_asset **out, qa_error *e) {
    bool nav3 = bytes.data[3] == '3';
    uint32_t version = qa_load_u32le(bytes.data + 4);
    if (bytes.size < 20 || (nav3 ? version < 1 || version > 6 : version < 12 || version > 18)) {
        qa_error_set(e, QA_ERROR_FORMAT, 4, "Invalid NAV version or header size");
        return false;
    }
    int32_t nodes = qa_load_i32le(bytes.data + 8), links = qa_load_i32le(bytes.data + 12),
            hints = qa_load_i32le(bytes.data + 16);
    if (nodes < 0 || links < 0 || hints < 0) {
        qa_error_set(e, QA_ERROR_FORMAT, 8, "Negative navigation record count");
        return false;
    }
    size_t offset = 20, at = 0, headers, origins, link_table, hint_table, entities;
    float heuristic = 1;
    if (nav3 || version >= 16) {
        if (!take(&offset, 1, 4, bytes.size, &at, e))
            return false;
        heuristic = qa_load_f32le(bytes.data + at);
        if (!isfinite(heuristic) || heuristic <= 0) {
            qa_error_set(e, QA_ERROR_FORMAT, at, "Invalid navigation cost multiplier");
            return false;
        }
    }
    size_t hint_stride = nav3 && version >= 4 ? 48 : 36;
    if (!take(&offset, (size_t)nodes, 8, bytes.size, &headers, e) ||
        !take(&offset, (size_t)nodes, 12, bytes.size, &origins, e) ||
        !take(&offset, (size_t)links, 6, bytes.size, &link_table, e) ||
        !take(&offset, (size_t)hints, hint_stride, bytes.size, &hint_table, e) ||
        !take(&offset, 1, 4, bytes.size, &at, e))
        return false;
    int32_t entity_count = qa_load_i32le(bytes.data + at);
    size_t tail_count = nav3 ? 0 : version <= 12 ? 0 : version <= 14 ? 2 : 1;
    size_t entity_stride = 26 + (nav3 && version >= 2 ? 4 : 0) + tail_count * 4;
    if (entity_count < 0 ||
        !take(&offset, (size_t)entity_count, entity_stride, bytes.size, &entities, e)) {
        if (entity_count < 0)
            qa_error_set(e, QA_ERROR_FORMAT, at, "Negative navigation entity count");
        return false;
    }
    if (offset != bytes.size) {
        qa_error_set(e, QA_ERROR_FORMAT, offset, "Unconsumed navigation bytes");
        return false;
    }
    size_t allocation = 0, node_offset, link_offset, hint_offset, entity_offset;
    if (!nav_layout(&allocation, (size_t)nodes, sizeof(qa_nav_source_node), &node_offset) ||
        !nav_layout(&allocation, (size_t)links, sizeof(qa_nav_source_link), &link_offset) ||
        !nav_layout(&allocation, (size_t)hints, sizeof(qa_nav_hint), &hint_offset) ||
        !nav_layout(&allocation, (size_t)entity_count, sizeof(qa_nav_source_entity),
                    &entity_offset)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Navigation asset size overflow");
        return false;
    }
    qa_nav_asset *asset = calloc(1, sizeof(*asset));
    if (asset == NULL)
        goto memory;
    asset->storage = calloc(allocation == 0 ? 1 : allocation, 1);
    if (asset->storage == NULL) {
        free(asset);
        goto memory;
    }
    asset->kind = nav3 ? QA_NAV_NAV3 : QA_NAV_NAV2;
    atomic_init(&asset->references, 1);
    uint8_t *storage = asset->storage;
    qa_nav_source_node *node = (void *)(storage + node_offset);
    qa_nav_source_link *link = (void *)(storage + link_offset);
    qa_nav_hint *hint = (void *)(storage + hint_offset);
    qa_nav_source_entity *entity = (void *)(storage + entity_offset);
    asset->kex = (qa_nav_source_view){.kind = asset->kind,
                                      .version = version,
                                      .heuristic = heuristic,
                                      .node_count = (size_t)nodes,
                                      .link_count = (size_t)links,
                                      .traversal_count = (size_t)hints,
                                      .entity_count = (size_t)entity_count,
                                      .nodes = node,
                                      .links = link,
                                      .traversals = hint,
                                      .entities = entity};
    for (int32_t i = 0; i < nodes; ++i) {
        at = headers + (size_t)i * 8;
        const uint8_t *p = bytes.data + at;
        node[i] = (qa_nav_source_node){.flags = qa_load_u16le(p),
                                       .link_count = qa_load_u16le(p + 2),
                                       .first_link = qa_load_u16le(p + 4),
                                       .radius = qa_load_u16le(p + 6),
                                       .origin = nav_vector(bytes.data + origins + (size_t)i * 12)};
        if ((uint32_t)node[i].first_link + node[i].link_count > (uint32_t)links ||
            !qa_vec_finite(node[i].origin))
            goto malformed;
    }
    for (int32_t i = 0; i < links; ++i) {
        at = link_table + (size_t)i * 6;
        const uint8_t *p = bytes.data + at;
        link[i] = (qa_nav_source_link){.target = qa_load_u16le(p),
                                       .type = p[2],
                                       .stored_flags = p[3],
                                       .flags = nav3 && version < 3   ? 3
                                                : nav3 && version < 6 ? p[3] & ~12u
                                                                      : p[3],
                                       .traversal = qa_load_u16le(p + 4)};
        if (link[i].target >= (uint32_t)nodes ||
            (link[i].traversal != UINT16_MAX && link[i].traversal >= (uint32_t)hints))
            goto malformed;
    }
    for (int32_t i = 0; i < hints; ++i) {
        at = hint_table + (size_t)i * hint_stride;
        const uint8_t *p = bytes.data + at;
        hint[i] = (qa_nav_hint){.funnel = nav_vector(p),
                                .start = nav_vector(p + 12),
                                .end = nav_vector(p + 24),
                                .has_ladder_plane = hint_stride == 48};
        if (hint[i].has_ladder_plane)
            hint[i].ladder_plane = nav_vector(p + 36);
        if (!qa_vec_finite(hint[i].funnel) || !qa_vec_finite(hint[i].start) ||
            !qa_vec_finite(hint[i].end) || !qa_vec_finite(hint[i].ladder_plane))
            goto malformed;
    }
    for (int32_t i = 0; i < entity_count; ++i) {
        at = entities + (size_t)i * entity_stride;
        const uint8_t *p = bytes.data + at;
        entity[i].link = qa_load_u16le(p);
        p += 2;
        entity[i].has_model = nav3;
        if (nav3 && version >= 2) {
            entity[i].model = qa_load_i32le(p);
            p += 4;
        }
        entity[i].bounds = nav_bounds(p);
        p += 24;
        entity[i].tail_count = (uint8_t)tail_count;
        for (size_t word = 0; word < tail_count; ++word)
            entity[i].tail[word] = qa_load_i32le(p + word * 4);
        if (entity[i].link >= (uint32_t)links || !nav_bounds_valid(entity[i].bounds))
            goto malformed;
    }
    *out = asset;
    return true;
malformed:
    qa_nav_asset_release(asset);
    qa_error_set(e, QA_ERROR_FORMAT, at, "Invalid navigation record or reference");
    return false;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating navigation asset");
    return false;
}
