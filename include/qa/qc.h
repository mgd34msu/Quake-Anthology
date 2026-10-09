#ifndef QA_QC_H
#define QA_QC_H

#include "qa/gameplay.h"
#include "qa/inventory.h"
#include "qa/session.h"
#include "qa/vfs.h"
#include "qa/world.h"

/* QuakeC is an external-program compatibility lane. Native anthology gameplay
 * does not depend on this API. A program is immutable and may back several
 * private instances; each instance has its own globals, edicts and strings. */
typedef struct qa_qc_program qa_qc_program;
typedef struct qa_qc_instance qa_qc_instance;
typedef struct qa_qc_checkpoint qa_qc_checkpoint;

typedef enum qa_qc_api {
    QA_QC_API_NETQUAKE,
    QA_QC_API_QUAKEWORLD
} qa_qc_api;

typedef enum qa_qc_profile {
    QA_QC_NETQUAKE,
    QA_QC_QUAKEWORLD,
    QA_QC_RERELEASE
} qa_qc_profile;

typedef enum qa_qc_value_type {
    QA_QC_VOID,
    QA_QC_STRING,
    QA_QC_FLOAT,
    QA_QC_VECTOR,
    QA_QC_ENTITY,
    QA_QC_FIELD,
    QA_QC_FUNCTION,
    QA_QC_POINTER,
    QA_QC_OPAQUE
} qa_qc_value_type;

/* External version 6 opcode numbers and their global operand widths. */
#define QA_QC_OPCODE_LIST(X) \
X(DONE,0,      3,0,0,0,3,0) \
X(MUL_F,1,     1,1,0,1,0,0) \
X(MUL_V,2,     3,3,0,1,0,0) \
X(MUL_FV,3,    1,3,0,3,0,0) \
X(MUL_VF,4,    3,1,0,3,0,0) \
X(DIV_F,5,     1,1,0,1,0,0) \
X(ADD_F,6,     1,1,0,1,0,0) \
X(ADD_V,7,     3,3,0,3,0,0) \
X(SUB_F,8,     1,1,0,1,0,0) \
X(SUB_V,9,     3,3,0,3,0,0) \
X(EQ_F,10,      1,1,0,1,0,0) \
X(EQ_V,11,      3,3,0,1,0,0) \
X(EQ_S,12,      1,1,0,1,0,0) \
X(EQ_E,13,      1,1,0,1,0,0) \
X(EQ_FN,14,     1,1,0,1,0,0) \
X(NE_F,15,      1,1,0,1,0,0) \
X(NE_V,16,      3,3,0,1,0,0) \
X(NE_S,17,      1,1,0,1,0,0) \
X(NE_E,18,      1,1,0,1,0,0) \
X(NE_FN,19,     1,1,0,1,0,0) \
X(LE,20,        1,1,0,1,0,0) \
X(GE,21,        1,1,0,1,0,0) \
X(LT,22,        1,1,0,1,0,0) \
X(GT,23,        1,1,0,1,0,0) \
X(LOAD_F,24,    1,1,0,1,0,0) \
X(LOAD_V,25,    1,1,0,3,0,0) \
X(LOAD_S,26,    1,1,0,1,0,0) \
X(LOAD_ENT,27,  1,1,0,1,0,0) \
X(LOAD_FLD,28,  1,1,0,1,0,0) \
X(LOAD_FN,29,   1,1,0,1,0,0) \
X(ADDRESS,30,   1,1,0,1,0,0) \
X(STORE_F,31,   1,0,1,0,0,0) \
X(STORE_V,32,   3,0,3,0,0,0) \
X(STORE_S,33,   1,0,1,0,0,0) \
X(STORE_ENT,34, 1,0,1,0,0,0) \
X(STORE_FLD,35, 1,0,1,0,0,0) \
X(STORE_FN,36,  1,0,1,0,0,0) \
X(STOREP_F,37,  1,1,0,0,0,0) \
X(STOREP_V,38,  3,1,0,0,0,0) \
X(STOREP_S,39,  1,1,0,0,0,0) \
X(STOREP_ENT,40,1,1,0,0,0,0) \
X(STOREP_FLD,41,1,1,0,0,0,0) \
X(STOREP_FN,42, 1,1,0,0,0,0) \
X(RETURN,43,    3,0,0,0,3,0) \
X(NOT_F,44,     1,0,0,1,0,0) \
X(NOT_V,45,     3,0,0,1,0,0) \
X(NOT_S,46,     1,0,0,1,0,0) \
X(NOT_ENT,47,   1,0,0,1,0,0) \
X(NOT_FN,48,    1,0,0,1,0,0) \
X(IF,49,        1,0,0,0,0,0) \
X(IFNOT,50,     1,0,0,0,0,0) \
X(CALL0,51,     1,0,0,0,3,0) \
X(CALL1,52,     1,0,0,0,3,1) \
X(CALL2,53,     1,0,0,0,3,2) \
X(CALL3,54,     1,0,0,0,3,3) \
X(CALL4,55,     1,0,0,0,3,4) \
X(CALL5,56,     1,0,0,0,3,5) \
X(CALL6,57,     1,0,0,0,3,6) \
X(CALL7,58,     1,0,0,0,3,7) \
X(CALL8,59,     1,0,0,0,3,8) \
X(STATE,60,     1,1,0,0,0,0) \
X(GOTO,61,      0,0,0,0,0,0) \
X(AND,62,       1,1,0,1,0,0) \
X(OR,63,        1,1,0,1,0,0) \
X(BITAND,64,    1,1,0,1,0,0) \
X(BITOR,65,     1,1,0,1,0,0)

typedef enum qa_qc_opcode {
#define QA_QC_OPCODE(name, number, ra, rb, wb, wc, result, arguments) QA_QC_##name = number,
QA_QC_OPCODE_LIST(QA_QC_OPCODE)
#undef QA_QC_OPCODE
} qa_qc_opcode;

typedef struct qa_qc_statement {
    qa_qc_opcode opcode;
    uint16_t a, b, c;
} qa_qc_statement;
typedef struct qa_qc_statement_access {
    uint32_t read[25], write[3];
    uint32_t read_count, write_count;
} qa_qc_statement_access;

typedef struct qa_qc_definition {
    qa_qc_value_type type;
    uint16_t native_type, offset;
    bool save;
    const char *name; /* Borrowed from the program. */
} qa_qc_definition;

#define QA_QC_GAME_FIELD_LIST(X) \
    X(movetype) X(solid) X(flags) X(health) X(waterlevel) X(watertype) \
    X(ltime) X(nextthink) X(gravity) X(ideal_yaw) X(yaw_speed) X(avelocity) \
    X(enemy) X(goalentity) X(owner) X(modelindex) X(model) \
    X(origin) X(angles) X(velocity) X(mins) X(maxs) X(size) \
    X(absmin) X(absmax) X(groundentity) X(takedamage) X(colormap) X(skin) \
    X(frame) X(think) X(chain) X(effects) X(alpha) X(scale) \
    X(weaponmodel) X(weaponframe) X(punchangle)
typedef struct qa_qc_game_fields {
#define QA_QC_GAME_FIELD(name) const qa_qc_definition *name;
    QA_QC_GAME_FIELD_LIST(QA_QC_GAME_FIELD)
#undef QA_QC_GAME_FIELD
} qa_qc_game_fields;

#define QA_QC_ENGINE_GLOBAL_LIST(X) \
    X(self) X(time) X(frametime) X(v_forward) X(v_right) X(v_up) \
    X(trace_allsolid) X(trace_startsolid) X(trace_fraction) X(trace_endpos) \
    X(trace_plane_normal) X(trace_plane_dist) X(trace_ent) X(trace_inopen) X(trace_inwater)
typedef struct qa_qc_engine_globals {
#define QA_QC_ENGINE_GLOBAL(name) const qa_qc_definition *name;
    QA_QC_ENGINE_GLOBAL_LIST(QA_QC_ENGINE_GLOBAL)
#undef QA_QC_ENGINE_GLOBAL
} qa_qc_engine_globals;

typedef struct qa_qc_function {
    int32_t first_statement;
    uint32_t parameter_start, local_words;
    const char *name, *file; /* Borrowed from the program. */
    uint8_t parameter_count, parameter_sizes[8];
    bool named_builtin;
} qa_qc_function;

typedef struct qa_qc_program_info {
    qa_qc_api api;
    uint32_t system_crc, entity_field_words, global_words;
    uint32_t statement_count, global_count, field_count, function_count;
    uint16_t file_crc;
} qa_qc_program_info;

/* Version 6 programs with the original NetQuake (5927) and QuakeWorld (54730)
 * system layouts are accepted. Unknown definition types retain their words as
 * opaque values. source is copied for diagnostics. */
bool qa_qc_program_load(qa_bytes bytes, const char *source,
                        qa_qc_program **out, qa_error *error);
bool qa_qc_program_load_vfs(qa_vfs *vfs, const char *path,
                            qa_qc_program **out, qa_error *error);
void qa_qc_program_destroy(qa_qc_program *program);
qa_qc_program_info qa_qc_program_describe(const qa_qc_program *program);
const qa_qc_statement *qa_qc_program_statement(const qa_qc_program *program,
                                                uint32_t index);
bool qa_qc_program_statement_access(const qa_qc_program *, uint32_t,
    qa_qc_statement_access *, qa_error *);
bool qa_qc_program_function_returns_only(const qa_qc_program *, uint32_t);
const qa_qc_definition *qa_qc_program_global(const qa_qc_program *program,
                                              uint32_t index);
/* Read the immutable authored initial word while qualifying a source ABI.
 * This does not observe or copy a mutable instance's globals. */
bool qa_qc_program_initial_int(const qa_qc_program *, uint32_t word,
                               int32_t *out, qa_error *);
const qa_qc_definition *qa_qc_program_field(const qa_qc_program *program,
                                             uint32_t index);
/* Fixed engine definitions are resolved once when the immutable program loads.
 * Missing definitions remain NULL; consumers preserve their source policy. */
const qa_qc_game_fields *qa_qc_program_resolved_fields(const qa_qc_program *);
const qa_qc_engine_globals *qa_qc_program_resolved_globals(const qa_qc_program *);
const qa_qc_function *qa_qc_program_function(const qa_qc_program *program,
                                             uint32_t index);
uint32_t qa_qc_program_function_end(const qa_qc_program *, const qa_qc_function *);
const qa_qc_definition *qa_qc_program_find_global(const qa_qc_program *program,
                                                   const char *name);
const qa_qc_definition *qa_qc_program_find_field(const qa_qc_program *program,
                                                  const char *name);
const qa_qc_function *qa_qc_program_find_function(const qa_qc_program *program,
                                                   const char *name,
                                                   uint32_t *index);

typedef struct qa_qc_entity_layout {
    uint32_t stride_bytes;
    uint32_t variables_offset_bytes;
    uint32_t field_words;
} qa_qc_entity_layout;

/* The original edict prefix stores free at byte zero and freetime in the word
 * immediately before variables. Custom layouts retain those two positions. */

qa_qc_entity_layout qa_qc_default_entity_layout(const qa_qc_program *program,
                                                  qa_qc_profile profile);

/* Host calls which are not pure VM operations. Numbered profiles use the
 * original builtin numbers; rerelease extension builtins are resolved by name. */
typedef enum qa_qc_builtin {
    QA_QC_BUILTIN_SETORIGIN, QA_QC_BUILTIN_SETMODEL, QA_QC_BUILTIN_SETSIZE,
    QA_QC_BUILTIN_BREAK, QA_QC_BUILTIN_SOUND, QA_QC_BUILTIN_OBJERROR,
    QA_QC_BUILTIN_SPAWN, QA_QC_BUILTIN_REMOVE, QA_QC_BUILTIN_TRACELINE,
    QA_QC_BUILTIN_CHECKCLIENT, QA_QC_BUILTIN_PRECACHE_SOUND,
    QA_QC_BUILTIN_PRECACHE_MODEL, QA_QC_BUILTIN_STUFFCMD,
    QA_QC_BUILTIN_FINDRADIUS, QA_QC_BUILTIN_BPRINT, QA_QC_BUILTIN_SPRINT,
    QA_QC_BUILTIN_DPRINT, QA_QC_BUILTIN_COREDUMP, QA_QC_BUILTIN_EPRINT,
    QA_QC_BUILTIN_WALKMOVE, QA_QC_BUILTIN_DROPTOFLOOR,
    QA_QC_BUILTIN_LIGHTSTYLE, QA_QC_BUILTIN_CHECKBOTTOM,
    QA_QC_BUILTIN_POINTCONTENTS, QA_QC_BUILTIN_AIM, QA_QC_BUILTIN_CVAR,
    QA_QC_BUILTIN_LOCALCMD, QA_QC_BUILTIN_PARTICLE, QA_QC_BUILTIN_CHANGEYAW,
    QA_QC_BUILTIN_WRITEBYTE, QA_QC_BUILTIN_WRITECHAR,
    QA_QC_BUILTIN_WRITESHORT, QA_QC_BUILTIN_WRITELONG,
    QA_QC_BUILTIN_WRITECOORD, QA_QC_BUILTIN_WRITEANGLE,
    QA_QC_BUILTIN_WRITESTRING, QA_QC_BUILTIN_WRITEENTITY,
    QA_QC_BUILTIN_MOVETOGOAL, QA_QC_BUILTIN_PRECACHE_FILE,
    QA_QC_BUILTIN_MAKESTATIC, QA_QC_BUILTIN_CHANGELEVEL,
    QA_QC_BUILTIN_CVAR_SET, QA_QC_BUILTIN_CENTERPRINT,
    QA_QC_BUILTIN_AMBIENTSOUND, QA_QC_BUILTIN_SETSPAWNPARMS,
    QA_QC_BUILTIN_LOGFRAG, QA_QC_BUILTIN_INFOKEY,
    QA_QC_BUILTIN_MULTICAST, QA_QC_BUILTIN_SETCOLOR,
    QA_QC_BUILTIN_EX_BPRINT, QA_QC_BUILTIN_EX_SPRINT,
    QA_QC_BUILTIN_EX_CENTERPRINT, QA_QC_BUILTIN_EX_FINALE_FINISHED,
    QA_QC_BUILTIN_EX_LOCALSOUND, QA_QC_BUILTIN_EX_DRAW_POINT,
    QA_QC_BUILTIN_EX_DRAW_LINE, QA_QC_BUILTIN_EX_DRAW_ARROW,
    QA_QC_BUILTIN_EX_DRAW_RAY, QA_QC_BUILTIN_EX_DRAW_CIRCLE,
    QA_QC_BUILTIN_EX_DRAW_BOUNDS, QA_QC_BUILTIN_EX_DRAW_WORLDTEXT,
    QA_QC_BUILTIN_EX_DRAW_SPHERE, QA_QC_BUILTIN_EX_DRAW_CYLINDER,
    QA_QC_BUILTIN_EX_BOT_MOVETOPOINT, QA_QC_BUILTIN_EX_BOT_FOLLOWENTITY,
    QA_QC_BUILTIN_EX_CHECK_PLAYER_FLAGS, QA_QC_BUILTIN_EX_WALKPATHTOGOAL,
    QA_QC_BUILTIN_EX_PROMPT, QA_QC_BUILTIN_EX_PROMPTCHOICE,
    QA_QC_BUILTIN_EX_CLEARPROMPT,
    QA_QC_BUILTIN_NAMED /* Binding name selects a program-specific extension. */
} qa_qc_builtin;

typedef struct qa_qc_builtin_requirement {
    int32_t number; /* Zero for a named-only builtin. */
    qa_qc_builtin builtin;
    const char *name;
} qa_qc_builtin_requirement;

const qa_qc_builtin_requirement *qa_qc_builtin_requirements(qa_qc_profile profile,
                                                             size_t *count);
typedef bool (*qa_qc_builtin_fn)(void *context, qa_qc_instance *instance,
                                 qa_qc_builtin builtin, const char *name,
                                 qa_error *error);
typedef bool (*qa_qc_unknown_builtin_fn)(void *context,
                                         qa_qc_instance *instance,
                                         int32_t number, const char *name,
                                         qa_error *error);
/* Builtin and observer callbacks are synchronous and may reenter this instance.
 * Event and continuation values are borrowed only for the active callback. */
typedef struct qa_qc_builtin_binding {
    qa_qc_builtin builtin;
    const char *name; /* Required for QA_QC_BUILTIN_NAMED. */
    void *context;
    qa_qc_builtin_fn call;
} qa_qc_builtin_binding;

typedef enum qa_qc_slot_kind {
    QA_QC_SLOT_FREE,
    QA_QC_SLOT_WORLD,
    QA_QC_SLOT_OWNED,
    QA_QC_SLOT_BORROWED
} qa_qc_slot_kind;

typedef struct qa_qc_slot_binding {
    qa_qc_slot_kind kind;
    uint32_t slot;
    qa_actor_id actor;
    qa_actor_owner owner;
    uint32_t source_slot;
} qa_qc_slot_binding;

typedef enum qa_qc_store_kind {
    QA_QC_STORE_GLOBAL,
    QA_QC_STORE_ENTITY
} qa_qc_store_kind;

typedef struct qa_qc_store_event {
    qa_qc_store_kind kind;
    int32_t entity_reference; /* Zero for globals and the world edict. */
    uint32_t function, statement, depth;
    uint32_t word, count;
    uint32_t before[3], after[3];
} qa_qc_store_event;

typedef struct qa_qc_call_event {
    uint32_t function, caller, statement, argument_count, depth;
} qa_qc_call_event;

/* A replacement must synchronously call continue or skip exactly once before
 * returning. continue may recursively enter QC and therefore may notify the
 * same observer again. The handle expires when its callback returns. */
typedef struct qa_qc_call_next {
    qa_qc_instance *instance;
    uint64_t invocation;
} qa_qc_call_next;

typedef enum qa_qc_inline_saved_scope {
    QA_QC_INLINE_NOT_STANDALONE,
    QA_QC_INLINE_FRAME,
    QA_QC_INLINE_GLOBAL
} qa_qc_inline_saved_scope;

/* entry is the first statement owned by the region and exit is its join
 * statement. A standalone region returns the float in saved_word. */
typedef struct qa_qc_inline_region {
    uint32_t function, entry, exit;
    bool replaceable;
    qa_qc_inline_saved_scope saved_scope;
    uint32_t saved_word;
} qa_qc_inline_region;

typedef struct qa_qc_inline_event {
    qa_qc_inline_region region;
    uint32_t depth;
} qa_qc_inline_event;

typedef struct qa_qc_inline_next {
    qa_qc_instance *instance;
    uint64_t invocation;
} qa_qc_inline_next;

/* stored observes an already committed guest write. Returning false aborts the
 * current execution but does not roll that write back. */
typedef bool (*qa_qc_store_observer_fn)(void *context, qa_qc_instance *instance,
                                        const qa_qc_store_event *event,
                                        qa_error *error);
typedef bool (*qa_qc_call_observer_fn)(void *context, qa_qc_instance *instance,
                                       const qa_qc_call_event *event,
                                       qa_error *error);
typedef bool (*qa_qc_call_replace_fn)(void *context, qa_qc_instance *instance,
                                      const qa_qc_call_event *event,
                                      qa_qc_call_next next, qa_error *error);
typedef bool (*qa_qc_trace_fn)(void *context, qa_qc_instance *instance,
                               uint32_t function, uint32_t statement,
                               qa_error *error);
typedef bool (*qa_qc_inline_fn)(void *context, qa_qc_instance *instance,
                                const qa_qc_inline_event *event,
                                qa_qc_inline_next next, qa_error *error);

typedef struct qa_qc_observers {
    void *context;
    qa_qc_store_observer_fn stored;
    qa_qc_call_observer_fn entered, left;
    qa_qc_call_replace_fn replace;
    qa_qc_inline_fn inline_boundary;
    qa_qc_trace_fn trace;
    /* The store observer has no global-store consumer. */
    bool entity_stores_only;
} qa_qc_observers;

typedef enum qa_qc_entity_access_kind {
    QA_QC_ENTITY_READ,
    QA_QC_ENTITY_WRITE,
    QA_QC_ENTITY_BIND
} qa_qc_entity_access_kind;

typedef struct qa_qc_entity_access {
    qa_qc_entity_access_kind kind;
    qa_qc_slot_binding binding;
    int32_t reference;
    uint32_t word, count;
} qa_qc_entity_access;

/* BIND admits an actual bound/restored actor after its body is synchronized.
 * READ/WRITE refresh borrowed fields before access; uninterrupted bytecode
 * LOADs may reuse that projection. Owned guest fields are authoritative. A
 * projection refresh must not mutate the shared authority it reads. */
typedef bool (*qa_qc_entity_access_fn)(void *context,
                                       qa_qc_instance *instance,
                                       const qa_qc_entity_access *access,
                                       qa_error *error);

/* Host checkpoint bytes cover host-owned state coupled to this VM, such as
 * routed message buffers, cvars, callbacks and the source RNG. Checkpoint and
 * restore callbacks may inspect guest state but cannot execute guest code. A
 * successful checkpoint callback transfers an owned qa_buffer to the VM. */
typedef struct qa_qc_host {
    qa_session *session;
    qa_world *world;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_pickups *pickups;
    qa_vfs *vfs;
    qa_actor_owner owner;
    qa_actor_definition default_definition;
    const qa_qc_builtin_binding *builtins;
    size_t builtin_count;
    const char *const *extensions;
    size_t extension_count;
    void *context;
    /* Optional compatibility escape hatch for mod-specific builtins absent
     * from the selected profile. number is zero for a named #0 builtin. */
    qa_qc_unknown_builtin_fn unknown_builtin;
    uint32_t (*random_u32)(void *context);
    bool (*server_active)(void *context);
    /* Pure retained source clock in seconds for edict allocation and free
     * metadata. Guest callback globals may carry a different invocation time. */
    double (*source_time_seconds)(void *context);
    /* Optional selected movement authority. Called as a pure ownership lookup;
     * it must not execute guest code or alter the actor registry. When absent,
     * owned actors and this host's execution actors retain legacy authority. */
    bool (*may_move)(void *context, qa_actor_id actor);
    /* Declared component fields are projected by prepare_entity instead of
     * the implicit original body-field projection on borrowed actors. */
    bool declared_projection;
    qa_qc_entity_access_fn prepare_entity;
    bool (*checkpoint)(void *context, qa_buffer *out, qa_error *error);
    bool (*restore)(void *context, qa_bytes state, qa_error *error);
} qa_qc_host;

typedef struct qa_qc_options {
    qa_qc_profile profile;
    qa_qc_entity_layout entity_layout; /* Zero selects the original layout. */
    uint32_t entity_capacity;
    /* First slot eligible for spawn/projected actors. Zero selects one. Hosts
     * reserve world/client rows below it before source execution begins. */
    uint32_t first_dynamic_slot;
    uint32_t statement_limit, call_limit, local_word_limit;
    bool require_complete_host_profile;
    const qa_qc_inline_region *inline_regions;
    size_t inline_region_count;
    qa_qc_host host;
    qa_qc_observers observers;
} qa_qc_options;

/* The program is borrowed and must outlive the instance. The session, world and
 * gameplay pointers are shared authorities, never cloned by the QC instance.
 * An instance and its callbacks have one thread owner. */
bool qa_qc_instance_create(const qa_qc_program *program,
                           const qa_qc_options *options,
                           qa_qc_instance **out, qa_error *error);
bool qa_qc_instance_destroy(qa_qc_instance *instance, qa_error *error);
bool qa_qc_idle(const qa_qc_instance *instance);
const qa_qc_program *qa_qc_instance_program(const qa_qc_instance *instance);
qa_qc_profile qa_qc_instance_profile(const qa_qc_instance *instance);
/* Borrows the existing immutable admitted region span until instance
 * destruction. Reading it does not enter Source execution or projection. */
const qa_qc_inline_region *qa_qc_instance_inline_regions(const qa_qc_instance *,
    size_t *count);
uint32_t qa_qc_entity_count(const qa_qc_instance *instance);
uint32_t qa_qc_argument_count(const qa_qc_instance *instance);

/* Source slots are explicit. Binding OWNED requires the actor registry record
 * to have this instance's owner and the same source slot. Borrowed rows retain
 * private QC fields; body and declared shared fields refresh at every access. */
bool qa_qc_bind_actor(qa_qc_instance *instance, uint32_t slot,
                      qa_actor_id actor, qa_qc_slot_kind kind,
                      qa_error *error);
/* Reserved-row handoff retains source fields and performs normal body
 * synchronization. It qualifies the exact previous full actor binding. OWNED
 * requires this physical source slot; BORROWED retains canonical identity. */
bool qa_qc_reserved_actor_rebind_ready(const qa_qc_instance *);
bool qa_qc_rebind_reserved_actor(qa_qc_instance *, uint32_t slot,
                                  qa_actor_id previous, qa_actor_id actor,
                                  qa_qc_slot_kind kind, qa_error *);
bool qa_qc_unbind_actor(qa_qc_instance *instance, uint32_t slot,
                        qa_error *error);
bool qa_qc_slot(const qa_qc_instance *instance, uint32_t slot,
                qa_qc_slot_binding *out);
/* Cold ABI views borrow stable instance storage. Only OWNED rows publish a
 * body; borrowed rows retain their existing canonical-to-guest projection. */
bool qa_qc_entity_body_fields(qa_qc_instance *, uint32_t slot,
    const qa_entity_body_fields **out, qa_error *);
bool qa_qc_entity_collision_fields(const qa_qc_instance *, uint32_t slot,
    qa_entity_collision_fields *out, qa_error *);
/* Reads the actual ABI reference of an allocated physical row, including the
 * world and reserved/free rows. This does not bind or project an actor. */
bool qa_qc_slot_reference(const qa_qc_instance *, uint32_t slot,
                           int32_t *out, qa_error *);
/* Pure retained flags observation: no actor projection, body refresh or guest
 * callbacks. A live actor without an allocated row or declared flags field
 * returns found=false; a retired actor is an error. */
bool qa_qc_actor_movement_flags_read(const qa_qc_instance *, qa_actor_id,
    uint32_t *out, bool *found, qa_error *);
bool qa_qc_actor_reference(qa_qc_instance *instance, qa_actor_id actor,
                           bool project, int32_t *out, qa_error *error);
bool qa_qc_reference_actor(const qa_qc_instance *instance, int32_t reference,
                           qa_actor_id *out, qa_error *error);
/* Forward every shared-session release after registry invalidation. */
void qa_qc_actor_released(qa_qc_instance *instance, qa_actor_record released);
/* Reacquire OWNED actors from the registry's owner/source map after restore or
 * travel. Checkpoints restore borrowed identities from saved actor IDs. */
bool qa_qc_rebind_sources(qa_qc_instance *instance, qa_error *error);
/* Host map admission uses the same allocator/removal rules as guest builtins. */
bool qa_qc_spawn_entity(qa_qc_instance *, int32_t *reference, qa_error *);
bool qa_qc_remove_entity(qa_qc_instance *, int32_t reference, qa_error *);

bool qa_qc_execute(qa_qc_instance *instance, uint32_t function,
                   uint32_t argument_count, qa_error *error);
bool qa_qc_execute_named(qa_qc_instance *instance, const char *function,
                         uint32_t argument_count, qa_error *error);
bool qa_qc_call_continue(qa_qc_call_next next, qa_error *error);
/* While next's replacement is active, report a successfully continued source
 * body. Skipped, cancelled, failed and expired continuations return false. */
bool qa_qc_call_completed(qa_qc_call_next next);
bool qa_qc_call_skip(qa_qc_call_next next, const uint32_t result[3],
                      qa_error *error);
/* Cancel is valid only while the continued source function is on the stack.
 * It unwinds that function with result. The callback which requests it must
 * return successfully without making further guest calls. */
bool qa_qc_call_cancel(qa_qc_call_next next, const uint32_t result[3],
                        qa_error *error);
/* These setters update the captured call staging owned by next. They are the
 * reentry-safe way for a replacement to transform source arguments before it
 * continues the original function. */
bool qa_qc_call_set_arg_int(qa_qc_call_next next, uint32_t argument,
                            int32_t value, qa_error *error);
bool qa_qc_call_set_arg_float(qa_qc_call_next next, uint32_t argument,
                              float value, qa_error *error);
bool qa_qc_call_set_arg_vector(qa_qc_call_next next, uint32_t argument,
                               qa_vec3 value, qa_error *error);
bool qa_qc_inline_continue(qa_qc_inline_next next, qa_error *error);
bool qa_qc_inline_skip_to_join(qa_qc_inline_next next, qa_error *error);
bool qa_qc_execute_region(qa_qc_instance *instance,
                          const qa_qc_inline_region *region,
                          uint32_t argument_count, float *result,
                          qa_error *error);

/* Arguments use the original reserved globals at 4 + 3*n; return values use
 * words 1..3. Ordinary C float operations are the source numeric behavior. */
bool qa_qc_arg_int(const qa_qc_instance *, uint32_t argument,
                   int32_t *out, qa_error *error);
bool qa_qc_arg_float(const qa_qc_instance *, uint32_t argument,
                     float *out, qa_error *error);
bool qa_qc_arg_vector(const qa_qc_instance *, uint32_t argument,
                      qa_vec3 *out, qa_error *error);
bool qa_qc_arg_string(const qa_qc_instance *, uint32_t argument,
                      const char **out, qa_error *error);
bool qa_qc_return_int(qa_qc_instance *, int32_t value, qa_error *error);
bool qa_qc_return_float(qa_qc_instance *, float value, qa_error *error);
bool qa_qc_return_vector(qa_qc_instance *, qa_vec3 value, qa_error *error);

bool qa_qc_global_int(const qa_qc_instance *, uint32_t word,
                      int32_t *out, qa_error *error);
bool qa_qc_global_float(const qa_qc_instance *, uint32_t word,
                        float *out, qa_error *error);
bool qa_qc_global_vector(const qa_qc_instance *, uint32_t word,
                         qa_vec3 *out, qa_error *error);
bool qa_qc_set_global_int(qa_qc_instance *, uint32_t word,
                          int32_t value, qa_error *error);
bool qa_qc_set_global_float(qa_qc_instance *, uint32_t word,
                            float value, qa_error *error);
bool qa_qc_set_global_vector(qa_qc_instance *, uint32_t word,
                             qa_vec3 value, qa_error *error);
/* Host ABI staging/restoration, without guest-write observation. The words
 * remain checked against the program layout; checkpoint callbacks cannot use it. */
bool qa_qc_stage_globals(qa_qc_instance *, uint32_t word, const uint32_t *,
                          uint32_t count, qa_error *);
bool qa_qc_entity_int(qa_qc_instance *, int32_t reference, uint32_t word,
                      int32_t *out, qa_error *error);
bool qa_qc_entity_float(qa_qc_instance *, int32_t reference, uint32_t word,
                        float *out, qa_error *error);
bool qa_qc_entity_vector(qa_qc_instance *, int32_t reference, uint32_t word,
                         qa_vec3 *out, qa_error *error);
bool qa_qc_set_entity_int(qa_qc_instance *, int32_t reference, uint32_t word,
                          int32_t value, qa_error *error);
bool qa_qc_set_entity_float(qa_qc_instance *, int32_t reference, uint32_t word,
                            float value, qa_error *error);
bool qa_qc_set_entity_vector(qa_qc_instance *, int32_t reference, uint32_t word,
                             qa_vec3 value, qa_error *error);

/* Canonical-to-guest projection bypasses access and store callbacks. It is
 * valid from prepare_entity and from a shared-service refresh callback. */
bool qa_qc_project_entity_int(qa_qc_instance *, int32_t reference,
                              uint32_t word, int32_t value, qa_error *error);
bool qa_qc_project_entity_float(qa_qc_instance *, int32_t reference,
                                uint32_t word, float value, qa_error *error);
bool qa_qc_project_entity_vector(qa_qc_instance *, int32_t reference,
                                 uint32_t word, qa_vec3 value,
                                 qa_error *error);

/* Returned text is borrowed until a later string allocation or instance
 * destruction. QW negative engine-string IDs retain their original meaning. */
bool qa_qc_string(const qa_qc_instance *, int32_t id,
                  const char **out, qa_error *error);
bool qa_qc_string_allocate(qa_qc_instance *, const char *text,
                           int32_t *out, qa_error *error);
/* Named buffers may also refresh canonical string projections during a
 * checkpoint callback; capture records their post-callback contents. */
bool qa_qc_engine_string(qa_qc_instance *, const char *name, const char *text,
                         size_t capacity, int32_t *out, qa_error *error);

/* Checkpoints are accepted only at an idle callback boundary and, when bound,
 * a session safe point. They include changed globals, live edict fields,
 * runtime strings and aliases, free-slot reuse times, actor maps and the optional
 * host blob. Immutable literals and initial globals come from the loaded program;
 * profiling and debug state are not saved. Restore requires the same program,
 * profile and layout, with enough capacity for the saved logical edict extent.
 * Guest storage commits before host/body rebinding; a later restore error leaves
 * that committed image visible and must be treated as a failed load boundary. */
bool qa_qc_checkpoint_capture(qa_qc_instance *instance,
                              qa_qc_checkpoint **out, qa_error *error);
void qa_qc_checkpoint_destroy(qa_qc_checkpoint *checkpoint);
bool qa_qc_checkpoint_restore(qa_qc_instance *instance,
                              const qa_qc_checkpoint *checkpoint,
                              qa_error *error);
/* Encode publishes an owned buffer on success; release it with qa_buffer_free. */
bool qa_qc_checkpoint_encode(const qa_qc_checkpoint *checkpoint,
                             qa_buffer *out, qa_error *error);
bool qa_qc_checkpoint_decode(qa_bytes bytes, qa_qc_checkpoint **out,
                             qa_error *error);

#endif
