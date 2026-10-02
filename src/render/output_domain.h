#ifndef QA_RENDER_OUTPUT_DOMAIN_H
#define QA_RENDER_OUTPUT_DOMAIN_H
#include "qa/scene.h"
#include "qa/source_save.h"
typedef struct qa_output_domain_region {
    qa_scene_rect rect;
    qa_scene_draw_buffer buffer;
    bool source;
} qa_output_domain_region;
typedef struct qa_output_domains {
    qa_output_domain_region *regions;
    size_t count, capacity;
    uint32_t width, height;
} qa_output_domains;
void qa_output_domains_extent(qa_output_domains *, uint32_t width, uint32_t height);
bool qa_output_domains_assign(qa_output_domains *, qa_scene_rect, qa_scene_draw_buffer,
    bool source, uint32_t width, uint32_t height, qa_error *);
bool qa_output_domains_source(const qa_output_domains *, uint32_t x, uint32_t y,
    qa_scene_draw_buffer);
bool qa_output_domains_rect_read(const qa_output_domains *, qa_scene_rect,
    qa_scene_draw_buffer, bool *, qa_error *);
bool qa_output_domains_codec(qa_source_save_io *, qa_output_domains *, uint32_t width, uint32_t height);
void qa_output_domains_destroy(qa_output_domains *);
#endif
