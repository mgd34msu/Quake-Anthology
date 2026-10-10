#include "guest_q3_scene_private.h"

static qa_command_result declared_command(void *,const qa_command_invocation *,qa_error *);
#include "qa/builtin.h"
#include <limits.h>

static void *command_allocate(void *context, size_t size, size_t alignment, qa_error *error)
{ return qa_unified_frame_lease_alloc(context, size, 1, alignment, error); }

bool q3scene_fail(qa_error *e, qa_status code, const char *text)
{ qa_error_set(e, code, 0, "%s", text); return false; }
bool q3scene_current(const application_q3_scene *s)
{
    if(!s||s->failed||!s->options.source.current(s->options.source.context,&s->context)) return false;
    if(s->active_event) {
        qa_actor_id actor_id; bool owned,found; qa_error error={0};
        if(!s->options.source.live(s->options.source.context,s->active_event->actor)||
            !s->options.source.actor(s->options.source.context,(uint32_t)s->active_event->player.clientNum,&actor_id,&owned,&found,&error)||
            !found||!qa_actor_id_equal(actor_id,s->active_event->actor)) return false;
    }
    return true;
}
static bool argument(const qa_qvm_call *call, size_t i, int32_t *v, qa_error *e)
{ return qa_qvm_call_argument(call, i + 1, v, e); }
static bool store(application_q3_scene *s, uint32_t at, int32_t v, qa_error *e)
{ uint8_t b[4]; qa_store_u32le(b, (uint32_t)v); return qa_qvm_write(s->vm, at, (qa_bytes){b, 4}, e); }
static bool words(application_q3_scene *s, uint32_t at, const float *values, size_t count, qa_error *e)
{
    uint8_t b[36];
    for (size_t i = 0; i < count; ++i) { uint32_t word; memcpy(&word, values + i, 4); qa_store_u32le(b + i * 4, word); }
    return qa_qvm_write(s->vm, at, (qa_bytes){b, count * 4}, e);
}
static bool actor(void *context, uint32_t slot, qa_actor_id *out, bool *owned, bool *found, qa_error *e)
{
    application_q3_scene *s = context; *out = (qa_actor_id){0}; *owned = *found = false;
    if (!q3scene_current(s)) return q3scene_fail(e, QA_ERROR_ARGUMENT, "Component scene actor source is stale");
    for (size_t i = 0; i < s->actor_count; ++i) if (s->actors[i].slot == slot) {
        qa_actor_id actual; bool actual_owned, actual_found;
        if (!s->options.source.actor(s->options.source.context,slot,&actual,&actual_owned,&actual_found,e)) return false;
        *out = s->actors[i].actor; *owned = s->actors[i].owned;
        *found = actual_found && actual_owned == *owned && qa_actor_id_equal(actual,*out) &&
            s->options.source.live(s->options.source.context, *out); break;
    }
    return true;
}
static bool live(void *context, qa_actor_id id)
{ application_q3_scene *s = context; return q3scene_current(s) && s->options.source.live(s->options.source.context, id); }
static bool body_current(void *context, uint64_t sequence, int32_t time)
{ application_q3_scene *s = context; return s->frame_present && s->frame == sequence &&
    s->body_context.time_ms == time && !s->failed &&
    s->options.source.current(s->options.source.context,&s->body_context); }
static bool source_entity(void *context, const qa_qvm_call *call, int32_t pointer,
    const qa_q3_ref_entity *ref, bool *suppress, qa_error *e)
{ application_q3_scene *s = context; if(!s->body) { *suppress=false; return true; } return application_q3_component_body_source_entity(s->body, call, pointer, ref, suppress, e); }
static bool event_hook(void *context, const qa_qvm_call *call, int32_t *result, qa_error *e)
{
    application_q3_scene *s = context; const application_q3_scene_profile *p = s->options.profile;
    if (s->restoring_scene) {
        int32_t ptr; qa_bytes bytes;
        if (!qa_qvm_call_argument(call, p->event_argument, &ptr, e) || ptr < 0 ||
            (uint64_t)(uint32_t)ptr + p->state + 184 > qa_qvm_memory_size(s->vm) ||
            !qa_qvm_span(s->vm, ptr, p->state, 184, &bytes, e)) return false;
        int32_t type = qa_load_i32le(bytes.data + 4), event = qa_load_i32le(bytes.data + 180);
        if (!store(s, (uint32_t)ptr + p->previous_event, type > (int32_t)p->event_type ? 1 : event, e)) return false;
    }
    return qa_qvm_proceed(call, result, e);
}
static bool syscall(void *context, const qa_qvm_call *call, int32_t trap, int32_t *result, qa_error *e)
{
    application_q3_scene *s = context;
    if (!q3scene_current(s)) return q3scene_fail(e, QA_ERROR_ARGUMENT, "Component scene syscall lost its actual source");
    int32_t code; bool engine;
    if (!qa_qvm_classify_syscall(QA_QVM_CGAME, s->options.profile->abi, trap, &code, &engine, e)) return false;
    if (!engine) return s->lower.syscall(s->lower.context, call, trap, result, e);
    if(s->options.profile->player_events&&code!=50&&(code<7||code>9))
        return s->lower.syscall(s->lower.context,call,trap,result,e);
    *result = 0;
    if (code == 50) {
        int32_t ptr; return argument(call, 0, &ptr, e) && qa_qvm_write_gamestate(s->vm, ptr, true, s->game_state, e);
    }
    if (code == 51) {
        int32_t a, b;
        if (!argument(call, 0, &a, e) || !argument(call, 1, &b, e)) return false;
        int32_t time = s->snapshot_number ? s->snapshots[(uint32_t)s->snapshot_number % 32].value.server_time : 0;
        return store(s, qa_qvm_mask_address(s->vm, a), s->snapshot_number, e) && store(s, qa_qvm_mask_address(s->vm, b), time, e);
    }
    if (code == 52) {
        int32_t number, ptr;
        if (!argument(call, 0, &number, e) || !argument(call, 1, &ptr, e)) return false;
        if (number > s->snapshot_number) return q3scene_fail(e, QA_ERROR_FORMAT, "Component requested a future source snapshot");
        q3scene_snapshot *row = number > 0 ? s->snapshots + (uint32_t)number % 32 : NULL;
        if (!row || row->number != number) return true;
        bool ok = qa_qvm_write_snapshot(s->vm, ptr, true, &row->value, 0, e); *result = ok; return ok;
    }
    if (code == 53) {
        int32_t seq;
        if (!argument(call, 0, &seq, e)) return false;
        q3scene_command *row = seq >= 0 ? s->commands + (uint32_t)seq % 64 : NULL;
        if (!row || row->sequence != seq) return q3scene_fail(e, QA_ERROR_FORMAT, "Component requested an unavailable source command");
        if (!qa_unified_frame_lease_retain(row->lease, e)) return false;
        qa_command_tokens_free(&s->reached); qa_unified_frame_lease_release(s->reached_lease);
        s->reached = row->tokens; s->reached_lease = row->lease;
        *result = s->reached.count != 0; return true;
    }
    if (code >= 7 && code <= 9) {
        const qa_command_tokens *t = s->lexical ? s->lexical : &s->reached;
        if (code == 7) { *result = (int32_t)t->count; return true; }
        int32_t index = 0, ptr, cap;
        if (code == 8 && !argument(call, 0, &index, e)) return false;
        if (!argument(call, code == 8 ? 1 : 0, &ptr, e) || !argument(call, code == 8 ? 2 : 1, &cap, e)) return false;
        if (cap < 0) return q3scene_fail(e, QA_ERROR_ARGUMENT, "Component argument destination has a negative extent");
        char joined[1024];
        const char *value;
        if (code == 9) {
            size_t used = 0;
            for (size_t i = 1; i < t->count; ++i) {
                size_t size = strlen(t->values[i]), separator = i > 1 ? 1u : 0u;
                if (separator + size >= sizeof(joined) - used)
                    return q3scene_fail(e, QA_ERROR_FORMAT, "Cmd_Args exceeds its source buffer");
                if (separator) joined[used++] = ' ';
                memcpy(joined + used, t->values[i], size); used += size;
            }
            joined[used] = 0; value = joined;
        } else value = index >= 0 && (size_t)index < t->count ? t->values[index] : "";
        return qa_qvm_write_string(s->vm, ptr, (qa_bytes){(const uint8_t *)value, strlen(value)}, (size_t)cap, e);
    }
    return s->lower.syscall(s->lower.context, call, trap, result, e);
}
static bool host_checkpoint(void *context, qa_buffer *out, qa_error *e)
{ application_q3_scene *s = context; return s->lower.checkpoint(s->lower.context, out, e); }
static bool host_restore(void *context, qa_bytes bytes, qa_error *e)
{ application_q3_scene *s = context; return s->lower.restore(s->lower.context, bytes, e); }
bool application_q3_scene_idle(const application_q3_scene *s)
{ return s && !s->busy && !s->acquired && !s->lexical && (!s->vm || qa_qvm_can_destroy(s->vm)) &&
    (!s->host || qa_q3_host_destroy_ready(s->host)) && (!s->body || application_q3_component_body_idle(s->body)); }
bool application_q3_scene_create(const application_q3_scene_options *options,
    application_q3_scene **out, qa_error *e)
{
    if (!options || !out || *out || !options->profile || !options->assets ||
        options->host.role != QA_QVM_CGAME || options->host.abi != options->profile->abi || !options->host.owner ||
        !options->source.acquire || !options->source.current || !options->source.actor || !options->source.live || !options->source.release ||
        !options->viewer.registry || options->host.source_entity)
        return q3scene_fail(e, QA_ERROR_ARGUMENT, "Component CGAME requires its actual profile, assets and retained source");
    application_q3_scene *s = calloc(1, sizeof(*s));
    if (!s) return q3scene_fail(e, QA_ERROR_MEMORY, "Retaining external component CGAME executor");
    s->options = *options; s->revision = s->scene_revision = -1;
    for (size_t i = 0; i < 64; ++i) s->commands[i].sequence = -1;
    *out = s;
    s->game_state = calloc(1, sizeof(*s->game_state));
    s->players = calloc(options->profile->capacity, sizeof(*s->players));
    if (!s->game_state || !s->players) return q3scene_fail(e, QA_ERROR_MEMORY, "Retaining component source context");
    if (!options->profile->player_events) {
        s->actors = calloc(options->profile->capacity, sizeof(*s->actors));
        s->snapshot_entities = calloc((size_t)options->profile->capacity * 32, sizeof(*s->snapshot_entities));
        s->command_storage = qa_unified_frame_pool_create(16u * 1024u * 1024u, 128, e);
        if (!s->actors || !s->snapshot_entities || !s->command_storage)
            return q3scene_fail(e, QA_ERROR_MEMORY, "Reserving component scene history");
    }
    qa_q3_host_options host = options->host;
    host.console_command=declared_command; host.console_command_context=s;
    if(!options->profile->player_events) { host.source_entity = source_entity; host.source_entity_context = s; }
    if (!qa_q3_host_create(&host, &s->host, e)) return false;
    s->lower = qa_q3_host_qvm_options(s->host, QA_QVM_INTERPRETED);
    qa_qvm_options vm = s->lower; vm.context = s; vm.syscall = syscall; vm.checkpoint = host_checkpoint; vm.restore = host_restore;
    if (!qa_qvm_create(options->profile->image, &vm, &s->vm, e) || !qa_q3_host_attach_qvm(s->host, s->vm, e)) return false;
    if(options->profile->player_events) return true;
    application_q3_component_body_options body = {.vm = s->vm, .image = options->profile->image,
        .profile = &options->profile->body, .owner = host.owner, .assets = options->assets,
        .source = {.context = s, .actor = actor, .live = live, .current = body_current}};
    return application_q3_component_body_create(&body, &s->body, e) &&
        qa_qvm_bind_function(s->vm, options->profile->event_entry, true, event_hook, s, &s->event_binding, e);
}
static bool camera(application_q3_scene *s, qa_error *e)
{
    const application_q3_scene_profile *p = s->options.profile; const application_q3_scene_context *c = &s->context;
    if (c->time_ms < 0 || c->frame_ms < 0 || !qa_vec_finite(c->origin) ||
        !qa_vec_finite(c->axis[0]) || !qa_vec_finite(c->axis[1]) || !qa_vec_finite(c->axis[2]))
        return q3scene_fail(e, QA_ERROR_FORMAT, "Component camera or time is outside its source ABI");
    float origin[] = {c->origin.x,c->origin.y,c->origin.z}, axis[] = {c->axis[0].x,c->axis[0].y,c->axis[0].z,
        c->axis[1].x,c->axis[1].y,c->axis[1].z,c->axis[2].x,c->axis[2].y,c->axis[2].z};
    const double degrees = 57.295779513082320876;
    float angles[] = {(float)(atan2(-c->axis[0].z, hypot(c->axis[0].x,c->axis[0].y))*degrees),
        (float)(atan2(c->axis[0].y,c->axis[0].x)*degrees),0};
    qa_vec3 right, up; qa_builtin_angle_vectors(qa_v3(angles[0],angles[1],0), NULL, &right, &up);
    angles[2] = (float)(atan2(qa_vec_dot(c->axis[1],up),-qa_vec_dot(c->axis[1],right))*degrees);
    for (size_t i = 0; i < p->origin.count; ++i) if (!words(s,p->origin.rows[i],origin,3,e)) return false;
    for (size_t i = 0; i < p->angles.count; ++i) if (!words(s,p->angles.rows[i],angles,3,e)) return false;
    for (size_t i = 0; i < p->axis.count; ++i) if (!words(s,p->axis.rows[i],axis,9,e)) return false;
    return true;
}
static bool call_list(application_q3_scene *s, const q3scene_calls *list, qa_error *e)
{
    for (size_t i = 0; i < list->count; ++i) {
        const q3scene_call *call = list->rows + i;
        if (!q3scene_current(s)) return q3scene_fail(e, QA_ERROR_ARGUMENT, "Component caller lost its source publication");
        if (call->weapon_presented) {
            bool presented;
            if (!s->options.source.weapon_presented) return q3scene_fail(e, QA_ERROR_UNSUPPORTED, "Component weapon condition has no actual source producer");
            if (!s->options.source.weapon_presented(s->options.source.context,s->options.viewer,&presented,e)) return false;
            if (!q3scene_current(s)) return q3scene_fail(e, QA_ERROR_ARGUMENT, "Component weapon selection changed during its caller");
            if (!presented) continue;
        }
        if (!camera(s,e)) return false;
        int32_t args[62], result;
        for (size_t j = 0; j < call->count; ++j) {
            const q3scene_argument *a = call->arguments + j;
            switch (a->kind) {
            case Q3SCENE_LITERAL: args[j]=a->word; break;
            case Q3SCENE_CLIENT: args[j]=s->context.client_number; break;
            case Q3SCENE_TIME: args[j]=s->context.time_ms; break;
            case Q3SCENE_SNAPSHOT: args[j]=s->options.profile->player_events?0:s->snapshot_number?s->snapshot_number-1:0; break;
            case Q3SCENE_COMMAND_SEQUENCE: args[j]=s->options.profile->player_events?0:s->context.snapshot->server_command_number; break;
            case Q3SCENE_PLAYER_STATE: args[j]=(int32_t)s->options.profile->player_state; break;
            case Q3SCENE_SNAPSHOT_ADDRESS: args[j]=(int32_t)s->options.profile->snapshot_address; break;
            case Q3SCENE_ENTITY_STATE: case Q3SCENE_CENTITY: case Q3SCENE_ORIGIN: case Q3SCENE_EVENT: case Q3SCENE_PARAMETER:
                if(!s->active_event) return q3scene_fail(e,QA_ERROR_ARGUMENT,"Original presentation call lacks its actual Source player event");
                { uint32_t address=s->options.profile->entities+(uint32_t)s->active_event->player.clientNum*s->options.profile->stride;
                    args[j]=a->kind==Q3SCENE_EVENT?s->active_event->event:a->kind==Q3SCENE_PARAMETER?s->active_event->parameter:
                        (int32_t)(address+(a->kind==Q3SCENE_ENTITY_STATE?s->options.profile->state:a->kind==Q3SCENE_ORIGIN?s->options.profile->entity_origin:0));
                } break;
            }
        }
        if (!qa_qvm_invoke(s->vm,call->entry,args,call->count,&result,e) || !q3scene_current(s))
            return e && e->code != QA_OK ? false : q3scene_fail(e, QA_ERROR_ARGUMENT, "Component caller changed its retained source");
    }
    return true;
}
static bool acquire(application_q3_scene *s, bool baseline, qa_error *e)
{
    application_q3_scene_context c = {0};
    if (!s->options.source.acquire(s->options.source.context,baseline,&c,e)) return false;
    s->context=c; s->acquired=true;
    if (!q3scene_current(s) || !c.game_state || !c.snapshot || c.game_state_revision<0 ||
        c.game_state_revision<s->revision || c.revision<0 || c.revision<s->scene_revision || c.time_ms<0 || c.frame_ms<0 ||
        c.client_number<0 || (uint32_t)c.client_number>=s->options.profile->capacity ||
        (!s->options.profile->player_events&&c.actor_count>s->options.profile->capacity) || (c.actor_count&&!c.actors) || (c.command_count&&!c.commands) ||
        c.snapshot->entity_count>256 || (c.snapshot->entity_count&&!c.snapshot->entities))
        return q3scene_fail(e, QA_ERROR_FORMAT, "Component publication is incomplete or moved backwards");
    *s->game_state=*c.game_state;
    if (c.game_state_revision!=s->revision && !qa_qvm_write_gamestate(s->vm,(int32_t)s->options.profile->game_state,true,s->game_state,e)) return false;
    for (size_t i=0;i<s->options.profile->time.count;++i)
        if(!store(s,s->options.profile->time.rows[i],c.time_ms,e)) return false;
    for (size_t i=0;i<s->options.profile->frame_time.count;++i)
        if(!store(s,s->options.profile->frame_time.rows[i],c.frame_ms,e)) return false;
    if(s->options.profile->player_events) {
        const application_q3_scene_profile *p=s->options.profile;
        qa_q3_snapshot synthetic={.valid=true,.server_time=c.snapshot->server_time,.player=c.snapshot->player};
        if(!qa_qvm_write_snapshot(s->vm,(int32_t)p->snapshot_address,true,&synthetic,0,e)||
            !qa_qvm_write_player(s->vm,(int32_t)p->player_state,true,false,&c.snapshot->player,e)) return false;
        for(size_t i=0;i<p->snapshot_pointers.count;++i) if(!store(s,p->snapshot_pointers.rows[i],(int32_t)p->snapshot_address,e)) return false;
    }
    return camera(s,e);
}
bool application_q3_scene_entered_context(const application_q3_scene *s,application_q3_scene_context *out)
{
    if(!s||!out||!s->busy||!s->acquired||!q3scene_current(s)) return false;
    *out=s->context; return true;
}
bool application_q3_scene_retained_context(const application_q3_scene *s,application_q3_scene_context *out)
{
    if(!s||!out||s->busy||s->acquired||s->failed||!s->initialized) return false;
    *out=s->context; return true;
}
static void release(application_q3_scene *s)
{
    if (s->acquired) { s->acquired=false; s->options.source.release(s->options.source.context,&s->context); }
    s->context.game_state=s->game_state;
    s->context.snapshot=s->snapshot_number?&s->snapshots[(uint32_t)s->snapshot_number%32].value:NULL;
    s->context.actors=s->actors; s->context.actor_count=s->actor_count;
    s->context.commands=NULL; s->context.command_count=0;
    s->body_context.game_state=s->game_state;
    s->body_context.snapshot=s->context.snapshot;
    s->body_context.actors=s->actors; s->body_context.actor_count=s->actor_count;
    s->body_context.commands=NULL; s->body_context.command_count=0;
}
static bool finish_output(application_q3_scene *s,bool succeeded,qa_error *e)
{
    if(!s->options.finish_output) return succeeded;
    qa_error first=e?*e:(qa_error){0},cleanup={0};
    bool finished=s->options.finish_output(s->options.output_context,succeeded,&cleanup);
    if(!succeeded) { if(e) *e=first; return false; }
    if(!finished&&e) *e=cleanup;
    return finished;
}
static bool accept(application_q3_scene *s, bool baseline, bool *changed, qa_error *e)
{
    const application_q3_scene_profile *p=s->options.profile; application_q3_scene_context *c=&s->context;
    *changed=c->revision!=s->scene_revision;
    if (!*changed) return true;
    if(p->player_events) {
        if(s->snapshot_number==INT32_MAX) return q3scene_fail(e,QA_ERROR_FORMAT,"Component source snapshot counter exhausted");
        int32_t number=s->snapshot_number+1; q3scene_snapshot *row=s->snapshots+(uint32_t)number%32;
        *row=(q3scene_snapshot){.number=number,.value={.valid=true,.server_time=c->snapshot->server_time,.player=c->snapshot->player}};
        s->snapshot_number=number; s->scene_revision=c->revision;
        return true;
    }
    if (c->actor_count > p->capacity || c->snapshot->entity_count > p->capacity)
        return q3scene_fail(e, QA_ERROR_FORMAT, "Component scene leaves declared centities");
    for (size_t i=0;i<c->actor_count;++i) {
        uint32_t slot=c->actors[i].slot;
        if (slot>=p->capacity || !c->actors[i].actor.registry) return q3scene_fail(e,QA_ERROR_FORMAT,"Component actor leaves declared centities");
        for (size_t j=0;j<i;++j) if (c->actors[j].slot==slot) return q3scene_fail(e,QA_ERROR_FORMAT,"Component scene repeats an actor slot");
        if (!qa_actor_id_equal(s->players[slot],c->actors[i].actor)) {
            if (!qa_qvm_write(s->vm,p->entities+slot*p->stride,
                (qa_bytes){s->defaults.data+(size_t)slot*p->stride,p->stride},e)) return false;
            s->players[slot]=c->actors[i].actor;
        }
    }
    if (c->actor_count) memcpy(s->actors,c->actors,c->actor_count*sizeof(*s->actors));
    s->actor_count=c->actor_count;
    if (baseline&&!p->player_events) for (size_t i=0;i<c->snapshot->entity_count;++i) {
        const qa_q3_entity *entity=c->snapshot->entities+i;
        if (entity->number<0 || (uint32_t)entity->number>=p->capacity) return q3scene_fail(e,QA_ERROR_FORMAT,"Component baseline leaves centities");
        uint32_t address=p->entities+(uint32_t)entity->number*p->stride;
        if (!store(s,address+p->previous_event,entity->eType>(int32_t)p->event_type?1:entity->event,e) ||
            !store(s,address+p->snapshot_time,c->snapshot->server_time,e)) return false;
    }
    int32_t latest=c->snapshot->server_command_number;
    for (size_t i=0;i<c->command_count;++i) {
        const application_q3_scene_command *command=c->commands+i;
        if (command->sequence<0 || command->sequence>latest || !command->text) return q3scene_fail(e,QA_ERROR_FORMAT,"Component reliable sequence is invalid");
        if ((int64_t)command->sequence<=(int64_t)latest-64) continue;
        q3scene_command *row=s->commands+(uint32_t)command->sequence%64;
        if (row->sequence==command->sequence && row->addressed==command->addressed) continue;
        qa_unified_frame_lease *lease=qa_unified_frame_lease_acquire(s->command_storage,e);
        if (!lease) return false;
        qa_command_tokens t={0};
        bool okay;
        if (command->addressed&&command->arguments) {
            okay=qa_command_tokens_copy(command->arguments,&t,command_allocate,lease,e);
        } else okay=qa_command_tokenize(command->addressed?command->text:"",QA_RULESET_Q3,false,&t,command_allocate,lease,e);
        if (!okay) { qa_unified_frame_lease_release(lease); return false; }
        qa_command_tokens_free(&row->tokens); qa_unified_frame_lease_release(row->lease);
        *row=(q3scene_command){.sequence=command->sequence,.addressed=command->addressed,.tokens=t,.lease=lease};
    }
    for (size_t i=0;i<64;++i) if (s->commands[i].sequence>=0 &&
        (int64_t)s->commands[i].sequence<=(int64_t)latest-64) {
        qa_command_tokens_free(&s->commands[i].tokens); qa_unified_frame_lease_release(s->commands[i].lease);
        s->commands[i]=(q3scene_command){.sequence=-1};
    }
    if (s->snapshot_number==INT32_MAX) return q3scene_fail(e,QA_ERROR_FORMAT,"Component source snapshot counter exhausted");
    int32_t number=s->snapshot_number+1; q3scene_snapshot *row=s->snapshots+(uint32_t)number%32;
    qa_q3_entity *entities=s->snapshot_entities+(uint32_t)number%32*p->capacity;
    if (c->snapshot->entity_count) memcpy(entities,c->snapshot->entities,c->snapshot->entity_count*sizeof(*entities));
    *row=(q3scene_snapshot){.number=number,.value=*c->snapshot,.entities=entities}; row->value.entities=entities;
    s->snapshot_number=number; s->scene_revision=c->revision;
    return true;
}
bool application_q3_scene_initialize(application_q3_scene *s, qa_error *e)
{
    if (!application_q3_scene_idle(s) || s->initialized || s->failed)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component Init requires its fresh real executor");
    const application_q3_scene_profile *p=s->options.profile;
    s->busy=true; bool ok=acquire(s,!p->player_events,e);
    for (size_t i=0;ok&&i<p->cvar_count;++i) ok=qa_cvars_set(s->options.host.cvars,p->cvars[i].name,p->cvars[i].value,true,e);
    if (ok) ok=call_list(s,&p->initialize,e);
    if (ok) {
        s->defaults.size=(size_t)p->stride*p->capacity; s->defaults.data=malloc(s->defaults.size);
        if (!s->defaults.data) ok=q3scene_fail(e,QA_ERROR_MEMORY,"Retaining actual post-Init centity defaults");
        else ok=qa_qvm_read(s->vm,p->entities,s->defaults.data,s->defaults.size,e);
    }
    bool changed;
    if (ok) { s->revision=s->context.game_state_revision; ok=p->player_events||store(s,p->command_sequence,s->context.snapshot->server_command_number,e); }
    if (ok&&p->player_events) ok=accept(s,false,&changed,e);
    if (ok&&s->context.baseline&&!p->player_events) {
        ok=accept(s,true,&changed,e);
        if(ok) { s->restoring_scene=true; ok=call_list(s,&p->snapshots,e); s->restoring_scene=false; }
    }
    if(ok) ok=qa_q3_host_end_registration(s->host,e);
    ok=finish_output(s,ok,e);
    s->initialized=ok; s->failed=!ok; release(s); s->busy=false; return ok;
}
bool application_q3_scene_advance(application_q3_scene *s, uint64_t sequence, qa_error *e)
{
    if (!application_q3_scene_idle(s)||!s->initialized||s->failed)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component advance requires its initialized unborrowed executor");
    if (s->frame_present&&sequence<=s->frame) return true;
    s->busy=true; s->frame=sequence; s->frame_present=true;
    bool ok=acquire(s,false,e), entered=false, changed=false;
    if (ok) { s->body_context=s->context; entered=s->body&&application_q3_component_body_begin(s->body,sequence,s->context.time_ms,e); ok=!s->body||entered; }
    if (ok&&s->revision!=s->context.game_state_revision) {
        ok=call_list(s,&s->options.profile->refresh,e); if (ok) s->revision=s->context.game_state_revision;
    }
    if (ok) ok=accept(s,false,&changed,e);
    if (ok&&changed) ok=call_list(s,&s->options.profile->snapshots,e);
    if (ok) ok=call_list(s,&s->options.profile->frame,e);
    if (entered) { qa_error end={0}; bool ended=application_q3_component_body_end(s->body,ok,&end); if (ok&&!ended) { ok=false; if(e)*e=end; } }
    ok=finish_output(s,ok,e);
    if (!ok) s->failed=true;
    release(s); s->busy=false; return ok;
}
bool application_q3_scene_consume(application_q3_scene *s,const application_q3_scene_player_event *event,uint64_t sequence,qa_error *e)
{
    if(!s||!event||!s->options.profile->player_events||!s->initialized||s->failed||!application_q3_scene_idle(s))
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Original player event requires its initialized returned CG owner");
    if(s->event_present&&sequence<=s->event_sequence) return true;
    const application_q3_scene_profile *p=s->options.profile; int32_t slot=event->player.clientNum;
    if(slot<0||(uint32_t)slot>=p->capacity||!qa_vec_finite(event->origin)) return q3scene_fail(e,QA_ERROR_FORMAT,"Original player event leaves its declared CG projection");
    if(!s->options.source.live(s->options.source.context,event->actor)) { s->event_sequence=sequence; s->event_present=true; return true; }
    qa_actor_id actor_id; bool owned,found;
    if(!s->options.source.actor(s->options.source.context,(uint32_t)slot,&actor_id,&owned,&found,e)) return false;
    if(!found||!qa_actor_id_equal(actor_id,event->actor)) { s->event_sequence=sequence; s->event_present=true; return true; }
    s->event_sequence=sequence; s->event_present=true; s->busy=true; s->active_event=event;
    bool ok=acquire(s,false,e); uint32_t entity=p->entities+(uint32_t)slot*p->stride;
    if(ok&&!qa_actor_id_equal(s->players[slot],event->actor)) {
        ok=qa_qvm_write(s->vm,entity,(qa_bytes){s->defaults.data+(size_t)slot*p->stride,p->stride},e);
        if(ok) s->players[slot]=event->actor;
    }
    if(ok&&s->revision!=s->context.game_state_revision) { ok=call_list(s,&p->refresh,e); if(ok) s->revision=s->context.game_state_revision; }
    if(ok) ok=qa_qvm_write_player(s->vm,(int32_t)p->player_state,true,false,&event->player,e)&&call_list(s,&p->project,e)&&
        store(s,entity+p->state+180,event->event,e)&&store(s,entity+p->state+184,event->parameter,e);
    float origin[]={event->origin.x,event->origin.y,event->origin.z};
    if(ok) ok=words(s,entity+p->entity_origin,origin,3,e)&&call_list(s,&p->event,e);
    bool retired=false;
    if(!ok&&(!e||e->code==QA_ERROR_ARGUMENT)&&s->options.source.current(s->options.source.context,&s->context)) {
        qa_error check={0};
        retired=!s->options.source.live(s->options.source.context,event->actor);
        if(!retired&&s->options.source.actor(s->options.source.context,(uint32_t)slot,&actor_id,&owned,&found,&check))
            retired=!found||!qa_actor_id_equal(actor_id,event->actor);
    }
    if(retired) {
        if(e) *e=(qa_error){0};
        ok=!s->options.finish_output||s->options.finish_output(s->options.output_context,false,e);
    } else ok=finish_output(s,ok,e);
    s->active_event=NULL;
    if(!ok) s->failed=true;
    release(s); s->busy=false; return ok;
}
bool application_q3_scene_hud(application_q3_scene *s,uint64_t sequence,qa_error *e)
{
    if (!application_q3_scene_idle(s)||!s->initialized||!s->frame_present||s->frame!=sequence||!q3scene_current(s))
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component HUD requires its actual completed frame");
    if (!s->options.profile->has_hud||(s->hud_present&&s->hud_frame==sequence)) return true;
    s->busy=true; s->hud_present=true; s->hud_frame=sequence;
    bool ok=acquire(s,false,e)&&call_list(s,&s->options.profile->hud,e);
    ok=finish_output(s,ok,e);
    if (!ok) s->failed=true;
    release(s); s->busy=false; return ok;
}
bool application_q3_scene_console(application_q3_scene *s,const qa_command_invocation *command,bool *handled,qa_error *e)
{
    if (!application_q3_scene_idle(s)||!s->initialized||!command||!handled||s->failed)
        return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component console requires its genuine lexical invocation");
    qa_command_tokens tokens={.count=command->argc,.args_text=(char *)command->args_text,
        .values=(char **)command->argv,.borrowed=true};
    s->busy=true; bool ok=acquire(s,false,e); int32_t args[]={2},result=0;
    if (ok) { s->lexical=&tokens; ok=qa_qvm_invoke(s->vm,0,args,1,&result,e)&&q3scene_current(s); s->lexical=NULL; }
    ok=finish_output(s,ok,e);
    if (!ok) s->failed=true; else *handled=result!=0;
    release(s); s->busy=false; return ok;
}

static qa_command_result declared_command(void *context,const qa_command_invocation *command,qa_error *error)
{
    application_q3_scene *scene=context;
    if (!scene || !command || command->receiver!=scene->options.host.owner ||
        command->registration_owner!=(scene->options.host.service_owner ?
            scene->options.host.service_owner : scene->options.host.owner) ||
        !qa_console_invocation_current(command->console,command)) {
        q3scene_fail(error,QA_ERROR_ARGUMENT,"Component declaration lost its actual executor"); return QA_COMMAND_FAILED;
    }
    bool handled=false;
    bool okay=application_q3_scene_console(scene,command,&handled,error);
    return !okay ? QA_COMMAND_FAILED : handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED;
}

void q3scene_history_clear(application_q3_scene *s)
{
    free(s->snapshot_entities);
    for(size_t i=0;i<64;++i) {
        qa_command_tokens_free(&s->commands[i].tokens); qa_unified_frame_lease_release(s->commands[i].lease);
    }
    qa_unified_frame_lease_release(s->reached_lease);
    qa_unified_frame_pool_destroy(&s->command_storage);
    qa_command_tokens_free(&s->reached); free(s->actors); free(s->players); free(s->game_state); qa_buffer_free(&s->defaults);
}
bool application_q3_scene_destroy(application_q3_scene **owner,qa_error *e)
{
    if(!owner||!*owner) return true;
    application_q3_scene *s=*owner;
    if(!application_q3_scene_idle(s)) return q3scene_fail(e,QA_ERROR_ARGUMENT,"Component CGAME still has actual execution or body borrowers");
    if(s->body) { if(!application_q3_component_body_destroy(s->body,e)) return false; s->body=NULL; }
    if(s->event_binding) { if(!qa_qvm_unbind(s->vm,s->event_binding,e)) return false; s->event_binding=0; }
    if(s->vm) { if(!qa_qvm_destroy(s->vm,e)) return false; s->vm=NULL; qa_q3_host_qvm_consumed(s->host); }
    if(s->host) { if(!qa_q3_host_destroy(s->host,e)) return false; s->host=NULL; }
    else if(s->options.host.frontend_lifetime&&s->options.host.release_frontend) {
        s->options.host.release_frontend(s->options.host.frontend_lifetime);
        s->options.host.frontend_lifetime=NULL;
    }
    q3scene_history_clear(s); free(s); *owner=NULL; return true;
}
application_q3_component_body *application_q3_scene_bodies(application_q3_scene *s)
{ return s&&s->initialized&&!s->failed?s->body:NULL; }
