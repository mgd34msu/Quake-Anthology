#include "internal.h"
#include "qa/text.h"
#include "qa/command_lexer.h"
#include "qa/input_release.h"
#include <float.h>
#include <stdio.h>

static bool source_storage_prepare(qa_input_seat *seat,size_t physical,size_t bindings,
    size_t commands,qa_error *error) {
    if(physical<seat->physical_capacity) physical=seat->physical_capacity;
    if(bindings>SIZE_MAX-physical || seat->held_count>SIZE_MAX-physical-bindings) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Input source storage overflow");return false;
    }
    size_t held=physical+bindings+seat->held_count;
    if(commands>SIZE_MAX-held) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Input source storage overflow");return false;
    }
    if(held>seat->held_capacity) {
        size_t capacity=seat->held_capacity?seat->held_capacity:16;
        while(capacity<held && capacity<=SIZE_MAX/2) capacity*=2;
        if(capacity<held) capacity=held;
        if(capacity>SIZE_MAX/sizeof(*seat->held)) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Input source storage overflow");return false;
        }
        seat->held_storage.sealed=false;
        qa_held_binding *rows=qa_arena_grow(&seat->held_storage,seat->held,
            seat->held_count*sizeof(*rows),capacity*sizeof(*rows),_Alignof(qa_held_binding),error);
        qa_arena_seal(&seat->held_storage);
        if(!rows) return false;
        seat->held=rows;seat->held_capacity=capacity;
    }
    size_t needed=held+commands;
    for(size_t action=0;action<QA_INPUT_ACTION_COUNT;++action) {
        qa_input_button *button=&seat->buttons[action];
        size_t capacity=button->sources?button->capacity:4;
        while(capacity<needed && capacity<=SIZE_MAX/2) capacity*=2;
        if(capacity<needed) capacity=needed;
        if(!qa_input_button_prepare(button,capacity,error)) return false;
        qa_arena_seal(&button->source_storage);
    }
    seat->physical_capacity=physical;
    return true;
}
bool qa_input_seat_prepare_sources(qa_input_seat *seat,size_t physical,qa_error *error) {
    if(!seat) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Input source preparation requires its seat");return false;}
    if(!source_storage_prepare(seat,physical,seat->binding_count,
        qa_strings_count(seat->command_sources),error)) return false;
    seat->physical_prepared=true;return true;
}
bool qa_input_reserve(void **data, size_t *capacity, size_t needed, size_t stride,
                      qa_error *error) {
    if (needed <= *capacity)
        return true;
    size_t grown = *capacity ? *capacity : 16;
    while (grown < needed) {
        if (grown > SIZE_MAX / 2) {
            grown = needed;
            break;
        }
        grown *= 2;
    }
    if (grown > SIZE_MAX / stride) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Input storage overflow");
        return false;
    }
    void *next = realloc(*data, grown * stride);
    if (!next) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating input storage");
        return false;
    }
    *data = next;
    *capacity = grown;
    return true;
}
bool qa_input_physical_equal(qa_physical_input a, qa_physical_input b) {
    return a.kind == b.kind && a.code == b.code &&
           (a.kind < QA_PHYSICAL_BUTTON || a.device == b.device) &&
           (a.kind != QA_PHYSICAL_AXIS || a.positive == b.positive);
}
uint64_t qa_input_physical_source(qa_physical_input input) {
    return ((uint64_t)input.kind << 61) | ((uint64_t)(uint32_t)input.device << 24) |
           ((uint64_t)input.code << 1) | (uint64_t)input.positive;
}
static bool command_source_span(qa_input_seat *s,qa_bytes text,uint64_t *out,qa_error *error) {
    if(!s->command_sources && !qa_strings_create(&s->command_sources,error)) return false;
    qa_string_id id=qa_strings_find(s->command_sources,text);
    if(!id) {
        size_t commands=qa_strings_count(s->command_sources);
        if(commands==SIZE_MAX || !source_storage_prepare(s,s->physical_capacity,
            s->binding_count,commands+1,error) ||
           !qa_strings_intern(s->command_sources,text,&id,error)) return false;
    }
    *out = UINT64_C(0x8000000000000000) | id;
    return true;
}
bool qa_input_command_source(qa_input_seat *s,const char *text,uint64_t *out,qa_error *error) {
    return command_source_span(s,(qa_bytes){(const uint8_t *)text,strlen(text)},out,error);
}
bool qa_input_physical_valid(qa_physical_input input) {
    if (input.kind == QA_PHYSICAL_KEY)
        return input.code <= UINT16_MAX;
    if (input.kind == QA_PHYSICAL_MOUSE)
        return input.code > 0 && input.code <= 255;
    if (input.kind == QA_PHYSICAL_BUTTON)
        return input.device >= 0 && input.code <= 255;
    return input.kind == QA_PHYSICAL_AXIS && input.device >= 0 && input.code < QA_AXIS_COUNT;
}
void qa_input_binding_record_release(qa_binding_record *binding) {
    if (binding && --binding->references == 0)
        free(binding);
}
static qa_binding_record *binding_find(const qa_input_seat *s, qa_physical_input input,
                                       size_t *index) {
    for (size_t i = 0; i < s->binding_count; ++i)
        if (qa_input_physical_equal(s->bindings[i]->view.input, input)) {
            if (index)
                *index = i;
            return s->bindings[i];
        }
    return NULL;
}
static bool binding_text_prepare(qa_input_seat *, const qa_input_seat *,
                                 const qa_binding_record *, qa_error *);
static bool binding_sources_prepare(qa_input_seat *, const qa_input_seat *,
                                    const qa_binding_record *, qa_error *);
static bool binding_equal(const qa_input_binding *a, const qa_input_binding *b) {
    return qa_input_physical_equal(a->input, b->input) &&
        a->kind == b->kind && (a->kind == QA_BIND_COMMAND
            ? !strcmp(a->command, b->command) : a->action == b->action);
}
bool qa_input_bindings_equal(const qa_input_seat *a, const qa_input_seat *b) {
    if (a->binding_count != b->binding_count) return false;
    for (size_t i = 0; i < a->binding_count; ++i)
        if (!binding_equal(&a->bindings[i]->view, &b->bindings[i]->view)) return false;
    return true;
}
static bool context_ready(const qa_input_seat_options *options,
                          const qa_command_context *context, qa_error *error) {
    if (!options || !context || options->seat >= 4 || context->origin != QA_COMMAND_SEAT ||
        context->dialect < QA_RULESET_NETQUAKE || context->dialect > QA_RULESET_Q3 ||
        context->script || context->console_text) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input command context");
        return false;
    }
    if (options->context_ready)
        return options->context_ready(options->context_user, options->seat, context, error);
    if (context->seat != options->seat || !context->direct || context->session ||
        context->owner || context->client || context->registry || context->generation ||
        context->actor.registry || context->actor.generation || context->actor.slot) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input command context lacks its actual owner qualifier");
        return false;
    }
    return true;
}
qa_input_seat *qa_input_seat_bindings_create(qa_error *error) {
    qa_input_seat *s = calloc(1, sizeof(*s));
    if (!s) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating input seat");
        return NULL;
    }
    s->focused = true;
    s->next_ui = 1;
    qa_arena_seal(&s->binding_text);
    return s;
}
qa_input_seat *qa_input_seat_create(const qa_input_seat_options *o, qa_error *error) {
    if (!o || !o->console || !qa_gamepad_tuning_valid(&o->gamepad)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid local input seat");
        return NULL;
    }
    if (!context_ready(o, &o->context, error)) return NULL;
    qa_input_seat *s = qa_input_seat_bindings_create(error);
    if (!s) return NULL;
    s->options = *o;
    if (!qa_strings_create(&s->command_sources, error) ||
        !binding_sources_prepare(s, NULL, NULL, error) ||
        !qa_input_reserve((void **)&s->ui, &s->ui_capacity, 1, sizeof(*s->ui), error)) {
        qa_input_seat_destroy(s);
        return NULL;
    }
    s->ui[s->ui_count++] = (qa_ui_record){.handler = o->ui, .user = o->ui_user, .active = true};
    return s;
}
void qa_input_seat_destroy(qa_input_seat *s) {
    if (!s || s->release)
        return;
    /* The owner releases input while its console is still alive. Destruction
     * then has no callbacks and remains safe during failed construction. */
    for (size_t i = 0; i < s->binding_count; ++i)
        qa_input_binding_record_release(s->bindings[i]);
    for (size_t i = 0; i < s->held_count; ++i)
        qa_input_binding_record_release(s->held[i].binding);
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
        qa_input_button_destroy(&s->buttons[i]);
    qa_strings_destroy(s->command_sources);
    qa_arena_destroy(&s->release_storage);
    free(s->bindings);
    qa_arena_destroy(&s->held_storage);
    free(s->ui);
    free(s->catchers);
    qa_arena_destroy(&s->binding_text);
    free(s->recipient_pending);
    while (s->recipient_retired) {
        qa_input_recipient_handoff *next=s->recipient_retired->next;
        free(s->recipient_retired); s->recipient_retired=next;
    }
    free(s);
}
qa_command_context qa_input_seat_context(const qa_input_seat *s) { return s->options.context; }
uint32_t qa_input_seat_ordinal(const qa_input_seat *s) { return s->options.seat; }
bool qa_input_seat_context_ready(const qa_input_seat *s,
                                 const qa_command_context *context, qa_error *error) {
    if (!s || !qa_console_idle(s->options.console)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input context requires its returned physical owner");
        return false;
    }
    return context_ready(&s->options, context, error);
}
void qa_input_seat_context_publish(qa_input_seat *s, const qa_command_context *context) {
    s->options.context = *context;
}
bool qa_input_seat_recipient_read(const qa_input_seat *s,qa_console **console,qa_cvars **cvars,
    qa_command_context *command) {
    if (!s || !console || !cvars || !command) return false;
    *console=s->options.console; *cvars=s->options.cvars; *command=s->options.context; return true;
}
bool qa_input_recipient_command_equal(const qa_command_context *a,const qa_command_context *b) {
    return a && b && !a->script && !b->script && a->owner==b->owner && a->session==b->session &&
        a->client==b->client && a->seat==b->seat && a->dialect==b->dialect && a->origin==b->origin &&
        a->direct==b->direct && a->console_text==b->console_text && a->registry==b->registry &&
        a->generation==b->generation && a->cvar_view==b->cvar_view && qa_actor_id_equal(a->actor,b->actor);
}
static bool recipient_endpoints_ready(const qa_input_seat *s,qa_console *console,qa_cvars *cvars,
    const qa_command_context *command) {
    if (!s || !console || !cvars || !command || !qa_console_idle(s->options.console) ||
        !qa_console_idle(console) || !qa_cvars_same_store(qa_console_cvars(console),cvars) ||
        command->cvar_view!=qa_cvars_view_identity(cvars) || !qa_console_context_bound(console,command) ||
        command->origin!=QA_COMMAND_SEAT || command->script || command->console_text ||
        command->dialect<QA_RULESET_NETQUAKE || command->dialect>QA_RULESET_Q3) return false;
    return true;
}
static bool recipient_release_ready(const qa_input_seat *s,qa_console *console,qa_cvars *cvars,
    const qa_command_context *command) {
    if (!recipient_endpoints_ready(s,console,cvars,command)) return false;
    qa_input_release_scope all={.all=true,.controller=-1};
    qa_input_release_scope empty={.controller=-1};
    return s->release?(qa_input_release_completed_is(s->release,s,&all) ||
        (!qa_input_seat_has_held(s) && qa_input_release_completed_empty_is(s->release,s,&empty))):
        !qa_input_seat_has_held(s);
}
static bool recipient_changes_owner(const qa_input_seat *s,qa_console *console,qa_cvars *cvars) {
    return s->options.context.owner && (s->options.console!=console || s->options.cvars!=cvars);
}
bool qa_input_seat_recipient_ready_is(const qa_input_seat *s,qa_console *console,qa_cvars *cvars,
    const qa_command_context *command) {
    if (!recipient_release_ready(s,console,cvars,command)) return false;
    const qa_input_recipient_handoff *pending=s->recipient_pending;
    return !recipient_changes_owner(s,console,cvars) || (pending &&
        pending->former.console==s->options.console && pending->former.cvars==s->options.cvars &&
        qa_input_recipient_command_equal(&pending->former.context,&s->options.context) &&
        pending->destination_console==console && pending->destination_cvars==cvars &&
        qa_input_recipient_command_equal(&pending->destination,command));
}
static bool recipient_stage(qa_input_seat *s,qa_console *console,qa_cvars *cvars,
    const qa_command_context *command,qa_error *error) {
    qa_input_seat_options options=s->options;
    options.console=console; options.cvars=cvars; options.context=*command;
    if (!context_ready(&options,command,error)) return false;
    if (!recipient_changes_owner(s,console,cvars)) {
        free(s->recipient_pending); s->recipient_pending=NULL; return true;
    }
    if (!s->recipient_pending) s->recipient_pending=calloc(1,sizeof(*s->recipient_pending));
    if (!s->recipient_pending) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining the former physical input recipient"); return false;
    }
    *s->recipient_pending=(qa_input_recipient_handoff){.former=s->options,
        .destination_console=console,.destination_cvars=cvars,.destination=*command};
    return true;
}
bool qa_input_seat_recipient_ready(qa_input_seat *s,qa_console *console,qa_cvars *cvars,
    const qa_command_context *command,qa_error *error) {
    if (!recipient_release_ready(s,console,cvars,command)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Input recipient requires its returned consoles and completed ALL release");
        return false;
    }
    return recipient_stage(s,console,cvars,command,error);
}
bool qa_input_seat_recipient_retirement_ready(qa_input_seat *s,const qa_input_release *release,
    qa_console *console,qa_cvars *cvars,const qa_command_context *command,
    qa_console_release_disposition disposition,qa_console_release_retirement_fn guard,void *context,qa_error *error) {
    qa_input_release_scope all={.all=true,.controller=-1};
    if (!recipient_endpoints_ready(s,console,cvars,command) || !release || s->release!=release ||
        !qa_input_release_retirement_scope_ready(release,s,&all,disposition,guard,context,error)) {
        if (!error || error->code==QA_OK)
            qa_error_set(error,QA_ERROR_ARGUMENT,0,"Input handoff lost its exact qualified ALL retirement history");
        return false;
    }
    return recipient_stage(s,console,cvars,command,error);
}
void qa_input_seat_recipient_publish(qa_input_seat *s,qa_console *console,qa_cvars *cvars,
    const qa_command_context *command) {
    if (!qa_input_seat_recipient_ready_is(s,console,cvars,command)) return;
    if (recipient_changes_owner(s,console,cvars)) {
        qa_input_recipient_handoff *receipt=s->recipient_pending; s->recipient_pending=NULL;
        receipt->next=s->recipient_retired; s->recipient_retired=receipt;
    }
    for (qa_input_recipient_handoff **at=&s->recipient_retired;*at;) {
        if ((*at)->former.console==console && (*at)->former.cvars==cvars) {
            qa_input_recipient_handoff *receipt=*at; *at=receipt->next; free(receipt);
        } else at=&(*at)->next;
    }
    s->options.console=console; s->options.cvars=cvars; s->options.context=*command;
}
bool qa_input_seat_recipient_retired_is(const qa_input_seat *s,const qa_console *console,const qa_cvars *cvars,
    const qa_command_context *command) {
    if (!s || !console || !cvars || !command) return false;
    for (const qa_input_recipient_handoff *at=s->recipient_retired;at;at=at->next)
        if (at->former.console==console && at->former.cvars==cvars &&
            qa_input_recipient_command_equal(&at->former.context,command)) return true;
    return false;
}
bool qa_input_seat_recipient_retired_consume(qa_input_seat *s,const qa_console *console,const qa_cvars *cvars,
    const qa_command_context *command,qa_error *error) {
    if (!s || !qa_console_idle(console)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Former input recipient still has an entered command"); return false;
    }
    for (qa_input_recipient_handoff **at=&s->recipient_retired;*at;at=&(*at)->next)
        if ((*at)->former.console==console && (*at)->former.cvars==cvars &&
            qa_input_recipient_command_equal(&(*at)->former.context,command)) {
            qa_input_recipient_handoff *receipt=*at; *at=receipt->next; free(receipt); return true;
        }
    qa_error_set(error,QA_ERROR_ARGUMENT,0,"Former input recipient has no completed physical handoff"); return false;
}
qa_input_focus qa_input_seat_focus(const qa_input_seat *s) { return s->focus; }
bool qa_input_seat_focused(const qa_input_seat *s) { return s->focused; }
bool qa_input_seat_action_active(const qa_input_seat *s, qa_input_action action) {
    return s && (unsigned)action < QA_INPUT_ACTION_COUNT && s->buttons[action].count != 0;
}
bool qa_input_seat_has_held(const qa_input_seat *s) {
    if (s->held_count)
        return true;
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
        if (s->buttons[i].count)
            return true;
    return false;
}
bool qa_input_seat_key_down(const qa_input_seat *s, qa_physical_input input) {
    if (!s) return false;
    for (size_t i = 0; i < s->held_count; ++i)
        if (qa_input_physical_equal(s->held[i].input, input)) return true;
    return false;
}
uint32_t qa_input_seat_catcher(const qa_input_seat *s, uint64_t owner) {
    uint32_t mask = 0;
    if (!s) return mask;
    for (size_t i = 0; i < s->catcher_count; ++i) {
        if (owner && s->catchers[i].owner == owner) return s->catchers[i].mask;
        if (!owner) mask |= s->catchers[i].mask;
    }
    if (!owner) {
        if (s->focus == QA_INPUT_CONSOLE) mask |= QA_INPUT_CATCH_CONSOLE;
        else if (s->focus == QA_INPUT_CHAT) mask |= QA_INPUT_CATCH_CHAT;
        else if (s->focus == QA_INPUT_UI) mask |= QA_INPUT_CATCH_UI;
    }
    return mask;
}
void qa_input_seat_retire_catcher(qa_input_seat *s, uint64_t owner) {
    if (!qa_input_release_mutation_access(s,NULL)) return;
    if (!s || !owner) return;
    for (size_t i = 0; i < s->catcher_count; ++i) if (s->catchers[i].owner == owner) {
        memmove(s->catchers + i, s->catchers + i + 1,
                (s->catcher_count - i - 1) * sizeof(*s->catchers));
        --s->catcher_count; return;
    }
}
bool qa_input_seat_set_catcher(qa_input_seat *s, uint64_t owner, uint32_t mask, qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (!s || !owner) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input catcher requires a seat and provider owner"); return false;
    }
    if (!mask) { qa_input_seat_retire_catcher(s, owner); return true; }
    for (size_t i = 0; i < s->catcher_count; ++i) if (s->catchers[i].owner == owner) {
        s->catchers[i].mask = mask; return true;
    }
    if (!qa_input_reserve((void **)&s->catchers, &s->catcher_capacity,
                          s->catcher_count + 1, sizeof(*s->catchers), error)) return false;
    s->catchers[s->catcher_count++] = (qa_input_catcher){owner, mask}; return true;
}
size_t qa_input_seat_catcher_count(const qa_input_seat *s) { return s ? s->catcher_count : 0; }
bool qa_input_seat_catcher_at(const qa_input_seat *s, size_t index, qa_input_catcher *out) {
    if (!s || !out || index >= s->catcher_count) return false;
    *out = s->catchers[index]; return true;
}
qa_gamepad_input *qa_input_seat_gamepad(qa_input_seat *s) { return &s->gamepad; }
qa_gamepad_tuning *qa_input_seat_gamepad_tuning(qa_input_seat *s) { return &s->options.gamepad; }
bool qa_input_seat_profile(qa_input_seat *s, qa_ruleset_id dialect, qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (dialect < QA_RULESET_NETQUAKE || dialect > QA_RULESET_Q3 || qa_input_seat_has_held(s)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Input profile requires valid dialect and released keys");
        return false;
    }
    s->options.context.dialect = dialect;
    return true;
}
bool qa_input_seat_bind(qa_input_seat *s, const qa_input_binding *binding, qa_error *error) {
    if (!qa_input_release_action_access(s,error)) return false;
    if (!s || !binding || !qa_input_physical_valid(binding->input) ||
        (binding->kind != QA_BIND_ACTION && binding->kind != QA_BIND_COMMAND) ||
        (binding->kind == QA_BIND_ACTION &&
         (binding->action < 0 || binding->action >= QA_INPUT_ACTION_COUNT)) ||
        (binding->kind == QA_BIND_COMMAND && !binding->command)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input binding");
        return false;
    }
    size_t index = 0;
    qa_binding_record *old = binding_find(s, binding->input, &index);
    if (old && binding_equal(&old->view, binding)) return true;
    size_t n = binding->kind == QA_BIND_COMMAND ? strlen(binding->command) : 0;
    if (n > SIZE_MAX - sizeof(qa_binding_record) - 1) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Binding length overflow");
        return false;
    }
    qa_binding_record *record = malloc(sizeof(*record) + n + 1);
    if (!record) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating key binding");
        return false;
    }
    *record = (qa_binding_record){.view = *binding, .references = 1};
    record->text[0] = 0;
    if (binding->kind == QA_BIND_COMMAND) {
        memcpy(record->text, binding->command, n + 1);
        record->view.command = record->text;
    } else
        record->view.command = NULL;
    if (!binding_text_prepare(s, NULL, record, error)) {
        free(record);
        return false;
    }
    if (old) {
        s->bindings[index] = record;
        qa_input_binding_record_release(old);
        ++s->binding_revision;
        return true;
    }
    if (!qa_input_reserve((void **)&s->bindings, &s->binding_capacity, s->binding_count + 1,
                          sizeof(*s->bindings), error)) {
        free(record);
        return false;
    }
    s->bindings[s->binding_count++] = record;
    ++s->binding_revision;
    return true;
}
bool qa_input_seat_unbind(qa_input_seat *s, qa_physical_input input) {
    if (!qa_input_release_action_access(s,NULL)) return false;
    size_t index = 0;
    qa_binding_record *old = binding_find(s, input, &index);
    if (!old)
        return false;
    memmove(s->bindings + index, s->bindings + index + 1,
            (s->binding_count - index - 1) * sizeof(*s->bindings));
    --s->binding_count;
    qa_input_binding_record_release(old);
    ++s->binding_revision;
    return true;
}
void qa_input_seat_unbind_all(qa_input_seat *s) {
    if (!qa_input_release_action_access(s,NULL)) return;
    if (s->binding_count) ++s->binding_revision;
    for (size_t i = 0; i < s->binding_count; ++i)
        qa_input_binding_record_release(s->bindings[i]);
    s->binding_count = 0;
}
size_t qa_input_seat_binding_count(const qa_input_seat *s) { return s->binding_count; }
uint64_t qa_input_seat_binding_revision(const qa_input_seat *s) { return s->binding_revision; }
const qa_input_binding *qa_input_seat_binding_at(const qa_input_seat *s, size_t i) {
    return i < s->binding_count ? &s->bindings[i]->view : NULL;
}
const qa_input_binding *qa_input_seat_binding(const qa_input_seat *s, qa_physical_input input) {
    qa_binding_record *b = binding_find(s, input, NULL);
    return b ? &b->view : NULL;
}
static bool replace_bindings(qa_input_seat *s, qa_input_seat *candidate, qa_error *error) {
    if (!qa_input_binding_text_prepare(s, candidate, error)) return false;
    uint64_t revision = s->binding_revision + !qa_input_bindings_equal(s, candidate);
    qa_input_seat_unbind_all(s);
    free(s->bindings);
    s->bindings = candidate->bindings;
    s->binding_count = candidate->binding_count;
    s->binding_capacity = candidate->binding_capacity;
    s->binding_revision = revision;
    candidate->bindings = NULL;
    candidate->binding_count = candidate->binding_capacity = 0;
    return true;
}
static void binding_candidate_dispose(qa_input_seat *candidate) {
    qa_input_seat_unbind_all(candidate);
    free(candidate->bindings);
    qa_arena_destroy(&candidate->binding_text);
    qa_arena_destroy(&candidate->held_storage);
    for(size_t action=0;action<QA_INPUT_ACTION_COUNT;++action)
        qa_input_button_destroy(&candidate->buttons[action]);
    qa_strings_destroy(candidate->command_sources);
}
bool qa_input_seat_replace_bindings(qa_input_seat *s, const qa_input_binding *bindings,
                                    size_t count, qa_error *error) {
    if (!qa_input_release_action_access(s,error)) return false;
    if (!s || (count && !bindings)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid replacement binding list"); return false;
    }
    qa_input_seat candidate = {0};
    for (size_t i = 0; i < count; ++i) if (!qa_input_seat_bind(&candidate, &bindings[i], error)) {
        binding_candidate_dispose(&candidate); return false;
    }
    bool ok = replace_bindings(s, &candidate, error);
    binding_candidate_dispose(&candidate); return ok;
}
bool qa_input_seat_configuration_owned_is(const qa_input_seat *active,
                                         const qa_input_seat *candidate) {
    return active && candidate && active!=candidate &&
        active->options.seat==candidate->options.seat &&
        qa_console_idle(active->options.console) && qa_console_idle(candidate->options.console) &&
        qa_gamepad_tuning_valid(&candidate->options.gamepad);
}
bool qa_input_seat_configuration_ready(const qa_input_seat *active,
                                        const qa_input_seat *candidate, qa_error *error) {
    if (!qa_input_seat_configuration_owned_is(active,candidate)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Input configuration requires distinct returned owners of the same seat");
        return false;
    }
    return qa_input_seat_context_ready(active,&active->options.context,error) &&
        qa_input_seat_context_ready(candidate,&candidate->options.context,error) &&
        qa_input_binding_text_prepare((qa_input_seat *)active,candidate,error) &&
        qa_input_binding_text_prepare((qa_input_seat *)candidate,active,error);
}
bool qa_input_seat_configuration_clone(const qa_input_seat *source,qa_input_seat **out,qa_error *error) {
    if (!source || !out || *out || !qa_console_idle(source->options.console) ||
        source->binding_count>SIZE_MAX/sizeof(*source->bindings)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Input configuration clone requires its returned actual dictionary");
        return false;
    }
    for (size_t i=0;i<source->binding_count;++i) if (source->bindings[i]->references==SIZE_MAX) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Input binding reference count exceeds storage"); return false;
    }
    qa_input_seat *copy=source->options.console?qa_input_seat_create(&source->options,error):
        qa_input_seat_bindings_create(error);
    if (!copy) return false;
    copy->options=source->options;
    if (source->binding_count) {
        copy->bindings=malloc(source->binding_count*sizeof(*copy->bindings));
        if (!copy->bindings) {
            qa_input_seat_destroy(copy);
            qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining the prepared input dictionary"); return false;
        }
        copy->binding_capacity=source->binding_count;
        for (size_t i=0;i<source->binding_count;++i) {
            copy->bindings[i]=source->bindings[i]; ++copy->bindings[i]->references; ++copy->binding_count;
        }
    }
    if (!qa_input_binding_text_prepare(copy, source, error)) {
        qa_input_seat_destroy(copy); return false;
    }
    copy->binding_revision = source->binding_revision;
    *out=copy; return true;
}
void qa_input_seat_configuration_publish(qa_input_seat *active, qa_input_seat *candidate) {
    if (!qa_input_bindings_equal(active, candidate)) {
        ++active->binding_revision;
        ++candidate->binding_revision;
    }
    qa_binding_record **bindings = active->bindings;
    size_t count = active->binding_count, capacity = active->binding_capacity;
    qa_gamepad_tuning gamepad = active->options.gamepad;
    qa_arena binding_text = active->binding_text;
    char *scratch = active->scratch;
    size_t scratch_size = active->scratch_size;
    active->bindings = candidate->bindings;
    active->binding_count = candidate->binding_count;
    active->binding_capacity = candidate->binding_capacity;
    active->options.gamepad = candidate->options.gamepad;
    active->binding_text = candidate->binding_text;
    active->scratch = candidate->scratch;
    active->scratch_size = candidate->scratch_size;
    candidate->bindings = bindings;
    candidate->binding_count = count;
    candidate->binding_capacity = capacity;
    candidate->options.gamepad = gamepad;
    candidate->binding_text = binding_text;
    candidate->scratch = scratch;
    candidate->scratch_size = scratch_size;
}
bool qa_input_seat_remap_controller(qa_input_seat *s, int32_t device, qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (device < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Negative controller instance");
        return false;
    }
    /* Clone into a candidate list so failure and duplicate remaps are atomic. */
    qa_input_seat candidate = {0};
    for (size_t i = 0; i < s->binding_count; ++i) {
        qa_input_binding b = s->bindings[i]->view;
        if (b.input.kind >= QA_PHYSICAL_BUTTON)
            b.input.device = device;
        if (!qa_input_seat_bind(&candidate, &b, error)) {
            binding_candidate_dispose(&candidate);
            return false;
        }
    }
    bool ok = replace_bindings(s, &candidate, error);
    binding_candidate_dispose(&candidate);
    return ok;
}
bool qa_input_seat_action(qa_input_seat *s, qa_input_action action, uint64_t source, bool down,
                          double time, qa_error *error) {
    if (!qa_input_release_action_access(s,error)) return false;
    if (!s || action < 0 || action >= QA_INPUT_ACTION_COUNT || !isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input action");
        return false;
    }
    if(down && !s->physical_prepared) {
        qa_input_button *button=&s->buttons[action];
        size_t capacity=button->sources?button->capacity:4;
        if(button->count==capacity && !source_storage_prepare(s,button->count+1,
            s->binding_count,qa_strings_count(s->command_sources),error)) return false;
    }
    if (down) {
        bool active=s->buttons[action].count!=0;
        if (!qa_input_button_down(&s->buttons[action], source, time, error)) return false;
        if (!active && s->options.action_pressed)
            s->options.action_pressed(s->options.action_user,action);
        return true;
    }
    qa_input_button_up(&s->buttons[action], source, time, 10);
    return true;
}
static uint64_t command_key(qa_physical_input p) {
    if (p.kind == QA_PHYSICAL_KEY)
        return p.code;
    if (p.kind == QA_PHYSICAL_MOUSE)
        return QA_KEY_MOUSE1 + qa_input_mouse_button(p.code) - 1;
    return UINT64_C(65536) + (uint64_t)(uint32_t)p.device * 64 +
           (p.kind == QA_PHYSICAL_BUTTON ? p.code : 32 + p.code * 2 + (unsigned)p.positive);
}
static bool binding_command_sources_prepare(qa_input_seat *seat,const qa_binding_record *record,
    qa_error *error) {
    if(!record || record->view.kind!=QA_BIND_COMMAND) return true;
    char source[21];uint64_t id;
    snprintf(source,sizeof(source),"%llu",(unsigned long long)command_key(record->view.input));
    if(!qa_input_command_source(seat,source,&id,error)) return false;
    for(unsigned rule=QA_RULESET_NETQUAKE;rule<=QA_RULESET_Q3;++rule) {
        const char *text=record->view.command;
        size_t left=strlen(text);
        while(left) {
            size_t length=qa_command_separator(text,left,(qa_ruleset_id)rule);
            size_t consumed=length<left?length+1:length;
            const char *segment=text;
            while(length && (unsigned char)*segment<=32) {++segment;--length;}
            if(length && *segment=='+') {
                qa_command_token_span command,source_token;
                qa_error lexical={0};
                if(qa_command_token_next(segment,length,0,(qa_ruleset_id)rule,false,&command,&lexical) &&
                   command.found && qa_command_token_next(segment,length,command.end,(qa_ruleset_id)rule,
                       false,&source_token,&lexical) && source_token.found &&
                   !command_source_span(seat,(qa_bytes){(const uint8_t *)segment+source_token.start,
                       source_token.size},&id,error)) return false;
            }
            text+=consumed;left-=consumed;
        }
    }
    return true;
}
static bool binding_sources_prepare(qa_input_seat *seat,const qa_input_seat *other,
    const qa_binding_record *extra,qa_error *error) {
    if(!seat->options.console) return true;
    uint64_t console_source;
    bool ok=qa_input_command_source(seat,QA_INPUT_CONSOLE_SOURCE,&console_source,error);
    const qa_input_seat *owners[]={seat,other};
    for(size_t owner=0;ok && owner<sizeof(owners)/sizeof(*owners);++owner) {
        const qa_input_seat *source=owners[owner];
        if(!source) continue;
        for(size_t index=0;ok && index<source->binding_count;++index)
            ok=binding_command_sources_prepare(seat,source->bindings[index],error);
        for(size_t index=0;ok && index<source->held_count;++index)
            ok=binding_command_sources_prepare(seat,source->held[index].binding,error);
    }
    if(ok) ok=binding_command_sources_prepare(seat,extra,error);
    if(!ok) return false;
    size_t bindings=other && other->binding_count>seat->binding_count?other->binding_count:seat->binding_count;
    if(extra && bindings==SIZE_MAX) {qa_error_set(error,QA_ERROR_MEMORY,0,"Input binding count overflow");return false;}
    return source_storage_prepare(seat,seat->physical_capacity,bindings+(extra!=NULL),
        qa_strings_count(seat->command_sources),error);
}
/* uint64 digits, the largest finite double's integer digits, two spaces,
 * newline and NUL. The reservation uses the actual formatted length. */
enum { BINDING_TAIL_CAPACITY = 20 + DBL_MAX_10_EXP + 5 };
static bool binding_format(const qa_input_binding *binding, qa_ruleset_id dialect, bool down,
                           const char *tail, size_t tail_size, char *out, size_t *length,
                           qa_error *error) {
    size_t written = 0;
    const char *text = binding->command;
    size_t left = strlen(text);
    bool had_button = false;
    while (left) {
        size_t n = qa_command_separator(text, left, dialect),
               consumed = n < left ? n + 1 : n;
        const char *segment = text;
        size_t size = n;
        while (size && (unsigned char)*segment <= 32) {
            ++segment;
            --size;
        }
        while (size && (unsigned char)segment[size - 1] <= 32)
            --size;
        bool button = size && *segment == '+';
        if (button || (size && (down || had_button))) {
            size_t suffix = button ? tail_size : 1;
            if (suffix > SIZE_MAX - size || size + suffix > SIZE_MAX - written) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Input text length overflow");
                return false;
            }
            if (out) {
                if (button) {
                    out[written] = down ? '+' : '-';
                    memcpy(out + written + 1, segment + 1, size - 1);
                    memcpy(out + written + size, tail, tail_size);
                } else {
                    memcpy(out + written, segment, size);
                    out[written + size] = '\n';
                }
            }
            written += size + suffix;
        }
        if (button)
            had_button = true;
        text += consumed;
        left -= consumed;
    }
    if (out) out[written] = 0;
    *length = written;
    return true;
}
static bool binding_text_bound(const qa_binding_record *record, const char *tail,
                               size_t tail_size, size_t *maximum, qa_error *error) {
    if (!record || record->view.kind != QA_BIND_COMMAND) return true;
    for (unsigned rule = QA_RULESET_NETQUAKE; rule <= QA_RULESET_Q3; ++rule) {
        for (unsigned down = 0; down < 2; ++down) {
            size_t length;
            if (!binding_format(&record->view, (qa_ruleset_id)rule, down != 0, tail, tail_size,
                                NULL, &length, error)) return false;
            if (length > *maximum) *maximum = length;
        }
    }
    return true;
}
static bool binding_text_prepare(qa_input_seat *seat, const qa_input_seat *other,
                                 const qa_binding_record *extra, qa_error *error) {
    if (!seat->options.console) return true;
    char tail[BINDING_TAIL_CAPACITY];
    int written = snprintf(tail, sizeof(tail), " %llu %.0f\n",
                           (unsigned long long)UINT64_MAX, DBL_MAX);
    if (written < 0 || (size_t)written >= sizeof(tail)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Formatting input command suffix");
        return false;
    }
    size_t maximum = 0;
    const qa_input_seat *owners[] = {seat, other};
    for (size_t owner = 0; owner < sizeof(owners) / sizeof(*owners); ++owner) {
        const qa_input_seat *s = owners[owner];
        if (!s) continue;
        for (size_t i = 0; i < s->binding_count; ++i)
            if (!binding_text_bound(s->bindings[i], tail, (size_t)written, &maximum, error))
                return false;
        for (size_t i = 0; i < s->held_count; ++i)
            if (!binding_text_bound(s->held[i].binding, tail, (size_t)written, &maximum, error))
                return false;
    }
    if (!binding_text_bound(extra, tail, (size_t)written, &maximum, error)) return false;
    if(!binding_sources_prepare(seat,other,extra,error)) return false;
    if (!maximum) {
        qa_arena_seal(&seat->binding_text); return true;
    }
    if (maximum == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Input text length overflow"); return false;
    }
    seat->binding_text.sealed = false;
    bool ok = qa_arena_reserve(&seat->binding_text, maximum + 1, error);
    qa_arena_seal(&seat->binding_text);
    return ok;
}
bool qa_input_binding_text_prepare(qa_input_seat *seat, const qa_input_seat *other,
                                   qa_error *error) {
    return binding_text_prepare(seat, other, NULL, error);
}
static bool format_binding(qa_input_seat *s, const qa_held_binding *held, bool down, double time,
                            qa_error *error) {
    const qa_input_binding *b = held->binding ? &held->binding->view : NULL;
    s->scratch_size = 0;
    if (!b || b->kind == QA_BIND_ACTION) return true;
    char tail[BINDING_TAIL_CAPACITY];
    int written = snprintf(tail, sizeof(tail), " %llu %.0f\n",
                           (unsigned long long)command_key(held->input), trunc(time));
    if (written < 0 || (size_t)written >= sizeof(tail)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Formatting input command suffix"); return false;
    }
    size_t length;
    if (!binding_format(b, s->options.context.dialect, down, tail, (size_t)written,
                        NULL, &length, error)) return false;
    if (!length) return true;
    if (length == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Input text length overflow"); return false;
    }
    qa_arena_reset(&s->binding_text);
    s->scratch = qa_arena_alloc(&s->binding_text, length + 1, 1, error);
    if (!s->scratch) return false;
    return binding_format(b, s->options.context.dialect, down, tail, (size_t)written,
                          s->scratch, &s->scratch_size, error);
}
bool qa_input_binding_release_text(qa_input_seat *s,const qa_held_binding *held,double time,
    const char **out,qa_error *error) {
    if (!format_binding(s,held,false,time,error)) return false;
    *out=s->scratch_size?s->scratch:""; return true;
}
static bool run_binding(qa_input_seat *s,const qa_held_binding *held,bool down,double time,
    qa_error *error) {
    const qa_input_binding *binding=held->binding?&held->binding->view:NULL;
    if (binding && binding->kind==QA_BIND_ACTION)
        return qa_input_seat_action(s,binding->action,qa_input_physical_source(held->input),down,time,error);
    if (!format_binding(s,held,down,time,error)) return false;
    if (!s->scratch_size) return true;
    qa_command_context context = s->options.context;
    context.script = "key-binding";
    context.direct = false;
    return qa_console_append(s->options.console, &context, s->scratch, error);
}
static bool digital(qa_input_seat *s, qa_physical_input input, bool down, double time,
                    bool consumed, qa_error *error) {
    size_t index = 0;
    while (index < s->held_count && !qa_input_physical_equal(s->held[index].input, input))
        ++index;
    if (down) {
        if (index < s->held_count)
            return true;
        if(!s->physical_prepared && !source_storage_prepare(s,s->held_count+1,
            s->binding_count,qa_strings_count(s->command_sources),error)) return false;
        if(s->held_count==s->held_capacity) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Physical input exceeds its admitted source storage");
            return false;
        }
        qa_binding_record *binding = !consumed && s->focus == QA_INPUT_GAME && s->focused
                                         ? binding_find(s, input, NULL)
                                         : NULL;
        qa_held_binding held = {input, binding};
        if (binding)
            ++binding->references;
        if (!run_binding(s, &held, true, time, error)) {
            qa_input_binding_record_release(binding);
            return false;
        }
        s->held[s->held_count++] = held;
        return true;
    }
    if (index == s->held_count)
        return true;
    if (!run_binding(s, &s->held[index], false, time, error))
        return false;
    qa_input_binding_record_release(s->held[index].binding);
    memmove(s->held + index, s->held + index + 1, (s->held_count - index - 1) * sizeof(*s->held));
    --s->held_count;
    return true;
}
bool qa_input_seat_release_device(qa_input_seat *s, int32_t device, double time, qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    bool success = true;
    for (size_t i = 0; i < s->held_count;) {
        qa_held_binding *held = &s->held[i];
        if (held->input.kind < QA_PHYSICAL_BUTTON || held->input.device != device) {
            ++i;
            continue;
        }
        if (!run_binding(s, held, false, time, error)) {
            success = false;
            ++i; continue;
        }
        qa_input_binding_record_release(held->binding);
        memmove(held, held + 1, (s->held_count - i - 1) * sizeof(*held));
        --s->held_count;
    }
    if (success) {
        qa_gamepad_clear(&s->gamepad);
        qa_gamepad_calibration_reset(&s->gamepad);
    }
    return success;
}
bool qa_input_seat_release(qa_input_seat *s, double time, qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (!isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input release time");
        return false;
    }
    bool success = true;
    for (size_t i = 0; i < s->held_count;) {
        if (!run_binding(s, &s->held[i], false, time, error)) {
            success = false;
            ++i; continue;
        }
        qa_input_binding_record_release(s->held[i].binding);
        memmove(s->held+i,s->held+i+1,(s->held_count-i-1)*sizeof(*s->held));
        --s->held_count;
    }
    if (success) {
        for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
            qa_input_button_release(&s->buttons[i], time);
        qa_gamepad_clear(&s->gamepad);
        s->mouse = (qa_vec2){0};
        s->impulse = 0;
    }
    return success;
}
bool qa_input_seat_set_focus(qa_input_seat *s, qa_input_focus focus, double time, qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (focus < QA_INPUT_GAME || focus > QA_INPUT_UI) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid seat focus");
        return false;
    }
    bool result = qa_input_seat_release(s, time, error);
    s->focus = focus;
    return result;
}
bool qa_input_seat_ui_push(qa_input_seat *s, qa_input_ui_handler handler, void *user, double time,
                           qa_input_ui_token *out, qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (!handler || !out || s->next_ui == UINT64_MAX ||
        !qa_input_reserve((void **)&s->ui, &s->ui_capacity, s->ui_count + 1, sizeof(*s->ui), error))
        return false;
    if (!qa_input_seat_release(s, time, error))
        return false;
    s->ui[s->ui_count - 1].focus = s->focus;
    *out = s->next_ui++;
    s->ui[s->ui_count++] = (qa_ui_record){
        .id = *out, .handler = handler, .user = user, .focus = s->focus, .active = true};
    return true;
}
bool qa_input_seat_ui_remove(qa_input_seat *s, qa_input_ui_token id, double time, qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (!id)
        return true;
    for (size_t i = 1; i < s->ui_count; ++i)
        if (s->ui[i].id == id && s->ui[i].active) {
            s->ui[i].active = false;
            if (i + 1 != s->ui_count)
                return true;
            bool success = qa_input_seat_release(s, time, error);
            while (s->ui_count > 1 && !s->ui[s->ui_count - 1].active)
                --s->ui_count;
            s->focus = s->ui[s->ui_count - 1].focus;
            return success;
        }
    return true;
}
bool qa_input_seat_event(qa_input_seat *s, const qa_input_event *event, bool *consumed,
                         qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (!s || !event || !isfinite(event->time_ms) || event->time_ms < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input event");
        return false;
    }
    if (consumed)
        *consumed = false;
    bool released = true;
    if (event->kind == QA_INPUT_EVENT_FOCUS) {
        s->focused = event->down;
        if (!s->focused)
            released = qa_input_seat_release(s, event->time_ms, error);
    } else if (!s->focused)
        return true;
    bool used = false;
    bool dispatched = !s->options.before_ui ||
        s->options.before_ui(s->options.before_ui_user, s, event, &used, error);
    qa_ui_record ui = s->ui[s->ui_count - 1];
    if (dispatched && !used) used = ui.handler && ui.handler(ui.user, s, s->focus, event);
    /* A rejected source event also suppresses a new gameplay press, while an
     * already held physical release must reach digital() below. */
    if (!dispatched) used = true;
    if (consumed)
        *consumed = used || s->focus == QA_INPUT_GAME;
    if (!released || !dispatched) {
        if ((event->kind == QA_INPUT_EVENT_KEY || event->kind == QA_INPUT_EVENT_BUTTON) &&
            !event->down && qa_input_physical_valid(event->input)) {
            qa_error retirement = {0};
            (void)digital(s, event->input, false, event->time_ms, true, &retirement);
        }
        return false;
    }
    switch (event->kind) {
    case QA_INPUT_EVENT_KEY:
    case QA_INPUT_EVENT_BUTTON:
        if (!qa_input_physical_valid(event->input) ||
            (event->kind == QA_INPUT_EVENT_KEY ? event->input.kind != QA_PHYSICAL_KEY
                                               : event->input.kind != QA_PHYSICAL_MOUSE &&
                                                     event->input.kind != QA_PHYSICAL_BUTTON)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid physical input");
            return false;
        }
        return digital(s, event->input, event->down, event->time_ms, used, error);
    case QA_INPUT_EVENT_AXIS:
        if (!qa_input_physical_valid(event->input) || event->input.kind != QA_PHYSICAL_AXIS)
            return false;
        if (!qa_gamepad_axis(&s->gamepad, (qa_controller_axis)event->input.code, event->value,
                             !used && s->focus == QA_INPUT_GAME, error))
            return false;
        for (unsigned side = 0; side < 2; ++side) {
            qa_physical_input p = event->input;
            p.positive = side != 0;
            float value = p.positive ? event->value : -event->value;
            if (!digital(s, p, value > s->options.gamepad.trigger_threshold, event->time_ms, used,
                         error))
                return false;
        }
        return true;
    case QA_INPUT_EVENT_MOUSE:
        if (!isfinite(event->delta.x) || !isfinite(event->delta.y)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid pointer displacement");
            return false;
        }
        if (!used && s->focus == QA_INPUT_GAME) {
            qa_vec2 next = {s->mouse.x + event->delta.x, s->mouse.y + event->delta.y};
            if (!isfinite(next.x) || !isfinite(next.y)) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Pointer displacement overflow");
                return false;
            }
            s->mouse = next;
        }
        return true;
    case QA_INPUT_EVENT_WHEEL: {
        if (!isfinite(event->delta.y) || fabsf(event->delta.y) > (float)UINT16_MAX) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid wheel displacement");
            return false;
        }
        qa_physical_input p = {.kind = QA_PHYSICAL_KEY,
                               .code = event->delta.y > 0 ? QA_KEY_WHEEL_UP : QA_KEY_WHEEL_DOWN};
        unsigned count = (unsigned)ceilf(fabsf(event->delta.y));
        for (unsigned i = 0; i < count; ++i)
            if (!digital(s, p, true, event->time_ms, used, error) ||
                !digital(s, p, false, event->time_ms, false, error))
                return false;
        return true;
    }
    case QA_INPUT_EVENT_TEXT:
    case QA_INPUT_EVENT_FOCUS:
    case QA_INPUT_EVENT_TOUCH:
        return true;
    default:
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unknown input event");
        return false;
    }
}
bool qa_input_seat_sample(qa_input_seat *s, double now, double frame, qa_seat_input_sample *out,
                          qa_error *error) {
    if (!qa_input_release_mutation_access(s,error)) return false;
    if (!out || !isfinite(now) || now < 0 || !isfinite(frame) || frame <= 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid seat sample");
        return false;
    }
    qa_seat_input_sample result = {.mouse = s->mouse,
                                   .frame_ms = frame,
                                   .game_focus = s->focus == QA_INPUT_GAME,
                                   .any_key_down = s->held_count != 0,
                                   .impulse = s->impulse};
    if (!qa_gamepad_sample_read(&s->gamepad, &s->options.gamepad, frame, &result.gamepad, error))
        return false;
    qa_ruleset_id dialect = s->options.context.dialect;
    const qa_ruleset_descriptor *rules = qa_ruleset_read(dialect);
    qa_game_family timing = rules ? rules->family
        : dialect <= QA_RULESET_QUAKEWORLD ? QA_GAME_Q1 : QA_GAME_Q2;
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i) {
        result.buttons[i].active = s->buttons[i].count != 0;
        result.buttons[i].pressed = s->buttons[i].pressed;
        if (!qa_input_button_sample(&s->buttons[i], timing, now, frame, &result.buttons[i].fraction,
                                    error))
            return false;
    }
    s->mouse = (qa_vec2){0};
    s->impulse = 0;
    *out = result;
    return true;
}
bool qa_input_seat_impulse(qa_input_seat *s, const char *text, qa_error *error) {
    if (!qa_input_release_action_access(s,error)) return false;
    if (!text) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing impulse");
        return false;
    }
    qa_ruleset_id d = s->options.context.dialect;
    if (d == QA_RULESET_Q3 || d == QA_RULESET_Q2_RERELEASE) {
        const char *start = text;
        while (*start && (unsigned char)*start <= 32)
            ++start;
        double value = 0;
        if ((*start &&
             !qa_parse_number((qa_bytes){(const uint8_t *)start, strlen(start)}, &value, error)) ||
            !isfinite(value) || value < 0 || value > 255 || value != trunc(value)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Impulse must fit an unsigned byte");
            return false;
        }
        s->impulse = (uint8_t)value;
        return true;
    }
    const unsigned char *p = (const unsigned char *)text;
    bool negative = false;
    if (d == QA_RULESET_Q2_CLASSIC)
        while (*p && *p <= 32)
            ++p;
    if (*p == '-' || (d == QA_RULESET_Q2_CLASSIC && *p == '+'))
        negative = *p++ == '-';
    unsigned base = 10, value = 0;
    if (d <= QA_RULESET_QUAKEWORLD && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (d <= QA_RULESET_QUAKEWORLD && p[0] == '\'') {
        s->impulse = (uint8_t)(negative ? 0u - p[1] : p[1]);
        return true;
    }
    for (; *p; ++p) {
        unsigned digit = *p >= '0' && *p <= '9'   ? (unsigned)(*p - '0')
                         : *p >= 'a' && *p <= 'f' ? (unsigned)(*p - 'a' + 10)
                         : *p >= 'A' && *p <= 'F' ? (unsigned)(*p - 'A' + 10)
                                                  : base;
        if (digit >= base)
            break;
        value = (value * base + digit) & 255;
    }
    s->impulse = (uint8_t)(negative ? 0u - value : value);
    return true;
}
