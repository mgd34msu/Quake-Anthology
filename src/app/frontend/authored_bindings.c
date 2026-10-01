#include "authored_bindings.h"
#include <stdlib.h>
#include <string.h>

typedef struct binding_set { qa_input_binding *rows; size_t count; } binding_set;
struct frontend_authored_bindings {
    binding_set authored,selected;
    qa_physical_input *overridden;
    size_t overridden_count;
    bool initialized,all_chosen,collecting,selection_applied;
};
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
static bool equal(const char *left,const char *right)
{
    for (;;++left,++right) {
        unsigned a=(unsigned char)*left,b=(unsigned char)*right;
        if (a>='A' && a<='Z') a+='a'-'A'; if (b>='A' && b<='Z') b+='a'-'A';
        if (a!=b) return false; if (!a) return true;
    }
}
static bool same_input(qa_physical_input a,qa_physical_input b)
{
    return a.kind==b.kind && a.code==b.code &&
        (a.kind<QA_PHYSICAL_BUTTON || a.device==b.device) &&
        (a.kind!=QA_PHYSICAL_AXIS || a.positive==b.positive);
}
static size_t find(const binding_set *set,qa_physical_input input)
{
    size_t index=0;
    while (index<set->count && !same_input(set->rows[index].input,input)) ++index;
    return index;
}
static void clear(binding_set *set)
{
    for (size_t i=0;i<set->count;++i)
        if (set->rows[i].kind==QA_BIND_COMMAND) free((void *)set->rows[i].command);
    free(set->rows); *set=(binding_set){0};
}
static bool put(binding_set *set,const qa_input_binding *binding,bool replace,qa_error *error)
{
    size_t index=find(set,binding->input);
    if (index<set->count && !replace) return true;
    qa_input_binding copy=*binding;
    if (copy.kind==QA_BIND_COMMAND) {
        size_t length=strlen(copy.command);
        char *text=malloc(length+1);
        if (!text) return fail(error,QA_ERROR_MEMORY,"Retaining authored binding command");
        memcpy(text,copy.command,length+1); copy.command=text;
    }
    if (index==set->count) {
        if (set->count>=SIZE_MAX/sizeof(*set->rows)) {
            if (copy.kind==QA_BIND_COMMAND) free((void *)copy.command);
            return fail(error,QA_ERROR_MEMORY,"Authored bindings exceed storage");
        }
        qa_input_binding *rows=realloc(set->rows,(set->count+1)*sizeof(*rows));
        if (!rows) {
            if (copy.kind==QA_BIND_COMMAND) free((void *)copy.command);
            return fail(error,QA_ERROR_MEMORY,"Growing authored binding defaults");
        }
        set->rows=rows; ++set->count;
    } else if (set->rows[index].kind==QA_BIND_COMMAND) free((void *)set->rows[index].command);
    set->rows[index]=copy; return true;
}
static bool copy_set(binding_set *out,const binding_set *source,qa_error *error)
{
    for (size_t i=0;i<source->count;++i)
        if (!put(out,source->rows+i,false,error)) return false;
    return true;
}
static bool selection_command(const qa_input_binding *binding)
{
    if (binding->kind!=QA_BIND_COMMAND) return false;
    const char *text=binding->command;
    const char *names[]={"weapon","impulse","use"};
    for (size_t i=0;i<3;++i) {
        size_t n=strlen(names[i]);
        if (strlen(text)<=n) continue;
        bool match=true;
        for (size_t j=0;j<n;++j) {
            unsigned c=(unsigned char)text[j];
            if (c>='A' && c<='Z') c+='a'-'A';
            if (c!=(unsigned char)names[i][j]) { match=false; break; }
        }
        if (match && (text[n]==' ' || text[n]=='\t' || text[n]=='\r' || text[n]=='\n' ||
            text[n]=='\v' || text[n]=='\f')) return true;
    }
    return false;
}
static bool general(binding_set *set,qa_console_dialect dialect,qa_error *error)
{
    if ((unsigned)dialect>QA_CONSOLE_Q3) return fail(error,QA_ERROR_ARGUMENT,"Invalid binding movement dialect");
    qa_input_binding binding;
    for (size_t i=0;qa_input_default_binding_at(dialect,0,i,&binding);++i)
        if (!put(set,&binding,false,error)) return false;
    return true;
}
static bool seat_set(binding_set *set,const qa_input_seat *input,bool remove_selection,qa_error *error)
{
    for (size_t i=0;i<qa_input_seat_binding_count(input);++i) {
        const qa_input_binding *binding=qa_input_seat_binding_at(input,i);
        if (!remove_selection || !selection_command(binding))
            if (!put(set,binding,false,error)) return false;
    }
    return true;
}
frontend_authored_bindings *frontend_authored_bindings_create(qa_error *error)
{
    frontend_authored_bindings *owner=calloc(1,sizeof(*owner));
    if (!owner) fail(error,QA_ERROR_MEMORY,"Allocating authored binding metadata");
    return owner;
}
void frontend_authored_bindings_destroy(frontend_authored_bindings *owner)
{
    if (!owner) return;
    clear(&owner->authored); clear(&owner->selected); free(owner->overridden); free(owner);
}
bool frontend_authored_bindings_clone(const frontend_authored_bindings *previous,
    frontend_authored_bindings **out,qa_error *error)
{
    if (!previous || !out || *out) return fail(error,QA_ERROR_ARGUMENT,"Binding carry requires its real metadata owner");
    frontend_authored_bindings *owner=frontend_authored_bindings_create(error);
    if (!owner) return false;
    bool ok=copy_set(&owner->authored,&previous->authored,error) && copy_set(&owner->selected,&previous->selected,error);
    if (ok && previous->overridden_count) {
        owner->overridden=malloc(previous->overridden_count*sizeof(*owner->overridden));
        if (!owner->overridden) ok=fail(error,QA_ERROR_MEMORY,"Retaining explicit binding overrides");
        else { memcpy(owner->overridden,previous->overridden,previous->overridden_count*sizeof(*owner->overridden));
            owner->overridden_count=previous->overridden_count; }
    }
    if (!ok) { frontend_authored_bindings_destroy(owner); return false; }
    owner->initialized=previous->initialized; owner->all_chosen=previous->all_chosen;
    owner->collecting=previous->collecting; owner->selection_applied=previous->selection_applied;
    *out=owner; return true;
}
static bool defaults(frontend_authored_bindings *owner,const frontend_authored_bindings *primary,
    qa_input_seat *input,qa_console_dialect dialect,qa_error *error)
{
    if (!owner || !input || owner->initialized || (primary && !primary->initialized))
        return fail(error,QA_ERROR_ARGUMENT,"Authored defaults require the actual unfinished seat phase");
    binding_set authored={0},selected={0},live={0};
    bool ok=(primary?copy_set(&authored,&primary->authored,error):seat_set(&authored,input,true,error)) &&
        (owner->selected.count?copy_set(&selected,&owner->selected,error):general(&selected,dialect,error)) &&
        (primary || seat_set(&live,input,true,error));
    for (size_t i=0;ok && i<selected.count;++i)
        ok=put(&authored,selected.rows+i,false,error) && (primary || put(&live,selected.rows+i,true,error));
    if (ok && !primary) ok=qa_input_seat_replace_bindings(input,live.rows,live.count,error);
    clear(&live);
    if (!ok) { clear(&authored); clear(&selected); return false; }
    clear(&owner->selected); owner->authored=authored; owner->selected=selected;
    owner->initialized=owner->collecting=true; owner->selection_applied=!primary;
    return true;
}
bool frontend_authored_bindings_defaults(frontend_authored_bindings *owner,qa_input_seat *input,
    qa_console_dialect dialect,qa_error *error)
{ return defaults(owner,NULL,input,dialect,error); }
bool frontend_authored_bindings_secondary(frontend_authored_bindings *owner,
    const frontend_authored_bindings *primary,qa_input_seat *input,qa_console_dialect dialect,qa_error *error)
{ return defaults(owner,primary,input,dialect,error); }
static qa_physical_input canonical_input(qa_physical_input input)
{
    if (input.kind>=QA_PHYSICAL_BUTTON) input.device=0;
    return input;
}
static bool override_add(qa_physical_input **rows,size_t *count,qa_physical_input input,qa_error *error)
{
    input=canonical_input(input);
    for (size_t i=0;i<*count;++i) if (same_input((*rows)[i],input)) return true;
    if (*count>=SIZE_MAX/sizeof(**rows))
        return fail(error,QA_ERROR_MEMORY,"Retained binding overrides exceed storage");
    qa_physical_input *next=realloc(*rows,(*count+1)*sizeof(*next));
    if (!next) return fail(error,QA_ERROR_MEMORY,"Retaining live binding choices");
    *rows=next; next[(*count)++]=input; return true;
}
static bool same_target(const qa_input_binding *left,const qa_input_binding *right)
{
    return left && right && left->kind==right->kind &&
        (left->kind==QA_BIND_COMMAND?!strcmp(left->command,right->command):left->action==right->action);
}
bool frontend_authored_bindings_restore_previous(frontend_authored_bindings *owner,
    const frontend_authored_bindings *previous,const qa_input_seat *previous_input,
    qa_input_seat *input,qa_error *error)
{
    if (!owner || !previous || owner==previous || !owner->initialized || !owner->collecting ||
        !previous->initialized || previous->collecting || !previous_input || !input || previous_input==input)
        return fail(error,QA_ERROR_ARGUMENT,"Binding archive carry requires the same retained completed seat");
    binding_set live={0},current={0}; qa_physical_input *overridden=NULL; size_t count=0;
    bool ok=seat_set(&live,previous_input,false,error);
    for (size_t i=0;ok && i<live.count;++i) {
        qa_input_binding binding=live.rows[i]; binding.input=canonical_input(binding.input);
        ok=put(&current,&binding,true,error);
    }
    for (size_t i=0;ok && i<owner->overridden_count;++i)
        ok=override_add(&overridden,&count,owner->overridden[i],error);
    for (size_t i=0;ok && i<previous->overridden_count;++i)
        ok=override_add(&overridden,&count,previous->overridden[i],error);
    for (size_t i=0;ok && i<previous->selected.count;++i) {
        const qa_input_binding *expected=previous->selected.rows+i;
        size_t actual=find(&current,canonical_input(expected->input));
        if (actual==current.count || !same_target(expected,current.rows+actual))
            ok=override_add(&overridden,&count,expected->input,error);
    }
    for (size_t i=0;ok && i<current.count;++i) {
        const qa_input_binding *actual=current.rows+i;
        size_t expected=find(&previous->selected,actual->input);
        if (expected==previous->selected.count || !same_target(previous->selected.rows+expected,actual))
            ok=override_add(&overridden,&count,actual->input,error);
    }
    if (ok) ok=qa_input_seat_replace_bindings(input,live.rows,live.count,error);
    clear(&live); clear(&current);
    if (!ok) { free(overridden); return false; }
    free(owner->overridden); owner->overridden=overridden; owner->overridden_count=count;
    owner->all_chosen=previous->all_chosen; owner->selection_applied=false;
    return true;
}
void frontend_authored_bindings_profile(frontend_authored_bindings *owner)
{ if (owner) owner->all_chosen=true; }
void frontend_authored_bindings_finish(frontend_authored_bindings *owner)
{ if (owner) owner->collecting=false; }
bool frontend_authored_bindings_ready(const frontend_authored_bindings *owner)
{ return owner && owner->initialized; }
bool frontend_authored_bindings_observe(frontend_authored_bindings *owner,
    const qa_command_invocation *command,qa_error *error)
{
    if (!owner || !command) return fail(error,QA_ERROR_ARGUMENT,"Missing admitted binding command");
    if (!owner->collecting || !command->argc) return true;
    if (equal(command->argv[0],"unbindall")) { owner->all_chosen=true; return true; }
    if (command->argc<2 || (!equal(command->argv[0],"bind") && !equal(command->argv[0],"unbind"))) return true;
    qa_physical_input input;
    if (!qa_input_physical_parse(command->argv[1],0,&input)) return true;
    for (size_t i=0;i<owner->overridden_count;++i) if (same_input(owner->overridden[i],input)) return true;
    if (owner->overridden_count>=SIZE_MAX/sizeof(*owner->overridden))
        return fail(error,QA_ERROR_MEMORY,"Explicit binding overrides exceed storage");
    qa_physical_input *rows=realloc(owner->overridden,(owner->overridden_count+1)*sizeof(*rows));
    if (!rows) return fail(error,QA_ERROR_MEMORY,"Retaining explicit binding override");
    owner->overridden=rows; rows[owner->overridden_count++]=input; return true;
}
typedef struct weapon_slot { const char *item,*key; } weapon_slot;
static const weapon_slot slots[]={
    {"q1:weapon/axe","1"},{"q1:weapon/shotgun","2"},{"q1:weapon/supershotgun","3"},
    {"q1:weapon/nailgun","4"},{"q1:weapon/supernailgun","5"},{"q1:weapon/grenadelauncher","6"},
    {"q1:weapon/rocketlauncher","7"},{"q1:weapon/lightning","8"},
    {"q1:weapon/hipnotic:laser","9"},{"q1:weapon/hipnotic:mjolnir","0"},{"q1:weapon/mg3:laser","9"},
    {"q2:weapon_blaster","1"},{"q2:weapon_shotgun","2"},{"q2:weapon_supershotgun","3"},
    {"q2:weapon_machinegun","4"},{"q2:weapon_chaingun","5"},{"q2:ammo_grenades","g"},
    {"q2:weapon_grenadelauncher","6"},{"q2:weapon_rocketlauncher","7"},
    {"q2:weapon_hyperblaster","8"},{"q2:weapon_railgun","9"},{"q2:weapon_bfg","0"},
    {"q3:weapon/gauntlet","1"},{"q3:weapon/machinegun","2"},{"q3:weapon/shotgun","3"},
    {"q3:weapon/grenadelauncher","4"},{"q3:weapon/rocketlauncher","5"},{"q3:weapon/lightning","6"},
    {"q3:weapon/railgun","7"},{"q3:weapon/plasmagun","8"},{"q3:weapon/bfg","9"},{"q3:weapon/grapple","0"}};
static bool has_item(qa_strings *strings,const qa_item_definition *items,size_t count,const char *wanted)
{
    for (size_t i=0;i<count;++i) if (items[i].weapon) {
        const char *id=qa_strings_cstr(strings,items[i].item);
        if (id && !strcmp(id,wanted)) return true;
    }
    return false;
}
static bool selected(binding_set *set,qa_console_dialect dialect,qa_strings *strings,
    const qa_item_definition *items,size_t count,qa_error *error)
{
    if (!general(set,dialect,error)) return false;
    for (size_t i=0;i<sizeof(slots)/sizeof(*slots);++i) {
        if (!has_item(strings,items,count,slots[i].item)) continue;
        qa_input_binding binding={.kind=QA_BIND_COMMAND};
        if (!qa_input_physical_parse(slots[i].key,0,&binding.input) || find(set,binding.input)<set->count) continue;
        size_t length=strlen(slots[i].item);
        char *command=malloc(length+5);
        if (!command) return fail(error,QA_ERROR_MEMORY,"Retaining selected weapon binding");
        memcpy(command,"use ",4); memcpy(command+4,slots[i].item,length+1); binding.command=command;
        bool ok=put(set,&binding,false,error); free(command); if (!ok) return false;
    }
    return true;
}
bool frontend_authored_bindings_seed(frontend_authored_bindings *owner,qa_console_dialect dialect,
    qa_strings *strings,const qa_item_definition *items,size_t count,qa_error *error)
{
    if (!owner || owner->initialized || !strings || (count && !items))
        return fail(error,QA_ERROR_ARGUMENT,"Binding seed requires the actual unfinished source catalog");
    binding_set next={0};
    if (!selected(&next,dialect,strings,items,count,error)) { clear(&next); return false; }
    clear(&owner->selected); owner->selected=next; return true;
}
static bool selected_unchanged(const binding_set *set,qa_console_dialect dialect,
    qa_strings *strings,const qa_item_definition *items,size_t count)
{
    size_t index=0,general_count=0; qa_input_binding binding;
    while (qa_input_default_binding_at(dialect,0,index,&binding)) {
        if (index>=set->count || !same_input(binding.input,set->rows[index].input) ||
            set->rows[index].kind!=QA_BIND_COMMAND || strcmp(binding.command,set->rows[index].command)) return false;
        ++index;
    }
    general_count=index;
    bool used[sizeof(slots)/sizeof(*slots)]={0};
    for (size_t i=0;i<sizeof(slots)/sizeof(*slots);++i) {
        if (!has_item(strings,items,count,slots[i].item)) continue;
        qa_physical_input input;
        if (!qa_input_physical_parse(slots[i].key,0,&input)) return false;
        bool occupied=false;
        for (size_t j=0;j<general_count;++j)
            if (same_input(input,set->rows[j].input)) { occupied=true; break; }
        for (size_t j=0;!occupied && j<i;++j)
            if (used[j] && !strcmp(slots[j].key,slots[i].key)) occupied=true;
        if (occupied) continue;
        used[i]=true;
        if (index>=set->count || !same_input(input,set->rows[index].input) ||
            set->rows[index].kind!=QA_BIND_COMMAND || strncmp(set->rows[index].command,"use ",4) ||
            strcmp(set->rows[index].command+4,slots[i].item)) return false;
        ++index;
    }
    return index==set->count;
}
static qa_input_binding remap(qa_input_binding binding,int32_t controller)
{
    if (binding.input.kind>=QA_PHYSICAL_BUTTON) binding.input.device=controller;
    return binding;
}
bool frontend_authored_bindings_select(frontend_authored_bindings *owner,qa_input_seat *input,
    qa_console_dialect dialect,qa_strings *strings,const qa_item_definition *items,size_t count,
    int32_t controller,qa_error *error)
{
    if (!owner || !owner->initialized || !input || !strings || (count && !items) || controller<0 ||
        (unsigned)dialect>QA_CONSOLE_Q3)
        return fail(error,QA_ERROR_ARGUMENT,"Selected defaults require actual seat item definitions");
    if (owner->selection_applied && selected_unchanged(&owner->selected,dialect,strings,items,count)) return true;
    binding_set next={0},live={0};
    bool ok=selected(&next,dialect,strings,items,count,error);
    if (ok && !owner->all_chosen) ok=seat_set(&live,input,false,error);
    for (size_t i=0;ok && !owner->all_chosen && i<next.count;++i) {
        bool overridden=false;
        for (size_t j=0;j<owner->overridden_count;++j)
            if (same_input(owner->overridden[j],next.rows[i].input)) { overridden=true; break; }
        qa_input_binding binding=remap(next.rows[i],controller);
        if (!overridden) ok=put(&live,&binding,true,error);
    }
    if (ok && !owner->all_chosen) ok=qa_input_seat_replace_bindings(input,live.rows,live.count,error);
    clear(&live);
    if (!ok) { clear(&next); return false; }
    clear(&owner->selected); owner->selected=next; owner->selection_applied=true; return true;
}
bool frontend_authored_bindings_reset(frontend_authored_bindings *owner,qa_input_seat *input,
    int32_t controller,qa_error *error)
{
    if (!owner || !owner->initialized || !input || controller<0)
        return fail(error,QA_ERROR_ARGUMENT,"Reset requires actual authored binding defaults");
    binding_set reset={0}; bool ok=true;
    for (size_t i=0;ok && i<owner->authored.count;++i)
        if (!selection_command(owner->authored.rows+i)) {
            qa_input_binding binding=remap(owner->authored.rows[i],controller);
            ok=put(&reset,&binding,false,error);
        }
    for (size_t i=0;ok && i<owner->selected.count;++i) {
        qa_input_binding binding=remap(owner->selected.rows[i],controller);
        ok=put(&reset,&binding,false,error);
    }
    if (ok) ok=qa_input_seat_replace_bindings(input,reset.rows,reset.count,error);
    clear(&reset);
    if (ok) { owner->all_chosen=owner->selection_applied=true; free(owner->overridden); owner->overridden=NULL; owner->overridden_count=0; }
    return ok;
}
static bool physical_fields(qa_source_save_io *io,qa_physical_input *input)
{
    uint32_t kind=input->kind;
    if (!qa_source_save_u32(io,&kind)) return false;
    if (kind>QA_PHYSICAL_AXIS) return fail(io->error,QA_ERROR_FORMAT,"Invalid authored physical kind");
    if (!qa_source_save_i32(io,&input->device) || !qa_source_save_u32(io,&input->code) ||
        !qa_source_save_bool(io,&input->positive)) return false;
    input->kind=(qa_physical_kind)kind;
    return qa_input_physical_valid(*input) || fail(io->error,QA_ERROR_FORMAT,"Invalid authored physical control");
}
static bool set_fields(binding_set *set,qa_source_save_io *io)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ; size_t count=set->count;
    if (!qa_source_save_count(io,&count,reading?(io->input.size-io->offset)/18:SIZE_MAX)) return false;
    for (size_t i=0;i<count;++i) {
        qa_input_binding binding=reading?(qa_input_binding){0}:set->rows[i];
        uint32_t kind=binding.kind,action=binding.action;
        if (!physical_fields(io,&binding.input) || !qa_source_save_u32(io,&kind)) return false;
        if (kind>QA_BIND_COMMAND) return fail(io->error,QA_ERROR_FORMAT,"Invalid authored binding kind");
        binding.kind=(qa_binding_kind)kind;
        if (kind==QA_BIND_COMMAND) {
            size_t length=reading?0:strlen(binding.command);
            if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX-1) || length==SIZE_MAX) return false;
            char *text=reading?malloc(length+1):(char *)binding.command;
            if (!text) return fail(io->error,QA_ERROR_MEMORY,"Decoding authored binding command");
            bool ok=qa_source_save_bytes(io,text,length);
            if (reading) {
                text[length]=0; binding.command=text;
                if (ok && memchr(text,0,length)) ok=fail(io->error,QA_ERROR_FORMAT,"Authored command contains embedded NUL");
                if (ok && find(set,binding.input)<set->count) ok=fail(io->error,QA_ERROR_FORMAT,"Duplicate authored control");
                if (ok) ok=put(set,&binding,false,io->error);
                free(text);
            }
            if (!ok) return false;
        } else {
            if (!qa_source_save_u32(io,&action)) return false;
            if (action>=QA_INPUT_ACTION_COUNT) return fail(io->error,QA_ERROR_FORMAT,"Invalid authored input action");
            binding.action=(qa_input_action)action;
            if (reading && find(set,binding.input)<set->count)
                return fail(io->error,QA_ERROR_FORMAT,"Duplicate authored control");
            if (reading && !put(set,&binding,false,io->error)) return false;
        }
    }
    return true;
}
bool frontend_authored_bindings_fields(frontend_authored_bindings *owner,qa_source_save_io *io)
{
    if (!owner || !io || (io->direction==QA_SOURCE_SAVE_READ &&
        (owner->initialized || owner->authored.count || owner->selected.count || owner->overridden_count)))
        return fail(io?io->error:NULL,QA_ERROR_ARGUMENT,"Binding metadata import requires an empty owner");
    if (!qa_source_save_bool(io,&owner->initialized) || !qa_source_save_bool(io,&owner->all_chosen) ||
        !qa_source_save_bool(io,&owner->collecting) || !qa_source_save_bool(io,&owner->selection_applied) ||
        !set_fields(&owner->authored,io) || !set_fields(&owner->selected,io)) return false;
    bool reading=io->direction==QA_SOURCE_SAVE_READ; size_t count=owner->overridden_count;
    if (!qa_source_save_count(io,&count,reading?(io->input.size-io->offset)/13:SIZE_MAX/sizeof(*owner->overridden))) return false;
    if (reading && count) {
        owner->overridden=calloc(count,sizeof(*owner->overridden));
        if (!owner->overridden) return fail(io->error,QA_ERROR_MEMORY,"Decoding explicit binding overrides");
        owner->overridden_count=count;
    }
    for (size_t i=0;i<count;++i) {
        if (!physical_fields(io,owner->overridden+i)) return false;
        for (size_t j=0;j<i;++j) if (same_input(owner->overridden[i],owner->overridden[j]))
            return fail(io->error,QA_ERROR_FORMAT,"Duplicate explicit binding override");
    }
    return owner->initialized || (!owner->authored.count && !count && !owner->selection_applied &&
        !owner->all_chosen && !owner->collecting) || fail(io->error,QA_ERROR_FORMAT,"Uninitialized binding metadata has authored state");
}
