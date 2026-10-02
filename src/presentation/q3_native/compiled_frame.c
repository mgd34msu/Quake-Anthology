#include "compiled_frame.h"
#include "entity.h"

static bool fail(qa_error *e, const char *text)
{ qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", text); return false; }
static bool player(const qa_q3_player *p, qa_q3_product product)
{ return p && p->product == product && p->clientNum >= 0 && p->clientNum < 64; }
bool q3n_compiled_frame_current(const q3n_compiled_frame *f)
{
    if (!f || !f->owner || !f->context || !f->current || !f->entity || !f->entity_event ||
        !f->entity_trajectory || !f->entity_weapon || !f->trace_number || !f->entities || !f->revision ||
        f->stage < Q3N_COMPILED_INITIALIZATION || f->stage > Q3N_COMPILED_AWAITING_SNAPSHOT ||
        (f->stage == Q3N_COMPILED_COMPLETED_FRAME ? f->scope != 0 : f->scope == 0) ||
        !q3n_compiled_source_current(&f->source)) return false;
    qa_q3_product product = f->source.basis.product;
    if (f->stage != Q3N_COMPILED_INITIALIZATION && !f->source.basis.initialized) return false;
    if (f->next_snapshot && !f->snapshot) return false;
    if (f->snapshot && (!f->snapshot->valid || !player(&f->snapshot->player, product) ||
        f->snapshot->message_number > f->processed_snapshot)) return false;
    if (f->next_snapshot && (!f->next_snapshot->valid || !player(&f->next_snapshot->player, product) ||
        f->next_snapshot->message_number <= f->snapshot->message_number ||
        f->next_snapshot->message_number > f->processed_snapshot)) return false;
    if ((f->predicted_state == NULL) != (f->predicted_entity == NULL) ||
        (f->predicted_next_state == NULL) != (f->predicted_entity == NULL) ||
        (f->predicted_player == NULL) != (f->predicted_entity == NULL) ||
        (f->predicted_state && f->predicted_state == f->predicted_next_state)) return false;
    if (f->predicted_player && (!player(f->predicted_player, product) ||
        (f->snapshot && f->predicted_player == &f->snapshot->player) ||
        (f->next_snapshot && f->predicted_player == &f->next_snapshot->player))) return false;
    if (f->stage == Q3N_COMPILED_INITIALIZATION && (f->snapshot || f->next_snapshot || f->transition_player)) return false;
    if (f->stage == Q3N_COMPILED_COMPLETED_FRAME && !f->snapshot) return false;
    if (f->stage == Q3N_COMPILED_PLAYER_TRANSITION ?
        !player(f->transition_player, product) || !player(f->previous_player, product) :
        f->transition_player || f->previous_player) return false;
    if (f->stage == Q3N_COMPILED_AWAITING_SNAPSHOT && f->snapshot && !(f->snapshot->flags & 2)) return false;
    return f->current(f->context, f) && q3n_compiled_source_current(&f->source);
}
bool q3n_compiled_frame_read(const q3n_compiled_frame *options, q3n_compiled_frame *out, qa_error *e)
{
    if (!out || !q3n_compiled_frame_current(options)) return fail(e, "Compiled frame requires its real CLIENT cache and entered scope");
    *out = *options; return true;
}
bool q3n_compiled_frame_entity(const q3n_compiled_frame *f, uint32_t number, q3n_compiled_entity *out, qa_error *e)
{
    if (!out || number >= QA_Q3_ENTITY_NONE || !q3n_compiled_frame_current(f))
        return fail(e, "Compiled entity observation requires its current physical cache row");
    q3n_compiled_entity row = {0};
    if (!f->entity(f->context, f, number, &row, e)) return false;
    if (!row.current || !row.next || row.predicted || row.number != number ||
        row.presentation != &f->entities[number] || row.current_valid != row.presentation->valid ||
        (!row.published && row.current_valid) ||
        (row.published && (!q3n_compiled_source_actor_known(&f->source,row.actor) || row.current->number != (int32_t)number ||
            row.presentation->physical != number || !qa_actor_id_equal(row.actor, row.presentation->actor) ||
            row.snapshot_number > f->processed_snapshot)) || !q3n_compiled_frame_current(f))
        return fail(e, "Compiled entity differs from its actual full actor and cache binding");
    row.frame = f; *out = row; return true;
}
bool q3n_compiled_frame_predicted(const q3n_compiled_frame *f, q3n_compiled_entity *out, qa_error *e)
{
    if (!out || !q3n_compiled_frame_current(f) || !f->predicted_entity ||
        f->predicted_state->number < 0 || f->predicted_state->number >= QA_Q3_ENTITY_NONE ||
        !qa_actor_id_equal(f->predicted_entity->actor, f->source.basis.viewer))
        return fail(e, "Compiled predicted entity requires its actual viewer continuation");
    *out = (q3n_compiled_entity){.frame=f,.current=f->predicted_state,.next=f->predicted_next_state,
        .presentation=f->predicted_entity,.actor=f->source.basis.viewer,.number=(uint32_t)f->predicted_state->number,
        .current_valid=f->predicted_entity->valid,.predicted=true}; return true;
}
bool q3n_compiled_entity_current(const q3n_compiled_entity *row)
{
    if (!row) return false;
    q3n_compiled_entity actual;
    bool ok = row->predicted ? q3n_compiled_frame_predicted(row->frame,&actual,NULL) :
        q3n_compiled_frame_entity(row->frame,row->number,&actual,NULL);
    return ok && row->current==actual.current && row->next==actual.next && row->presentation==actual.presentation &&
        qa_actor_id_equal(row->actor,actual.actor) && row->snapshot_number==actual.snapshot_number &&
        row->published==actual.published && row->current_valid==actual.current_valid && row->interpolate==actual.interpolate;
}
bool q3n_compiled_frame_entity_event(const q3n_compiled_frame *f,uint32_t number,int32_t event,int32_t parameter,qa_error *e)
{
    q3n_compiled_entity row;
    return q3n_compiled_frame_entity(f,number,&row,e) && row.published &&
        f->entity_event(f->context,f,number,event,parameter,e) &&
        (q3n_compiled_entity_current(&row) || fail(e,"Compiled event store retired its cache binding"));
}
bool q3n_compiled_frame_entity_trajectory(const q3n_compiled_entity *row,int32_t cb,int32_t nb,int32_t ca,int32_t na,qa_error *e)
{
    if (!q3n_compiled_entity_current(row) || (!row->predicted && !row->published) ||
        row->current->pos.type!=cb || row->next->pos.type!=nb) return fail(e,"Compiled trajectory store lost its expected cache values");
    return row->frame->entity_trajectory(row->frame->context,row,cb,nb,ca,na,e) &&
        ((q3n_compiled_entity_current(row) && row->current->pos.type==ca && row->next->pos.type==na) ||
            fail(e,"Compiled trajectory store differs from its actual cache result"));
}
bool q3n_compiled_frame_entity_weapon(const q3n_compiled_entity *row,int32_t before,int32_t after,qa_error *e)
{
    if (!q3n_compiled_entity_current(row) || (!row->predicted && !row->published) || row->current->weapon!=before)
        return fail(e,"Compiled weapon store lost its expected cache value");
    return row->frame->entity_weapon(row->frame->context,row,before,after,e) &&
        ((q3n_compiled_entity_current(row) && row->current->weapon==after) || fail(e,"Compiled weapon store differs from its cache result"));
}
bool q3n_compiled_frame_prediction_error_clear(const q3n_compiled_frame *f,qa_error *e)
{
    return q3n_compiled_frame_current(f) && f->prediction_error_clear &&
        f->prediction_error_clear(f->context,f,e) &&
        (q3n_compiled_frame_current(f) || fail(e,"Compiled prediction error clear retired its frame"));
}
bool q3n_compiled_frame_trace_number(const q3n_compiled_frame *f,const qa_trace_result *hit,int32_t *out,qa_error *e)
{
    if (!hit || !out || !q3n_compiled_frame_current(f)) return fail(e,"Compiled trace requires its actual source collision witness");
    int32_t number;
    if (!f->trace_number(f->context,f,hit,&number,e)) return false;
    if (number<0 || number>QA_Q3_ENTITY_NONE || !q3n_compiled_frame_current(f)) return fail(e,"Compiled trace returned a retired or invalid source number");
    *out=number; return true;
}
