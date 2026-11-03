/* Port of src/compat/qc/mod-input.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/mod_input.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
struct qa_compat_qc_mod_input_capture {
  qa_mod_client_application *application; int64_t reference;
  int64_t *handler_indices; const qa_mod_qc_input_output **handlers; size_t handler_count;
  int64_t *entered; size_t entered_count, entered_capacity;
};
typedef qa_compat_qc_mod_input I;
static bool actor_eq(qa_actor_id a, qa_actor_id b){return qa_same_actor(a, b);}
/* isDeepStrictEqual on numbers is Object.is. */
static bool same_value(double a, double b){ return (isnan(a) && isnan(b)) || (a==b && signbit(a)==signbit(b)); }
void qa_compat_qc_mod_input_init(I *in, qa_compat_qc_machine *m, const qa_compat_qc_mod_input_field *f, size_t n, void *ctx, int64_t (*ref)(void *, qa_actor_id), bool (*live)(void *, qa_actor_id)) {
  memset(in,0,sizeof *in); in->machine=m; in->fields=f; in->field_count=n; in->ctx=ctx; in->reference=ref; in->live=live;
}
void qa_compat_qc_mod_input_destroy(I *in) {
  for (size_t i=0;i<in->capture_count;i++){ free(in->captures[i].handler_indices); free(in->captures[i].handlers); free(in->captures[i].entered); }
  free(in->captures); free(in->frames); free(in->frame_cells);
  for (size_t i=0;i<in->retired_count;i++) free(in->retired_cells[i]);
  free(in->retired_cells); memset(in,0,sizeof *in);
}
typedef struct { I *in; qa_mod_client_application *app; qa_compat_qc_words *words; uint8_t *saved; size_t *offsets; size_t count; bool nested; qa_actor_id actor; } Cleanup;
/* TS closures stay callable; a closed scope keeps its cell (saved==NULL)
 * until the input stack is next entered from idle. */
static void retire_cell(I *in, Cleanup *c) {
  free(c->saved); free(c->offsets); c->saved=NULL; c->offsets=NULL;
  if (in->retired_count==in->retired_capacity){ size_t cap=in->retired_capacity?in->retired_capacity*2:4; in->retired_cells=realloc(in->retired_cells,cap*sizeof *in->retired_cells); if(!in->retired_cells) qa_range_error("mod input allocation failed"); in->retired_capacity=cap; }
  in->retired_cells[in->retired_count++]=c;
}
static void cleanup_fn(void *ctx) {
  Cleanup *c = ctx; I *in = c->in;
  if (!c->saved) return;
  int64_t index = -1;
  for (size_t i=0;i<in->frame_count;i++) if (in->frame_cells[i]==c) index=(int64_t)i;
  if (index < 0) return;
  memmove(&in->frames[index], &in->frames[index+1], (in->frame_count-(size_t)index-1)*sizeof *in->frames);
  memmove(&in->frame_cells[index], &in->frame_cells[index+1], (in->frame_count-(size_t)index-1)*sizeof *in->frame_cells); in->frame_count--;
  if (c->nested) {
    bool parent=false; for(size_t i=0;i<in->frame_count;i++) if(actor_eq(in->frames[i]->identity.actor,c->actor)) parent=true;
    if (parent) for (size_t i=0;i<c->count;i++) memcpy(c->words->bytes + c->offsets[i], c->saved + i*12, c->in->fields[i].words*4);
  }
  retire_cell(in, c);
}
qa_compat_qc_mod_input_cleanup qa_compat_qc_mod_input_open(I *in, qa_mod_client_application *app) {
  qa_actor_id actor = app->identity.actor;
  if (in->frame_count==0) { for (size_t i=0;i<in->retired_count;i++) free(in->retired_cells[i]); in->retired_count=0; }
  qa_mod_client_input_values values = qa_mod_client_input_values_of(app);
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(in->machine->entities, in->reference(in->ctx, actor));
  bool nested=false; for(size_t i=0;i<in->frame_count;i++) if(actor_eq(in->frames[i]->identity.actor,actor)) nested=true;
  Cleanup *c = calloc(1,sizeof *c); if(!c) qa_range_error("mod input allocation failed");
  c->in=in; c->app=app; c->words=words; c->count=in->field_count; c->nested=nested; c->actor=actor;
  c->saved=malloc((in->field_count?in->field_count:1)*12); c->offsets=malloc((in->field_count?in->field_count:1)*sizeof *c->offsets);
  if(!c->saved||!c->offsets) qa_range_error("mod input allocation failed");
  for (size_t i=0;i<in->field_count;i++){ c->offsets[i]=(size_t)in->fields[i].offset*4; memcpy(c->saved+i*12, words->bytes+c->offsets[i], (size_t)in->fields[i].words*4); }
  for (size_t i=0;i<in->field_count;i++) {
    const qa_compat_qc_mod_input_field *field=&in->fields[i];
    const qa_mod_runtime_value *value=NULL;
    for(size_t j=0;j<values.count;j++) if(strcmp(values.entries[j].name, field->declaration.input)==0) value=&values.entries[j].value;
    if (value && strcmp(value->kind,"vector")==0) qa_compat_qc_words_set_vector(words, field->offset, value->vector_value);
    else if (value && strcmp(value->kind,"float")==0) {
      bool always = field->declaration.update && strcmp(field->declaration.update,"always")==0;
      if (always || value->number_value != 0) {
        double scalar = value->number_value * (field->declaration.has_scale ? field->declaration.scale : 1);
        if (!isfinite((double)qa_float32(scalar))) { for(size_t k=0;k<in->field_count;k++) memcpy(words->bytes+c->offsets[k], c->saved+k*12, (size_t)in->fields[k].words*4); retire_cell(in, c); qa_range_error("Source input exceeds the QC scalar ABI"); }
        qa_compat_qc_words_set_float(words, field->offset, scalar);
      }
    } else { for(size_t k=0;k<in->field_count;k++) memcpy(words->bytes+c->offsets[k], c->saved+k*12, (size_t)in->fields[k].words*4); retire_cell(in, c); qa_range_error("Missing validated QuakeC client input"); }
  }
  if (in->frame_count==in->frame_capacity){ size_t cap=in->frame_capacity?in->frame_capacity*2:4; in->frames=realloc(in->frames,cap*sizeof *in->frames); in->frame_cells=realloc(in->frame_cells,cap*sizeof *in->frame_cells); if(!in->frames||!in->frame_cells) qa_range_error("mod input allocation failed"); in->frame_capacity=cap; }
  in->frame_cells[in->frame_count]=c; in->frames[in->frame_count++]=app;
  return (qa_compat_qc_mod_input_cleanup){c, cleanup_fn};
}
qa_mod_client_input_values qa_compat_qc_mod_input_values(I *in, qa_mod_client_application *app) {
  if (in->frame_count==0 || in->frames[in->frame_count-1]!=app) qa_range_error("QuakeC client input scope is not active");
  return qa_mod_client_input_values_of(app);
}
void qa_compat_qc_mod_input_observe_call(I *in, qa_compat_qc_call_site call) {
  if (!in->capture_count) return;
  struct qa_compat_qc_mod_input_capture *cap=&in->captures[in->capture_count-1];
  bool is_handler=false; for(size_t i=0;i<cap->handler_count;i++) if(cap->handler_indices[i]==call.function_index) is_handler=true;
  if (in->frame_count && in->frames[in->frame_count-1]==cap->application
    && qa_compat_qc_words_int(&in->machine->globals, qa_compat_qc_machine_global_offset(in->machine,"self"))==cap->reference && is_handler) {
    bool has=false; for(size_t i=0;i<cap->entered_count;i++) if(cap->entered[i]==call.function_index) has=true;
    if(!has){ if(cap->entered_count==cap->entered_capacity){ size_t c=cap->entered_capacity?cap->entered_capacity*2:4; cap->entered=realloc(cap->entered,c*sizeof *cap->entered); if(!cap->entered) qa_range_error("mod input allocation failed"); cap->entered_capacity=c; } cap->entered[cap->entered_count++]=call.function_index; }
  }
}
/* TS finally: the capture leaves the stack on return or raise. */
typedef struct { I *in; size_t index; void *befores; } OutputScope;
static void pop_capture(void *arg) {
  OutputScope *s = arg; struct qa_compat_qc_mod_input_capture *cap = &s->in->captures[s->index];
  free(cap->entered); free(cap->handler_indices); free(cap->handlers); free(s->befores);
  s->in->capture_count = s->index;
}
qa_mod_client_input_output *qa_compat_qc_mod_input_output(I *in, const qa_mod_qc_input_output *outputs, size_t output_count, qa_mod_client_application *app, void *run_ctx, void (*run)(void *), size_t *out_count) {
  int64_t reference = in->reference(in->ctx, app->identity.actor);
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(in->machine->entities, reference);
  int64_t *hidx = malloc((output_count?output_count:1)*sizeof *hidx);
  const qa_mod_qc_input_output **handlers = malloc((output_count?output_count:1)*sizeof *handlers);
  if(!hidx||!handlers) qa_range_error("mod input allocation failed");
  size_t hn=0;
  typedef struct { const qa_compat_qc_mod_input_field *field; bool is_vector; qa_vec3 vec; double num; } Before;
  Before *befores = malloc((output_count?output_count:1)*sizeof *befores); size_t bn=0;
  if(!befores) qa_range_error("mod input allocation failed");
  for (size_t i=0;i<output_count;i++) {
    if (outputs[i].is_handler) { hidx[hn]=qa_compat_qc_program_function_named(in->machine->program, outputs[i].function)->index; handlers[hn]=&outputs[i]; hn++; continue; }
    const qa_compat_qc_mod_input_field *field=NULL;
    for(size_t j=0;j<in->field_count;j++) if(strcmp(in->fields[j].declaration.field, outputs[i].field)==0) field=&in->fields[j];
    if(!field) qa_range_error("QC output lacks its declared input field");
    befores[bn].field=field;
    befores[bn].is_vector=field->words==3; befores[bn].num=NAN; /* TS before!==after holds for a vector before */
    if(field->words==3) befores[bn].vec=qa_compat_qc_words_vector(words,field->offset); else befores[bn].num=qa_compat_qc_words_float(words,field->offset);
    bn++;
  }
  if (in->capture_count==in->capture_capacity){ size_t c=in->capture_capacity?in->capture_capacity*2:4; in->captures=realloc(in->captures,c*sizeof *in->captures); if(!in->captures) qa_range_error("mod input allocation failed"); in->capture_capacity=c; }
  size_t capture_index=in->capture_count++;
  struct qa_compat_qc_mod_input_capture *cap=&in->captures[capture_index];
  cap->application=app; cap->reference=reference; cap->handler_indices=hidx; cap->handlers=handlers; cap->handler_count=hn; cap->entered=NULL; cap->entered_count=0; cap->entered_capacity=0;
  OutputScope output_scope = {in, capture_index, befores}; qa_cleanup scope; qa_cleanup_push(&scope, pop_capture, &output_scope);
  run(run_ctx);
  cap=&in->captures[capture_index]; /* a nested output may have grown the capture stack */
  qa_mod_client_input_output *result = calloc(output_count+hn+1, sizeof *result); size_t rn=0;
  if(!result) qa_range_error("mod input allocation failed");
  bool framed=false; for(size_t i=0;i<in->frame_count;i++) if(in->frames[i]==app) framed=true;
  if (in->live(in->ctx, app->identity.actor) && framed) {
    for (size_t i=0;i<bn;i++) {
      const qa_compat_qc_mod_input_field *field=befores[i].field; const char *input=field->declaration.input;
      if (strcmp(input,"view-angles")==0) {
        qa_vec3 v=qa_compat_qc_words_vector(words,field->offset);
        if (!befores[i].is_vector || !same_value(v.x,befores[i].vec.x) || !same_value(v.y,befores[i].vec.y) || !same_value(v.z,befores[i].vec.z)){ result[rn]=(qa_mod_client_input_output){QA_MOD_CLIENT_INPUT_OUTPUT_SET,input,v,0,NULL,0}; rn++; }
      } else {
        double after=qa_compat_qc_words_float(words,field->offset);
        if (befores[i].num!=after){ result[rn]=(qa_mod_client_input_output){QA_MOD_CLIENT_INPUT_OUTPUT_SET,input,(qa_vec3){0,0,0},after/(field->declaration.has_scale?field->declaration.scale:1),NULL,0}; rn++; }
      }
    }
    for (size_t i=0;i<cap->entered_count;i++) for(size_t j=hn;j-->0;) if(hidx[j]==cap->entered[i]) { /* TS Map: the last output per function wins */
      result[rn]=(qa_mod_client_input_output){QA_MOD_CLIENT_INPUT_OUTPUT_CONSUME,NULL,(qa_vec3){0,0,0},0,handlers[j]->inputs,handlers[j]->input_count}; rn++; break;
    }
  }
  qa_cleanup_pop(&scope); pop_capture(&output_scope);
  *out_count=rn; return result;
}
bool qa_compat_qc_mod_input_get_active(const I *in) { return in->frame_count != 0; }
