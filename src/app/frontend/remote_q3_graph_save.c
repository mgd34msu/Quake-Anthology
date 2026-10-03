#include "remote_q3_graph_private.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error,qa_status status,const char *text)
{ qa_error_set(error,status,0,"%s",text); return false; }
static bool geometry_fields(qa_source_save_io *io,qa_collision_portal_checkpoint *state)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint8_t magic[4]={'Q','R','C','G'}; uint32_t family=state->family,format=state->format;
    if (!qa_source_save_bytes(io,magic,sizeof(magic)) || memcmp(magic,"QRCG",sizeof(magic)) ||
        !qa_source_save_u32(io,&family) ||
        family!=QA_COLLISION_Q3 || !qa_source_save_u32(io,&format) ||
        !qa_source_save_u64(io,&state->map_identity) || !qa_source_save_u32(io,&state->area_count) ||
        !qa_source_save_bool(io,&state->no_areas) ||
        !qa_source_save_count(io,&state->area_pair_count,SIZE_MAX/sizeof(*state->area_pairs))) return false;
    state->family=(qa_collision_family)family; state->format=(qa_bsp_format)format;
    if (state->portal_count || (state->area_count &&
        (size_t)state->area_count>SIZE_MAX/(size_t)state->area_count) ||
        state->area_pair_count!=(size_t)state->area_count*(size_t)state->area_count) return false;
    if (reading) {
        if (io->offset>io->input.size || state->area_pair_count>(io->input.size-io->offset)/sizeof(uint32_t))
            return fail(io->error,QA_ERROR_FORMAT,"Remote collision matrix exceeds its complete envelope");
        if (state->area_pair_count) {
            state->area_pairs=calloc(state->area_pair_count,sizeof(*state->area_pairs));
            if (!state->area_pairs) return fail(io->error,QA_ERROR_MEMORY,"Retaining remote collision area references");
        }
    }
    if (state->area_pair_count && !state->area_pairs) return false;
    for (size_t i=0;i<state->area_pair_count;++i)
        if (!qa_source_save_u32(io,state->area_pairs+i)) return false;
    return true;
}
bool frontend_remote_q3_graph_geometry_checkpoint(const qa_collision_geometry *geometry,
    qa_buffer *out,qa_error *error)
{
    if (!geometry || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Remote collision capture requires its actual geometry and empty output");
    qa_collision_portal_checkpoint state={0}; qa_source_save_io io={0};
    bool okay=qa_collision_capture_portals(geometry,&state,error) &&
        qa_source_save_writer(&io,NULL,error) && geometry_fields(&io,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_collision_portal_checkpoint_free(&state);
    if (!okay && (!error || error->code==QA_OK))
        fail(error,QA_ERROR_FORMAT,"Remote collision capture is not a complete Q3 portal owner");
    return okay;
}
bool frontend_remote_q3_graph_geometry_restore(qa_collision_geometry *geometry,qa_bytes bytes,qa_error *error)
{
    if (!geometry) return fail(error,QA_ERROR_ARGUMENT,"Remote collision import requires its actual detached geometry");
    qa_collision_portal_checkpoint state={0}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && geometry_fields(&io,&state) &&
        qa_source_save_finish(&io,NULL) && qa_collision_restore_portals(geometry,&state,error);
    qa_source_save_dispose(&io); qa_collision_portal_checkpoint_free(&state);
    if (!okay && (!error || error->code==QA_OK))
        fail(error,QA_ERROR_FORMAT,"Invalid complete remote collision continuation");
    return okay;
}
bool frontend_remote_q3_graph_geometry_validate(qa_bytes bytes,qa_error *error)
{
    qa_collision_portal_checkpoint state={0}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && geometry_fields(&io,&state) &&
        qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); qa_collision_portal_checkpoint_free(&state);
    if(!okay && (!error || error->code==QA_OK))
        fail(error,QA_ERROR_FORMAT,"Invalid complete remote collision portal envelope");
    return okay;
}
