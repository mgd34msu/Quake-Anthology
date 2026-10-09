#include "guest_q3_mod_private.h"
#include "qa/input.h"

typedef struct input_field { const mod_output *definition; application_q3_mod_output before; } input_field;
typedef struct input_command {
    struct input_command *next;
    const mod_output *definition;
    uint32_t address;
    qa_q3_usercmd before;
} input_command;
struct application_q3_mod_application {
    application_q3_mod *owner;
    application_q3_mod_application *previous;
    qa_actor_id actor;
    application_q3_mod_inputs inputs;
    application_q3_mod_input_values_fn values;
    void *context;
    bool reading;
};
struct application_q3_mod_capture {
    application_q3_mod *owner;
    application_q3_mod_capture *previous;
    application_q3_mod_application *application;
    const mod_input_binding *binding;
    input_field *fields;
    size_t field_count;
    input_command *commands;
    application_q3_mod_output *changes, *consumed;
    size_t change_count, consumed_count;
};
typedef struct selected_entry { const mod_output *definition; input_command *command; } selected_entry;
struct application_q3_mod_entry {
    application_q3_mod *owner;
    application_q3_mod_capture *capture;
    selected_entry *selected;
    size_t count;
};
static bool active(const application_q3_mod_capture *c)
{
    return c && c->owner->capture==c && c->owner->application==c->application &&
        c->owner->services.live_client(c->owner->services.context,c->application->actor);
}
static bool append(application_q3_mod_output **rows, size_t *count,
    application_q3_mod_output value, qa_error *e)
{
    if (*count>=SIZE_MAX/sizeof(**rows)) return q3mod_fail(e,QA_ERROR_MEMORY,"Source input output inventory overflows");
    void *next=realloc(*rows,(*count+1)*sizeof(**rows));
    if (!next) return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining original input changes");
    *rows=next; (*rows)[(*count)++]=value; return true;
}
static bool field_read(application_q3_mod_capture *c, const mod_output *d,
    application_q3_mod_output *out, qa_error *e)
{
    uint32_t address; uint8_t bytes[12]; size_t width=d->input==Q3_MOD_VIEW_ANGLES?12:4;
    if (!q3mod_address(c->owner,c->application->actor,d->record,d->offset,width,&address,e) ||
        !qa_qvm_read(c->owner->vm,address,bytes,width,e)) return false;
    application_q3_mod_output value={.input=d->input};
    if (d->input==Q3_MOD_VIEW_ANGLES) {
        value.value.angles=(qa_vec3){qa_load_f32le(bytes),qa_load_f32le(bytes+4),qa_load_f32le(bytes+8)};
        if (!qa_vec_finite(value.value.angles)) return q3mod_fail(e,QA_ERROR_FORMAT,"Original input angles are nonfinite");
    } else {
        value.value.scalar=(d->encoding==MOD_INT32?(double)qa_load_i32le(bytes):qa_load_f32le(bytes))/d->scale;
        if (!isfinite(value.value.scalar)) return q3mod_fail(e,QA_ERROR_FORMAT,"Original input field is nonfinite");
    }
    *out=value; return true;
}
static bool same(application_q3_mod_output a, application_q3_mod_output b)
{
    if (a.input!=b.input) return false;
    return a.input==Q3_MOD_VIEW_ANGLES?a.value.angles.x==b.value.angles.x &&
        a.value.angles.y==b.value.angles.y && a.value.angles.z==b.value.angles.z:a.value.scalar==b.value.scalar;
}
static double command_scalar(const qa_q3_usercmd *c, application_q3_mod_input input)
{
    switch (input) {
        case Q3_MOD_ATTACK:return (c->buttons&1)!=0;
        case Q3_MOD_JUMP:return c->upmove>=10;
        case Q3_MOD_FORWARD:return (double)c->forwardmove/qa_input_command_units(QA_RULESET_Q3);
        case Q3_MOD_SIDE:return (double)c->rightmove/qa_input_command_units(QA_RULESET_Q3);
        case Q3_MOD_UP:return (double)c->upmove/qa_input_command_units(QA_RULESET_Q3);
        default:return 0;
    }
}
static bool commands(application_q3_mod_capture *c, input_command *command, bool publish, qa_error *e)
{
    qa_q3_usercmd after;
    if (!qa_qvm_read_usercmd(c->owner->vm,(int32_t)command->address,&after,e)) return false;
    qa_q3_usercmd before=command->before; command->before=after;
    if (!publish) return true;
    for (size_t index=0;index<command->definition->input_count;++index) {
        application_q3_mod_input i=command->definition->ordered_inputs[index];
        application_q3_mod_output value={.input=(application_q3_mod_input)i};
        if (i==Q3_MOD_VIEW_ANGLES) {
            if (before.angles[0]==after.angles[0] && before.angles[1]==after.angles[1] && before.angles[2]==after.angles[2]) continue;
            qa_q3_player player;
            if (!c->owner->services.player_state(c->owner->services.context,c->application->actor,&player,e) || !q3mod_current(c->owner,e)) return false;
            qa_movement_command source = {.kind = QA_RULESET_Q3}, projected;
            memcpy(source.angle_words, after.angles, sizeof(source.angle_words));
            qa_input_command_basis from = {.kind = QA_RULESET_Q3,
                .words = true, .relative = true, .wrap_words = true};
            memcpy(from.delta_words, player.deltaAngles, sizeof(from.delta_words));
            qa_input_command_basis to = {.kind = QA_RULESET_Q3};
            qa_input_command_convert(&source, NULL, &from, &to,
                (qa_input_axis_rule){0}, &projected);
            value.value.angles = projected.angles;
        } else {
            value.value.scalar=command_scalar(&after,(application_q3_mod_input)i);
            if (value.value.scalar==command_scalar(&before,(application_q3_mod_input)i)) continue;
        }
        if (!append(&c->changes,&c->change_count,value,e)) return false;
    }
    return true;
}
static bool reconcile(application_q3_mod_capture *c, bool publish, qa_error *e)
{
    for (size_t i=0;i<c->field_count;++i) {
        application_q3_mod_output next;
        if (!field_read(c,c->fields[i].definition,&next,e)) return false;
        bool changed=!same(next,c->fields[i].before); c->fields[i].before=next;
        if (publish && changed && !append(&c->changes,&c->change_count,next,e)) return false;
    }
    for (input_command *command=c->commands;command;command=command->next)
        if (!commands(c,command,publish,e)) return false;
    return true;
}
bool application_q3_mod_open(application_q3_mod *o, qa_actor_id actor,
    const application_q3_mod_inputs *inputs, application_q3_mod_application **out, qa_error *e)
{
    if (!out || *out || !inputs || !q3mod_current(o,e) || !o->profile->clients || !o->services.live_client(o->services.context,actor))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input application requires its actual admitted source client");
    if (inputs->values[Q3_MOD_SELF].kind!=Q3_MOD_VALUE_ACTOR ||
        !qa_actor_id_equal(inputs->values[Q3_MOD_SELF].as.actor,actor))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input application self differs from its full source actor");
    application_q3_mod_application *a=calloc(1,sizeof(*a));
    if (!a) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning original input application");
    if (active(o->capture) && !reconcile(o->capture,true,e)) { free(a); return false; }
    a->owner=o; a->previous=o->application; a->actor=actor; a->inputs=*inputs;
    o->application=a; *out=a; return true;
}
bool application_q3_mod_close(application_q3_mod_application **in, qa_error *e)
{
    if (!in || !*in) return true;
    application_q3_mod_application *a=*in; application_q3_mod *o=a->owner;
    if (o->application!=a || a->reading || (o->capture && o->capture->application==a))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input application still retains a capture or nested application");
    o->application=a->previous; free(a); *in=NULL;
    return !active(o->capture) || reconcile(o->capture,false,e);
}
bool application_q3_mod_client_live(const application_q3_mod *o, qa_actor_id actor)
{
    return o && o->profile->clients && o->services.live_client &&
        o->services.live_client(o->services.context,actor);
}
bool application_q3_mod_application_current(application_q3_mod *o,
    const application_q3_mod_application *a,qa_actor_id actor)
{
    return o&&a&&o->application==a&&a->owner==o&&qa_actor_id_equal(a->actor,actor)&&
        a->inputs.values[Q3_MOD_SELF].kind==Q3_MOD_VALUE_ACTOR&&
        qa_actor_id_equal(a->inputs.values[Q3_MOD_SELF].as.actor,actor)&&
        q3mod_current(o,NULL)&&application_q3_mod_client_live(o,actor);
}
bool application_q3_mod_input_update(application_q3_mod_application *a,
    const application_q3_mod_inputs *inputs, qa_error *e)
{
    if (!a || !inputs || a->owner->application!=a || !q3mod_current(a->owner,e) ||
        !application_q3_mod_client_live(a->owner,a->actor) ||
        inputs->values[Q3_MOD_SELF].kind!=Q3_MOD_VALUE_ACTOR ||
        !qa_actor_id_equal(inputs->values[Q3_MOD_SELF].as.actor,a->actor))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input refresh lost its actual current application actor");
    a->inputs=*inputs; return true;
}
bool application_q3_mod_input_source(application_q3_mod_application *a,
    application_q3_mod_input_values_fn values,void *context,qa_error *e)
{
    if (!a || !values || a->owner->application!=a || a->values || !q3mod_current(a->owner,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input source requires its actual unbound application");
    a->values=values; a->context=context; return true;
}
bool application_q3_mod_input_current(application_q3_mod *o,qa_actor_id actor,
    application_q3_mod_inputs *out,bool *found,qa_error *e)
{
    if (!o || !out || !found || !q3mod_current(o,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input read requires its actual source owner");
    *found=false;
    application_q3_mod_application *a=o->application;
    while (a && !qa_actor_id_equal(a->actor,actor)) a=a->previous;
    if (!a) return true;
    if (a->reading || !application_q3_mod_client_live(o,actor))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input read lost its real admitted application");
    application_q3_mod_inputs value=a->inputs;
    a->reading=true;
    bool ok=!a->values || a->values(a->context,&value,e);
    a->reading=false;
    if (!ok || !q3mod_current(o,e)) return false;
    if (value.values[Q3_MOD_SELF].kind!=Q3_MOD_VALUE_ACTOR ||
        !qa_actor_id_equal(value.values[Q3_MOD_SELF].as.actor,actor) ||
        !application_q3_mod_client_live(o,actor))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input read changed its full application actor");
    *out=value; *found=true; return true;
}
static bool pointer(application_q3_mod *o, const mod_pointer *p, const qa_qvm_call *call,
    uint32_t *out, qa_error *e)
{
    int32_t word; uint8_t bytes[4];
    if (p->argument) { if (!qa_qvm_call_argument(call,p->root,&word,e)) return false; }
    else { if (!qa_qvm_read(o->vm,p->root,bytes,4,e)) return false; word=qa_load_i32le(bytes); }
    int64_t address=word;
    for (size_t i=0;i<p->count;++i) {
        int64_t at=address+p->indirections[i];
        if (at<0 || at>UINT32_MAX || !qa_qvm_read(o->vm,(uint32_t)at,bytes,4,e))
            return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input pointer indirection leaves actual source memory");
        address=qa_load_i32le(bytes);
    }
    address+=p->offset;
    if (address<0 || address>INT32_MAX) return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input pointer leaves signed source address space");
    *out=(uint32_t)address; return true;
}
bool application_q3_mod_entry_begin(application_q3_mod *o, const qa_qvm_call *call,
    application_q3_mod_entry **out, qa_error *e)
{
    if (!o || !out || *out || !call || call->vm!=o->vm || !q3mod_current(o,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input entry requires its actual source token and empty scope");
    application_q3_mod_entry *scope=calloc(1,sizeof(*scope));
    if (!scope) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning original input entry boundary");
    scope->owner=o; scope->capture=active(o->capture)?o->capture:NULL;
    if (scope->capture) {
        application_q3_mod_capture *c=scope->capture;
        scope->selected=c->binding->output_count?calloc(c->binding->output_count,sizeof(*scope->selected)):NULL;
        if (c->binding->output_count && !scope->selected) { free(scope); return q3mod_fail(e,QA_ERROR_MEMORY,"Retaining original handler selection"); }
        for (size_t i=0;i<c->binding->output_count;++i) {
            const mod_output *d=c->binding->outputs+i;
            if (d->kind==MOD_FIELD || d->entry!=call->instruction) continue;
            uint32_t source, wanted;
            if (!pointer(o,&d->actor,call,&source,e) || !q3mod_address(o,c->application->actor,d->record,0,0,&wanted,e)) goto fail;
            if (source!=wanted) continue;
            selected_entry *selected=scope->selected+scope->count++;
            selected->definition=d;
            if (d->kind==MOD_COMMAND) {
                input_command *command=calloc(1,sizeof(*command));
                if (!command) { q3mod_fail(e,QA_ERROR_MEMORY,"Retaining actual user-command baseline"); goto fail; }
                command->definition=d;
                uint8_t qualification[24];
                if (!pointer(o,&d->command,call,&command->address,e) ||
                    !qa_qvm_read(o->vm,command->address,qualification,sizeof(qualification),e) ||
                    !qa_qvm_read_usercmd(o->vm,(int32_t)command->address,&command->before,e)) { free(command); goto fail; }
                input_command **tail=&c->commands; while (*tail) tail=&(*tail)->next;
                *tail=command; selected->command=command;
            }
        }
    }
    ++o->calls; *out=scope; return true;
fail:
    { qa_error ignored={0}; application_q3_mod_entry *cleanup=scope; ++o->calls;
      application_q3_mod_entry_end(&cleanup,false,0,&ignored); }
    return false;
}
bool application_q3_mod_entry_end(application_q3_mod_entry **in, bool succeeded, int32_t result, qa_error *e)
{
    if (!in || !*in) return true;
    application_q3_mod_entry *scope=*in; application_q3_mod_capture *c=scope->capture;
    bool ok=true;
    for (size_t i=0;i<scope->count;++i) {
        selected_entry *selected=scope->selected+i; const mod_output *d=selected->definition;
        if (succeeded && active(c)) {
            if (selected->command) { if (!commands(c,selected->command,true,e)) ok=false; }
            else {
                double returned=result;
                if (d->has_return && d->encoding==MOD_FLOAT32) {
                    float f; memcpy(&f,&result,4); returned=f;
                    if (!isfinite(returned)) {
                        ok=q3mod_fail(e,QA_ERROR_FORMAT,"Original input handler returned a nonfinite scalar");
                        continue;
                    }
                }
                if ((!d->has_return || returned==d->returned) &&
                    !append(&c->consumed,&c->consumed_count,(application_q3_mod_output){.consume=true,.value.inputs=d->inputs},e)) ok=false;
            }
        }
        if (selected->command) {
            input_command **row=&c->commands;
            while (*row && *row!=selected->command) row=&(*row)->next;
            if (*row) *row=selected->command->next;
            free(selected->command);
        }
    }
    --scope->owner->calls; free(scope->selected); free(scope); *in=NULL; return ok;
}
bool application_q3_mod_input_run(application_q3_mod *o, size_t index,
    application_q3_mod_application *a,application_q3_mod_input_prepare_fn prepare,void *context,
    application_q3_mod_output **out, size_t *count, qa_error *e)
{
    if (!o || !out || *out || !count || !a || a->owner!=o || o->application!=a ||
        index>=o->profile->input_count || !q3mod_current(o,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Input binding requires its actual application and empty output");
    *count=0; const mod_input_binding *binding=o->profile->inputs+index;
    application_q3_mod_capture c={.owner=o,.previous=o->capture,.application=a,.binding=binding};
    bool captured=binding->before && binding->output_count;
    if (captured) {
        c.fields=calloc(binding->output_count,sizeof(*c.fields));
        if (!c.fields) return q3mod_fail(e,QA_ERROR_MEMORY,"Owning source input field baselines");
        for (size_t i=0;i<binding->output_count;++i) if (binding->outputs[i].kind==MOD_FIELD) {
            input_field *f=c.fields+c.field_count++; f->definition=binding->outputs+i;
            if (!field_read(&c,f->definition,&f->before,e)) { free(c.fields); return false; }
        }
        o->capture=&c;
    }
    bool ok=true; double ignored;
    for (size_t i=0;ok && i<binding->call_count && application_q3_mod_client_live(o,a->actor);++i) {
        if (prepare) ok=prepare(context,binding->calls[i].entry,e);
        if (ok && !application_q3_mod_application_current(o,a,a->actor))
            ok=q3mod_fail(e,QA_ERROR_ARGUMENT,"Source input preparation retired its actual application");
        if (ok) ok=q3mod_invoke(o,binding->calls+i,&a->inputs,&ignored,e);
    }
    if (ok && captured && active(&c)) ok=reconcile(&c,true,e);
    if (captured) o->capture=c.previous;
    if (ok && captured && o->services.live_client(o->services.context,a->actor)) {
        if (c.consumed_count>SIZE_MAX-c.change_count || c.change_count+c.consumed_count>SIZE_MAX/sizeof(**out))
            ok=q3mod_fail(e,QA_ERROR_MEMORY,"Source input result inventory overflows");
        else if (c.change_count+c.consumed_count) {
            size_t n=c.change_count+c.consumed_count;
            *out=malloc(n*sizeof(**out));
            if (!*out) ok=q3mod_fail(e,QA_ERROR_MEMORY,"Owning ordered source input results");
            else {
                if (c.change_count) memcpy(*out,c.changes,c.change_count*sizeof(**out));
                if (c.consumed_count) memcpy(*out+c.change_count,c.consumed,c.consumed_count*sizeof(**out));
                *count=n;
            }
        }
    }
    free(c.fields); free(c.changes); free(c.consumed); return ok;
}
