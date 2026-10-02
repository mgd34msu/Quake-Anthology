#include "guest_qc_bot_orders.h"
#include "bots_private.h"

static bool actor_read(struct application_qc_state *engine,qa_qc_instance *vm,
    uint32_t argument,qa_actor_id *out,bool *found,qa_error *error)
{
    int32_t reference;
    if (!qa_qc_arg_int(vm,argument,&reference,error)) return false;
    *found=false;
    qa_qc_entity_layout layout=qa_qc_default_entity_layout(engine->provider->state.qc.program,engine->profile);
    if (reference<0 || !layout.stride_bytes || (uint32_t)reference%layout.stride_bytes) return true;
    uint32_t slot=(uint32_t)reference/layout.stride_bytes;
    if (slot>=qa_qc_entity_count(vm)) return true;
    qa_qc_slot_binding binding; int32_t actual;
    if (!qa_qc_slot(vm,slot,&binding) || !qa_qc_slot_reference(vm,slot,&actual,error) || actual!=reference)
        return application_fail(error,QA_ERROR_FORMAT,"QC bot order lost its physical entity reference");
    if (binding.kind==QA_QC_SLOT_FREE) return true;
    const qa_actor_record *record=qa_actors_get(qa_session_actors(engine->services.session),binding.actor);
    if (!record) return true;
    if (binding.owner!=record->owner || binding.source_slot!=(record->has_source?record->source_slot:0))
        return application_fail(error,QA_ERROR_NOT_FOUND,"QC bot order changed its full canonical binding");
    *out=binding.actor; *found=true; return true;
}
bool application_qc_bot_order(struct application_qc_state *engine,qa_qc_instance *vm,
    bool follow,qa_error *error)
{
    if (!engine || !engine->provider || !engine->provider->application ||
        engine->profile!=QA_QC_RERELEASE || vm!=engine->provider->state.qc.instance)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC bot order requires its actual rerelease source host");
    qa_actor_id actor,target={0}; qa_vec3 point={0}; bool found;
    if (!actor_read(engine,vm,0,&actor,&found,error)) return false;
    if (!found) return qa_qc_return_float(vm,0,error);
    if (follow) {
        if (!actor_read(engine,vm,1,&target,&found,error)) return false;
        if (!found) return qa_qc_return_float(vm,0,error);
    } else if (!qa_qc_arg_vector(vm,1,&point,error)) return false;
    qa_application *app=engine->provider->application;
    application_bots *bots=app->bots;
    if (!bots || !bots->population) return qa_qc_return_float(vm,0,error);
    if (bots->application!=app)
        return application_fail(error,QA_ERROR_FORMAT,"QC bot order belongs to another actual population");
    qa_bot_view admitted; qa_error admission={0};
    if (!qa_bots_read(bots->population,actor,&admitted,&admission)) {
        if (admission.code==QA_ERROR_ARGUMENT) return qa_qc_return_float(vm,0,error);
        if (error) *error=admission;
        return false;
    }
    qa_bot_order_status status=QA_BOT_ORDER_ERROR;
    bool ok=follow?qa_bots_follow(bots->population,actor,target,&status,error):
        qa_bots_move_to(bots->population,actor,point,&status,error);
    _Static_assert(QA_BOT_ORDER_ERROR==0 && QA_BOT_ORDER_SUCCESS==1 && QA_BOT_ORDER_ACTIVE==2,
        "Native bot order statuses must match the rerelease source contract");
    return ok && qa_qc_return_float(vm,(float)status,error);
}
