#include "output_domain.h"
#include <stdlib.h>
#include <string.h>

static bool valid(qa_scene_rect r, qa_scene_draw_buffer buffer, uint32_t width, uint32_t height)
{
    return buffer >= QA_DRAW_FRONT && buffer <= QA_DRAW_BACK_RIGHT && r.x >= 0 && r.y >= 0 && r.width && r.height &&
        (uint64_t)(uint32_t)r.x + r.width <= width && (uint64_t)(uint32_t)r.y + r.height <= height;
}
static bool covers(qa_scene_rect a, qa_scene_rect b)
{
    return a.x <= b.x && a.y <= b.y && (uint64_t)(uint32_t)a.x + a.width >=
        (uint64_t)(uint32_t)b.x + b.width && (uint64_t)(uint32_t)a.y + a.height >=
        (uint64_t)(uint32_t)b.y + b.height;
}
void qa_output_domains_extent(qa_output_domains *owner,uint32_t width,uint32_t height)
{
    if (owner->width!=width || owner->height!=height) owner->count=0;
    owner->width=width; owner->height=height;
}
bool qa_output_domains_assign(qa_output_domains *owner, qa_scene_rect rect,
    qa_scene_draw_buffer buffer, bool source, uint32_t width, uint32_t height, qa_error *error)
{
    if (!owner || !valid(rect, buffer, width, height)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Output color domain requires its real framebuffer region"); return false;
    }
    qa_output_domains_extent(owner,width,height);
    if (owner->count == owner->capacity) {
        size_t capacity = owner->capacity ? owner->capacity * 2 : 8;
        if (capacity < owner->capacity || capacity > SIZE_MAX / sizeof(*owner->regions)) {
            qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "Output color domain region storage overflow"); return false;
        }
        qa_output_domain_region *regions = realloc(owner->regions, capacity * sizeof(*regions));
        if (!regions) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining output color domains"); return false; }
        owner->regions = regions; owner->capacity = capacity;
    }
    size_t kept = 0;
    for (size_t i = 0; i < owner->count; ++i)
        if (owner->regions[i].buffer != buffer || !covers(rect, owner->regions[i].rect))
            owner->regions[kept++] = owner->regions[i];
    owner->regions[kept++] = (qa_output_domain_region){rect, buffer, source};
    owner->count = kept;
    return true;
}
bool qa_output_domains_source(const qa_output_domains *owner, uint32_t x, uint32_t y,
    qa_scene_draw_buffer buffer)
{
    for (size_t i = owner->count; i; --i) {
        const qa_output_domain_region *r = owner->regions + i - 1;
        if (r->buffer == buffer && x >= (uint32_t)r->rect.x && y >= (uint32_t)r->rect.y &&
            x - (uint32_t)r->rect.x < r->rect.width && y - (uint32_t)r->rect.y < r->rect.height) return r->source;
    }
    return false;
}
bool qa_output_domains_rect_read(const qa_output_domains *owner, qa_scene_rect rect,
    qa_scene_draw_buffer buffer, bool *out, qa_error *error)
{
    if (!owner || !out || rect.x<0 || rect.y<0 || !rect.width || !rect.height ||
        buffer<QA_DRAW_FRONT || buffer>QA_DRAW_BACK_RIGHT ||
        (owner->count && !valid(rect,buffer,owner->width,owner->height))) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Output domain read requires its actual framebuffer region"); return false;
    }
    uint64_t right=(uint64_t)(uint32_t)rect.x+rect.width, top=(uint64_t)(uint32_t)rect.y+rect.height;
    for (size_t i=owner->count;i;--i) {
        const qa_output_domain_region *region=owner->regions+i-1;
        const qa_scene_rect *r=&region->rect;
        if (region->buffer!=buffer || (uint32_t)r->x>=right || (uint32_t)r->y>=top ||
            (uint64_t)(uint32_t)r->x+r->width<=(uint32_t)rect.x ||
            (uint64_t)(uint32_t)r->y+r->height<=(uint32_t)rect.y) continue;
        if (covers(*r,rect)) { *out=region->source; return true; }
        break;
    }
    bool source=qa_output_domains_source(owner,(uint32_t)rect.x,(uint32_t)rect.y,buffer);
    /* A domain is constant between its retained rectangle boundaries. Reading
     * every boundary intersection proves the whole viewport without pixels,
     * allocation or native renderer entry. */
    for (size_t x=0;x<=owner->count*2;++x) {
        uint64_t px=(uint32_t)rect.x;
        if (x) { const qa_scene_rect *r=&owner->regions[(x-1)/2].rect;
            px=(uint32_t)r->x+((x-1)%2?(uint64_t)r->width:0); }
        if (px<(uint32_t)rect.x || px>=right) continue;
        for (size_t y=0;y<=owner->count*2;++y) {
            uint64_t py=(uint32_t)rect.y;
            if (y) { const qa_scene_rect *r=&owner->regions[(y-1)/2].rect;
                py=(uint32_t)r->y+((y-1)%2?(uint64_t)r->height:0); }
            if (py<(uint32_t)rect.y || py>=top) continue;
            if (qa_output_domains_source(owner,(uint32_t)px,(uint32_t)py,buffer)!=source) {
                qa_error_set(error,QA_ERROR_UNSUPPORTED,0,"Generic overlay spans different retained output color domains"); return false;
            }
        }
    }
    *out=source; return true;
}
bool qa_output_domains_codec(qa_source_save_io *io, qa_output_domains *owner,
    uint32_t width, uint32_t height)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (reading) { owner->width=width; owner->height=height; }
    size_t count = owner->count;
    if (!qa_source_save_count(io, &count, reading ? (io->input.size - io->offset) / 21 : SIZE_MAX / sizeof(*owner->regions))) return false;
    if (reading && count) {
        owner->regions = calloc(count, sizeof(*owner->regions));
        if (!owner->regions) { qa_error_set(io->error, QA_ERROR_MEMORY, 0, "Restoring output color regions"); return false; }
        owner->count = owner->capacity = count;
    }
    for (size_t i = 0; i < count; ++i) {
        qa_output_domain_region *r = owner->regions + i;
        uint32_t buffer = r->buffer;
        if (!qa_source_save_i32(io, &r->rect.x) || !qa_source_save_i32(io, &r->rect.y) ||
            !qa_source_save_u32(io, &r->rect.width) || !qa_source_save_u32(io, &r->rect.height) ||
            !qa_source_save_u32(io, &buffer) || buffer > QA_DRAW_BACK_RIGHT ||
            !qa_source_save_bool(io, &r->source)) return false;
        if (reading) r->buffer = (qa_scene_draw_buffer)buffer;
        if (!valid(r->rect, r->buffer, width, height)) return false;
    }
    return true;
}
void qa_output_domains_destroy(qa_output_domains *owner)
{ free(owner->regions); memset(owner, 0, sizeof(*owner)); }
