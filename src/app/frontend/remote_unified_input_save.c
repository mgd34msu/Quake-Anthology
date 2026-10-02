#include "remote_unified_input_save.h"
#include "remote_unified_input_private.h"
#include "remote_unified_private.h"
#include "remote_unified_save.h"
#include "qa/source_save.h"
#include <math.h>
#include <string.h>

typedef struct saved_input {
    uint32_t epoch,physical;
    uint64_t configuration,connection,namespace_revision;
    qa_sha256_digest descriptor;
    frontend_unified_command_builder builder,next;
    qa_seat_input_sample sample;
    qa_unified_input_batch command;
    uint64_t sequence,last_sequence;
    double time,pending_time,elapsed;
    bool submitted,has_sample,has_pending;
} saved_input;
static bool real(qa_source_save_io *io,double *v)
{ return qa_source_save_f64(io,v) && isfinite(*v); }
static bool scalar(qa_source_save_io *io,float *v)
{ return qa_source_save_f32(io,v) && isfinite(*v); }
static bool builder(qa_source_save_io *io,frontend_unified_command_builder *b)
{
    uint32_t kind=(uint32_t)b->kind;
    bool ok=qa_source_save_u32(io,&kind) && kind<=QA_MOVEMENT_Q3 &&
        real(io,&b->angles.x) && real(io,&b->angles.y) && real(io,&b->angles.z) &&
        real(io,&b->mouse_x) && real(io,&b->mouse_y) && real(io,&b->drift_velocity) &&
        real(io,&b->drift_seconds) && qa_source_save_bool(io,&b->drifting) &&
        qa_source_save_bool(io,&b->previous_mouse_look);
    if(ok) b->kind=(qa_movement_kind)kind;
    return ok;
}
static bool sample(qa_source_save_io *io,qa_seat_input_sample *s)
{
    for(size_t i=0;i<QA_INPUT_ACTION_COUNT;++i)
        if(!scalar(io,&s->buttons[i].fraction) || s->buttons[i].fraction<0 || s->buttons[i].fraction>1 ||
            !qa_source_save_bool(io,&s->buttons[i].active) || !qa_source_save_bool(io,&s->buttons[i].pressed)) return false;
    return scalar(io,&s->mouse.x) && scalar(io,&s->mouse.y) &&
        scalar(io,&s->gamepad.move.x) && scalar(io,&s->gamepad.move.y) &&
        scalar(io,&s->gamepad.look_degrees.x) && scalar(io,&s->gamepad.look_degrees.y) &&
        real(io,&s->frame_ms) && s->frame_ms>0 && qa_source_save_bool(io,&s->game_focus) &&
        qa_source_save_bool(io,&s->any_key_down) && qa_source_save_u8(io,&s->impulse);
}
static bool command(qa_source_save_io *io,saved_input *s)
{
    qa_unified_document *doc=NULL;qa_buffer bytes={0};
    bool read=io->direction==QA_SOURCE_SAVE_READ;
    bool ok=read || (qa_unified_inputs_document(s->epoch,s->command.commands,1,&doc,io->error) &&
        qa_unified_document_encode(doc,&bytes,io->error));
    size_t size=bytes.size;
    if(ok) ok=qa_source_save_count(io,&size,SIZE_MAX);
    if(ok && read) {
        ok=size && size<=io->input.size-io->offset &&
            qa_unified_document_decode(QA_UNIFIED_INPUT_DOCUMENT,(qa_bytes){io->input.data+io->offset,size},&doc,io->error) &&
            qa_unified_inputs_read(doc,&s->command,io->error) && s->command.epoch==s->epoch && s->command.count==1;
        if(ok) io->offset+=size;
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,size);
    qa_unified_document_destroy(doc);qa_buffer_free(&bytes);return ok;
}
static bool fields(qa_source_save_io *io,saved_input *s)
{
    uint8_t tag[8]={'Q','U','I','P',2,0,0,0};
    const uint8_t expected[8]={'Q','U','I','P',2,0,0,0};
    if(!qa_source_save_bytes(io,tag,8) || memcmp(tag,expected,8) ||
        !qa_source_save_u32(io,&s->epoch) || !s->epoch || !qa_source_save_u32(io,&s->physical) ||
        !qa_source_save_u64(io,&s->configuration) || !qa_source_save_u64(io,&s->connection) ||
        !qa_source_save_u64(io,&s->namespace_revision) || !s->namespace_revision ||
        !qa_source_save_bytes(io,s->descriptor.bytes,sizeof(s->descriptor.bytes)) ||
        !builder(io,&s->builder) || !real(io,&s->time) ||
        !qa_source_save_bool(io,&s->submitted) || !qa_source_save_u64(io,&s->last_sequence) ||
        s->last_sequence>QA_UNIFIED_SAFE_INTEGER || (!s->submitted && s->last_sequence) ||
        !qa_source_save_bool(io,&s->has_sample) || !qa_source_save_bool(io,&s->has_pending) ||
        (s->has_pending && !s->has_sample)) return false;
    if(!s->has_sample) return true;
    if(!qa_source_save_u64(io,&s->sequence) || s->sequence>QA_UNIFIED_SAFE_INTEGER ||
        (s->submitted && s->sequence<=s->last_sequence) || !real(io,&s->elapsed) || s->elapsed<0 ||
        !sample(io,&s->sample)) return false;
    if(!s->has_pending) return true;
    return builder(io,&s->next) && s->next.kind==s->builder.kind && real(io,&s->pending_time) &&
        command(io,s) && s->command.commands[0].sequence==s->sequence &&
        s->command.commands[0].command.kind==s->builder.kind;
}
bool frontend_unified_input_checkpoint(const frontend_unified_input *p,qa_buffer *out,qa_error *e)
{
    if(!p || !out || out->data || !frontend_unified_input_idle(p) ||
        !frontend_client_source_current(&p->client_view) || !frontend_neutral_config_current(&p->configuration) ||
        !frontend_remote_unified_checkpoint_current(p->replica,e) || p->epoch!=frontend_remote_unified_epoch(p->replica)) return false;
    saved_input s={.epoch=p->epoch,.physical=p->configuration.physical_seat,
        .configuration=p->client_view.source.configuration_generation,.connection=p->client_view.source.connection_epoch,
        .namespace_revision=p->configuration.namespace_revision,
        .descriptor=p->client_view.source.descriptor->identity,.builder=p->builder,.next=p->pending_builder,
        .sample=p->retained_sample,.sequence=p->retained_sequence,.last_sequence=p->last_sequence,
        .time=p->command_time,.pending_time=p->pending_time,.elapsed=p->retained_elapsed,
        .submitted=p->submitted,.has_sample=p->has_sample,.has_pending=p->has_pending};
    s.command.epoch=p->epoch;s.command.count=p->has_pending?1u:0u;s.command.commands[0]=p->pending;
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,e) && fields(&io,&s) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}
bool frontend_unified_input_restore(qa_frontend *f,frontend_remote_unified *replica,
    frontend_remote_unified_prediction *prediction,qa_bytes bytes,frontend_unified_input **out,qa_error *e)
{
    if(!out || *out) return false;
    saved_input s={0};qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,&s) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    frontend_unified_input *p=NULL;
    if(ok) ok=frontend_input_import_create(f,replica,prediction,&p,e);
    if(ok) ok=p->epoch==s.epoch && p->configuration.physical_seat==s.physical && p->builder.kind==s.builder.kind &&
        p->client_view.source.configuration_generation==s.configuration && p->client_view.source.connection_epoch==s.connection &&
        p->configuration.namespace_revision==s.namespace_revision &&
        qa_sha256_equal(&p->client_view.source.descriptor->identity,&s.descriptor);
    if(ok && s.has_pending) {
        const qa_unified_input *v=s.command.commands;
        size_t length=strlen(p->arsenal->selection.instance);
        ok=v->has_arsenal && v->arsenal.provider.size==length &&
            !memcmp(v->arsenal.provider.data,p->arsenal->selection.instance,length) && !v->arsenal.weapon.size;
    }
    if(ok) {
        p->builder=s.builder;p->pending_builder=s.next;p->retained_sample=s.sample;
        p->retained_sequence=s.sequence;p->last_sequence=s.last_sequence;p->command_time=s.time;
        p->pending_time=s.pending_time;p->retained_elapsed=s.elapsed;p->submitted=s.submitted;
        p->has_sample=s.has_sample;p->has_pending=s.has_pending;
        if(s.has_pending) {
            p->pending=s.command.commands[0];
            p->pending.arsenal.provider=(qa_bytes){(const unsigned char *)p->arsenal->selection.instance,
                strlen(p->arsenal->selection.instance)};
            p->pending.arsenal.weapon=(qa_bytes){0};
        }
        *out=p;p=NULL;
    }
    if(p) (void)frontend_unified_input_destroy(&p,NULL);
    qa_unified_inputs_free(&s.command);
    if(!ok && (!e || e->code==QA_OK)) frontend_unified_fail(e,QA_ERROR_FORMAT,"Unified physical input leaves its retained CLIENT recipe");
    return ok;
}
