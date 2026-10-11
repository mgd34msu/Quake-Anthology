#include "compiled_source.h"
#include <stdlib.h>
#include <string.h>

struct q3n_compiled_source {
    q3n_compiled_source_options options;
    q3n_compiled_source_basis constructor;
    q3n_compiled_source_rebind_ticket *prepared;
};
struct q3n_compiled_source_rebind_ticket {
    q3n_compiled_source *source;
    q3n_compiled_source_view before;
    q3n_compiled_source_basis candidate;
};
static bool fail(qa_error *e, const char *text)
{ qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", text); return false; }
static bool shape_fields(const q3n_compiled_source_basis *b)
{
    return b->registry && b->provider && b->receiver && b->instance &&
        b->content && b->assets && b->publication && b->serial &&
        b->viewer.registry==qa_actors_identity(b->registry) &&
        (b->product == QA_Q3_ARENA || b->product == QA_Q3_TEAM_ARENA) &&
        b->max_clients > 0 && b->max_clients <= 64 &&
        b->client_number >= -1 && b->client_number < b->max_clients &&
        b->initial_command>=0 && b->reached_command>=b->initial_command && !(b->snapshot_bit&~4u);
}
static bool shape(const q3n_compiled_source_basis *b)
{ return shape_fields(b) && qa_actors_get(b->registry,b->viewer); }
static bool identity(const q3n_compiled_source_basis *a, const q3n_compiled_source_basis *b)
{
    return a->application == b->application && a->session == b->session && a->registry == b->registry &&
        a->provider == b->provider && a->receiver == b->receiver &&
        a->instance==b->instance && a->content == b->content && a->assets == b->assets &&
        a->product == b->product && a->publication == b->publication && a->map_revision == b->map_revision &&
        qa_actor_id_equal(a->viewer, b->viewer) && a->seat == b->seat && a->physical_seat == b->physical_seat &&
        a->client_number == b->client_number && a->initial_command==b->initial_command && a->snapshot_bit==b->snapshot_bit;
}
bool q3n_compiled_source_create(const q3n_compiled_source_options *o,q3n_compiled_source **out,qa_error *e)
{
    if (!o || !o->context || !o->read || !o->current || !o->configstring || !o->idle || !o->client_actor || !o->actor_known || !out || *out ||
        (o->checkpoint_read==NULL)!=(o->checkpoint_current==NULL))
        return fail(e, "Compiled Q3 CLIENT requires its retained receiver callbacks");
    q3n_compiled_source_basis basis = {0};
    if (!o->read(o->context,&basis,e) || !shape(&basis) || !o->current(o->context,&basis))
        return fail(e, "Compiled Q3 CLIENT constructor has no current source declaration");
    q3n_compiled_source *s = calloc(1, sizeof(*s));
    if (!s) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining compiled Q3 CLIENT source"); return false; }
    s->options = *o; s->constructor = basis; *out = s; return true;
}
bool q3n_compiled_source_idle(const q3n_compiled_source *s)
{ return s && !s->prepared && s->options.idle(s->options.context); }
bool q3n_compiled_source_destroy(q3n_compiled_source **slot, qa_error *e)
{
    if (!slot || !*slot) return true;
    if (!q3n_compiled_source_idle(*slot)) return fail(e, "Compiled Q3 CLIENT callbacks must return before retirement");
    free(*slot); *slot = NULL; return true;
}
bool q3n_compiled_source_read(const q3n_compiled_source *s, q3n_compiled_source_view *out, qa_error *e)
{
    if (!s || !out) return fail(e, "Missing compiled Q3 CLIENT source receipt");
    q3n_compiled_source_basis b = {0};
    if (!s->options.read(s->options.context, &b, e) || !shape(&b) ||
        !identity(&s->constructor, &b) || !s->options.current(s->options.context, &b))
        return fail(e, "Compiled Q3 CLIENT source declaration was retired");
    *out = (q3n_compiled_source_view){s, b}; return true;
}
bool q3n_compiled_source_current(const q3n_compiled_source_view *v)
{
    q3n_compiled_source_view actual;
    return v && q3n_compiled_source_read(v->owner, &actual, NULL) && identity(&v->basis, &actual.basis) &&
        v->basis.serial == actual.basis.serial && v->basis.time == actual.basis.time &&
        v->basis.game_type == actual.basis.game_type && v->basis.max_clients == actual.basis.max_clients &&
        v->basis.level_start_time == actual.basis.level_start_time && v->basis.initialized == actual.basis.initialized &&
        v->basis.reached_command==actual.basis.reached_command;
}
bool q3n_compiled_source_checkpoint_read(const q3n_compiled_source *s,q3n_compiled_source_view *out,qa_error *e)
{
    if(!s || !out || (s->prepared && !q3n_compiled_source_rebind_checkpoint_current(s->prepared)))
        return fail(e,"Compiled checkpoint needs its actual retained source or exact prepared rebind");
    if(!s->options.checkpoint_read)return q3n_compiled_source_read(s,out,e);
    q3n_compiled_source_basis b={0};
    if(!s->options.checkpoint_read(s->options.context,&b,e)||!shape_fields(&b)||!identity(&s->constructor,&b)||
       !s->options.checkpoint_current(s->options.context,&b)||!s->options.actor_known(s->options.context,b.viewer)||
       !s->options.checkpoint_current(s->options.context,&b))
        return fail(e,"Compiled checkpoint lost its actual retained source or saved wire actor");
    *out=(q3n_compiled_source_view){s,b}; return true;
}
bool q3n_compiled_source_checkpoint_current(const q3n_compiled_source_view *v)
{
    q3n_compiled_source_view actual;
    return v && q3n_compiled_source_checkpoint_read(v->owner,&actual,NULL) && identity(&v->basis,&actual.basis) &&
        v->basis.serial==actual.basis.serial && v->basis.time==actual.basis.time &&
        v->basis.game_type==actual.basis.game_type && v->basis.max_clients==actual.basis.max_clients &&
        v->basis.level_start_time==actual.basis.level_start_time && v->basis.initialized==actual.basis.initialized &&
        v->basis.reached_command==actual.basis.reached_command;
}
bool q3n_compiled_source_rebind(q3n_compiled_source *s,const q3n_compiled_source_view *before,qa_error *e)
{
    if(!s||s->prepared||!before||before->owner!=s||!identity(&s->constructor,&before->basis))
        return fail(e,"Compiled round rebind requires its actual retained previous source receipt");
    q3n_compiled_source_basis after={0},expected=before->basis;
    if(!s->options.read(s->options.context,&after,e)||!shape(&after)||
       after.snapshot_bit==before->basis.snapshot_bit||
       !s->options.current(s->options.context,&after))
        return fail(e,"Compiled round rebind requires the actual committed source snapshot-bit toggle");
    expected.viewer=after.viewer; expected.snapshot_bit=after.snapshot_bit;
    if(!identity(&expected,&after)||after.reached_command<before->basis.reached_command||
       after.initialized!=before->basis.initialized)
        return fail(e,"Compiled round rebind changed its retained CLIENT constructor namespace");
    s->constructor.viewer=after.viewer; s->constructor.snapshot_bit=after.snapshot_bit;
    return true;
}
static bool round_candidate(const q3n_compiled_source_basis *before,const q3n_compiled_source_basis *candidate)
{
    if(!shape(candidate)||candidate->snapshot_bit==before->snapshot_bit||
       candidate->reached_command<before->reached_command||candidate->initialized!=before->initialized) return false;
    q3n_compiled_source_basis expected=*before;
    expected.viewer=candidate->viewer; expected.snapshot_bit=candidate->snapshot_bit;
    return identity(&expected,candidate);
}
bool q3n_compiled_source_rebind_checkpoint_current(const q3n_compiled_source_rebind_ticket *t)
{
    const q3n_compiled_source *s = t ? t->source : NULL;
    if (!s || s->prepared != t || !s->options.checkpoint_read || !s->options.checkpoint_current ||
        t->before.owner != s || !identity(&s->constructor,&t->before.basis) ||
        !shape_fields(&t->candidate) || !s->options.actor_known(s->options.context,t->candidate.viewer) ||
        t->candidate.snapshot_bit == t->before.basis.snapshot_bit ||
        t->candidate.reached_command < t->before.basis.reached_command ||
        t->candidate.initialized != t->before.basis.initialized) return false;
    q3n_compiled_source_basis actual = {0}, expected = t->before.basis;
    expected.viewer = t->candidate.viewer; expected.snapshot_bit = t->candidate.snapshot_bit;
    return identity(&expected,&t->candidate) &&
        s->options.checkpoint_read(s->options.context,&actual,NULL) && identity(&actual,&t->before.basis) &&
        actual.serial == t->before.basis.serial && actual.time == t->before.basis.time &&
        actual.game_type == t->before.basis.game_type && actual.max_clients == t->before.basis.max_clients &&
        actual.level_start_time == t->before.basis.level_start_time &&
        actual.reached_command == t->before.basis.reached_command && actual.initialized == t->before.basis.initialized &&
        s->options.checkpoint_current(s->options.context,&t->before.basis) &&
        s->options.checkpoint_current(s->options.context,&t->candidate);
}
bool q3n_compiled_source_rebind_ready(const q3n_compiled_source_rebind_ticket *t)
{
    const q3n_compiled_source *s=t?t->source:NULL;
    return s && s->prepared==t && q3n_compiled_source_current(&t->before) &&
        round_candidate(&t->before.basis,&t->candidate) &&
        s->options.current(s->options.context,&t->candidate) && q3n_compiled_source_current(&t->before);
}
bool q3n_compiled_source_rebind_prepare(q3n_compiled_source *s,const q3n_compiled_source_view *before,
    const q3n_compiled_source_basis *candidate,q3n_compiled_source_rebind_ticket **out,qa_error *e)
{
    if(!s||s->prepared||!before||before->owner!=s||!candidate||!out||*out||
       !q3n_compiled_source_current(before)||!round_candidate(&before->basis,candidate)||
       !s->options.current(s->options.context,candidate))
        return fail(e,"Compiled round preparation requires its actual retained and staged source receipts");
    q3n_compiled_source_rebind_ticket *t=calloc(1,sizeof(*t));
    if(!t) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining compiled round source rebind"); return false; }
    t->source=s; t->before=*before; t->candidate=*candidate; s->prepared=t;
    if(!q3n_compiled_source_rebind_ready(t)) { s->prepared=NULL; free(t);
        return fail(e,"Compiled round preparation changed its actual source receipts"); }
    *out=t; return true;
}
static bool rebind_context_is(const q3n_compiled_source_rebind_ticket *t,
    const q3n_compiled_source *s,const qa_command_context *before,const qa_command_context *after,bool cold)
{
    return t && t->source==s && before && after &&
        (cold ? q3n_compiled_source_rebind_checkpoint_current(t) : q3n_compiled_source_rebind_ready(t)) &&
        before->session==after->session && before->owner==after->owner && before->client==after->client &&
        before->seat==after->seat && before->dialect==after->dialect && before->origin==after->origin &&
        before->direct==after->direct && before->console_text==after->console_text &&
        before->registry==after->registry && before->generation==after->generation &&
        ((!before->script&&!after->script)||(before->script&&after->script&&!strcmp(before->script,after->script)));
}
bool q3n_compiled_source_rebind_context_is(const q3n_compiled_source_rebind_ticket *t,
    const q3n_compiled_source *s,const qa_command_context *before,const qa_command_context *after)
{ return rebind_context_is(t,s,before,after,false); }
bool q3n_compiled_source_rebind_checkpoint_context_is(const q3n_compiled_source_rebind_ticket *t,
    const q3n_compiled_source *s,const qa_command_context *before,const qa_command_context *after)
{ return rebind_context_is(t,s,before,after,true); }
void q3n_compiled_source_rebind_commit(q3n_compiled_source_rebind_ticket **out)
{
    if(!out||!*out)return;
    q3n_compiled_source_rebind_ticket *t=*out;
    t->source->constructor.viewer=t->candidate.viewer;
    t->source->constructor.snapshot_bit=t->candidate.snapshot_bit;
    t->source->prepared=NULL; free(t); *out=NULL;
}
void q3n_compiled_source_rebind_abort(q3n_compiled_source_rebind_ticket **out)
{
    if(!out||!*out)return;
    q3n_compiled_source_rebind_ticket *t=*out;
    t->source->prepared=NULL; free(t); *out=NULL;
}
bool q3n_compiled_source_configstring(const q3n_compiled_source *s, uint32_t index,
    const char **text, uint64_t *revision, qa_error *e)
{
    q3n_compiled_source_view before;
    if (!text || !revision || index >= QA_Q3_CONFIGSTRINGS || !q3n_compiled_source_read(s, &before, e))
        return fail(e, "Compiled configstring requires its real reached CLIENT dictionary");
    const char *value = NULL; uint64_t stamp = 0;
    if (!s->options.configstring(s->options.context, index, &value, &stamp, e)) return false;
    if (!value || !q3n_compiled_source_current(&before)) return fail(e, "Compiled configstring source changed during observation");
    *text = value; *revision = stamp; return true;
}
bool q3n_compiled_source_client_actor(const q3n_compiled_source *s,uint32_t physical,qa_actor_id *out,bool *found,qa_error *e)
{
    q3n_compiled_source_view view; qa_actor_id actor={0}; bool present=false;
    if(!out || !found || !q3n_compiled_source_read(s,&view,e) || physical>=(uint32_t)view.basis.max_clients)
        return fail(e,"Compiled client identity requires its actual physical source slot");
    if(!s->options.client_actor(s->options.context,physical,&actor,&present,e))return false;
    if(!q3n_compiled_source_current(&view) || (present && !qa_actors_get(view.basis.registry,actor)))
        return fail(e,"Compiled client identity left its actual replica actor registry");
    if(present)*out=actor;
    *found=present; return true;
}
bool q3n_compiled_source_actor_known(const q3n_compiled_source_view *view,qa_actor_id actor)
{
    return q3n_compiled_source_current(view) &&
        actor.registry==qa_actors_identity(view->basis.registry) &&
        view->owner->options.actor_known(view->owner->options.context,actor) &&
        q3n_compiled_source_current(view);
}
