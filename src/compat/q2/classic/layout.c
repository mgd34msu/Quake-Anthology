/* Port of src/compat/q2/classic/layout.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/layout.h"
#include <math.h>
#include <string.h>

static void put_le(uint8_t *b, uint64_t v, size_t n) { for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(v >> (8 * i)); }
static uint64_t get_le(const uint8_t *b, size_t n) { uint64_t v = 0; for (size_t i = n; i-- > 0;) v = (v << 8) | b[i]; return v; }
int16_t qa_compat_q2_view_i16(const uint8_t *v, size_t o) { return (int16_t)get_le(v + o, 2); }
uint16_t qa_compat_q2_view_u16(const uint8_t *v, size_t o) { return (uint16_t)get_le(v + o, 2); }
int32_t qa_compat_q2_view_i32(const uint8_t *v, size_t o) { return (int32_t)get_le(v + o, 4); }
uint32_t qa_compat_q2_view_u32(const uint8_t *v, size_t o) { return (uint32_t)get_le(v + o, 4); }
double qa_compat_q2_view_f32(const uint8_t *v, size_t o) { uint32_t u = (uint32_t)get_le(v + o, 4); float f; memcpy(&f, &u, 4); return f; }
void qa_compat_q2_view_set_i16(uint8_t *v, size_t o, int32_t x) { put_le(v + o, (uint16_t)x, 2); }
void qa_compat_q2_view_set_u16(uint8_t *v, size_t o, uint32_t x) { put_le(v + o, (uint16_t)x, 2); }
void qa_compat_q2_view_set_i32(uint8_t *v, size_t o, int32_t x) { put_le(v + o, (uint32_t)x, 4); }
void qa_compat_q2_view_set_f32(uint8_t *v, size_t o, double x) { float f = (float)x; uint32_t u; memcpy(&u, &f, 4); put_le(v + o, u, 4); }

qa_guest_address qa_compat_q2_mem_offset(const qa_compat_q2_memory *m, qa_guest_address a, int64_t d) { return m->offset(m->ctx, a, d); }
qa_guest_address_result qa_compat_q2_mem_pointer(const qa_compat_q2_memory *m, int64_t raw) { return m->pointer(m->ctx, raw); }
static uint64_t rd(const qa_compat_q2_memory *m, qa_guest_address a, size_t n) { uint8_t b[8] = {0}; m->copy(m->ctx, a, b, (double)n); return get_le(b, n); }
static void wr(const qa_compat_q2_memory *m, qa_guest_address a, uint64_t v, size_t n) { uint8_t b[8]; put_le(b, v, n); m->write(m->ctx, a, b, n); }
uint8_t qa_compat_q2_mem_read_u8(const qa_compat_q2_memory *m, qa_guest_address a) { return (uint8_t)rd(m, a, 1); }
int16_t qa_compat_q2_mem_read_i16(const qa_compat_q2_memory *m, qa_guest_address a) { return (int16_t)rd(m, a, 2); }
uint16_t qa_compat_q2_mem_read_u16(const qa_compat_q2_memory *m, qa_guest_address a) { return (uint16_t)rd(m, a, 2); }
int32_t qa_compat_q2_mem_read_i32(const qa_compat_q2_memory *m, qa_guest_address a) { return (int32_t)rd(m, a, 4); }
uint32_t qa_compat_q2_mem_read_u32(const qa_compat_q2_memory *m, qa_guest_address a) { return (uint32_t)rd(m, a, 4); }
int64_t qa_compat_q2_mem_read_i64(const qa_compat_q2_memory *m, qa_guest_address a) { return (int64_t)rd(m, a, 8); }
uint64_t qa_compat_q2_mem_read_u64(const qa_compat_q2_memory *m, qa_guest_address a) { return rd(m, a, 8); }
double qa_compat_q2_mem_read_f32(const qa_compat_q2_memory *m, qa_guest_address a) { uint32_t u = (uint32_t)rd(m, a, 4); float f; memcpy(&f, &u, 4); return f; }
qa_vec3 qa_compat_q2_mem_read_f32_vector(const qa_compat_q2_memory *m, qa_guest_address a) {
  qa_vec3 v = {qa_compat_q2_mem_read_f32(m, a), qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, a, 4)),
               qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, a, 8))}; return v;
}
bool qa_compat_q2_mem_read_pointer(const qa_compat_q2_memory *m, qa_guest_address a, qa_guest_address *out) {
  uint64_t raw = rd(m, a, (size_t)m->pointer_bytes);
  int64_t sv = m->pointer_bytes == 4 ? (int64_t)(uint32_t)raw : (int64_t)raw;
  qa_guest_address_result r = m->pointer(m->ctx, sv);
  if (r.has_value && out) *out = r.value;
  return r.has_value;
}
void qa_compat_q2_mem_write_u8(const qa_compat_q2_memory *m, qa_guest_address a, uint32_t v) { wr(m, a, v, 1); }
void qa_compat_q2_mem_write_i16(const qa_compat_q2_memory *m, qa_guest_address a, int32_t v) { wr(m, a, (uint16_t)v, 2); }
void qa_compat_q2_mem_write_u16(const qa_compat_q2_memory *m, qa_guest_address a, uint32_t v) { wr(m, a, v, 2); }
void qa_compat_q2_mem_write_i32(const qa_compat_q2_memory *m, qa_guest_address a, int32_t v) { wr(m, a, (uint32_t)v, 4); }
void qa_compat_q2_mem_write_u32(const qa_compat_q2_memory *m, qa_guest_address a, uint32_t v) { wr(m, a, v, 4); }
void qa_compat_q2_mem_write_i64(const qa_compat_q2_memory *m, qa_guest_address a, int64_t v) { wr(m, a, (uint64_t)v, 8); }
void qa_compat_q2_mem_write_u64(const qa_compat_q2_memory *m, qa_guest_address a, uint64_t v) { wr(m, a, v, 8); }
void qa_compat_q2_mem_write_f32(const qa_compat_q2_memory *m, qa_guest_address a, double v) { float f = (float)v; uint32_t u; memcpy(&u, &f, 4); wr(m, a, u, 4); }
void qa_compat_q2_mem_write_pointer(const qa_compat_q2_memory *m, qa_guest_address a, bool has, qa_guest_address v) {
  wr(m, a, has ? (uint64_t)v.byte_offset : 0, (size_t)m->pointer_bytes);
}
double qa_compat_q2_native_allocation_bytes(double logical) {
  if (logical != trunc(logical) || logical < 0 || logical > 9007199254740991.0 - 4095) qa_range_error("Invalid native allocation size");
  double r = ceil(logical / 4096) * 4096; return r < 4096 ? 4096 : r;
}
qa_guest_address qa_compat_q2_allocate_native_memory(const qa_compat_q2_memory *m, double logical, const char *label) {
  return m->allocate(m->ctx, qa_compat_q2_native_allocation_bytes(logical), true, 4096, label);
}
const qa_guest_call_value *qa_compat_q2_guest_argument(const qa_guest_call_value *args, size_t count, size_t index) {
  if (index >= count) qa_range_error("Missing guest argument %zu", index);
  return &args[index];
}
int64_t qa_compat_q2_guest_integer(const qa_guest_call_value *args, size_t count, size_t index) {
  const qa_guest_call_value *v = qa_compat_q2_guest_argument(args, count, index);
  if (v->kind == QA_GUEST_VALUE_INT64) return v->int64_value;
  if (v->kind == QA_GUEST_VALUE_UINT64) return (int64_t)v->uint64_value;
  if (v->kind == QA_GUEST_VALUE_INT32 || v->kind == QA_GUEST_VALUE_UINT32) return (int64_t)v->number_value;
  qa_range_error("Guest integer required");
}
bool qa_compat_q2_guest_pointer(const qa_guest_call_value *args, size_t count, size_t index, qa_guest_address *out) {
  const qa_guest_call_value *v = qa_compat_q2_guest_argument(args, count, index);
  if (v->kind != QA_GUEST_VALUE_POINTER) qa_range_error("Guest pointer required");
  if (!v->pointer_value.has_value) return false;
  if (out) *out = v->pointer_value.value;
  return true;
}
qa_guest_address qa_compat_q2_guest_required_pointer(const qa_guest_call_value *args, size_t count, size_t index) {
  qa_guest_address a; memset(&a, 0, sizeof a);
  if (!qa_compat_q2_guest_pointer(args, count, index, &a)) qa_range_error("Nonnull guest pointer required");
  return a;
}
qa_guest_call_value qa_compat_q2_call_pointer(bool has, qa_guest_address value) {
  qa_guest_call_value v; memset(&v, 0, sizeof v); v.kind = QA_GUEST_VALUE_POINTER; v.pointer_value.has_value = has; v.pointer_value.value = value; return v;
}
qa_guest_call_value qa_compat_q2_call_int32(double value) { qa_guest_call_value v; memset(&v, 0, sizeof v); v.kind = QA_GUEST_VALUE_INT32; v.number_value = value; return v; }
qa_guest_call_value qa_compat_q2_call_uint32(double value) { qa_guest_call_value v; memset(&v, 0, sizeof v); v.kind = QA_GUEST_VALUE_UINT32; v.number_value = value; return v; }
qa_guest_call_value qa_compat_q2_call_void(void) { qa_guest_call_value v; memset(&v, 0, sizeof v); v.kind = QA_GUEST_VALUE_VOID; return v; }

const qa_native_call_abi qa_compat_q2_classic_abi = {"windows-i386", "pe32", 4, "cdecl"};
static const qa_guest_field_layout usercmd_fields[] = {
  {"msec",0,"uint8",1},{"buttons",1,"uint8",1},{"angles",2,"int16",3},{"move",8,"int16",3},{"impulse",14,"uint8",1},{"lightlevel",15,"uint8",1}};
const qa_guest_layout qa_compat_q2_classic_usercmd_layout = {"q2-classic:usercmd",16,2,4,usercmd_fields,6};
static const qa_guest_field_layout ps_fields[] = {
  {"pmove.pm_type",0,"int32",1},{"pmove.origin",4,"int16",3},{"pmove.velocity",10,"int16",3},{"pmove.pm_flags",16,"uint8",1},
  {"pmove.pm_time",17,"uint8",1},{"pmove.gravity",18,"int16",1},{"pmove.delta_angles",20,"int16",3},{"viewangles",28,"float32",3},
  {"viewoffset",40,"float32",3},{"kick_angles",52,"float32",3},{"gunangles",64,"float32",3},{"gunoffset",76,"float32",3},
  {"gunindex",88,"int32",1},{"gunframe",92,"int32",1},{"blend",96,"float32",4},{"fov",112,"float32",1},{"rdflags",116,"int32",1},{"stats",120,"int16",32}};
const qa_guest_layout qa_compat_q2_classic_player_state_layout = {"q2-classic:player-state",184,4,4,ps_fields,18};
static const qa_guest_field_layout edict_fields[] = {
  {"s.number",0,"int32",1},{"s.origin",4,"float32",3},{"s.angles",16,"float32",3},{"s.old_origin",28,"float32",3},{"s.modelindex",40,"int32",4},
  {"s.frame",56,"int32",1},{"s.skinnum",60,"int32",1},{"s.effects",64,"uint32",1},{"s.renderfx",68,"int32",1},{"s.solid",72,"int32",1},
  {"s.sound",76,"int32",1},{"s.event",80,"int32",1},{"client",84,"pointer",1},{"inuse",88,"int32",1},{"linkcount",92,"int32",1},
  {"area",96,"pointer",2},{"num_clusters",104,"int32",1},{"clusternums",108,"int32",16},{"headnode",172,"int32",1},{"areanum",176,"int32",2},
  {"svflags",184,"int32",1},{"mins",188,"float32",3},{"maxs",200,"float32",3},{"absmin",212,"float32",3},{"absmax",224,"float32",3},
  {"size",236,"float32",3},{"solid",248,"int32",1},{"clipmask",252,"int32",1},{"owner",256,"pointer",1}};
const qa_guest_layout qa_compat_q2_classic_edict_layout = {"q2-classic:edict-prefix",260,4,4,edict_fields,29};
static const qa_guest_field_layout trace_fields[] = {
  {"allsolid",0,"int32",1},{"startsolid",4,"int32",1},{"fraction",8,"float32",1},{"endpos",12,"float32",3},{"plane.normal",24,"float32",3},
  {"plane.dist",36,"float32",1},{"plane.type",40,"uint8",1},{"plane.signbits",41,"uint8",1},{"plane.pad",42,"uint8",2},{"surface",44,"pointer",1},
  {"contents",48,"int32",1},{"ent",52,"pointer",1}};
const qa_guest_layout qa_compat_q2_classic_trace_layout = {"q2-classic:trace",56,4,4,trace_fields,12};
const qa_guest_value_layout qa_compat_q2_q2_int = {false, "int32", {0}};
const qa_guest_value_layout qa_compat_q2_q2_pointer = {false, "pointer", {0}};
const qa_guest_value_layout qa_compat_q2_q2_float = {false, "float32", {0}};
const qa_guest_value_layout qa_compat_q2_q2_double = {false, "float64", {0}};
const qa_guest_value_layout qa_compat_q2_q2_trace = {true, 0, {"q2-classic:trace",56,4,4,trace_fields,12}};

qa_compat_q2_guest_call_signature qa_compat_q2_classic_signature(const qa_guest_value_layout *p, size_t n, bool is_void, qa_guest_value_layout result, bool variadic) {
  qa_compat_q2_guest_call_signature s; memset(&s, 0, sizeof s);
  s.abi = qa_compat_q2_classic_abi; s.parameters = p; s.parameter_count = n; s.result_is_void = is_void; s.result = result; s.variadic = variadic; return s;
}
#define I qa_compat_q2_q2_int
#define P qa_compat_q2_q2_pointer
#define F qa_compat_q2_q2_float
#define T qa_compat_q2_q2_trace
static const qa_guest_value_layout pa_ip[] = {I, P}, pa_p[] = {P}, pa_pip[] = {P, I, P}, pa_pp[] = {P, P},
  pa_piifff[] = {P, I, I, F, F, F}, pa_ppiifff[] = {P, P, I, I, F, F, F}, pa_ii[] = {I, I}, pa_trace[] = {P, P, P, P, P, I},
  pa_pppii[] = {P, P, P, I, I}, pa_pi[] = {P, I}, pa_i[] = {I}, pa_f[] = {F}, pa_ppi[] = {P, P, I}, pa_none[] = {I},
  pa_fi[] = {F, I}, pa_ppp[] = {P, P, P};
#define SIG(p, r) {qa_compat_q2_classic_abi, (p), sizeof(p) / sizeof((p)[0]), false, (r), false}
#define SIGV(p) {qa_compat_q2_classic_abi, (p), sizeof(p) / sizeof((p)[0]), true, {false, "int32", {0}}, false}
#define SIGVV(p) {qa_compat_q2_classic_abi, (p), sizeof(p) / sizeof((p)[0]), true, {false, "int32", {0}}, true}
#define SIGN(r) {qa_compat_q2_classic_abi, pa_none, 0, false, (r), false}
#define SIGVN() {qa_compat_q2_classic_abi, pa_none, 0, true, {false, "int32", {0}}, false}
const qa_compat_q2_classic_import qa_compat_q2_classic_imports[] = {
  {"bprintf", SIGVV(pa_ip)}, {"dprintf", SIGVV(pa_p)}, {"cprintf", SIGVV(pa_pip)}, {"centerprintf", SIGVV(pa_pp)},
  {"sound", SIGV(pa_piifff)}, {"positioned_sound", SIGV(pa_ppiifff)}, {"configstring", SIGV(pa_ip)}, {"error", SIGVV(pa_p)},
  {"modelindex", SIG(pa_p, I)}, {"soundindex", SIG(pa_p, I)}, {"imageindex", SIG(pa_p, I)}, {"setmodel", SIGV(pa_pp)},
  {"trace", SIG(pa_trace, T)}, {"pointcontents", SIG(pa_p, I)}, {"inPVS", SIG(pa_pp, I)}, {"inPHS", SIG(pa_pp, I)},
  {"SetAreaPortalState", SIGV(pa_ii)}, {"AreasConnected", SIG(pa_ii, I)}, {"linkentity", SIGV(pa_p)}, {"unlinkentity", SIGV(pa_p)},
  {"BoxEdicts", SIG(pa_pppii, I)}, {"Pmove", SIGV(pa_p)}, {"multicast", SIGV(pa_pi)}, {"unicast", SIGV(pa_pi)},
  {"WriteChar", SIGV(pa_i)}, {"WriteByte", SIGV(pa_i)}, {"WriteShort", SIGV(pa_i)}, {"WriteLong", SIGV(pa_i)},
  {"WriteFloat", SIGV(pa_f)}, {"WriteString", SIGV(pa_p)}, {"WritePosition", SIGV(pa_p)}, {"WriteDir", SIGV(pa_p)},
  {"WriteAngle", SIGV(pa_f)}, {"TagMalloc", SIG(pa_ii, P)}, {"TagFree", SIGV(pa_p)}, {"FreeTags", SIGV(pa_i)},
  {"cvar", SIG(pa_ppi, P)}, {"cvar_set", SIG(pa_pp, P)}, {"cvar_forceset", SIG(pa_pp, P)},
  {"argc", SIGN(I)}, {"argv", SIG(pa_i, P)}, {"args", SIGN(P)}, {"AddCommandString", SIGV(pa_p)}, {"DebugGraph", SIGV(pa_fi)},
};
const size_t qa_compat_q2_classic_import_count = sizeof qa_compat_q2_classic_imports / sizeof qa_compat_q2_classic_imports[0];
const qa_compat_q2_classic_export qa_compat_q2_classic_exports[] = {
  {"Init", 4, SIGVN()}, {"Shutdown", 8, SIGVN()}, {"SpawnEntities", 12, SIGV(pa_ppp)},
  {"WriteGame", 16, SIGV(pa_pi)}, {"ReadGame", 20, SIGV(pa_p)}, {"WriteLevel", 24, SIGV(pa_p)}, {"ReadLevel", 28, SIGV(pa_p)},
  {"ClientConnect", 32, SIG(pa_pp, I)}, {"ClientBegin", 36, SIGV(pa_p)}, {"ClientUserinfoChanged", 40, SIGV(pa_pp)},
  {"ClientDisconnect", 44, SIGV(pa_p)}, {"ClientCommand", 48, SIGV(pa_p)}, {"ClientThink", 52, SIGV(pa_pp)},
  {"RunFrame", 56, SIGVN()}, {"ServerCommand", 60, SIGVN()},
};
const size_t qa_compat_q2_classic_export_count = sizeof qa_compat_q2_classic_exports / sizeof qa_compat_q2_classic_exports[0];
const qa_compat_q2_classic_export *qa_compat_q2_classic_find_export(const char *name) {
  for (size_t i = 0; i < qa_compat_q2_classic_export_count; i++) if (strcmp(qa_compat_q2_classic_exports[i].name, name) == 0) return &qa_compat_q2_classic_exports[i];
  return NULL;
}
