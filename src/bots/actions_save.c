#include "actions_private.h"
#include "save_fields.h"
#include "qa/bot_actions_save.h"
#include <limits.h>

static const uint8_t source_magic[8]={'Q','A','E','A','S','R','C',0};
static bool input_fields(qa_source_save_io *io,qa_bot_input *input) {
    return qa_source_save_f32(io,&input->think_time) && qa_source_save_vec3(io,&input->direction) &&
        qa_source_save_f32(io,&input->speed) && qa_source_save_vec3(io,&input->view_angles) &&
        qa_source_save_u32(io,&input->action_flags) && qa_source_save_i32(io,&input->weapon);
}
bool qa_bot_actions_source_capture(const qa_bot_actions *actions,qa_buffer *out,qa_error *error) {
    if(!actions || !out || !qa_bot_actions_idle(actions)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Action capture requires its idle source owner and output");return false;
    }
    bool initialized=actions->initialized;uint32_t capacity=actions->capacity;
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && bot_save_signature(&io,source_magic) &&
        qa_source_save_bool(&io,&initialized) && qa_source_save_u32(&io,&capacity);
    for(uint32_t client=0;ok && initialized && client<capacity;++client) {
        qa_bot_input input;
        ok=qa_bot_actions_read(actions,client,&input,error) && input_fields(&io,&input);
    }
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}
bool qa_bot_actions_source_restore(qa_bot_actions *actions,qa_bytes bytes,qa_error *error) {
    if(!actions || !qa_bot_actions_idle(actions) || actions->initialized) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Action restore requires its empty idle source owner");return false;
    }
    bool initialized=false;uint32_t capacity=0;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && bot_save_signature(&io,source_magic) &&
        qa_source_save_bool(&io,&initialized) && qa_source_save_u32(&io,&capacity) && capacity<=INT32_MAX/40;
    if(ok && initialized && capacity>(io.input.size-io.offset)/40)
        ok=bot_save_fail(&io,QA_ERROR_FORMAT,"Truncated saved bot action inputs");
    if(ok && initialized) ok=qa_bot_actions_setup(actions,capacity,error);
    if(ok && !initialized) actions->capacity=capacity;
    for(uint32_t client=0;ok && initialized && client<capacity;++client) {
        qa_bot_input input={0};ok=input_fields(&io,&input) && qa_bot_actions_restore(actions,client,&input,error);
    }
    if(ok) ok=qa_source_save_finish(&io,NULL);
    if(!ok && (!error || error->code==QA_OK)) bot_save_fail(&io,QA_ERROR_FORMAT,"Invalid saved bot action state");
    qa_source_save_dispose(&io);return ok;
}
bool qa_bot_actions_capture(const qa_bot_actions *actions,qa_buffer *out,qa_error *error) {
    return qa_bot_actions_source_capture(actions,out,error);
}
bool qa_bot_actions_restore_bytes(qa_bot_actions *actions,qa_bytes bytes,qa_error *error) {
    if(!actions || !actions->owns_memory || !qa_bot_actions_idle(actions)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Standalone action restore requires an idle owned allocator");return false;
    }
    qa_bot_actions *candidate=NULL;
    bool ok=qa_bot_actions_create_source(NULL,&actions->services,&candidate,error) &&
        qa_bot_actions_source_restore(candidate,bytes,error);
    if(ok) {
        qa_bot_memory *previous=actions->memory;actions->memory=candidate->memory;candidate->memory=previous;
        actions->inputs=candidate->inputs;actions->capacity=candidate->capacity;actions->initialized=candidate->initialized;
    }
    qa_bot_actions_destroy(candidate);return ok;
}
