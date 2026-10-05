#ifndef QA_Q3_CINEMATIC_HANDLES_PRIVATE_H
#define QA_Q3_CINEMATIC_HANDLES_PRIVATE_H
#include "qa/q3_cinematic_handles.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct q3cin_movie {
    qa_q3_cinematic_source *source;
    qa_cinematic_asset *asset;
    qa_cinematic *playback;
    qa_q3_system_movie system;
    char *path;
    qa_scene_rect_f rect;
    uint32_t flags;
    uint64_t uploaded;
    uint64_t bus;
    bool pending;
    bool occupied;
    bool redefine;
    bool uploaded_shader;
    bool dirty;
    int32_t play_on_walls;
    int32_t status;
    uint32_t width,height,draw_width,draw_height;
} q3cin_movie;
struct qa_q3_cinematic_source {
    qa_q3_cinematic_handles *handles;
    qa_q3_cinematic_source_options options;
    qa_q3_cinematic_source *parent;
    uint64_t role_bus;
    void *diagnostic_context;
    void (*diagnostic_print)(void *,const char *);
    bool (*diagnostic_current)(void *,const qa_q3_cinematic_source *);
    struct qa_q3_cinematic_source *next;
    unsigned users;
};
struct qa_q3_cinematic_handles {
    qa_q3_cinematic_handles_options options;
    qa_q3_cinematic_source *sources;
    q3cin_movie movies[16];
    const qa_scene_image *scratch[16];
    qa_q3_cinematic_handles_stage *stage;
    qa_roq_scratch *decoder_scratch;
    int32_t selected_handle,decoder_handle;
    bool busy;
};
bool q3cin_fail(qa_error *,qa_status,const char *);
bool q3cin_enter(qa_q3_cinematic_source *,qa_error *);
bool q3cin_close(qa_q3_cinematic_handles *,uint32_t,qa_cinematic_end,qa_error *);
qa_cinematic_options q3cin_options(const qa_q3_cinematic_source *,uint32_t flags,uint64_t bus);
bool q3cin_scratch_valid(const qa_q3_cinematic_handles *,uint32_t,const qa_scene_image *);
bool q3cin_stage_retains_source(const qa_q3_cinematic_handles_stage *,const qa_q3_cinematic_source *);
bool q3cin_play_into(qa_q3_cinematic_source *,q3cin_movie slots[16],qa_media_library *,qa_roq_scratch *,int32_t *,int32_t *,
    const char *,qa_scene_rect_f,uint32_t,
    bool (*)(void *,const qa_q3_movie_request *,qa_q3_system_movie *,qa_error *),void *,int32_t *,qa_error *);
#endif
