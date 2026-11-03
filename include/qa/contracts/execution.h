/*
 * Port of src/contracts/execution.ts (types only): game API identities,
 * guest memory/callback shapes, and checkpoint records.
 * C accommodations: TS `bigint` -> int64_t/uint64_t, `symbol` address
 * spaces -> opaque const void *, DataView/Uint8Array -> byte views,
 * method-bearing interfaces -> function table + ctx. The ModuleIdentity
 * digest field is qa_content_digest (const char *) from content.h;
 * spelled plainly here to avoid the content<->execution include cycle.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_EXECUTION_H
#define QA_CONTRACTS_EXECUTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"

/* API identity unions. TS literal fields are noted per enumerator. */
typedef enum qa_quakec_api_kind {
  QA_QUAKEC_API_Q1_NETQUAKE,   /* "q1-netquake", programVersion 6, systemCrc 5927 */
  QA_QUAKEC_API_Q1_QUAKEWORLD  /* "q1-quakeworld", programVersion 6, systemCrc 54730 */
} qa_quakec_api_kind;
typedef struct qa_quakec_api_identity {
  qa_quakec_api_kind kind;
  int64_t program_version;
  int64_t system_crc;
} qa_quakec_api_identity;

typedef enum qa_q2_game_api_kind {
  QA_Q2_GAME_API_CLASSIC,   /* "q2-classic-game", version 3 */
  QA_Q2_GAME_API_RERELEASE  /* "q2-rerelease-game", version 2023 */
} qa_q2_game_api_kind;
typedef struct qa_q2_game_api_identity {
  qa_q2_game_api_kind kind;
  int64_t version;
} qa_q2_game_api_identity;

/* TS Q2CgameApiIdentity: kind "q2-rerelease-cgame", version 2022. */
typedef struct qa_q2_cgame_api_identity { int64_t version; } qa_q2_cgame_api_identity;

typedef enum qa_q3_api_kind {
  QA_Q3_API_QAGAME,  /* "q3-qagame", version 7 | 8 */
  QA_Q3_API_CGAME,   /* "q3-cgame", version 3 | 4 */
  QA_Q3_API_UI       /* "q3-ui", version 4 | 6 */
} qa_q3_api_kind;
typedef struct qa_q3_api_identity {
  qa_q3_api_kind kind;
  int64_t version;
} qa_q3_api_identity;

/* TS GameApiIdentity = QuakeC | Q2Game | Q2Cgame | Q3. */
typedef enum qa_game_api_identity_kind {
  QA_GAME_API_QUAKEC, QA_GAME_API_Q2_GAME, QA_GAME_API_Q2_CGAME, QA_GAME_API_Q3
} qa_game_api_identity_kind;
typedef struct qa_game_api_identity {
  qa_game_api_identity_kind kind;
  union {
    qa_quakec_api_identity quakec;
    qa_q2_game_api_identity q2_game;
    qa_q2_cgame_api_identity q2_cgame;
    qa_q3_api_identity q3;
  } as;
} qa_game_api_identity;

typedef struct qa_module_identity {
  qa_provider_id id;
  const char *artifact_path;
  const char *digest; /* qa_content_digest */
  const char *revision;
} qa_module_identity;

/* TS NativeAbi union. image: "pe32"|"pe32+"|"elf32"|"elf64";
 * call: "cdecl"|"microsoft-x64"|"system-v-i386"|"system-v-x86-64",
 * plus NativeCallAbi's extra windows-i386 calls "stdcall"|"thiscall"|
 * "fastcall". Strings kept verbatim because sameWeaponBehavior
 * compares kind/call/image/pointerBytes as TS strings/numbers. */
typedef struct qa_native_abi {
  const char *kind;  /* "windows-i386"|"windows-x86-64"|"linux-i386"|"linux-x86-64" */
  const char *image; /* "pe32"|"pe32+"|"elf32"|"elf64" */
  int64_t pointer_bytes; /* 4 | 8 */
  const char *call;
} qa_native_abi;
typedef qa_native_abi qa_native_call_abi;

typedef struct qa_guest_layout qa_guest_layout; /* below, self via fields */

typedef struct qa_quakec_builtin_binding {
  const char *kind; /* "numbered" | "named" */
  double number;
  const char *name;
  qa_callback_id callback;
} qa_quakec_builtin_binding;

typedef struct qa_quakec_host_profile {
  qa_quakec_api_identity api;
  qa_string_view_list program_search_order;
  const qa_guest_layout *globals_layout;
  const qa_guest_layout *entity_variables_layout;
  const qa_quakec_builtin_binding *builtins;
  size_t builtin_count;
  qa_string_view_list extensions;
} qa_quakec_host_profile;

/* TS ExecutionProfile union on kind: "typescript"|"quakec"|"qvm"
 * (magic 0x12721444) | "native-guest". */
typedef enum qa_execution_profile_kind {
  QA_EXECUTION_TYPESCRIPT, QA_EXECUTION_QUAKEC, QA_EXECUTION_QVM, QA_EXECUTION_NATIVE_GUEST
} qa_execution_profile_kind;
typedef struct qa_execution_profile {
  qa_execution_profile_kind kind;
  qa_module_identity module;
  qa_game_api_identity api;      /* typescript/qvm/native-guest */
  qa_quakec_host_profile host;   /* quakec */
  qa_provider_id implementation; /* typescript */
  int64_t magic;                 /* qvm: 0x12721444 */
  qa_native_abi abi;             /* native-guest */
  qa_numeric_profile numeric;
} qa_execution_profile;

typedef struct qa_guest_address {
  /* TS kind is the literal "guest-address". */
  const void *address_space; /* TS symbol */
  int64_t byte_offset;       /* TS bigint */
} qa_guest_address;

/* TS GuestStorage string union, kept as exact strings at layout sites:
 * "int8"|"uint8"|"int16"|"uint16"|"int32"|"uint32"|"int64"|"uint64"|
 * "float32"|"float64"|"pointer". */
typedef const char *qa_guest_storage;

typedef struct qa_guest_field_layout {
  const char *name;
  double byte_offset;
  qa_guest_storage storage;
  double count;
} qa_guest_field_layout;

struct qa_guest_layout {
  const char *id; /* `${string}:${string}` */
  double byte_length;
  double alignment;
  int64_t pointer_bytes; /* 4 | 8; byteOrder is the literal "little-endian" */
  const qa_guest_field_layout *fields;
  size_t field_count;
};

/** TS GuestMemory interface: function table + ctx. borrow/copy return
 * byte views (TS DataView / Uint8Array). pointer() yields has_value
 * false for the source null representation. */
typedef struct qa_guest_address_result {
  bool has_value;
  qa_guest_address value;
} qa_guest_address_result;
typedef struct qa_guest_memory {
  void *ctx;
  qa_module_identity module;
  const void *address_space;
  int64_t pointer_bytes;
  qa_guest_address_result (*pointer)(void *ctx, int64_t raw_value);
  qa_guest_address (*offset)(void *ctx, qa_guest_address address, int64_t displacement);
  qa_byte_view (*borrow)(void *ctx, qa_guest_address address, double byte_length);
  qa_byte_view (*copy)(void *ctx, qa_guest_address address, double byte_length);
  void (*write)(void *ctx, qa_guest_address address, qa_byte_view bytes);
} qa_guest_memory;

typedef struct qa_actor_id_result { bool has_value; qa_actor_id value; } qa_actor_id_result;

typedef struct qa_raw_entity_view {
  void *ctx;
  qa_module_identity module;
  double slot;
  qa_guest_address address;
  double stride_bytes;
  qa_guest_layout public_layout;
  qa_byte_view bytes;
  qa_actor_id_result (*current_actor)(void *ctx);
} qa_raw_entity_view;

typedef struct qa_raw_entity_table {
  void *ctx;
  qa_module_identity module;
  qa_guest_address base;
  double stride_bytes;
  double count;
  double capacity;
  qa_guest_layout layout;
  qa_raw_entity_view (*at_slot)(void *ctx, double slot);
  qa_raw_entity_view (*from_pointer)(void *ctx, qa_guest_address address);
  double (*live_count)(void *ctx); /* TS `get count()`; NULL where only `count` is kept */
} qa_raw_entity_table;

/* TS GuestCallbackReference union on kind. */
typedef enum qa_guest_callback_reference_kind {
  QA_GUEST_CALLBACK_TYPESCRIPT,   /* "typescript" */
  QA_GUEST_CALLBACK_QUAKEC,       /* "quakec" */
  QA_GUEST_CALLBACK_QVM,          /* "qvm" */
  QA_GUEST_CALLBACK_NATIVE_GUEST  /* "native-guest" */
} qa_guest_callback_reference_kind;
typedef struct qa_guest_callback_reference {
  qa_guest_callback_reference_kind kind;
  qa_provider_id provider;   /* typescript */
  qa_callback_id callback;   /* typescript */
  qa_module_identity module; /* quakec/qvm/native-guest */
  double function_index;     /* quakec */
  double instruction_index;  /* qvm */
  qa_guest_address address;  /* native-guest */
  qa_native_call_abi abi;    /* native-guest */
} qa_guest_callback_reference;

/* TS GuestCallValue union on kind; GuestCallResult adds {kind:"void"}. */
typedef enum qa_guest_call_value_kind {
  QA_GUEST_VALUE_VOID, QA_GUEST_VALUE_INT32, QA_GUEST_VALUE_UINT32,
  QA_GUEST_VALUE_INT64, QA_GUEST_VALUE_UINT64, QA_GUEST_VALUE_FLOAT32,
  QA_GUEST_VALUE_FLOAT64, QA_GUEST_VALUE_POINTER, QA_GUEST_VALUE_AGGREGATE
} qa_guest_call_value_kind;
typedef struct qa_guest_call_value {
  qa_guest_call_value_kind kind;
  double number_value;   /* int32/uint32/float32/float64 */
  int64_t int64_value;   /* TS bigint */
  uint64_t uint64_value; /* TS bigint */
  qa_guest_address_result pointer_value;
  qa_guest_layout layout; /* aggregate */
  qa_byte_view bytes;     /* aggregate */
} qa_guest_call_value;
typedef qa_guest_call_value qa_guest_call_result;

/* TS GuestValueLayout union: {kind:"scalar",storage} | {kind:"aggregate",layout}. */
typedef struct qa_guest_value_layout {
  bool is_aggregate;
  qa_guest_storage storage;
  qa_guest_layout layout;
} qa_guest_value_layout;

typedef struct qa_guest_callback_binding {
  qa_callback_id id;
  qa_guest_callback_reference reference;
  const qa_guest_value_layout *parameters;
  size_t parameter_count;
  bool result_is_void; /* TS result: GuestValueLayout | "void" */
  qa_guest_value_layout result;
} qa_guest_callback_binding;

/* TS SavedGuestCallbackReference: native-guest carries byteOffset
 * (bigint) instead of a live GuestAddress. */
typedef struct qa_saved_guest_callback_reference {
  qa_guest_callback_reference reference;
  int64_t byte_offset; /* native-guest only */
} qa_saved_guest_callback_reference;
typedef struct qa_saved_guest_callback_binding {
  qa_callback_id id;
  qa_saved_guest_callback_reference reference;
  const qa_guest_value_layout *parameters;
  size_t parameter_count;
  bool result_is_void;
  qa_guest_value_layout result;
} qa_saved_guest_callback_binding;

typedef struct qa_guest_call_context qa_guest_call_context;
struct qa_guest_call_context {
  qa_module_identity module;
  qa_guest_callback_reference callback;
  const qa_guest_call_context *parent;    /* nullable */
  const qa_raw_entity_view *self_view;    /* TS `self`, nullable */
  const qa_raw_entity_view *other;        /* nullable */
};

typedef struct qa_guest_private_state {
  qa_module_identity module;
  const char *format; /* `${string}:${string}` */
  qa_byte_view bytes;
} qa_guest_private_state;

typedef struct qa_guest_checkpoint qa_guest_checkpoint;

/** TS GuestExecutor interface: function table + ctx. */
typedef struct qa_guest_executor {
  void *ctx;
  qa_execution_profile profile;
  qa_guest_call_result (*invoke)(void *ctx, qa_guest_call_context context,
                                 const qa_guest_call_value *arguments, size_t argument_count);
  const qa_guest_checkpoint *(*checkpoint)(void *ctx);
  void (*restore)(void *ctx, const qa_guest_checkpoint *checkpoint);
} qa_guest_executor;

/* TS GuestRegionPermissions: "none"|"read"|"read-write"|"read-execute"|
 * "read-write-execute"|"execute" (exact strings). */
typedef const char *qa_guest_region_permissions;
/* TS GuestMemoryRegion. */
typedef struct qa_guest_memory_region {
  int64_t base; /* TS bigint */
  qa_guest_region_permissions permissions;
  qa_byte_view bytes;
} qa_guest_memory_region;
/* TS GuestMappingRecord. */
typedef struct qa_guest_mapping_record {
  int64_t base; /* TS bigint */
  double byte_length;
  qa_guest_region_permissions permissions;
  const char *label;
} qa_guest_mapping_record;
/* TS GuestAllocatorImage: the allocation cursor and the pooled-page
 * bookkeeping of a guest address space. */
typedef struct qa_guest_allocator_hint {
  int64_t alignment, base; /* TS bigint */
  double byte_length;
} qa_guest_allocator_hint;
typedef struct qa_guest_allocator_slab {
  int64_t base; /* TS bigint */
  qa_byte_view used;
  double next;
} qa_guest_allocator_slab;
typedef struct qa_guest_allocator_image {
  const qa_guest_allocator_hint *hints;
  size_t hint_count;
  const qa_guest_allocator_slab *slabs;
  size_t slab_count;
  const double *free_pages;
  size_t free_page_count;
} qa_guest_allocator_image;
/* TS NativeHeapBlock: a live block of the runtime's own heap table. heap is
 * the owning Windows heap handle; zero for the System V heap. */
typedef struct qa_native_heap_block {
  int64_t base; /* TS bigint */
  double size;
  int64_t heap; /* TS bigint */
} qa_native_heap_block;
/* TS WindowsHeapTables. has_* false = TS null. */
typedef struct qa_windows_reservation {
  int64_t base; /* TS bigint */
  double size;
} qa_windows_reservation;
typedef struct qa_windows_fls_slot {
  double index;
  bool has_callback;
  int64_t callback; /* TS bigint | null */
  bool has_value;
  int64_t value; /* TS bigint | null */
} qa_windows_fls_slot;
typedef struct qa_windows_heap_tables {
  const int64_t *heaps; /* TS bigint[] */
  size_t heap_count;
  const qa_windows_reservation *reservations;
  size_t reservation_count;
  const double *tls;
  size_t tls_count;
  /* Images saved before fiber-local storage was recorded read as none. */
  const qa_windows_fls_slot *fls;
  size_t fls_count;
  double last_error;
} qa_windows_heap_tables;
/* TS NativeGuestHeapImage: the part of a native process game code can change
 * after startup. Version 1 images carry none of it (TS null heap). */
typedef struct qa_native_guest_heap_image {
  const qa_guest_mapping_record *layout;
  size_t layout_count;
  qa_guest_allocator_image allocator;
  const qa_native_heap_block *blocks;
  size_t block_count;
  const qa_windows_heap_tables *windows; /* NULL = TS null */
} qa_native_guest_heap_image;

typedef struct qa_quakec_stack_frame {
  double statement;
  double function_index;
} qa_quakec_stack_frame;

/* TS QvmAbiProfile: "q3-modern" | "q3-1.16n-base" (exact strings). */
typedef const char *qa_qvm_abi_profile;

/* TS GuestCheckpoint union on kind: "quakec"|"qvm"|"native-guest"|
 * "typescript". CheckpointBase fields (module/random/callbacks) are
 * common and come first. */
typedef enum qa_guest_checkpoint_kind {
  QA_GUEST_CHECKPOINT_QUAKEC, QA_GUEST_CHECKPOINT_QVM,
  QA_GUEST_CHECKPOINT_NATIVE_GUEST, QA_GUEST_CHECKPOINT_TYPESCRIPT
} qa_guest_checkpoint_kind;
struct qa_guest_checkpoint {
  qa_guest_checkpoint_kind kind;
  qa_module_identity module;
  const qa_random_state *random;
  size_t random_count;
  const qa_saved_guest_callback_binding *callbacks;
  size_t callback_count;
  /* quakec */
  qa_quakec_api_identity quakec_api;
  qa_byte_view globals, entities, strings, locals;
  double entity_stride_bytes, entity_count, statement, function_index, argument_count;
  const qa_quakec_stack_frame *call_stack;
  size_t call_stack_count;
  /* qvm */
  bool has_abi_profile;
  qa_qvm_abi_profile abi_profile;
  qa_q3_api_identity q3_api;
  qa_byte_view data;
  double instruction_index, program_stack;
  qa_double_view operand_stack;
  /* native-guest. native_version is the TS literal 2 (saves written before
   * the heap image read as version 2 with a NULL heap). */
  double native_version;
  const qa_native_guest_heap_image *heap; /* NULL = TS null */
  qa_native_abi abi;
  const qa_guest_memory_region *regions;
  size_t region_count;
  qa_guest_layout processor_layout;
  qa_byte_view processor_state;
  /* shared private-state payloads by kind */
  qa_guest_private_state host_state;    /* quakec/qvm */
  qa_guest_private_state runtime_state; /* native-guest */
  qa_guest_private_state state;         /* typescript */
  qa_game_api_identity game_api;        /* typescript */
};

#endif
