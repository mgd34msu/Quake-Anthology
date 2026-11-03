/* Port of src/app/bootstrap/presentation-state.ts (PresentationState).
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/presentation_state.h"
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/content/q2/missionpacks/types.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/id_index.h"
#include "qa/foundation/js.h"
#include "qa/foundation/strings.h"
#include "qa/foundation/support.h"
#include "qa/materials/material.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"

#define MAX_SAFE 9007199254740991.0

/* ---- small helpers ---- */
static noreturn void fail(const char *format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}
static void *xmalloc(size_t size) { void *p = malloc(size ? size : 1); if (!p) qa_range_error("Out of memory"); return p; }
static void *xcalloc(size_t count, size_t size) { void *p = calloc(count ? count : 1, size ? size : 1); if (!p) qa_range_error("Out of memory"); return p; }
static void *xrealloc(void *p, size_t size) { void *n = realloc(p, size ? size : 1); if (!n) qa_range_error("Out of memory"); return n; }
static char *xstrdup(const char *text) {
  if (!text) return NULL;
  size_t n = strlen(text) + 1;
  char *out = xmalloc(n);
  memcpy(out, text, n);
  return out;
}
static bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static bool same_text(const char *a, const char *b) { return a == b || (a && b && strcmp(a, b) == 0); }
static uint64_t hash_text(const char *text) {
  uint64_t h = 1469598103934665603ull;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) { h ^= *p; h *= 1099511628211ull; }
  return h;
}
static double seconds_of(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
static bool owner_equal(const qa_presentation_owner *left, const qa_presentation_owner *right) {
  /* samePresentationOwner(left, right) with right possibly undefined. */
  if (left == NULL || right == NULL) return false;
  return qa_same_presentation_owner(left, *right);
}

/* A growable text, for the TS template-literal keys. */
typedef struct text_builder { char *data; size_t length, capacity; } text_builder;
static void tb_add(text_builder *b, const char *text) {
  size_t n = strlen(text);
  if (b->length + n + 1 > b->capacity) {
    size_t cap = b->capacity ? b->capacity : 64;
    while (cap < b->length + n + 1) cap *= 2;
    b->data = xrealloc(b->data, cap); b->capacity = cap;
  }
  memcpy(b->data + b->length, text, n + 1);
  b->length += n;
}
static void tb_number(text_builder *b, double value) { char buffer[40]; tb_add(b, qa_net_js_number_text(value, buffer)); }
static char *tb_take(text_builder *b) { if (!b->data) tb_add(b, ""); return b->data; }
static char *owner_key(const qa_presentation_owner *owner) {
  char *key = qa_presentation_owner_key(owner);
  if (!key) qa_range_error("Out of memory");
  return key;
}

/* ---- retained presentation nodes (TS objects; identity = address) ---- */
enum {
  ORDER_EMIT,          /* {kind, event, owner?, recipient?, sequence, content, seconds, sourceEntity} */
  ORDER_RESTORE,       /* {owner?, recipient?, sequence, content, seconds, sourceEntity, kind, event} */
  ORDER_LOCAL_PUBLISH, /* {kind, owner?, content, event, sequence, seconds} */
  ORDER_LOCAL_RESTORE, /* {kind, owner?, content, sequence, seconds, event} */
  ORDER_LOCAL_RETIRE   /* {kind, content, seconds, sequence, event} */
};
typedef struct node {
  qa_app_retained_presentation value; /* first: node pointers are request pointers */
  size_t refs;
  int order;
  char *strings[6]; /* local media: content, provider, intro, loop, original, replacement */
} node;
static node *node_retain(node *n) { n->refs++; return n; }
static void node_release(node *n) {
  if (!n || --n->refs) return;
  if (!n->value.local) qa_app_sim_presentation_event_release(&n->value.simulation);
  for (size_t i = 0; i < 6; i++) free(n->strings[i]);
  free(n);
}
static node *node_event(const qa_app_sim_presentation_event *view, int order) {
  node *n = xcalloc(1, sizeof *n);
  n->refs = 1; n->order = order;
  if (!qa_app_sim_presentation_event_copy(&n->value.simulation, view)) qa_range_error("Out of memory");
  return n;
}
static node *node_local(const qa_presentation_owner *owner, const char *content,
    const qa_component_presentation_media_request *event, double sequence, double seconds, int order) {
  node *n = xcalloc(1, sizeof *n);
  n->refs = 1; n->order = order; n->value.local = true;
  qa_app_local_presentation_media *m = &n->value.local_media;
  m->content = n->strings[0] = xstrdup(content);
  if (owner) { m->has_owner = true; m->owner.generation = owner->generation; m->owner.provider = n->strings[1] = xstrdup(owner->provider); }
  m->event.kind = event->kind;
  m->event.time_offset = event->time_offset;
  if (event->kind == QA_MEDIA_REQUEST_MUSIC) {
    m->event.intro = n->strings[2] = xstrdup(event->intro ? event->intro : "");
    m->event.loop = n->strings[3] = xstrdup(event->loop ? event->loop : "");
  } else if (event->kind == QA_MEDIA_REQUEST_SHADER_REMAP) {
    m->event.original = n->strings[4] = xstrdup(event->original ? event->original : "");
    m->event.replacement = n->strings[5] = xstrdup(event->replacement ? event->replacement : "");
  }
  m->sequence = sequence; m->seconds = seconds;
  return n;
}
/* {...value, sequence}: same field order, new identity. */
static node *node_with_sequence(const node *source, double sequence) {
  if (source->value.local) {
    const qa_app_local_presentation_media *m = &source->value.local_media;
    return node_local(m->has_owner ? &m->owner : NULL, m->content, &m->event, sequence, m->seconds, source->order);
  }
  qa_app_sim_presentation_event view = source->value.simulation;
  view.storage = NULL; view.sequence = sequence;
  return node_event(&view, source->order);
}
static double node_sequence(const node *n) {
  return n->value.local ? n->value.local_media.sequence : n->value.simulation.sequence;
}
static bool node_owner(const node *n, const qa_presentation_owner **out) {
  if (n->value.local) { *out = n->value.local_media.has_owner ? &n->value.local_media.owner : NULL; }
  else *out = n->value.simulation.has_owner ? &n->value.simulation.owner : NULL;
  return *out != NULL;
}
static int compare_node_ascending(const void *a, const void *b) {
  double x = node_sequence(*(node *const *)a), y = node_sequence(*(node *const *)b);
  return x < y ? -1 : x > y ? 1 : 0;
}

/* ---- TS persistentDomain / persistentSlot / shaderDomain / q2LoopKey ---- */
static char *shader_domain(const char *original) {
  char *stripped = qa_strip_shader_extension(original);
  if (!stripped) qa_range_error("Out of memory");
  char *normal = qa_normalize_shader_name(stripped);
  free(stripped);
  if (!normal) qa_range_error("Out of memory");
  text_builder b = {0};
  tb_add(&b, "shader:"); tb_add(&b, normal);
  free(normal);
  return tb_take(&b);
}
static const char *event_kind(const qa_app_sim_presentation_event *e) {
  if (eq(e->kind, "q1")) return e->q1->kind;
  if (eq(e->kind, "q2")) return e->q2->kind;
  if (eq(e->kind, "q1-level")) return e->q1_level->kind;
  if (eq(e->kind, "music")) return e->music.kind;
  if (eq(e->kind, "q1-sky")) return e->q1_sky.kind;
  if (eq(e->kind, "q1-client")) return e->q1_client.kind;
  return NULL;
}
static char *persistent_domain(const node *n) {
  if (n->value.local) {
    const qa_app_local_presentation_media *m = &n->value.local_media;
    return m->event.kind == QA_MEDIA_REQUEST_SHADER_REMAP ? shader_domain(m->event.original) : xstrdup("music:track");
  }
  const qa_app_sim_presentation_event *e = &n->value.simulation;
  const char *kind = event_kind(e);
  if (eq(e->kind, "q1-sky")) return xstrdup("sky");
  if (eq(e->kind, "q1-client")) {
    text_builder b = {0};
    tb_add(&b, "client:"); tb_add(&b, e->content); tb_add(&b, ":"); tb_number(&b, e->q1_client.slot);
    tb_add(&b, ":"); tb_add(&b, e->q1_client.kind);
    return tb_take(&b);
  }
  if ((eq(e->kind, "q1") || eq(e->kind, "q2")) && eq(kind, "lightstyle")) {
    text_builder b = {0};
    tb_add(&b, "style:"); tb_number(&b, eq(e->kind, "q1") ? e->q1->style : e->q2->style);
    return tb_take(&b);
  }
  if ((eq(e->kind, "q1") || eq(e->kind, "q1-level")) && eq(kind, "finale")) return xstrdup("finale");
  if (eq(e->kind, "music")) return xstrdup(eq(e->music.kind, "pause") ? "music:pause" : "music:track");
  if (eq(e->kind, "q2") && eq(kind, "music")) return xstrdup("music:track");
  return NULL;
}
static void recipient_text(text_builder *b, bool has, qa_actor_id recipient) {
  if (!has) { tb_add(b, "world"); return; }
  tb_number(b, (double)recipient.slot); tb_add(b, ":"); tb_number(b, (double)recipient.generation);
}
static char *persistent_slot(const node *n) {
  char *domain = persistent_domain(n);
  if (!domain) return NULL;
  text_builder b = {0};
  tb_add(&b, domain); tb_add(&b, ":");
  free(domain);
  if (n->value.local) tb_add(&b, "world");
  else recipient_text(&b, n->value.simulation.has_recipient, n->value.simulation.recipient);
  return tb_take(&b);
}
static bool domain_is(const node *n, const char *domain) {
  char *d = persistent_domain(n);
  bool same = same_text(d, domain);
  free(d);
  return same;
}
/* TS `${text.length}:${text}`: UTF-16 length; bytes for ASCII paths. */
static void length_prefixed(text_builder *b, const char *text) {
  tb_number(b, (double)strlen(text)); tb_add(b, ":"); tb_add(b, text);
}
static char *q2_loop_key(const qa_q2_presentation_event *event, bool has_recipient, qa_actor_id recipient) {
  text_builder b = {0};
  tb_add(&b, "sound|");
  if (!event->has_loop_owner) tb_add(&b, "-"); else length_prefixed(&b, event->loop_owner);
  tb_add(&b, "|");
  tb_number(&b, event->has_actor ? (double)event->actor.slot : -1); tb_add(&b, ":");
  tb_number(&b, event->has_actor ? (double)event->actor.generation : -1);
  tb_add(&b, "|"); tb_number(&b, event->channel); tb_add(&b, "|");
  length_prefixed(&b, event->path ? event->path : "");
  tb_add(&b, "|");
  if (!has_recipient) tb_add(&b, "-");
  else { tb_number(&b, (double)recipient.slot); tb_add(&b, ":"); tb_number(&b, (double)recipient.generation); }
  return tb_take(&b);
}
static char *join_key(const char *prefix, const char *rest) {
  text_builder b = {0};
  tb_add(&b, prefix); tb_add(&b, ":"); tb_add(&b, rest);
  return tb_take(&b);
}

/* ---- JS Map<string, RetainedPresentation> (insertion order) ---- */
typedef struct pentry { char *key; uint64_t hash; node *value; } pentry;
typedef struct pmap { pentry *items; size_t count, capacity; } pmap;
static ptrdiff_t pmap_find(const pmap *m, const char *key) {
  uint64_t h = hash_text(key);
  for (size_t i = 0; i < m->count; i++) if (m->items[i].hash == h && strcmp(m->items[i].key, key) == 0) return (ptrdiff_t)i;
  return -1;
}
/* Takes key ownership; retains value. */
static void pmap_set(pmap *m, char *key, node *value) {
  ptrdiff_t at = pmap_find(m, key);
  node_retain(value);
  if (at >= 0) { node_release(m->items[at].value); m->items[at].value = value; free(key); return; }
  if (m->count == m->capacity) { m->capacity = m->capacity ? m->capacity * 2 : 16; m->items = xrealloc(m->items, m->capacity * sizeof *m->items); }
  m->items[m->count++] = (pentry){key, hash_text(key), value};
}
static void pmap_remove_at(pmap *m, size_t i) {
  free(m->items[i].key); node_release(m->items[i].value);
  memmove(&m->items[i], &m->items[i + 1], (m->count - i - 1) * sizeof *m->items);
  m->count--;
}
static void pmap_delete(pmap *m, const char *key) { ptrdiff_t at = pmap_find(m, key); if (at >= 0) pmap_remove_at(m, (size_t)at); }
static void pmap_clear(pmap *m) { while (m->count) pmap_remove_at(m, m->count - 1); }

/* ---- localMediaOutput: Map<number, delivery> (insertion order) ---- */
typedef struct delivery {
  double sequence; node *request; bool retain, shader_replay;
  qa_app_presentation_current_fn current; void *current_ctx; void (*release_current)(void *ctx);
} delivery;
typedef struct lmap { delivery *items; size_t count, capacity; } lmap;
static void delivery_drop(delivery *d) {
  if (d->release_current) d->release_current(d->current_ctx);
  node_release(d->request);
}
static delivery *lmap_get(lmap *m, double sequence) {
  for (size_t i = 0; i < m->count; i++) if (m->items[i].sequence == sequence) return &m->items[i];
  return NULL;
}
/* Retains request. */
static void lmap_set(lmap *m, delivery value) {
  node_retain(value.request);
  delivery *existing = lmap_get(m, value.sequence);
  if (existing) { delivery old = *existing; *existing = value; delivery_drop(&old); return; }
  if (m->count == m->capacity) { m->capacity = m->capacity ? m->capacity * 2 : 8; m->items = xrealloc(m->items, m->capacity * sizeof *m->items); }
  m->items[m->count++] = value;
}
static void lmap_remove_at(lmap *m, size_t i) {
  delivery old = m->items[i];
  memmove(&m->items[i], &m->items[i + 1], (m->count - i - 1) * sizeof *m->items);
  m->count--;
  delivery_drop(&old);
}
static void lmap_delete(lmap *m, double sequence) {
  for (size_t i = 0; i < m->count; i++) if (m->items[i].sequence == sequence) { lmap_remove_at(m, i); return; }
}
static void lmap_clear(lmap *m) { while (m->count) lmap_remove_at(m, m->count - 1); }

/* ---- Map<number, {family, pattern}> ---- */
typedef struct style_entry { double style; bool q2; char *pattern; } style_entry;
typedef struct smap { style_entry *items; size_t count, capacity; } smap;
static bool same_value_zero(double a, double b) { return a == b || (a != a && b != b); }
static void smap_set(smap *m, double style, bool q2, const char *pattern) {
  for (size_t i = 0; i < m->count; i++) if (same_value_zero(m->items[i].style, style)) {
    char *copy = xstrdup(pattern);
    free(m->items[i].pattern); m->items[i].pattern = copy; m->items[i].q2 = q2; return;
  }
  if (m->count == m->capacity) { m->capacity = m->capacity ? m->capacity * 2 : 16; m->items = xrealloc(m->items, m->capacity * sizeof *m->items); }
  m->items[m->count++] = (style_entry){style, q2, xstrdup(pattern)};
}
static void smap_clear(smap *m) { for (size_t i = 0; i < m->count; i++) free(m->items[i].pattern); m->count = 0; }

/* ---- shaderSequences: Map<string, number> ---- */
typedef struct shader_seq { char *domain; double sequence; } shader_seq;

/* ---- owners: Map<ProviderId, entry> ---- */
typedef struct owner_entry {
  char *provider, *content;
  qa_presentation_owner token; /* token.provider == provider */
  qa_app_sim_q1_fog *fog;
  qa_content_id *accepted; /* the fog's acceptedContents */
  bool restored;           /* status "restored" vs "active" */
  uint64_t serial;         /* TS object identity */
} owner_entry;

struct qa_app_presentation_owner_binding {
  qa_app_presentation_state *state;
  char *provider, *content;
  qa_presentation_owner owner; /* owner.provider == provider */
  uint64_t serial;
  bool closed;
};

struct qa_app_presentation_state {
  void *ctx;
  qa_source_time (*now)(void *ctx);
  bool (*source_slot)(void *ctx, qa_actor_id actor, double *out);
  bool has_fog_options; qa_app_sim_q1_fog_options fog_options;
  qa_app_sim_q1_fog *fog;
  /* SimulationEvents' override of emitOwned (TS virtual dispatch); NULL
   * when the state is used on its own. */
  qa_app_presentation_emit_owned_fn emit_override; void *emit_override_ctx;

  double presentation_sequence, next_owner_generation;
  bool legacy_persistence;
  owner_entry **owners; size_t owner_count, owner_capacity; uint64_t next_serial;
  qa_event_ring source; qa_event_cursor source_cursor;
  qa_string_table *resource_names; qa_id_index resource_index;
  qa_resolved_resource_reference *resource_rows; char **resource_strings;
  size_t resource_count, resource_capacity;
  qa_string_table *resource_ids; int64_t *rows_by_id; size_t rows_by_id_capacity;
  smap styles, legacy_styles;
  pmap persistent;
  lmap local_media_output;
  bool local_media_enabled;
  double media_sequence;
  shader_seq *shader_sequences; size_t shader_count, shader_capacity;
  double restored_through, restored_owner_generation;

  /* C-only result buffers. */
  char **contents; size_t content_count, content_capacity; /* interned contents the fogs retain */
  qa_app_sim_presentation_event *taken; size_t taken_count;
  const qa_app_sim_presentation_event **taken_pointers;
  node **pinned; size_t pinned_count;
  node **persistent_pinned; size_t persistent_pinned_count;
  const qa_app_sim_presentation_event **persistent_pointers;
  qa_save_value **persistent_trees; size_t persistent_tree_count;
  qa_scene_light_style *light_styles; size_t light_style_capacity;
  void *adopted; void (*release_adopted)(void *data);
};

/* Interned content text that outlives the events: SimulationQ1Fog keeps
 * the context content of the events it has seen. */
static const char *intern_content(qa_app_presentation_state *s, const char *content) {
  for (size_t i = 0; i < s->content_count; i++) if (strcmp(s->contents[i], content) == 0) return s->contents[i];
  if (s->content_count == s->content_capacity) {
    s->content_capacity = s->content_capacity ? s->content_capacity * 2 : 8;
    s->contents = xrealloc(s->contents, s->content_capacity * sizeof *s->contents);
  }
  return s->contents[s->content_count++] = xstrdup(content);
}

static void release_taken(qa_app_presentation_state *s) {
  for (size_t i = 0; i < s->taken_count; i++) qa_app_sim_presentation_event_release(&s->taken[i]);
  free(s->taken); free(s->taken_pointers);
  s->taken = NULL; s->taken_pointers = NULL; s->taken_count = 0;
}
static void release_pins(node ***pins, size_t *count) {
  for (size_t i = 0; i < *count; i++) node_release((*pins)[i]);
  free(*pins); *pins = NULL; *count = 0;
}

/* ---- owners ---- */
static owner_entry *owners_get(const qa_app_presentation_state *s, const char *provider) {
  for (size_t i = 0; i < s->owner_count; i++) if (strcmp(s->owners[i]->provider, provider) == 0) return s->owners[i];
  return NULL;
}
static void owner_entry_free(owner_entry *e) {
  if (!e) return;
  qa_app_sim_q1_fog_destroy(e->fog);
  free(e->accepted); free(e->provider); free(e->content); free(e);
}
/* Map.set(provider, entry): replaces in place or appends. */
static void owners_set(qa_app_presentation_state *s, owner_entry *entry) {
  for (size_t i = 0; i < s->owner_count; i++) if (strcmp(s->owners[i]->provider, entry->provider) == 0) {
    if (s->owners[i] != entry) owner_entry_free(s->owners[i]);
    s->owners[i] = entry; return;
  }
  if (s->owner_count == s->owner_capacity) {
    s->owner_capacity = s->owner_capacity ? s->owner_capacity * 2 : 4;
    s->owners = xrealloc(s->owners, s->owner_capacity * sizeof *s->owners);
  }
  s->owners[s->owner_count++] = entry;
}
/* Map.delete(provider): the entry is returned to the caller to free. */
static owner_entry *owners_take(qa_app_presentation_state *s, const char *provider) {
  for (size_t i = 0; i < s->owner_count; i++) if (strcmp(s->owners[i]->provider, provider) == 0) {
    owner_entry *e = s->owners[i];
    memmove(&s->owners[i], &s->owners[i + 1], (s->owner_count - i - 1) * sizeof *s->owners);
    s->owner_count--;
    return e;
  }
  return NULL;
}
static void owners_clear(qa_app_presentation_state *s) {
  for (size_t i = 0; i < s->owner_count; i++) owner_entry_free(s->owners[i]);
  s->owner_count = 0;
}
/* TS ownerFog(content). */
static void owner_fog(qa_app_presentation_state *s, owner_entry *e) {
  if (!s->has_fog_options) return;
  qa_app_sim_q1_fog_options options = s->fog_options;
  size_t count = options.has_accepted ? options.accepted_content_count : 0;
  e->accepted = xcalloc(count + 1, sizeof *e->accepted);
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    bool seen = false; /* new Set([...]) */
    for (size_t j = 0; j < n; j++) if (strcmp(e->accepted[j], options.accepted_contents[i]) == 0) seen = true;
    if (!seen) e->accepted[n++] = options.accepted_contents[i];
  }
  bool seen = false;
  for (size_t j = 0; j < n; j++) if (strcmp(e->accepted[j], e->content) == 0) seen = true;
  if (!seen) e->accepted[n++] = e->content;
  options.accepted_contents = e->accepted; options.accepted_content_count = n; options.has_accepted = true;
  e->fog = qa_app_sim_q1_fog_create(options);
}
static owner_entry *owner_entry_new(qa_app_presentation_state *s, const char *provider, double generation,
    const char *content, bool restored) {
  owner_entry *e = xcalloc(1, sizeof *e);
  e->provider = xstrdup(provider); e->content = xstrdup(content);
  e->token.provider = e->provider; e->token.generation = generation;
  e->restored = restored; e->serial = ++s->next_serial;
  owner_fog(s, e);
  return e;
}

/* ---- the source ring ---- */
static int64_t admit_source(qa_app_presentation_state *s, const qa_app_sim_presentation_event *view) {
  qa_app_sim_presentation_event copy;
  if (!qa_app_sim_presentation_event_copy(&copy, view)) qa_range_error("Out of memory");
  int64_t id = qa_event_ring_append(&s->source, &copy);
  if (id == 0) {
    qa_app_sim_presentation_event_release(&copy);
    fail("Presentation event capacity exhausted (%d undrained events); drain presentation output before admitting more",
         QA_APP_PRESENTATION_EVENT_CAPACITY);
  }
  return id;
}
static void release_record(const void *record, int64_t id, void *ctx) {
  (void)id; (void)ctx;
  qa_app_sim_presentation_event_release((qa_app_sim_presentation_event *)(uintptr_t)record);
}
static void reset_source(qa_app_presentation_state *s) {
  qa_event_ring_for_each_live(&s->source, release_record, NULL);
  qa_event_ring_reset(&s->source);
}

/* ---- create / destroy ---- */
qa_app_presentation_state *qa_app_presentation_state_create(qa_source_time (*now)(void *ctx),
    bool (*source_slot)(void *ctx, qa_actor_id actor, double *out), void *ctx, const qa_app_sim_q1_fog_options *fog) {
  qa_app_presentation_state *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->ctx = ctx; s->now = now; s->source_slot = source_slot;
  s->next_owner_generation = 1; s->media_sequence = -1; s->restored_through = -1;
  if (!qa_event_ring_init(&s->source, QA_APP_PRESENTATION_EVENT_CAPACITY, sizeof(qa_app_sim_presentation_event))) { free(s); return NULL; }
  if (!qa_event_ring_open_cursor(&s->source, &s->source_cursor, false)) { qa_event_ring_destroy(&s->source); free(s); return NULL; }
  s->resource_names = qa_string_table_create(NULL, NULL);
  s->resource_ids = qa_string_table_create(NULL, NULL);
  if (!s->resource_names || !s->resource_ids) qa_range_error("Out of memory");
  qa_id_index_init(&s->resource_index);
  if (fog) {
    s->has_fog_options = true; s->fog_options = *fog;
    s->fog = qa_app_sim_q1_fog_create(*fog);
  }
  return s;
}
void qa_app_presentation_state_set_emit_owned_override(qa_app_presentation_state *state,
    qa_app_presentation_emit_owned_fn emit_owned, void *ctx) {
  state->emit_override = emit_owned; state->emit_override_ctx = ctx;
}
void qa_app_presentation_state_destroy(qa_app_presentation_state *s) {
  if (!s) return;
  reset_source(s);
  qa_event_ring_close_cursor(&s->source, &s->source_cursor);
  qa_event_ring_destroy(&s->source);
  release_taken(s);
  release_pins(&s->pinned, &s->pinned_count);
  release_pins(&s->persistent_pinned, &s->persistent_pinned_count);
  free(s->persistent_pointers);
  for (size_t i = 0; i < s->persistent_tree_count; i++) qa_save_value_free(s->persistent_trees[i]);
  free(s->persistent_trees);
  owners_clear(s); free(s->owners);
  qa_app_sim_q1_fog_destroy(s->fog);
  qa_string_table_destroy(s->resource_names); qa_string_table_destroy(s->resource_ids);
  qa_id_index_destroy(&s->resource_index);
  for (size_t i = 0; i < s->resource_count; i++) free(s->resource_strings[i]);
  free(s->resource_rows); free(s->resource_strings); free(s->rows_by_id);
  smap_clear(&s->styles); free(s->styles.items);
  smap_clear(&s->legacy_styles); free(s->legacy_styles.items);
  pmap_clear(&s->persistent); free(s->persistent.items);
  lmap_clear(&s->local_media_output); free(s->local_media_output.items);
  for (size_t i = 0; i < s->shader_count; i++) free(s->shader_sequences[i].domain);
  free(s->shader_sequences);
  for (size_t i = 0; i < s->content_count; i++) free(s->contents[i]);
  free(s->contents);
  free(s->light_styles);
  if (s->release_adopted) s->release_adopted(s->adopted);
  free(s);
}
void qa_app_presentation_state_adopt(qa_app_presentation_state *state, void *data, void (*release)(void *data)) {
  if (state->release_adopted) state->release_adopted(state->adopted);
  state->adopted = data; state->release_adopted = release;
}

/* ---- emitOwned / record ---- */
static bool actor_of(const qa_app_sim_source_presentation_event *source, qa_actor_id *out) {
  /* TS: event = view-reset ? source : q2-composition ? ("event" in source.event
   * ? source.event.event : source.event) : source.event; "actor" in event. */
  const char *k = source->kind;
  if (eq(k, "view-reset")) { *out = source->view_reset.actor; return true; }
  if (eq(k, "q1")) { *out = source->q1->actor; return source->q1->has_actor; }
  if (eq(k, "q1-composition")) {
    const qa_comp_q1_event *e = source->q1_composition;
    switch (e->kind) {
    case QA_COMP_Q1_EVENT_CLIENT_LEFT: case QA_COMP_Q1_EVENT_CTF_STATUS: case QA_COMP_Q1_EVENT_PROMPT:
    case QA_COMP_Q1_EVENT_CLEAR_PROMPT: case QA_COMP_Q1_EVENT_SOURCE_LOG: *out = e->actor; return true;
    default: return false;
    }
  }
  if (eq(k, "q2")) { *out = source->q2->actor; return source->q2->has_actor; }
  if (eq(k, "q2-weapon")) { /* actor: ActorId | null */
    if (!source->q2_weapon->actor) return false;
    *out = *source->q2_weapon->actor; return true;
  }
  if (eq(k, "q2-composition")) {
    const qa_q2_composition_event *e = source->q2_composition;
    switch (e->kind) {
    case QA_Q2_COMPOSITION_EVENT_CTF:
      if (eq(e->ctf->kind, "match-status")) return false;
      *out = e->ctf->actor; return true;
    case QA_Q2_COMPOSITION_EVENT_LMCTF: *out = e->lmctf->actor; return true;
    case QA_Q2_COMPOSITION_EVENT_GRAPPLE_PREDICTION: case QA_Q2_COMPOSITION_EVENT_KICK: *out = e->actor; return true;
    case QA_Q2_COMPOSITION_EVENT_MISSIONPACK_PLAYER: *out = e->player_effect->actor; return true;
    default: return false;
    }
  }
  if (eq(k, "q2-rerelease")) { *out = source->q2_rerelease->actor; return source->q2_rerelease->has_actor; }
  if (eq(k, "q2-player")) { if (eq(source->q2_player->kind, "print")) return false; *out = source->q2_player->actor; return true; }
  if (eq(k, "q3-character")) { *out = source->q3_character.actor; return true; }
  if (eq(k, "q3-source")) {
    const qa_app_q3_source_event *e = source->q3_source;
    if (e->kind == QA_APP_Q3_EVENT_SOUND) { *out = e->actor; return true; }
    if (e->kind == QA_APP_Q3_EVENT_ENTITY) { *out = e->entity.actor; return true; }
    if (e->kind == QA_APP_Q3_EVENT_PLAYER) { *out = e->player.actor; return true; }
    return false;
  }
  return false;
}
static qa_app_sim_q1_fog *fog_for(qa_app_presentation_state *s, const qa_app_sim_presentation_event *p) {
  if (!p->has_owner) return s->fog;
  owner_entry *e = owners_get(s, p->owner.provider);
  return e ? e->fog : NULL;
}
static void persistent_set_event(qa_app_presentation_state *s, char *key, const qa_app_sim_presentation_event *view, int order) {
  node *n = node_event(view, order);
  pmap_set(&s->persistent, key, n);
  node_release(n);
}
/* TS record(presentation); returns the source-ring id of presentation. */
static int64_t record(qa_app_presentation_state *s, const qa_app_sim_presentation_event *p, int order) {
  int64_t id = admit_source(s, p);
  if (eq(p->kind, "q1-composition") && p->q1_composition->kind == QA_COMP_Q1_EVENT_ADDON
      && p->q1_composition->addon_event->kind == QA_Q1AD_EVENT_FOG) {
    qa_app_sim_q1_fog *fog = fog_for(s, p);
    if (fog) {
      const qa_q1ad_event *f = p->q1_composition->addon_event;
      qa_app_sim_q1_fog_context context = {intern_content(s, p->content), p->sequence, p->seconds,
                                           p->has_source_entity, p->source_entity};
      qa_app_sim_q1_fog_event_in event = {f->has_player, f->player, f->density, f->color_vec, f->duration, f->sky_factor};
      size_t count = 0;
      const qa_app_sim_presentation_event *derived = qa_app_sim_q1_fog_update(fog, context, event, &count);
      qa_app_sim_presentation_event *copies = count ? xmalloc(count * sizeof *copies) : NULL;
      if (count) memcpy(copies, derived, count * sizeof *copies);
      for (size_t i = 0; i < count; i++) {
        if (p->has_owner) { copies[i].has_owner = true; copies[i].owner = p->owner; }
        admit_source(s, &copies[i]);
      }
      free(copies);
    }
  }
  node probe; memset(&probe, 0, sizeof probe); probe.value.simulation = *p;
  char *slot = persistent_slot(&probe);
  char *prefix = owner_key(p->has_owner ? &p->owner : NULL);
  if (slot) { persistent_set_event(s, join_key(prefix, slot), p, order); free(slot); }
  const char *kind = event_kind(p);
  if (eq(p->kind, "q1") && (eq(kind, "ambient") || eq(kind, "static-model"))) {
    text_builder b = {0};
    tb_add(&b, prefix); tb_add(&b, ":"); tb_add(&b, kind); tb_add(&b, ":"); tb_number(&b, p->sequence);
    persistent_set_event(s, tb_take(&b), p, order);
  }
  if (eq(p->kind, "q2") && eq(kind, "sound") && !eq(p->q2->loop, "once")) {
    char *loop = q2_loop_key(p->q2, p->has_recipient, p->recipient);
    char *key = join_key(prefix, loop);
    free(loop);
    if (eq(p->q2->loop, "stop")) { pmap_delete(&s->persistent, key); free(key); }
    else persistent_set_event(s, key, p, order);
  }
  if ((eq(p->kind, "q1") || eq(p->kind, "q2")) && eq(kind, "lightstyle") && (eq(p->kind, "q2") || !p->has_recipient)) {
    bool q2 = eq(p->kind, "q2");
    smap_set(&s->styles, q2 ? p->q2->style : p->q1->style, q2, q2 ? p->q2->pattern : p->q1->pattern);
  }
  free(prefix);
  return id;
}
/* TS PresentationState.emitOwned (the base implementation). */
qa_app_sim_presentation_event qa_app_presentation_state_emit_owned_event(qa_app_presentation_state *s,
    const qa_presentation_owner *owner, qa_content_id content, const qa_app_sim_source_presentation_event *source_in,
    bool has_time, qa_source_time time, bool has_recipient, qa_actor_id recipient) {
  if (!has_time) time = s->now(s->ctx);
  qa_app_sim_source_presentation_event source = *source_in;
  qa_q1_event normalized;
  if (eq(source.kind, "q1") && eq(source.q1->kind, "static-model")) {
    normalized = *source.q1;
    normalized.frame = trunc(normalized.frame);
    normalized.color_map = trunc(normalized.color_map);
    normalized.skin = trunc(normalized.skin);
    source.q1 = &normalized;
  }
  double seconds = seconds_of(time);
  qa_actor_id actor;
  bool has_actor = actor_of(&source, &actor);
  qa_app_sim_presentation_event presentation = qa_app_sim_presentation_event_view(&source);
  if (owner) { presentation.has_owner = true; presentation.owner = *owner; }
  if (has_recipient) { presentation.has_recipient = true; presentation.recipient = recipient; }
  presentation.sequence = s->presentation_sequence++;
  presentation.content = content;
  presentation.seconds = seconds;
  double slot = 0;
  presentation.has_source_entity = has_actor && s->source_slot(s->ctx, actor, &slot);
  presentation.source_entity = presentation.has_source_entity ? slot : 0;
  int64_t id = record(s, &presentation, ORDER_EMIT);
  qa_app_sim_presentation_event view = *(const qa_app_sim_presentation_event *)qa_event_ring_at(&s->source, id);
  view.storage = NULL;
  return view;
}
/* this.emitOwned(...): virtual (SimulationEvents overrides it). */
static void dispatch_emit_owned(qa_app_presentation_state *s, const qa_presentation_owner *owner, qa_content_id content,
    const qa_app_sim_source_presentation_event *source, bool has_time, qa_source_time time, bool has_recipient, qa_actor_id recipient) {
  if (s->emit_override) s->emit_override(s->emit_override_ctx, owner, content, source, has_time, time, has_recipient, recipient);
  else qa_app_presentation_state_emit_owned_event(s, owner, content, source, has_time, time, has_recipient, recipient);
}
void qa_app_presentation_state_emit(qa_app_presentation_state *s, qa_content_id content,
    const qa_app_sim_source_presentation_event *source, bool has_time, qa_source_time time,
    bool has_recipient, qa_actor_id recipient) {
  if (!has_time) { time = s->now(s->ctx); has_time = true; }
  dispatch_emit_owned(s, NULL, content, source, has_time, time, has_recipient, recipient);
}
static void emit_owner_event(qa_app_presentation_state *s, const char *content, const char *kind, qa_presentation_owner owner) {
  qa_app_sim_source_presentation_event event; memset(&event, 0, sizeof event);
  event.kind = "presentation-owner";
  event.presentation_owner.kind = kind; event.presentation_owner.owner = owner;
  qa_source_time none = {QA_SOURCE_TIME_SECONDS, 0};
  qa_actor_id nobody; memset(&nobody, 0, sizeof nobody);
  qa_app_presentation_state_emit(s, content, &event, false, none, false, nobody);
}

/* ---- styles ---- */
static void rebuild_styles(qa_app_presentation_state *s) {
  smap_clear(&s->styles);
  for (size_t i = 0; i < s->legacy_styles.count; i++)
    smap_set(&s->styles, s->legacy_styles.items[i].style, s->legacy_styles.items[i].q2, s->legacy_styles.items[i].pattern);
  size_t count = s->persistent.count;
  node **sorted = xmalloc(count * sizeof *sorted);
  for (size_t i = 0; i < count; i++) sorted[i] = s->persistent.items[i].value;
  qa_js_sort(sorted, count, sizeof *sorted, compare_node_ascending);
  for (size_t i = 0; i < count; i++) {
    if (sorted[i]->value.local) continue;
    const qa_app_sim_presentation_event *e = &sorted[i]->value.simulation;
    bool q1 = eq(e->kind, "q1"), q2 = eq(e->kind, "q2");
    if ((q1 || q2) && eq(event_kind(e), "lightstyle") && !e->has_recipient)
      smap_set(&s->styles, q2 ? e->q2->style : e->q1->style, q2, q2 ? e->q2->pattern : e->q1->pattern);
  }
  free(sorted);
}

/* ---- retireOwner ---- */
typedef struct domain_set { char **items; size_t count, capacity; } domain_set;
static bool domain_set_has(const domain_set *d, const char *domain) {
  for (size_t i = 0; i < d->count; i++) if (strcmp(d->items[i], domain) == 0) return true;
  return false;
}
static void domain_set_add(domain_set *d, char *domain) { /* takes ownership */
  if (domain_set_has(d, domain)) { free(domain); return; }
  if (d->count == d->capacity) { d->capacity = d->capacity ? d->capacity * 2 : 8; d->items = xrealloc(d->items, d->capacity * sizeof *d->items); }
  d->items[d->count++] = domain;
}
static void domain_set_free(domain_set *d) { for (size_t i = 0; i < d->count; i++) free(d->items[i]); free(d->items); }
/* Map<string, node> holding references. */
typedef struct shader_map { char **domains; node **nodes; size_t count, capacity; } shader_map;
static void shader_map_set(shader_map *m, char *domain, node *n) {
  for (size_t i = 0; i < m->count; i++) if (strcmp(m->domains[i], domain) == 0) {
    free(domain); node_retain(n); node_release(m->nodes[i]); m->nodes[i] = n; return;
  }
  if (m->count == m->capacity) {
    m->capacity = m->capacity ? m->capacity * 2 : 4;
    m->domains = xrealloc(m->domains, m->capacity * sizeof *m->domains);
    m->nodes = xrealloc(m->nodes, m->capacity * sizeof *m->nodes);
  }
  m->domains[m->count] = domain; m->nodes[m->count] = node_retain(n); m->count++;
}
static void shader_map_free(shader_map *m) {
  for (size_t i = 0; i < m->count; i++) { free(m->domains[i]); node_release(m->nodes[i]); }
  free(m->domains); free(m->nodes);
}
/* The persistent value of greatest sequence with the given domain (TS
 * filter + stable descending sort + [0]); NULL is undefined. */
static node *latest_with_domain(qa_app_presentation_state *s, const char *domain) {
  node *best = NULL;
  for (size_t i = 0; i < s->persistent.count; i++) {
    node *n = s->persistent.items[i].value;
    if (!domain_is(n, domain)) continue;
    if (!best || node_sequence(n) > node_sequence(best)) best = n;
  }
  return best;
}
static void local_output_add(qa_app_presentation_state *s, node *request, bool retain, bool shader_replay) {
  delivery d; memset(&d, 0, sizeof d);
  d.sequence = node_sequence(request); d.request = request; d.retain = retain; d.shader_replay = shader_replay;
  lmap_set(&s->local_media_output, d);
}
static int compare_event_ascending(const void *a, const void *b) {
  double x = ((const qa_app_sim_presentation_event *)a)->sequence, y = ((const qa_app_sim_presentation_event *)b)->sequence;
  return x < y ? -1 : x > y ? 1 : 0;
}
static void retire_owner(qa_app_presentation_state *s, qa_presentation_owner owner, const char *content) {
  domain_set affected = {0};
  shader_map local_shaders = {0};
  bool local_music = false;
  for (size_t i = 0; i < s->persistent.count;) {
    node *n = s->persistent.items[i].value;
    const qa_presentation_owner *event_owner;
    node_owner(n, &event_owner);
    if (!owner_equal(event_owner, &owner)) { i++; continue; }
    char *domain = persistent_domain(n);
    if (n->value.local) {
      if (n->value.local_media.event.kind == QA_MEDIA_REQUEST_SHADER_REMAP)
        shader_map_set(&local_shaders, shader_domain(n->value.local_media.event.original), n);
      else local_music = true;
      free(domain);
    } else if (domain) domain_set_add(&affected, domain);
    pmap_remove_at(&s->persistent, i);
  }
  for (size_t i = 0; i < s->local_media_output.count;) {
    node *n = s->local_media_output.items[i].request;
    const qa_presentation_owner *event_owner;
    node_owner(n, &event_owner);
    if (!owner_equal(event_owner, &owner)) { i++; continue; }
    if (n->value.local && n->value.local_media.event.kind == QA_MEDIA_REQUEST_SHADER_REMAP)
      shader_map_set(&local_shaders, shader_domain(n->value.local_media.event.original), n);
    lmap_remove_at(&s->local_media_output, i);
  }
  emit_owner_event(s, content, "retired", owner);
  /* replacements: Map<slot, event>, insertion order, then sorted. */
  size_t count = s->persistent.count, replacement_count = 0;
  char **slots = xcalloc(count, sizeof *slots);
  node **replacements = xcalloc(count, sizeof *replacements);
  for (size_t i = 0; i < count; i++) {
    node *n = s->persistent.items[i].value;
    if (n->value.local) continue;
    char *slot = persistent_slot(n);
    if (!slot) continue;
    char *domain = persistent_domain(n);
    bool hit = domain_set_has(&affected, domain ? domain : "");
    free(domain);
    if (!hit) { free(slot); continue; }
    size_t at = replacement_count;
    for (size_t j = 0; j < replacement_count; j++) if (strcmp(slots[j], slot) == 0) { at = j; break; }
    double prior = at < replacement_count ? node_sequence(replacements[at]) : -1;
    if (prior < node_sequence(n)) {
      if (at == replacement_count) { slots[replacement_count] = slot; replacements[replacement_count++] = n; slot = NULL; }
      else replacements[at] = n;
    }
    free(slot);
  }
  qa_js_sort(replacements, replacement_count, sizeof *replacements, compare_node_ascending);
  for (size_t i = 0; i < replacement_count; i++) {
    qa_app_sim_presentation_event view = replacements[i]->value.simulation;
    view.storage = NULL; view.sequence = s->presentation_sequence++;
    admit_source(s, &view);
  }
  for (size_t i = 0; i < replacement_count; i++) free(slots[i]);
  free(slots); free(replacements);
  if (s->local_media_enabled) {
    bool reveal = local_music;
    if (!reveal && domain_set_has(&affected, "music:track"))
      for (size_t i = 0; i < s->persistent.count; i++) {
        node *n = s->persistent.items[i].value;
        if (n->value.local && n->value.local_media.event.kind != QA_MEDIA_REQUEST_SHADER_REMAP) { reveal = true; break; }
      }
    if (reveal) {
      node *replacement = latest_with_domain(s, "music:track");
      if (replacement) {
        node *replay = node_with_sequence(replacement, s->presentation_sequence++);
        local_output_add(s, replay, false, false);
        node_release(replay);
      }
    }
  }
  if (s->local_media_enabled)
    for (size_t i = 0; i < local_shaders.count; i++) {
      node *removed = local_shaders.nodes[i];
      node *replacement = latest_with_domain(s, local_shaders.domains[i]);
      if (replacement) {
        node *request = node_with_sequence(replacement, s->presentation_sequence++);
        local_output_add(s, request, false, true);
        node_release(request);
      } else if (removed->value.local && removed->value.local_media.event.kind == QA_MEDIA_REQUEST_SHADER_REMAP) {
        const qa_app_local_presentation_media *m = &removed->value.local_media;
        qa_component_presentation_media_request cue = {QA_MEDIA_REQUEST_SHADER_REMAP, NULL, NULL, m->event.original, m->event.original, 0};
        node *request = node_local(NULL, m->content, &cue, s->presentation_sequence++, m->seconds, ORDER_LOCAL_RETIRE);
        local_output_add(s, request, false, true);
        node_release(request);
      }
    }
  /* fog = [...fog.presentation(), ...owners' fog.presentation() with owner]. */
  qa_app_sim_presentation_event *fog = NULL;
  size_t fog_count = 0;
  if (s->fog) {
    size_t n = 0;
    const qa_app_sim_presentation_event *events = qa_app_sim_q1_fog_presentation(s->fog, &n);
    fog = xrealloc(fog, (fog_count + n + 1) * sizeof *fog);
    memcpy(fog + fog_count, events, n * sizeof *fog);
    fog_count += n;
  }
  for (size_t i = 0; i < s->owner_count; i++) {
    owner_entry *e = s->owners[i];
    if (!e->fog) continue;
    size_t n = 0;
    const qa_app_sim_presentation_event *events = qa_app_sim_q1_fog_presentation(e->fog, &n);
    fog = xrealloc(fog, (fog_count + n + 1) * sizeof *fog);
    for (size_t j = 0; j < n; j++) {
      fog[fog_count + j] = events[j];
      fog[fog_count + j].has_owner = true; fog[fog_count + j].owner = e->token;
    }
    fog_count += n;
  }
  qa_js_sort(fog, fog_count, sizeof *fog, compare_event_ascending);
  for (size_t i = 0; i < fog_count; i++) {
    qa_app_sim_presentation_event view = fog[i];
    view.sequence = s->presentation_sequence++;
    admit_source(s, &view);
  }
  free(fog);
  rebuild_styles(s);
  domain_set_free(&affected);
  shader_map_free(&local_shaders);
}

/* ---- owners: bindOwner / finishOwnerRestore / refreshOwner ---- */
static bool persistent_in_content(qa_app_presentation_state *s, const char *content) {
  for (size_t i = 0; i < s->persistent.count; i++) {
    const node *n = s->persistent.items[i].value;
    const char *c = n->value.local ? n->value.local_media.content : n->value.simulation.content;
    if (same_text(c, content)) return true;
  }
  if (s->fog) {
    size_t count = 0;
    const qa_app_sim_presentation_event *events = qa_app_sim_q1_fog_presentation(s->fog, &count);
    for (size_t i = 0; i < count; i++) if (same_text(events[i].content, content)) return true;
  }
  return false;
}
qa_app_presentation_owner_binding *qa_app_presentation_state_bind_owner(qa_app_presentation_state *s,
    qa_provider_id provider, qa_content_id content, bool restoring) {
  owner_entry *prior = owners_get(s, provider);
  if (prior && !prior->restored) fail("Presentation owner is already active: %s", provider);
  if (prior && (!restoring || strcmp(prior->content, content) != 0)) fail("Restored presentation owner differs: %s", provider);
  if (restoring && s->legacy_persistence && persistent_in_content(s, content))
    fail("Legacy save has persistent presentation in component %s's content without recorded ownership; primary and component output cannot be distinguished", provider);
  if (!prior && !(s->next_owner_generation + 1 <= MAX_SAFE)) fail("Presentation owner generation exhausted");
  owner_entry *entry = prior;
  if (!entry) entry = owner_entry_new(s, provider, s->next_owner_generation++, content, false);
  entry->restored = false;
  owners_set(s, entry);
  qa_app_presentation_owner_binding *b = xcalloc(1, sizeof *b);
  b->state = s; b->provider = xstrdup(provider); b->content = xstrdup(content);
  b->owner.provider = b->provider; b->owner.generation = entry->token.generation; b->serial = entry->serial;
  return b;
}
qa_presentation_owner qa_app_presentation_owner_binding_owner(const qa_app_presentation_owner_binding *b) { return b->owner; }
static void binding_current(qa_app_presentation_owner_binding *b) {
  owner_entry *e = b->closed ? NULL : owners_get(b->state, b->provider);
  if (!e || e->serial != b->serial) fail("Presentation owner retired: %s", b->provider);
}
void qa_app_presentation_owner_binding_emit(qa_app_presentation_owner_binding *b, qa_content_id content,
    const qa_app_sim_source_presentation_event *source, bool has_time, qa_source_time time,
    bool has_recipient, qa_actor_id recipient) {
  binding_current(b);
  if (eq(source->kind, "presentation-owner")) fail("Component source cannot emit owner lifecycle events");
  dispatch_emit_owned(b->state, &b->owner, content, source, has_time, time, has_recipient, recipient);
}
void qa_app_presentation_owner_binding_register_resource(qa_app_presentation_owner_binding *b,
    qa_content_id content, const char *path, const qa_resolved_resource_reference *resource) {
  binding_current(b);
  qa_app_presentation_state_register_resource_reference(b->state, content, path, resource);
}
void qa_app_presentation_owner_binding_close(qa_app_presentation_owner_binding *b) {
  if (b->closed) return;
  b->closed = true;
  owner_entry *e = owners_get(b->state, b->provider);
  if (e && e->serial == b->serial) {
    owner_entry *removed = owners_take(b->state, b->provider);
    retire_owner(b->state, b->owner, b->content);
    owner_entry_free(removed);
  }
}
void qa_app_presentation_owner_binding_free(qa_app_presentation_owner_binding *b) {
  if (!b) return;
  free(b->provider); free(b->content); free(b);
}
void qa_app_presentation_state_finish_owner_restore(qa_app_presentation_state *s) {
  for (size_t i = 0; i < s->owner_count; i++)
    if (s->owners[i]->restored) fail("Saved presentation owner was not restored: %s", s->owners[i]->provider);
  s->legacy_persistence = false;
}
void qa_app_presentation_state_refresh_owner(qa_app_presentation_state *s, qa_provider_id provider) {
  owner_entry *e = owners_get(s, provider);
  if (!e || e->restored) fail("Presentation owner is not active: %s", provider);
  emit_owner_event(s, e->content, "refreshed", e->token);
}

void qa_app_presentation_state_retire_actor(qa_app_presentation_state *s, qa_actor_id actor) {
  if (s->fog) qa_app_sim_q1_fog_retire(s->fog, actor);
  for (size_t i = 0; i < s->owner_count; i++) if (s->owners[i]->fog) qa_app_sim_q1_fog_retire(s->owners[i]->fog, actor);
  for (size_t i = 0; i < s->persistent.count;) {
    const node *n = s->persistent.items[i].value;
    if (!n->value.local && n->value.simulation.has_recipient && qa_same_actor(n->value.simulation.recipient, actor))
      pmap_remove_at(&s->persistent, i);
    else i++;
  }
}

/* ---- resources ---- */
const qa_resolved_resource_reference *qa_app_presentation_state_resource(const qa_app_presentation_state *s,
    qa_resource_id id) {
  uint32_t key = qa_string_table_find(s->resource_ids, id);
  if (key == QA_STRING_NONE || key >= s->rows_by_id_capacity || s->rows_by_id[key] < 0) return NULL;
  return &s->resource_rows[s->rows_by_id[key]];
}
void qa_app_presentation_state_register_resource_reference(qa_app_presentation_state *s, qa_content_id content,
    const char *path, const qa_resolved_resource_reference *resource) {
  if (!same_text(resource->requested_path, path)) fail("Registered resource path does not match its source request");
  qa_id_index_reserve(&s->resource_index, 1);
  uint32_t path_id = qa_string_table_intern(s->resource_names, path);
  uint32_t content_id = qa_string_table_intern(s->resource_names, content);
  qa_id_index_insert(&s->resource_index, path_id, content_id, (int64_t)s->resource_count);
  if (s->resource_count == s->resource_capacity) {
    s->resource_capacity = s->resource_capacity ? s->resource_capacity * 2 : 32;
    s->resource_rows = xrealloc(s->resource_rows, s->resource_capacity * sizeof *s->resource_rows);
    s->resource_strings = xrealloc(s->resource_strings, s->resource_capacity * sizeof *s->resource_strings);
  }
  /* One block holding every string of the reference. */
  const char *texts[8] = {resource->id, resource->requested_path, resource->provenance.member_path, resource->digest,
                          resource->resolution.plan, resource->resolution.prefix, resource->resolution.source_prefix,
                          resource->resolution.target_path};
  size_t total = 0;
  for (size_t i = 0; i < 8; i++) total += texts[i] ? strlen(texts[i]) + 1 : 0;
  char *block = xmalloc(total), *cursor = block;
  const char *copies[8];
  for (size_t i = 0; i < 8; i++) {
    if (!texts[i]) { copies[i] = NULL; continue; }
    size_t n = strlen(texts[i]) + 1;
    memcpy(cursor, texts[i], n); copies[i] = cursor; cursor += n;
  }
  qa_resolved_resource_reference row = *resource;
  row.id = copies[0]; row.requested_path = copies[1]; row.provenance.member_path = copies[2]; row.digest = copies[3];
  row.resolution.plan = copies[4]; row.resolution.prefix = copies[5]; row.resolution.source_prefix = copies[6];
  row.resolution.target_path = copies[7];
  size_t index = s->resource_count++;
  s->resource_rows[index] = row; s->resource_strings[index] = block;
  if (resource->id) {
    uint32_t key = qa_string_table_intern(s->resource_ids, resource->id);
    if (key >= s->rows_by_id_capacity) {
      size_t cap = s->rows_by_id_capacity ? s->rows_by_id_capacity : 32;
      while (cap <= key) cap *= 2;
      s->rows_by_id = xrealloc(s->rows_by_id, cap * sizeof *s->rows_by_id);
      for (size_t i = s->rows_by_id_capacity; i < cap; i++) s->rows_by_id[i] = -1;
      s->rows_by_id_capacity = cap;
    }
    s->rows_by_id[key] = (int64_t)index;
  }
}
const qa_resolved_resource_reference *qa_app_presentation_state_resource_reference_for(
    const qa_app_presentation_state *s, qa_content_id content, const char *path) {
  uint32_t content_id = qa_string_table_find(s->resource_names, content);
  uint32_t path_id = qa_string_table_find(s->resource_names, path);
  if (content_id == QA_STRING_NONE || path_id == QA_STRING_NONE) return NULL;
  int64_t row = qa_id_index_find(&s->resource_index, path_id, content_id);
  return row < 0 ? NULL : &s->resource_rows[row];
}

/* ---- persistentPresentation ---- */
const qa_app_sim_presentation_event *const *qa_app_presentation_state_persistent_presentation(
    qa_app_presentation_state *s, size_t *out_count) {
  release_pins(&s->persistent_pinned, &s->persistent_pinned_count);
  free(s->persistent_pointers); s->persistent_pointers = NULL;
  node **sorted = xmalloc(s->persistent.count * sizeof *sorted);
  size_t count = 0;
  for (size_t i = 0; i < s->persistent.count; i++)
    if (!s->persistent.items[i].value->value.local) sorted[count++] = node_retain(s->persistent.items[i].value);
  qa_js_sort(sorted, count, sizeof *sorted, compare_node_ascending);
  s->persistent_pinned = sorted; s->persistent_pinned_count = count;
  s->persistent_pointers = xmalloc(count * sizeof *s->persistent_pointers);
  for (size_t i = 0; i < count; i++) s->persistent_pointers[i] = &sorted[i]->value.simulation;
  *out_count = count;
  return s->persistent_pointers;
}

/* ---- local media ---- */
static bool persistent_has_older_domain(qa_app_presentation_state *s, const char *domain) {
  for (size_t i = 0; i < s->persistent.count; i++) {
    const node *n = s->persistent.items[i].value;
    if (node_sequence(n) < s->restored_through && domain_is(n, domain)) return true;
  }
  return false;
}
static char *local_key(const node *request) {
  const qa_presentation_owner *owner;
  node_owner(request, &owner);
  char *prefix = owner_key(owner), *slot = persistent_slot(request);
  text_builder b = {0};
  tb_add(&b, "local:"); tb_add(&b, prefix); tb_add(&b, ":"); tb_add(&b, slot ? slot : "null");
  free(prefix); free(slot);
  return tb_take(&b);
}
void qa_app_presentation_state_publish_local_media(qa_app_presentation_state *s, const qa_presentation_owner *owner,
    qa_content_id content, const qa_component_presentation_media_request *event, bool initializing,
    qa_app_presentation_current_fn current, void *current_ctx, void (*release_current)(void *current_ctx)) {
  if (owner) {
    owner_entry *active = owners_get(s, owner->provider);
    if (!active || active->restored || !owner_equal(&active->token, owner) || strcmp(active->content, content) != 0) {
      if (release_current) release_current(current_ctx);
      fail("Local media requires its active presentation owner and content");
    }
  }
  qa_app_presentation_state_enable_local_media(s);
  char *domain = event->kind == QA_MEDIA_REQUEST_SHADER_REMAP ? shader_domain(event->original) : xstrdup("music:track");
  bool skip = initializing && (owner == NULL || owner->generation < s->restored_owner_generation)
      && persistent_has_older_domain(s, domain);
  free(domain);
  if (skip) { if (release_current) release_current(current_ctx); return; }
  qa_source_time time = s->now(s->ctx);
  node *request = node_local(owner, content, event, s->presentation_sequence++,
                             time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000, ORDER_LOCAL_PUBLISH);
  if (event->kind != QA_MEDIA_REQUEST_SHADER_REMAP) pmap_set(&s->persistent, local_key(request), request);
  delivery d = {request->value.local_media.sequence, request, event->kind == QA_MEDIA_REQUEST_SHADER_REMAP, false,
                current, current_ctx, current ? release_current : NULL};
  if (!current && release_current) release_current(current_ctx);
  lmap_set(&s->local_media_output, d);
  node_release(request);
}
void qa_app_presentation_state_enable_local_media(qa_app_presentation_state *s) {
  if (s->local_media_enabled) return;
  s->local_media_enabled = true;
  for (size_t i = 0; i < s->persistent.count; i++) {
    node *n = s->persistent.items[i].value;
    if (n->value.local) local_output_add(s, n, false, false);
  }
}
const qa_app_retained_presentation *const *qa_app_presentation_state_pending_local_media(
    qa_app_presentation_state *s, size_t *out_count) {
  release_pins(&s->pinned, &s->pinned_count);
  size_t count = s->local_media_output.count;
  node **sorted = xmalloc(count * sizeof *sorted);
  for (size_t i = 0; i < count; i++) sorted[i] = node_retain(s->local_media_output.items[i].request);
  qa_js_sort(sorted, count, sizeof *sorted, compare_node_ascending);
  s->pinned = sorted; s->pinned_count = count;
  *out_count = count;
  return (const qa_app_retained_presentation *const *)sorted;
}
double qa_app_presentation_state_applied_media_sequence(const qa_app_presentation_state *s) { return s->media_sequence; }
void qa_app_presentation_state_applied_media(qa_app_presentation_state *s, double sequence) {
  s->media_sequence = qa_js_max(s->media_sequence, sequence);
}
void qa_app_presentation_state_applied_shader(qa_app_presentation_state *s, const char *original, double sequence) {
  char *domain = shader_domain(original);
  for (size_t i = 0; i < s->shader_count; i++) if (strcmp(s->shader_sequences[i].domain, domain) == 0) {
    s->shader_sequences[i].sequence = qa_js_max(s->shader_sequences[i].sequence, sequence);
    free(domain); return;
  }
  if (s->shader_count == s->shader_capacity) {
    s->shader_capacity = s->shader_capacity ? s->shader_capacity * 2 : 8;
    s->shader_sequences = xrealloc(s->shader_sequences, s->shader_capacity * sizeof *s->shader_sequences);
  }
  s->shader_sequences[s->shader_count++] = (shader_seq){domain, qa_js_max(-1, sequence)};
}
static double shader_sequence(const qa_app_presentation_state *s, const char *domain) {
  for (size_t i = 0; i < s->shader_count; i++) if (strcmp(s->shader_sequences[i].domain, domain) == 0) return s->shader_sequences[i].sequence;
  return -1;
}
static node *pin(qa_app_presentation_state *s, node *n) {
  s->pinned = xrealloc(s->pinned, (s->pinned_count + 1) * sizeof *s->pinned);
  s->pinned[s->pinned_count++] = node_retain(n);
  return n;
}
const qa_app_retained_presentation *qa_app_presentation_state_resolve_shader_replay(qa_app_presentation_state *s,
    const qa_app_retained_presentation *request_in) {
  node *request = (node *)(uintptr_t)request_in;
  delivery *d = lmap_get(&s->local_media_output, node_sequence(request));
  if (!d || d->request != request) return NULL;
  if (!d->shader_replay || !request->value.local || request->value.local_media.event.kind != QA_MEDIA_REQUEST_SHADER_REMAP)
    return request_in;
  const qa_app_local_presentation_media *m = &request->value.local_media;
  char *domain = shader_domain(m->event.original);
  node *winner = latest_with_domain(s, domain);
  free(domain);
  if (winner && !winner->value.local) fail("Shader replay requires a local material cue");
  node *resolved;
  if (!winner) {
    qa_component_presentation_media_request cue = {QA_MEDIA_REQUEST_SHADER_REMAP, NULL, NULL, m->event.original, m->event.original, 0};
    resolved = node_local(NULL, m->content, &cue, m->sequence, m->seconds, ORDER_LOCAL_RESTORE);
  } else resolved = node_with_sequence(winner, m->sequence);
  local_output_add(s, resolved, false, false);
  pin(s, resolved);
  node_release(resolved);
  return &resolved->value;
}
bool qa_app_presentation_state_local_media_current(qa_app_presentation_state *s, const qa_app_retained_presentation *request_in) {
  const node *request = (const node *)request_in;
  delivery *d = lmap_get(&s->local_media_output, node_sequence(request));
  if (!d || d->request != request) return false;
  if (d->current && d->current(d->current_ctx) == false) return false;
  const qa_presentation_owner *owner;
  if (node_owner(request, &owner)) {
    owner_entry *e = owners_get(s, owner->provider);
    if (!owner_equal(e ? &e->token : NULL, owner)) return false;
  }
  if (!request->value.local || request->value.local_media.event.kind != QA_MEDIA_REQUEST_SHADER_REMAP) return true;
  char *domain = shader_domain(request->value.local_media.event.original);
  bool current = request->value.local_media.sequence >= shader_sequence(s, domain);
  free(domain);
  return current;
}
void qa_app_presentation_state_acknowledge_local_media(qa_app_presentation_state *s,
    const qa_app_retained_presentation *request_in, bool committed) {
  node *request = (node *)(uintptr_t)request_in;
  delivery *d = lmap_get(&s->local_media_output, node_sequence(request));
  if (!d || d->request != request) return;
  if (committed && d->retain) pmap_set(&s->persistent, local_key(request), request);
  lmap_delete(&s->local_media_output, node_sequence(request));
}
void qa_app_presentation_state_assert_local_media_consumed(const qa_app_presentation_state *s) {
  if (s->local_media_output.count != 0) fail("Save requires completed local presentation media");
}

/* ---- network admission ---- */
void qa_app_presentation_state_admit_replicated_owner(qa_app_presentation_state *s, qa_presentation_owner token,
    qa_content_id content) {
  owner_entry *previous = owners_get(s, token.provider);
  if (previous) {
    if (owner_equal(&previous->token, &token)) {
      if (strcmp(previous->content, content) != 0 || previous->restored) fail("Replicated presentation owner changed identity");
      return;
    }
    if (token.generation <= previous->token.generation) fail("Replicated presentation activation moved backward");
    owner_entry *removed = owners_take(s, token.provider);
    retire_owner(s, removed->token, removed->content);
    owner_entry_free(removed);
  }
  s->next_owner_generation = qa_js_max(s->next_owner_generation, token.generation + 1);
  double g = s->next_owner_generation;
  if (!(isfinite(g) && trunc(g) == g && fabs(g) <= MAX_SAFE)) fail("Presentation owner generation exhausted");
  owners_set(s, owner_entry_new(s, token.provider, token.generation, content, false));
}
void qa_app_presentation_state_retire_replicated_owner(qa_app_presentation_state *s, qa_presentation_owner token) {
  owner_entry *entry = owners_get(s, token.provider);
  if (!entry || !owner_equal(&entry->token, &token)) return;
  owner_entry *removed = owners_take(s, token.provider);
  retire_owner(s, token, removed->content);
  owner_entry_free(removed);
}
double qa_app_presentation_state_receive_presentation(qa_app_presentation_state *s, const qa_app_sim_presentation_event *source) {
  double sequence = s->presentation_sequence;
  if (eq(source->kind, "presentation-owner") && eq(source->presentation_owner.kind, "retired")) {
    qa_presentation_owner owner = source->presentation_owner.owner;
    owner_entry *entry = owners_get(s, owner.provider), *removed = NULL;
    if (entry && owner_equal(&entry->token, &owner)) removed = owners_take(s, owner.provider);
    retire_owner(s, owner, source->content);
    owner_entry_free(removed);
    return sequence;
  }
  qa_app_sim_presentation_event view = *source;
  view.storage = NULL; view.sequence = s->presentation_sequence++;
  record(s, &view, ORDER_EMIT);
  return sequence;
}

/* ---- output ---- */
static void store_taken(qa_app_presentation_state *s, qa_app_sim_presentation_event *taken, size_t count) {
  s->taken = taken; s->taken_count = count;
  s->taken_pointers = xmalloc(count * sizeof *s->taken_pointers);
  for (size_t i = 0; i < count; i++) s->taken_pointers[i] = &taken[i];
}
const qa_app_sim_presentation_event *qa_app_presentation_state_take(qa_app_presentation_state *s, size_t *out_count) {
  release_taken(s);
  size_t count = 0;
  qa_app_sim_presentation_event *taken = qa_event_ring_take(&s->source, &s->source_cursor, &count);
  if (count && !taken) qa_range_error("Out of memory");
  qa_event_ring_retire(&s->source);
  store_taken(s, taken, count);
  *out_count = count;
  return s->taken;
}
const qa_app_sim_presentation_event *const *qa_app_presentation_state_take_presentation(qa_app_presentation_state *s,
    size_t *out_count) {
  qa_app_presentation_state_take(s, out_count);
  return s->taken_pointers;
}
typedef struct drain_ctx { void (*visit)(void *ctx, const qa_app_sim_presentation_event *event); void *ctx; } drain_ctx;
static void drain_one(const void *record, int64_t id, void *ctx) {
  (void)id;
  drain_ctx *d = ctx;
  qa_app_sim_presentation_event *event = (qa_app_sim_presentation_event *)(uintptr_t)record;
  d->visit(d->ctx, event);
  qa_app_sim_presentation_event_release(event);
}
size_t qa_app_presentation_state_drain_presentation(qa_app_presentation_state *s,
    void (*visit)(void *ctx, const qa_app_sim_presentation_event *event), void *visit_ctx) {
  release_taken(s);
  drain_ctx d = {visit, visit_ctx};
  int64_t count = qa_event_ring_drain(&s->source, &s->source_cursor, drain_one, &d);
  qa_event_ring_retire(&s->source);
  return (size_t)count;
}
void qa_app_presentation_state_assert_output_consumed(qa_app_presentation_state *s) {
  qa_app_presentation_state_assert_local_media_consumed(s);
  if (qa_event_ring_pending(&s->source, &s->source_cursor) != 0) fail("Save requires consumed source output");
}

/* ---- capture ---- */
static qa_save_value *checked(qa_save_value *value) { if (!value) qa_range_error("Out of memory"); return value; }
static void put(qa_save_value *object, const char *key, qa_save_value *value) {
  if (!qa_save_value_set(object, key, checked(value))) qa_range_error("Out of memory");
}
static void push(qa_save_value *array, qa_save_value *value) {
  if (!qa_save_value_push(array, checked(value))) qa_range_error("Out of memory");
}
static qa_save_value *vector_value(qa_vec3 v) {
  qa_save_value *o = checked(qa_save_value_object());
  put(o, "x", qa_save_value_number(v.x)); put(o, "y", qa_save_value_number(v.y)); put(o, "z", qa_save_value_number(v.z));
  return o;
}
static qa_save_value *saved_actor_value(qa_actor_id actor) {
  qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
  qa_save_value *o = checked(qa_save_value_object());
  put(o, "slot", qa_save_value_number(saved.slot)); put(o, "generation", qa_save_value_number(saved.generation));
  return o;
}
/* A live ActorId in the unified event tree shape: {slot, generation} carrying the identity. */
static qa_save_value *live_actor_value(qa_actor_id actor) {
  qa_save_value *o = checked(qa_save_value_object());
  put(o, "slot", qa_save_value_number((double)actor.slot)); put(o, "generation", qa_save_value_number((double)actor.generation));
  if (!qa_save_value_attach(o, &actor, sizeof actor)) qa_range_error("Out of memory");
  return o;
}
static qa_save_value *owner_value(qa_presentation_owner owner) {
  qa_save_value *o = checked(qa_save_value_object());
  put(o, "provider", qa_save_value_string(owner.provider)); put(o, "generation", qa_save_value_number(owner.generation));
  return o;
}
static qa_save_value *media_event_value(const qa_component_presentation_media_request *e) {
  qa_save_value *o = checked(qa_save_value_object());
  if (e->kind == QA_MEDIA_REQUEST_MUSIC_STOP) put(o, "kind", qa_save_value_string("music-stop"));
  else if (e->kind == QA_MEDIA_REQUEST_SHADER_REMAP) {
    put(o, "kind", qa_save_value_string("shader-remap")); put(o, "original", qa_save_value_string(e->original));
    put(o, "replacement", qa_save_value_string(e->replacement)); put(o, "timeOffset", qa_save_value_number(e->time_offset));
  } else {
    put(o, "kind", qa_save_value_string("music")); put(o, "intro", qa_save_value_string(e->intro));
    put(o, "loop", qa_save_value_string(e->loop));
  }
  return o;
}
/* The persistent kinds TS capture accepts, as their event objects. live: actor ids as live identities (the
 * typed event) instead of saved ones (capture). */
static qa_save_value *source_event_tree(const qa_app_sim_presentation_event *e, bool live) {
  const char *kind = event_kind(e);
  qa_save_value *o = checked(qa_save_value_object());
  put(o, "kind", qa_save_value_string(kind));
  if (eq(e->kind, "q1-level") && eq(kind, "finale")) {
    put(o, "text", qa_save_value_string(e->q1_level->text)); put(o, "track", qa_save_value_number(e->q1_level->track));
  } else if (eq(e->kind, "q1-sky")) put(o, "name", qa_save_value_string(e->q1_sky.name));
  else if (eq(e->kind, "q1-client")) {
    put(o, "slot", qa_save_value_number(e->q1_client.slot));
    bool text = eq(kind, "name") || eq(kind, "social") || eq(kind, "player-info");
    put(o, "value", text ? qa_save_value_string(e->q1_client.text ? e->q1_client.text : "") : qa_save_value_number(e->q1_client.value));
  } else if (eq(e->kind, "music")) {
    if (eq(kind, "pause")) put(o, "paused", qa_save_value_boolean(e->music.paused));
    else put(o, "track", qa_save_value_number(e->music.track));
  } else if (eq(e->kind, "q2") && eq(kind, "music")) put(o, "track", qa_save_value_string(e->q2->track));
  else if (eq(e->kind, "q2") && eq(kind, "lightstyle")) {
    put(o, "style", qa_save_value_number(e->q2->style)); put(o, "pattern", qa_save_value_string(e->q2->pattern));
  } else if (eq(e->kind, "q2") && eq(kind, "sound")) {
    const qa_q2_presentation_event *q = e->q2;
    put(o, "actor", q->has_actor ? (live ? live_actor_value(q->actor) : saved_actor_value(q->actor)) : qa_save_value_null());
    put(o, "origin", vector_value(q->origin)); put(o, "path", qa_save_value_string(q->path));
    put(o, "channel", qa_save_value_number(q->channel)); put(o, "volume", qa_save_value_number(q->volume));
    put(o, "attenuation", qa_save_value_number(q->attenuation)); put(o, "reliable", qa_save_value_boolean(q->reliable));
    put(o, "loop", qa_save_value_string(q->loop));
    if (q->has_loop_owner) put(o, "loopOwner", qa_save_value_string(q->loop_owner));
    if (q->local) put(o, "local", qa_save_value_boolean(true));
  } else if (eq(e->kind, "q1") && eq(kind, "ambient")) {
    const qa_q1_event *q = e->q1;
    put(o, "origin", vector_value(q->origin)); put(o, "path", qa_save_value_string(q->path));
    put(o, "volume", qa_save_value_number(q->volume)); put(o, "attenuation", qa_save_value_number(q->attenuation));
  } else if (eq(e->kind, "q1") && eq(kind, "static-model")) {
    const qa_q1_event *q = e->q1;
    put(o, "path", qa_save_value_string(q->path)); put(o, "frame", qa_save_value_number(q->frame));
    put(o, "colorMap", qa_save_value_number(q->color_map)); put(o, "skin", qa_save_value_number(q->skin));
    put(o, "origin", vector_value(q->origin)); put(o, "angles", vector_value(q->angles));
  } else if (eq(e->kind, "q1") && eq(kind, "finale")) {
    put(o, "text", qa_save_value_string(e->q1->text)); put(o, "stage", qa_save_value_number(e->q1->stage));
  } else if (eq(e->kind, "q1") && eq(kind, "lightstyle")) {
    put(o, "style", qa_save_value_number(e->q1->style)); put(o, "pattern", qa_save_value_string(e->q1->pattern));
  } else {
    qa_save_value_free(o);
    fail("Unsupported persistent source event");
  }
  return o;
}
static qa_save_value *source_event_value(const qa_app_sim_presentation_event *e) { return source_event_tree(e, false); }
static bool capturable(const qa_app_sim_presentation_event *e) {
  const char *kind = event_kind(e);
  return (eq(e->kind, "q1-level") && eq(kind, "finale")) || eq(e->kind, "q1-sky") || eq(e->kind, "q1-client")
      || eq(e->kind, "music") || (eq(e->kind, "q2") && (eq(kind, "music") || eq(kind, "lightstyle") || eq(kind, "sound")))
      || (eq(e->kind, "q1") && (eq(kind, "ambient") || eq(kind, "static-model") || eq(kind, "finale") || eq(kind, "lightstyle")));
}
static qa_save_value *persistent_value(const char *key, const node *n) {
  qa_save_value *o = checked(qa_save_value_object());
  put(o, "key", qa_save_value_string(key));
  if (n->value.local) {
    const qa_app_local_presentation_media *m = &n->value.local_media;
    put(o, "kind", qa_save_value_string("local-media"));
    if (n->order == ORDER_LOCAL_RETIRE) {
      put(o, "content", qa_save_value_string(m->content)); put(o, "seconds", qa_save_value_number(m->seconds));
      put(o, "sequence", qa_save_value_number(m->sequence)); put(o, "event", media_event_value(&m->event));
      return o;
    }
    if (m->has_owner) put(o, "owner", owner_value(m->owner));
    put(o, "content", qa_save_value_string(m->content));
    if (n->order == ORDER_LOCAL_PUBLISH) put(o, "event", media_event_value(&m->event));
    put(o, "sequence", qa_save_value_number(m->sequence)); put(o, "seconds", qa_save_value_number(m->seconds));
    if (n->order != ORDER_LOCAL_PUBLISH) put(o, "event", media_event_value(&m->event));
    return o;
  }
  const qa_app_sim_presentation_event *e = &n->value.simulation;
  if (!capturable(e)) { qa_save_value_free(o); fail("Unsupported persistent source event"); }
  bool restored = n->order == ORDER_RESTORE;
  if (!restored) { put(o, "kind", qa_save_value_string(e->kind)); put(o, "event", source_event_value(e)); }
  if (e->has_owner) put(o, "owner", owner_value(e->owner));
  if (e->has_recipient) put(o, "recipient", saved_actor_value(e->recipient));
  put(o, "sequence", qa_save_value_number(e->sequence)); put(o, "content", qa_save_value_string(e->content));
  put(o, "seconds", qa_save_value_number(e->seconds));
  put(o, "sourceEntity", e->has_source_entity ? qa_save_value_number(e->source_entity) : qa_save_value_null());
  if (restored) { put(o, "kind", qa_save_value_string(e->kind)); put(o, "event", source_event_value(e)); }
  return o;
}
/* persistentPresentation() as the unified network's event trees (the TS objects in their typed shape). */
qa_save_value *const *qa_app_presentation_state_persistent_presentation_trees(qa_app_presentation_state *s, size_t *out_count) {
  size_t count = 0;
  const qa_app_sim_presentation_event *const *events = qa_app_presentation_state_persistent_presentation(s, &count);
  for (size_t i = 0; i < s->persistent_tree_count; i++) qa_save_value_free(s->persistent_trees[i]);
  free(s->persistent_trees); s->persistent_trees = NULL; s->persistent_tree_count = 0;
  s->persistent_trees = xmalloc((count ? count : 1) * sizeof *s->persistent_trees);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *e = events[i];
    qa_save_value *o = checked(qa_save_value_object());
    s->persistent_trees[s->persistent_tree_count++] = o;
    put(o, "kind", qa_save_value_string(e->kind)); put(o, "event", source_event_tree(e, true));
    if (e->has_owner) put(o, "owner", owner_value(e->owner));
    if (e->has_recipient) put(o, "recipient", live_actor_value(e->recipient));
    put(o, "sequence", qa_save_value_number(e->sequence)); put(o, "content", qa_save_value_string(e->content));
    put(o, "seconds", qa_save_value_number(e->seconds));
    put(o, "sourceEntity", e->has_source_entity ? qa_save_value_number(e->source_entity) : qa_save_value_null());
  }
  *out_count = s->persistent_tree_count;
  return s->persistent_trees;
}

static qa_save_value *styles_value(const smap *m) {
  qa_save_value *list = checked(qa_save_value_array());
  for (size_t i = 0; i < m->count; i++) {
    qa_save_value *o = checked(qa_save_value_object());
    put(o, "style", qa_save_value_number(m->items[i].style));
    put(o, "family", qa_save_value_string(m->items[i].q2 ? "q2" : "q1"));
    put(o, "pattern", qa_save_value_string(m->items[i].pattern));
    push(list, o);
  }
  return list;
}
typedef struct keyed { const char *key; const node *n; } keyed;
static int compare_keyed(const void *a, const void *b) { return compare_node_ascending(&((const keyed *)a)->n, &((const keyed *)b)->n); }
qa_save_value *qa_app_presentation_state_capture(qa_app_presentation_state *s) {
  qa_save_value *root = checked(qa_save_value_object());
  qa_save_value *ownership = checked(qa_save_value_object());
  put(ownership, "nextGeneration", qa_save_value_number(s->next_owner_generation));
  qa_save_value *owners = checked(qa_save_value_array());
  for (size_t i = 0; i < s->owner_count; i++) {
    const owner_entry *e = s->owners[i];
    qa_save_value *o = owner_value(e->token);
    put(o, "content", qa_save_value_string(e->content));
    put(o, "fog", e->fog ? qa_app_sim_q1_fog_capture(e->fog) : qa_save_value_null());
    push(owners, o);
  }
  put(ownership, "owners", owners);
  put(root, "ownership", ownership);
  put(root, "baseStyles", styles_value(&s->legacy_styles));
  put(root, "q1Fog", s->fog ? qa_app_sim_q1_fog_capture(s->fog) : qa_save_value_null());
  put(root, "presentationSequence", qa_save_value_number(s->presentation_sequence));
  put(root, "styles", styles_value(&s->styles));
  size_t count = s->persistent.count;
  keyed *rows = xmalloc(count * sizeof *rows);
  for (size_t i = 0; i < count; i++) rows[i] = (keyed){s->persistent.items[i].key, s->persistent.items[i].value};
  qa_js_sort(rows, count, sizeof *rows, compare_keyed);
  qa_save_value *persistent = checked(qa_save_value_array());
  qa_cleanup cleanup_rows, cleanup_root;
  qa_cleanup_push(&cleanup_root, (void (*)(void *))qa_save_value_free, root);
  qa_cleanup_push(&cleanup_rows, free, rows);
  put(root, "persistent", persistent);
  for (size_t i = 0; i < count; i++) push(persistent, persistent_value(rows[i].key, rows[i].n));
  qa_cleanup_pop(&cleanup_rows);
  qa_cleanup_pop(&cleanup_root);
  free(rows);
  return root;
}

/* ---- restore ---- */
typedef struct restore_ctx {
  qa_app_presentation_state *s;
  qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved); void *reference_ctx;
  qa_app_sim_presentation_event *restored; size_t restored_count, restored_capacity; /* owned copies */
} restore_ctx;
static void restore_ctx_free(void *arg) {
  restore_ctx *r = arg;
  for (size_t i = 0; i < r->restored_count; i++) qa_app_sim_presentation_event_release(&r->restored[i]);
  free(r->restored); r->restored = NULL; r->restored_count = 0;
}
static void restored_push(restore_ctx *r, const qa_app_sim_presentation_event *view) {
  if (r->restored_count == r->restored_capacity) {
    r->restored_capacity = r->restored_capacity ? r->restored_capacity * 2 : 32;
    r->restored = xrealloc(r->restored, r->restored_capacity * sizeof *r->restored);
  }
  if (!qa_app_sim_presentation_event_copy(&r->restored[r->restored_count], view)) qa_range_error("Out of memory");
  r->restored_count++;
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("Out of memory");
  return out;
}
static double safe_integer_check(double value) { return isfinite(value) && trunc(value) == value && fabs(value) <= MAX_SAFE; }
static qa_presentation_owner read_presentation_owner(const qa_save_reader *reader) {
  qa_save_reader g = field(reader, "generation");
  double generation = qa_save_reader_integer_min(&g, 1);
  if (!safe_integer_check(generation)) qa_save_reader_fail(reader, "Invalid presentation owner generation");
  qa_save_reader p = field(reader, "provider");
  qa_presentation_owner owner = {qa_namespaced(&p), generation};
  return owner;
}
static double source_client_integer(const qa_save_reader *reader, double minimum, double maximum) {
  double value = qa_save_reader_integer_min(reader, minimum);
  if (value > maximum) qa_save_reader_fail(reader, "source client value out of range");
  return value;
}
static const char *const family_choices[] = {"q1", "q2"};
static void read_style(const qa_save_reader *value, smap *into) {
  qa_save_reader style = field(value, "style"), family = field(value, "family"), pattern = field(value, "pattern");
  double key = qa_save_reader_integer_min(&style, 0);
  const char *f = qa_save_reader_choice_string(&family, family_choices, 2);
  smap_set(into, key, eq(f, "q2"), qa_save_reader_string(&pattern));
}
static void restore_owner(restore_ctx *r, const qa_save_reader *value) {
  qa_app_presentation_state *s = r->s;
  qa_presentation_owner token = read_presentation_owner(value);
  qa_save_reader content_reader = field(value, "content");
  qa_content_id content = qa_read_content_id(&content_reader);
  if (token.generation >= s->next_owner_generation || owners_get(s, token.provider)) qa_save_reader_fail(value, "Invalid saved presentation owner");
  owner_entry *entry = owner_entry_new(s, token.provider, token.generation, content, true);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, (void (*)(void *))owner_entry_free, entry);
  qa_save_reader fog = field(value, "fog");
  if (!qa_save_reader_is_null(&fog)) {
    if (!entry->fog) qa_save_reader_fail(value, "Owned fog requires a Q1 map");
    size_t count = 0;
    const qa_app_sim_presentation_event *events = qa_app_sim_q1_fog_restore(entry->fog, &fog, r->reference, r->reference_ctx, &count);
    for (size_t i = 0; i < count; i++) {
      qa_app_sim_presentation_event view = events[i];
      view.has_owner = true; view.owner = entry->token;
      restored_push(r, &view);
    }
  }
  qa_cleanup_pop(&cleanup);
  owners_set(s, entry);
}
static const char *const persistent_families[] = {"q1", "q2", "music", "q1-sky", "q1-client", "q1-level", "local-media"};
static const char *const persistent_kinds[] = {"ambient", "music", "sound", "static-model", "finale", "cd-track", "pause",
    "lightstyle", "skybox", "name", "social", "player-info", "colors", "frags", "ping", "music-stop", "shader-remap"};
static void restore_persistent(restore_ctx *r, const qa_save_reader *value) {
  qa_app_presentation_state *s = r->s;
  qa_save_reader event = field(value, "event"), family_reader = field(value, "kind");
  const char *family = qa_save_reader_choice_string(&family_reader, persistent_families, 7);
  qa_save_reader kind_reader = field(&event, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_reader, persistent_kinds, 17);
  qa_save_reader recipient = field(value, "recipient"), owner_reader = field(value, "owner");
  bool has_owner = !qa_save_reader_is_undefined(&owner_reader);
  qa_presentation_owner owner = {0};
  if (has_owner) {
    owner = read_presentation_owner(&owner_reader);
    owner_entry *e = owners_get(s, owner.provider);
    if (!owner_equal(e ? &e->token : NULL, &owner)) qa_save_reader_fail(value, "Persistent event has no saved presentation owner");
  }
  bool has_recipient = !qa_save_reader_is_undefined(&recipient);
  qa_actor_id recipient_actor; memset(&recipient_actor, 0, sizeof recipient_actor);
  if (has_recipient) recipient_actor = r->reference(r->reference_ctx, qa_read_saved_actor(&recipient));
  qa_save_reader sequence_reader = field(value, "sequence"), content_reader = field(value, "content"),
      seconds_reader = field(value, "seconds");
  double sequence = qa_save_reader_integer_min(&sequence_reader, 0);
  qa_content_id content = qa_read_content_id(&content_reader);
  double seconds = qa_save_reader_number(&seconds_reader);
  bool local = eq(family, "local-media");
  bool has_source_entity = false; double source_entity = 0;
  if (!local) {
    qa_save_reader source_entity_reader = field(value, "sourceEntity");
    if (!qa_save_reader_is_null(&source_entity_reader)) { has_source_entity = true; source_entity = qa_save_reader_integer_min(&source_entity_reader, 0); }
  }
  if (local) {
    if (has_recipient || (!eq(kind, "music") && !eq(kind, "music-stop") && !eq(kind, "shader-remap")))
      qa_save_reader_fail(value, "Invalid local presentation media request");
    qa_component_presentation_media_request request; memset(&request, 0, sizeof request);
    if (eq(kind, "music-stop")) request.kind = QA_MEDIA_REQUEST_MUSIC_STOP;
    else if (eq(kind, "shader-remap")) {
      qa_save_reader a = field(&event, "original"), b = field(&event, "replacement"), c = field(&event, "timeOffset");
      request.kind = QA_MEDIA_REQUEST_SHADER_REMAP;
      request.original = qa_save_reader_string(&a); request.replacement = qa_save_reader_string(&b);
      request.time_offset = qa_save_reader_number(&c);
    } else {
      qa_save_reader a = field(&event, "intro"), b = field(&event, "loop");
      request.kind = QA_MEDIA_REQUEST_MUSIC;
      request.intro = qa_save_reader_string(&a); request.loop = qa_save_reader_string(&b);
    }
    owner_entry *e = has_owner ? owners_get(s, owner.provider) : NULL;
    if ((has_owner && !same_text(e ? e->content : NULL, content)) || sequence >= s->presentation_sequence)
      qa_save_reader_fail(value, "Invalid local presentation media content or sequence");
    node *restored = node_local(has_owner ? &owner : NULL, content, &request, sequence, seconds, ORDER_LOCAL_RESTORE);
    pmap_set(&s->persistent, local_key(restored), restored);
    node_release(restored);
    return;
  }
  qa_app_sim_source_presentation_event source; memset(&source, 0, sizeof source);
  qa_q1_event q1; memset(&q1, 0, sizeof q1);
  qa_q2_presentation_event q2; memset(&q2, 0, sizeof q2);
  qa_q1_intermission_result finale; memset(&finale, 0, sizeof finale);
  source.kind = family;
  if (eq(family, "music") && eq(kind, "cd-track")) {
    qa_save_reader t = field(&event, "track");
    source.music.kind = "cd-track"; source.music.track = qa_save_reader_integer_min(&t, 0);
  } else if (eq(family, "q1-sky") && eq(kind, "skybox")) {
    qa_save_reader n = field(&event, "name");
    source.q1_sky.kind = "skybox"; source.q1_sky.name = qa_save_reader_string(&n);
  } else if (eq(family, "q1-client") && (eq(kind, "name") || eq(kind, "social") || eq(kind, "player-info"))) {
    qa_save_reader slot = field(&event, "slot");
    source.q1_client.kind = kind; source.q1_client.slot = source_client_integer(&slot, 0, 255);
    qa_save_reader v = field(&event, "value");
    source.q1_client.text = qa_save_reader_string(&v);
  } else if (eq(family, "q1-client") && (eq(kind, "colors") || eq(kind, "frags") || eq(kind, "ping"))) {
    qa_save_reader slot = field(&event, "slot");
    source.q1_client.kind = kind; source.q1_client.slot = source_client_integer(&slot, 0, 255);
    qa_save_reader v = field(&event, "value");
    source.q1_client.value = source_client_integer(&v, -32768, 32767);
  } else if (eq(family, "music") && eq(kind, "pause")) {
    qa_save_reader p = field(&event, "paused");
    source.music.kind = "pause"; source.music.paused = qa_save_reader_boolean(&p);
  } else if ((eq(family, "q1") || eq(family, "q2")) && eq(kind, "lightstyle")) {
    qa_save_reader st = field(&event, "style");
    double style = qa_save_reader_integer_min(&st, 0);
    qa_save_reader pt = field(&event, "pattern");
    const char *pattern = qa_save_reader_string(&pt);
    if (eq(family, "q1")) { q1.kind = "lightstyle"; q1.style = style; q1.pattern = pattern; source.q1 = &q1; }
    else { q2.kind = "lightstyle"; q2.style = style; q2.pattern = pattern; source.q2 = &q2; }
  } else if (eq(family, "q1-level") && eq(kind, "finale")) {
    qa_save_reader tx = field(&event, "text");
    const char *text = qa_save_reader_string(&tx);
    qa_save_reader tr = field(&event, "track");
    finale.kind = "finale"; snprintf(finale.text, sizeof finale.text, "%s", text);
    finale.track = qa_save_reader_integer_min(&tr, 0);
    source.q1_level = &finale;
  } else if (eq(family, "q1") && eq(kind, "finale")) {
    static const double stages[] = {1, 2, 3, 4, 5, 6};
    qa_save_reader tx = field(&event, "text"), stage = field(&event, "stage");
    q1.kind = "finale"; q1.text = qa_save_reader_string(&tx);
    q1.stage = qa_save_reader_choice_number(&stage, stages, 6);
    source.q1 = &q1;
  } else if (eq(family, "q1") && eq(kind, "ambient")) {
    qa_save_reader o = field(&event, "origin"), p = field(&event, "path"), v = field(&event, "volume"), a = field(&event, "attenuation");
    q1.kind = "ambient"; q1.has_origin = true; q1.origin = qa_read_vector(&o); q1.path = qa_save_reader_string(&p);
    q1.volume = qa_save_reader_number(&v); q1.attenuation = qa_save_reader_number(&a);
    source.q1 = &q1;
  } else if (eq(family, "q1") && eq(kind, "static-model")) {
    qa_save_reader p = field(&event, "path"), f = field(&event, "frame"), c = field(&event, "colorMap"),
        sk = field(&event, "skin"), o = field(&event, "origin"), an = field(&event, "angles");
    q1.kind = "static-model"; q1.path = qa_save_reader_string(&p);
    q1.frame = qa_save_reader_integer(&f); q1.color_map = qa_save_reader_integer(&c); q1.skin = qa_save_reader_integer(&sk);
    q1.has_origin = true; q1.origin = qa_read_vector(&o); q1.angles = qa_read_vector(&an);
    source.q1 = &q1;
  } else if (eq(family, "q2") && eq(kind, "music")) {
    qa_save_reader t = field(&event, "track");
    q2.kind = "music"; q2.track = qa_save_reader_string(&t); source.q2 = &q2;
  } else if (eq(family, "q2") && eq(kind, "sound")) {
    qa_save_reader actor = field(&event, "actor");
    q2.kind = "sound";
    if (!qa_save_reader_is_null(&actor)) { q2.has_actor = true; q2.actor = r->reference(r->reference_ctx, qa_read_saved_actor(&actor)); }
    qa_save_reader o = field(&event, "origin"), p = field(&event, "path"), c = field(&event, "channel"), v = field(&event, "volume"),
        a = field(&event, "attenuation"), rl = field(&event, "reliable"), lp = field(&event, "loop"), lo = field(&event, "loopOwner");
    q2.origin = qa_read_vector(&o); q2.path = qa_save_reader_string(&p); q2.channel = qa_save_reader_number(&c);
    q2.volume = qa_save_reader_number(&v); q2.attenuation = qa_save_reader_number(&a); q2.reliable = qa_save_reader_boolean(&rl);
    q2.loop = qa_save_reader_literal_string(&lp, "start");
    if (!qa_save_reader_is_undefined(&lo)) { q2.has_loop_owner = true; q2.loop_owner = qa_namespaced(&lo); }
    source.q2 = &q2;
  } else qa_save_reader_fail(&event, "Invalid persistent source event family");
  qa_app_sim_presentation_event view = qa_app_sim_presentation_event_view(&source);
  view.has_owner = has_owner; view.owner = owner;
  view.has_recipient = has_recipient; view.recipient = recipient_actor;
  view.sequence = sequence; view.content = content; view.seconds = seconds;
  view.has_source_entity = has_source_entity; view.source_entity = source_entity;
  node *restored = node_event(&view, ORDER_RESTORE);
  char *slot = persistent_slot(restored), *prefix = owner_key(has_owner ? &owner : NULL), *rest;
  if (slot) rest = slot;
  else if (eq(family, "q2") && eq(kind, "sound")) rest = q2_loop_key(&q2, has_recipient, recipient_actor);
  else {
    text_builder b = {0};
    tb_add(&b, kind); tb_add(&b, ":"); tb_number(&b, sequence);
    rest = tb_take(&b);
  }
  char *key = join_key(prefix, rest);
  free(prefix); free(rest);
  pmap_set(&s->persistent, key, restored);
  restored_push(r, &restored->value.simulation);
  node_release(restored);
}
void qa_app_presentation_state_restore(qa_app_presentation_state *s, const qa_save_reader *reader,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx) {
  qa_save_reader sequence = field(reader, "presentationSequence");
  s->presentation_sequence = qa_save_reader_integer_min(&sequence, 0);
  s->restored_through = s->presentation_sequence;
  lmap_clear(&s->local_media_output); s->local_media_enabled = false; s->media_sequence = -1;
  for (size_t i = 0; i < s->shader_count; i++) free(s->shader_sequences[i].domain);
  s->shader_count = 0;
  reset_source(s);
  restore_ctx r = {s, reference, reference_ctx, NULL, 0, 0};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, restore_ctx_free, &r);
  smap_clear(&s->styles); smap_clear(&s->legacy_styles); pmap_clear(&s->persistent);
  owners_clear(s);
  qa_save_reader ownership = field(reader, "ownership");
  bool absent = qa_save_reader_is_undefined(&ownership);
  s->legacy_persistence = absent;
  if (absent) s->next_owner_generation = 1;
  else { qa_save_reader next = field(&ownership, "nextGeneration"); s->next_owner_generation = qa_save_reader_integer_min(&next, 1); }
  if (!safe_integer_check(s->next_owner_generation)) qa_save_reader_fail(&ownership, "Invalid presentation generation counter");
  s->restored_owner_generation = s->next_owner_generation;
  if (!absent) {
    qa_save_reader owners = field(&ownership, "owners");
    size_t count = qa_save_reader_list_count(&owners);
    for (size_t i = 0; i < count; i++) { qa_save_reader value = item(&owners, i); restore_owner(&r, &value); }
  }
  qa_save_reader fog = field(reader, "q1Fog");
  if (!qa_save_reader_is_undefined(&fog) && !qa_save_reader_is_null(&fog)) {
    if (!s->fog) qa_save_reader_fail(&fog, "Q1 fog state requires a Q1 map");
    size_t count = 0;
    const qa_app_sim_presentation_event *events = qa_app_sim_q1_fog_restore(s->fog, &fog, reference, reference_ctx, &count);
    for (size_t i = 0; i < count; i++) restored_push(&r, &events[i]);
  } else if (s->fog) qa_app_sim_q1_fog_reset(s->fog);
  qa_save_reader styles = field(reader, "styles");
  size_t style_count = qa_save_reader_list_count(&styles);
  for (size_t i = 0; i < style_count; i++) { qa_save_reader value = item(&styles, i); read_style(&value, &s->styles); }
  qa_save_reader base = field(reader, "baseStyles");
  if (!qa_save_reader_is_undefined(&base)) {
    size_t base_count = qa_save_reader_list_count(&base);
    for (size_t i = 0; i < base_count; i++) { qa_save_reader value = item(&base, i); read_style(&value, &s->legacy_styles); }
  }
  if (s->legacy_persistence)
    for (size_t i = 0; i < s->styles.count; i++)
      smap_set(&s->legacy_styles, s->styles.items[i].style, s->styles.items[i].q2, s->styles.items[i].pattern);
  qa_save_reader persistent = field(reader, "persistent");
  size_t persistent_count = qa_save_reader_list_count(&persistent);
  for (size_t i = 0; i < persistent_count; i++) { qa_save_reader value = item(&persistent, i); restore_persistent(&r, &value); }
  qa_js_sort(r.restored, r.restored_count, sizeof *r.restored, compare_event_ascending);
  if (r.restored_count != 0) {
    if (qa_event_ring_append_all(&s->source, r.restored, r.restored_count) == 0)
      fail("Restored presentation output exceeds event capacity (%d)", QA_APP_PRESENTATION_EVENT_CAPACITY);
    /* The ring owns the records now. */
    free(r.restored); r.restored = NULL; r.restored_count = 0;
  }
  qa_cleanup_pop(&cleanup);
  restore_ctx_free(&r);
}

/* ---- light styles ---- */
const char *qa_app_presentation_state_light_style(qa_app_presentation_state *s, double style) {
  for (size_t i = 0; i < s->styles.count; i++) if (same_value_zero(s->styles.items[i].style, style)) return s->styles.items[i].pattern;
  return "";
}
const qa_scene_light_style *qa_app_presentation_state_light_styles(qa_app_presentation_state *s, double seconds,
    size_t *out_count) {
  if (s->styles.count > s->light_style_capacity) {
    s->light_styles = xrealloc(s->light_styles, s->styles.count * sizeof *s->light_styles);
    s->light_style_capacity = s->styles.count;
  }
  for (size_t i = 0; i < s->styles.count; i++) {
    const style_entry *e = &s->styles.items[i];
    size_t length = strlen(e->pattern);
    double letter = 12;
    if (length != 0) {
      /* charCodeAt(Math.floor(seconds * 10) % length): NaN position reads 0;
       * a position outside the string is NaN. */
      double position = fmod(floor(seconds * 10), (double)length);
      if (position != position) position = 0;
      letter = position < 0 || position >= (double)length ? NAN : (double)(unsigned char)e->pattern[(size_t)position] - 97;
    }
    double scale = letter / 12;
    qa_scene_light_style out; memset(&out, 0, sizeof out);
    out.style = e->style;
    if (!e->q2) { out.is_q2 = false; out.value = length == 0 ? 256 : letter * 22; }
    else { out.is_q2 = true; out.rgb.x = scale; out.rgb.y = scale; out.rgb.z = scale; out.white = scale * 3; }
    s->light_styles[i] = out;
  }
  *out_count = s->styles.count;
  return s->light_styles;
}

/* ---- the runtime's surface ---- */
void qa_app_presentation_state_emit_owned(qa_app_presentation_state *s, bool has_owner, qa_presentation_owner owner,
    qa_content_id content, qa_app_sim_source_presentation_event source, qa_source_time time, bool has_recipient,
    qa_actor_id recipient, qa_app_sim_presentation_event *out) {
  qa_app_sim_presentation_event event = qa_app_presentation_state_emit_owned_event(s, has_owner ? &owner : NULL, content,
      &source, true, time, has_recipient, recipient);
  if (out) *out = event;
}
bool qa_app_presentation_state_resource_for(qa_app_presentation_state *s, qa_content_id content, const char *path,
    qa_resource_id *out) {
  const qa_resolved_resource_reference *resource = qa_app_presentation_state_resource_reference_for(s, content, path);
  if (!resource) return false;
  if (out) *out = resource->id;
  return true;
}
void qa_app_presentation_state_register_resource(qa_app_presentation_state *s, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource) {
  qa_app_presentation_state_register_resource_reference(s, content, path, &resource);
}
