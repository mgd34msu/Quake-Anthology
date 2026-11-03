/* Port of src/compat/q2/classic/layout.ts. GPL-2.0-or-later.
 * Also hosts the contracts-side mirrors of MappedGuestMemory /
 * GuestCallRunner (TS owners: guest/core/contracts.ts,
 * guest/abi/runner.ts) shared by classic and rerelease compat: the
 * guest/abi headers conflict with contracts/execution.h in one TU
 * (landed native_* precedent), so memory and runner are fn+ctx
 * tables over qa_guest_address. Scalar accessors are implemented
 * over copy/write in layout.c (little-endian). */
#ifndef QA_COMPAT_Q2_CLASSIC_LAYOUT_H
#define QA_COMPAT_Q2_CLASSIC_LAYOUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary_reader.h" /* qa_compat_q2_guest_call_signature */
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/foundation/support.h"

/* --- MappedGuestMemory mirror (guest/core/contracts.ts) --- */
typedef void (*qa_compat_q2_write_observer_fn)(void *ctx);
typedef struct qa_compat_q2_memory {
  void *ctx;
  qa_module_identity module;
  const void *address_space;
  int64_t pointer_bytes; /* 4 | 8 */
  bool managed;          /* SparseGuestMemory.managed(memory) */
  qa_guest_address_result (*pointer)(void *ctx, int64_t raw_value);
  qa_guest_address (*offset)(void *ctx, qa_guest_address address, int64_t displacement);
  qa_guest_address (*allocate)(void *ctx, double byte_length, bool has_alignment,
                               int64_t alignment, const char *label);
  void (*unmap)(void *ctx, qa_guest_address address, double byte_length);
  void (*check)(void *ctx, qa_guest_address address, double byte_length, const char *access);
  uint8_t *(*borrow)(void *ctx, qa_guest_address address, double byte_length);
  void (*copy)(void *ctx, qa_guest_address address, uint8_t *dest, double byte_length);
  void (*copy_into)(void *ctx, qa_guest_address address, uint8_t *dest, size_t dest_length, double byte_length);
  void (*write)(void *ctx, qa_guest_address address, const uint8_t *bytes, size_t byte_length);
  int64_t (*find_zero)(void *ctx, qa_guest_address address, double maximum); /* may be NULL */
  uint64_t (*observe_writes)(void *ctx, qa_guest_address address, double byte_length,
                             void *cb_ctx, qa_compat_q2_write_observer_fn cb); /* may be NULL */
  void (*unobserve_writes)(void *ctx, uint64_t handle);
  /* TS allocate({ ..., pooled: true }): a 4096-byte page from the shared slab pool; NULL = unpooled allocate. */
  qa_guest_address (*allocate_pooled)(void *ctx, double byte_length, int64_t alignment, const char *label);
} qa_compat_q2_memory;

qa_guest_address qa_compat_q2_mem_offset(const qa_compat_q2_memory *m, qa_guest_address a, int64_t d);
qa_guest_address_result qa_compat_q2_mem_pointer(const qa_compat_q2_memory *m, int64_t raw);
uint8_t qa_compat_q2_mem_read_u8(const qa_compat_q2_memory *m, qa_guest_address a);
int16_t qa_compat_q2_mem_read_i16(const qa_compat_q2_memory *m, qa_guest_address a);
uint16_t qa_compat_q2_mem_read_u16(const qa_compat_q2_memory *m, qa_guest_address a);
int32_t qa_compat_q2_mem_read_i32(const qa_compat_q2_memory *m, qa_guest_address a);
uint32_t qa_compat_q2_mem_read_u32(const qa_compat_q2_memory *m, qa_guest_address a);
int64_t qa_compat_q2_mem_read_i64(const qa_compat_q2_memory *m, qa_guest_address a);
uint64_t qa_compat_q2_mem_read_u64(const qa_compat_q2_memory *m, qa_guest_address a);
double qa_compat_q2_mem_read_f32(const qa_compat_q2_memory *m, qa_guest_address a);
qa_vec3 qa_compat_q2_mem_read_f32_vector(const qa_compat_q2_memory *m, qa_guest_address a);
/* False is the TS null pointer. */
bool qa_compat_q2_mem_read_pointer(const qa_compat_q2_memory *m, qa_guest_address a, qa_guest_address *out);
void qa_compat_q2_mem_write_u8(const qa_compat_q2_memory *m, qa_guest_address a, uint32_t v);
void qa_compat_q2_mem_write_i16(const qa_compat_q2_memory *m, qa_guest_address a, int32_t v);
void qa_compat_q2_mem_write_u16(const qa_compat_q2_memory *m, qa_guest_address a, uint32_t v);
void qa_compat_q2_mem_write_i32(const qa_compat_q2_memory *m, qa_guest_address a, int32_t v);
void qa_compat_q2_mem_write_u32(const qa_compat_q2_memory *m, qa_guest_address a, uint32_t v);
void qa_compat_q2_mem_write_i64(const qa_compat_q2_memory *m, qa_guest_address a, int64_t v);
void qa_compat_q2_mem_write_u64(const qa_compat_q2_memory *m, qa_guest_address a, uint64_t v);
void qa_compat_q2_mem_write_f32(const qa_compat_q2_memory *m, qa_guest_address a, double v);
void qa_compat_q2_mem_write_pointer(const qa_compat_q2_memory *m, qa_guest_address a, bool has, qa_guest_address v);
/* guest/runtime/common/memory.ts allocateNativeMemory/nativeAllocationBytes. */
double qa_compat_q2_native_allocation_bytes(double logical_bytes);
qa_guest_address qa_compat_q2_allocate_native_memory(const qa_compat_q2_memory *m, double logical_bytes, const char *label);
/* allocateNativeMemory(memory, bytes, label, true): engine-owned scratch whose single page comes from the slab pool. */
qa_guest_address qa_compat_q2_allocate_pooled_native_memory(const qa_compat_q2_memory *m, double logical_bytes, const char *label);
/* Little-endian DataView helpers over a borrowed host view. */
int16_t qa_compat_q2_view_i16(const uint8_t *v, size_t off);
uint16_t qa_compat_q2_view_u16(const uint8_t *v, size_t off);
int32_t qa_compat_q2_view_i32(const uint8_t *v, size_t off);
uint32_t qa_compat_q2_view_u32(const uint8_t *v, size_t off);
double qa_compat_q2_view_f32(const uint8_t *v, size_t off);
void qa_compat_q2_view_set_i16(uint8_t *v, size_t off, int32_t x);
void qa_compat_q2_view_set_u16(uint8_t *v, size_t off, uint32_t x);
void qa_compat_q2_view_set_i32(uint8_t *v, size_t off, int32_t x);
void qa_compat_q2_view_set_f32(uint8_t *v, size_t off, double x);

/* --- GuestCallRunner mirror (guest/abi/runner.ts + core/callbacks) --- */
typedef qa_guest_call_result (*qa_compat_q2_host_invoke_fn)(void *ctx, const qa_guest_call_context *context,
                                                            const qa_guest_call_value *arguments, size_t count);
typedef struct qa_compat_q2_inline_continuation {
  void *ctx;
  void (*execute)(void *ctx);
  void (*skip)(void *ctx);
} qa_compat_q2_inline_continuation;
typedef struct qa_compat_q2_runner {
  void *ctx;
  qa_compat_q2_memory *memory;
  void *cpu_state; /* opaque processor state for capture/restore + simd */
  uint8_t *simd_xmm; size_t simd_xmm_len;
  qa_guest_call_result (*invoke)(void *ctx, qa_guest_address target, qa_compat_q2_guest_call_signature signature,
                                 const qa_guest_call_value *arguments, size_t count, qa_guest_call_context context,
                                 double instruction_budget);
  qa_guest_call_result (*invoke_loading)(void *ctx, qa_guest_address target, qa_compat_q2_guest_call_signature signature,
                                         const qa_guest_call_value *arguments, size_t count, qa_guest_call_context context,
                                         double instruction_budget, void (*next_frame)(void *), void *next_frame_ctx);
  const qa_guest_call_context *(*current_context)(void *ctx);
  double (*depth)(void *ctx);
  int64_t (*instructions_executed)(void *ctx);
  uint64_t (*read_rsp)(void *ctx, int width_bits);
  /* native-combat-call readNativeCombatField / X86AbiAdapter.arguments mirrors. */
  qa_guest_call_value (*read_combat_field)(void *ctx, const void *call, qa_compat_q2_guest_call_signature signature, const char *field);
  size_t (*adapter_arguments)(void *ctx, qa_compat_q2_guest_call_signature signature, qa_guest_call_value *out, size_t capacity);
  void *(*capture_processor)(void *ctx);
  void (*restore_processor)(void *ctx, void *snapshot);
  /* GuestCallbackTable. Removal handles are nonzero when bound. */
  qa_guest_address (*bind)(void *ctx, qa_callback_id id, qa_compat_q2_guest_call_signature signature,
                           void *cb_ctx, qa_compat_q2_host_invoke_fn cb);
  uint64_t (*observe_entry)(void *ctx, qa_guest_address address, void *cb_ctx, void (*cb)(void *));
  uint64_t (*bind_entry)(void *ctx, qa_guest_address address, qa_callback_id id, qa_compat_q2_guest_call_signature signature,
                         void *cb_ctx, qa_compat_q2_host_invoke_fn cb, void *accepts_ctx, bool (*accepts)(void *));
  void (*remove_entry)(void *ctx, uint64_t handle);
  uint64_t (*bind_inline_region)(void *ctx, qa_guest_address entry, qa_guest_address join, qa_native_call_abi abi,
                                 void *cb_ctx, void (*cb)(void *, qa_compat_q2_inline_continuation *));
  void (*remove_inline_region)(void *ctx, uint64_t handle);
  /* GuestCallRunner variadicLayouts option; set by the classic host. */
  void *variadic_ctx;
  size_t (*variadic_layouts)(void *ctx, qa_callback_id id, const qa_guest_call_value *fixed, size_t fixed_count,
                             qa_guest_value_layout *out, size_t capacity);
  /* cpu.state.registers.read/write by TS register name ("rsp", "rax", ...). */
  uint64_t (*read_register)(void *ctx, const char *name, int width_bits);
  void (*write_register)(void *ctx, const char *name, int width_bits, uint64_t value);
  /* bindInlineRegion with its accepts predicate. */
  uint64_t (*bind_inline_region_accepts)(void *ctx, qa_guest_address entry, qa_guest_address join, qa_native_call_abi abi,
                                         void *cb_ctx, void (*cb)(void *, qa_compat_q2_inline_continuation *),
                                         void *accepts_ctx, bool (*accepts)(void *));
  /* Releases a capture_processor snapshot; may be NULL. */
  void (*free_processor)(void *ctx, void *snapshot);
  /* The runner's try/finally for calls abandoned by a host-side unwind
   * (qa/compat/q2/native_throw.h): pop active calls deeper than depth. May be NULL. */
  void (*unwind)(void *ctx, double depth);
} qa_compat_q2_runner;

/* TS guest/runtime/common/memory.ts argument/integer/pointer helpers,
 * contracts-world spellings. */
const qa_guest_call_value *qa_compat_q2_guest_argument(const qa_guest_call_value *args, size_t count, size_t index);
int64_t qa_compat_q2_guest_integer(const qa_guest_call_value *args, size_t count, size_t index);
bool qa_compat_q2_guest_pointer(const qa_guest_call_value *args, size_t count, size_t index, qa_guest_address *out);
qa_guest_address qa_compat_q2_guest_required_pointer(const qa_guest_call_value *args, size_t count, size_t index);
qa_guest_call_value qa_compat_q2_call_pointer(bool has, qa_guest_address value);
qa_guest_call_value qa_compat_q2_call_int32(double value);
qa_guest_call_value qa_compat_q2_call_uint32(double value);
qa_guest_call_value qa_compat_q2_call_void(void);

/* --- layout.ts proper --- */
extern const qa_native_call_abi qa_compat_q2_classic_abi;
#define QA_COMPAT_Q2_CLASSIC_FRAME_SECONDS 0.1
#define QA_COMPAT_Q2_CLASSIC_EDICT_BYTES 260
#define QA_COMPAT_Q2_CLASSIC_PLAYER_STATE_BYTES 184
#define QA_COMPAT_Q2_CLASSIC_CLIENT_PREFIX_BYTES 188
#define QA_COMPAT_Q2_CLASSIC_USERCMD_BYTES 16
#define QA_COMPAT_Q2_CLASSIC_PMOVE_BYTES 240
#define QA_COMPAT_Q2_CLASSIC_EXPORT_BYTES 80
#define QA_COMPAT_Q2_CLASSIC_IMPORT_BYTES 176
extern const qa_guest_layout qa_compat_q2_classic_usercmd_layout;
extern const qa_guest_layout qa_compat_q2_classic_player_state_layout;
extern const qa_guest_layout qa_compat_q2_classic_edict_layout;
extern const qa_guest_layout qa_compat_q2_classic_trace_layout;
extern const qa_guest_value_layout qa_compat_q2_q2_int;
extern const qa_guest_value_layout qa_compat_q2_q2_pointer;
extern const qa_guest_value_layout qa_compat_q2_q2_float;
extern const qa_guest_value_layout qa_compat_q2_q2_double;
extern const qa_guest_value_layout qa_compat_q2_q2_trace;
/* TS classicSignature(parameters, result = "void", variadic = false). */
qa_compat_q2_guest_call_signature qa_compat_q2_classic_signature(const qa_guest_value_layout *parameters, size_t count,
                                                                 bool result_is_void, qa_guest_value_layout result, bool variadic);
typedef struct qa_compat_q2_classic_import { const char *name; qa_compat_q2_guest_call_signature signature; } qa_compat_q2_classic_import;
extern const qa_compat_q2_classic_import qa_compat_q2_classic_imports[];
extern const size_t qa_compat_q2_classic_import_count;
typedef struct qa_compat_q2_classic_export { const char *name; double offset; qa_compat_q2_guest_call_signature signature; } qa_compat_q2_classic_export;
extern const qa_compat_q2_classic_export qa_compat_q2_classic_exports[];
extern const size_t qa_compat_q2_classic_export_count;
/* NULL when the name is not a TS CLASSIC_Q2_EXPORTS key. */
const qa_compat_q2_classic_export *qa_compat_q2_classic_find_export(const char *name);
#endif
