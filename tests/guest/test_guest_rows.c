/* Rows read against src/guest/core/memory.ts and src/guest/x64/cpu.ts:
 * #singleMapping/#remember locality must not change any access result, and
 * #runManaged must match #runGeneric. Reproduces tests/guest/core/memory.test.ts
 * and tests/guest/x64/cpu.test.ts cases on those paths. GPL-2.0-or-later. */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/guest/core/callbacks.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/x64/cpu.h"

static int failures, checks;
#define CHECK(c)                                                 \
  do {                                                           \
    checks++;                                                    \
    if (!(c)) {                                                  \
      failures++;                                                \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c);    \
    }                                                            \
  } while (0)

static qa_guest_core_address A(uint64_t offset) {
  qa_guest_core_address address = {offset};
  return address;
}
static void mem_new(qa_sparse_guest_memory *mem, bool managed) {
  qa_sparse_guest_memory_options opts;
  memset(&opts, 0, sizeof opts);
  opts.module.id = "test:rows";
  opts.module.digest = "digest";
  opts.pointer_bytes = 8;
  if (managed) qa_sparse_guest_memory_init_managed(mem, &opts);
  else qa_sparse_guest_memory_init(mem, &opts);
}
static void map(qa_sparse_guest_memory *mem, uint64_t base, size_t length, qa_guest_permissions permissions,
                const uint8_t *bytes, size_t bytes_length) {
  qa_guest_map_options o;
  memset(&o, 0, sizeof o);
  o.base = base;
  o.byte_length = length;
  o.permissions = permissions;
  o.bytes = bytes;
  o.bytes_length = bytes_length;
  (void)qa_sparse_map(mem, &o);
}

/* Runs one access under a throw channel; returns the kind caught, or -1. */
typedef struct access_result {
  bool thrown;
  qa_guest_memory_fault_reason reason;
  uint64_t address;
  size_t byte_length;
  uint32_t value;
} access_result;
static access_result read32(qa_sparse_guest_memory *mem, uint64_t offset) {
  access_result out;
  memset(&out, 0, sizeof out);
  jmp_buf buf;
  qa_guest_throw tc;
  qa_guest_throw_init(&tc, &buf);
  mem->throw_ctx = &tc;
  if (setjmp(buf) == 0) {
    out.value = qa_sparse_read_u32(mem, A(offset));
  } else {
    out.thrown = true;
    out.reason = tc.memory.reason;
    out.address = tc.memory.address;
    out.byte_length = tc.memory.byte_length;
  }
  mem->throw_ctx = NULL;
  return out;
}

typedef struct observed {
  unsigned sentinel;
  int hits;
  int64_t offset, length;
} observed;
static void observe(void *ctx, const qa_guest_written_range *ranges, size_t count) {
  observed *o = ctx;
  CHECK(o->sentinel == 0xabcd);
  CHECK(count == 1);
  o->hits++;
  o->offset = ranges[0].byte_offset;
  o->length = ranges[0].byte_length;
}

static void test_memory_locality(void) {
  qa_sparse_guest_memory mem;
  mem_new(&mem, false);
  map(&mem, 0x10000, 0x1000, QA_PERM_READ_WRITE, NULL, 0);
  map(&mem, 0x11000, 0x1000, QA_PERM_READ_WRITE, NULL, 0);

  /* Recent mapping serves a contained read; a straddling one takes chunks. */
  qa_sparse_write_u32(&mem, A(0x10ffe), 0x44332211);
  CHECK(read32(&mem, 0x10ffe).value == 0x44332211);
  CHECK(qa_sparse_read_u16(&mem, A(0x10ffe)) == 0x2211);
  CHECK(qa_sparse_read_u16(&mem, A(0x11000)) == 0x4433);

  /* Protecting retires the remembered mapping: its permission no longer holds. */
  CHECK(!read32(&mem, 0x10100).thrown);
  qa_sparse_protect(&mem, A(0x10000), 0x1000, QA_PERM_EXECUTE);
  access_result denied = read32(&mem, 0x10100);
  CHECK(denied.thrown && denied.reason == QA_FAULT_PERMISSION && denied.address == 0x10100 && denied.byte_length == 4);

  /* Unmapping leaves no stale hit; remapping serves the new bytes. */
  CHECK(!read32(&mem, 0x11100).thrown);
  qa_sparse_unmap(&mem, A(0x11000), 0x1000);
  access_result gone = read32(&mem, 0x11100);
  CHECK(gone.thrown && gone.reason == QA_FAULT_UNMAPPED);
  uint8_t fresh[4] = {1, 2, 3, 4};
  map(&mem, 0x11000, 0x1000, QA_PERM_READ_WRITE, fresh, 4);
  CHECK(read32(&mem, 0x11000).value == 0x04030201);

  /* More live mappings than working-set slots: every read stays correct. */
  for (uint64_t i = 0; i < 2 * QA_SPARSE_WORKING_SET + 3; i++) {
    uint64_t base = 0x40000 + i * 0x1000;
    map(&mem, base, 0x1000, QA_PERM_READ_WRITE, NULL, 0);
    qa_sparse_write_u32(&mem, A(base + 8), (uint32_t)(i + 1));
  }
  for (int pass = 0; pass < 3; pass++)
    for (uint64_t i = 0; i < 2 * QA_SPARSE_WORKING_SET + 3; i++)
      CHECK(read32(&mem, 0x40000 + i * 0x1000 + 8).value == (uint32_t)(i + 1));

  /* A straddling write after a remembered single-mapping write still reaches
   * both pieces; an empty range checks its address and maps nothing. */
  qa_sparse_write_u32(&mem, A(0x40ffe), 0xa1b2c3d4);
  CHECK(qa_sparse_read_u16(&mem, A(0x40ffe)) == 0xc3d4 && qa_sparse_read_u16(&mem, A(0x41000)) == 0xa1b2);
  qa_sparse_check(&mem, A(0x90000), 0, QA_ACCESS_READ);
  access_result beyond = read32(&mem, 0x90000);
  CHECK(beyond.thrown && beyond.reason == QA_FAULT_UNMAPPED && beyond.address == 0x90000 && beyond.byte_length == 4);

  /* The single-mapping write path still notifies observers with their ctx. */
  observed seen = {0xabcd, 0, 0, 0};
  qa_guest_write_observer_handle handle = qa_sparse_observe_writes(&mem, A(0x42000), 16, observe, &seen);
  qa_sparse_write_u16(&mem, A(0x42004), 7);
  CHECK(seen.hits == 1 && seen.offset == 4 && seen.length == 2);
  qa_sparse_unobserve_writes(&mem, handle);
  qa_sparse_guest_memory_destroy(&mem);

  /* fetchByte's remembered execute mapping keeps its permission check. */
  qa_sparse_guest_memory code;
  mem_new(&code, false);
  const uint8_t ret = 0xc3;
  map(&code, 0x20000, 0x1000, QA_PERM_READ_EXECUTE, &ret, 1);
  CHECK(qa_sparse_fetch_byte(&code, 0x20000) == 0xc3);
  qa_sparse_protect(&code, A(0x20000), 0x1000, QA_PERM_READ);
  jmp_buf buf;
  qa_guest_throw tc;
  qa_guest_throw_init(&tc, &buf);
  code.throw_ctx = &tc;
  bool thrown = false;
  if (setjmp(buf) == 0) (void)qa_sparse_fetch_byte(&code, 0x20000);
  else thrown = tc.memory.reason == QA_FAULT_PERMISSION;
  code.throw_ctx = NULL;
  CHECK(thrown);
  qa_sparse_guest_memory_destroy(&code);
}

/* ---- managed versus generic ---- */

#define CODE_BASE 0x10000u
#define STACK_TOP 0x30000u
#define DATA_BASE 0x50000u
#define RETURN_ADDRESS 0x40000u

typedef struct machine {
  qa_sparse_guest_memory mem;
  qa_guest_processor_state state;
  qa_x64_cpu cpu;
} machine;

static void machine_init(machine *m, bool managed, const uint8_t *code, size_t length, qa_guest_permissions code_perm) {
  mem_new(&m->mem, managed);
  map(&m->mem, CODE_BASE, length < 4096 ? 4096 : length, code_perm, code, length);
  map(&m->mem, STACK_TOP - 0x1000, 0x1008, QA_PERM_READ_WRITE, NULL, 0);
  map(&m->mem, DATA_BASE, 0x1000, QA_PERM_READ_WRITE, NULL, 0);
  qa_sparse_write_u64(&m->mem, A(STACK_TOP), RETURN_ADDRESS);
  qa_guest_processor_initial_state init = {QA_GUEST_X86_64, CODE_BASE, STACK_TOP, 2, 0x37f, 0x1f80, 0xffff};
  if (managed) qa_create_managed_guest_processor_state(&m->state, &init);
  else qa_create_guest_processor_state(&m->state, &init);
  qa_x64_cpu_init(&m->cpu, &m->state, &m->mem, NULL, NULL, NULL);
  qa_integer_register_file_write(&m->state.registers, QA_REG_RDI, 64, DATA_BASE, false);
}
static void machine_destroy(machine *m) {
  qa_x64_cpu_destroy(&m->cpu);
  qa_sparse_guest_memory_destroy(&m->mem);
}

static void same_machine(machine *a, machine *b, const qa_guest_execution_stop *sa,
                         const qa_guest_execution_stop *sb, const char *label) {
  bool same = sa->kind == sb->kind && sa->instructions == sb->instructions &&
              a->state.instruction_pointer == b->state.instruction_pointer &&
              memcmp(a->state.registers.bytes, b->state.registers.bytes, 128) == 0 &&
              qa_processor_flags_value(&a->state.flags) == qa_processor_flags_value(&b->state.flags);
  if (same && sa->kind == QA_STOP_EXCEPTION) {
    same = sa->exception.kind == sb->exception.kind &&
           (sa->exception.kind == QA_EXCEPTION_MEMORY ? sa->exception.address.byte_offset == sb->exception.address.byte_offset
                                                      : sa->exception.vector == sb->exception.vector);
  }
  if (!same) {
    fprintf(stderr, "%s: kinds %d/%d instructions %lld/%lld ip %llx/%llx\n", label, (int)sa->kind, (int)sb->kind,
            (long long)sa->instructions, (long long)sb->instructions,
            (unsigned long long)a->state.instruction_pointer, (unsigned long long)b->state.instruction_pointer);
  }
  CHECK(same);
}

/* Same program, stepped over every budget from 1 to `steps`, then run through
 * twice on one CPU so the second pass uses retained plans and blocks. */
static void equivalent(const char *label, const uint8_t *code, size_t length, qa_guest_permissions permissions,
                       int64_t steps) {
  for (int64_t budget = 1; budget <= steps; budget++) {
    machine a, b;
    machine_init(&a, false, code, length, permissions);
    machine_init(&b, true, code, length, permissions);
    int64_t remaining = steps;
    while (remaining > 0) {
      int64_t slice = budget < remaining ? budget : remaining;
      qa_guest_execution_stop sa = qa_x64_cpu_run(&a.cpu, slice, true, A(RETURN_ADDRESS));
      qa_guest_execution_stop sb = qa_x64_cpu_run(&b.cpu, slice, true, A(RETURN_ADDRESS));
      same_machine(&a, &b, &sa, &sb, label);
      if (sa.kind != QA_STOP_BUDGET || sb.kind != QA_STOP_BUDGET) break;
      remaining -= slice;
    }
    machine_destroy(&a);
    machine_destroy(&b);
  }
  machine a, b;
  machine_init(&a, false, code, length, permissions);
  machine_init(&b, true, code, length, permissions);
  for (int pass = 0; pass < 3; pass++) {
    qa_guest_execution_stop sa = qa_x64_cpu_run(&a.cpu, 4 * steps, true, A(RETURN_ADDRESS));
    qa_guest_execution_stop sb = qa_x64_cpu_run(&b.cpu, 4 * steps, true, A(RETURN_ADDRESS));
    same_machine(&a, &b, &sa, &sb, label);
    /* Rerun from the start: both CPUs now hold retained code. */
    a.state.instruction_pointer = CODE_BASE;
    b.state.instruction_pointer = CODE_BASE;
    qa_integer_register_file_write(&a.state.registers, QA_REG_RSP, 64, STACK_TOP, false);
    qa_integer_register_file_write(&b.state.registers, QA_REG_RSP, 64, STACK_TOP, false);
  }
  CHECK(memcmp(qa_sparse_borrow(&a.mem, A(DATA_BASE), 64), qa_sparse_borrow(&b.mem, A(DATA_BASE), 64), 64) == 0);
  machine_destroy(&a);
  machine_destroy(&b);
}

static void test_managed_equivalence(void) {
  /* mov ecx,5; L: add rax,rcx; mov [rdi],eax; sub ecx,1; jnz L; ret */
  static const uint8_t loop[] = {0xb9, 5, 0, 0, 0, 0x48, 0x01, 0xc8, 0x89, 0x07, 0x83, 0xe9, 0x01, 0x75, 0xf6, 0xc3};
  equivalent("loop", loop, sizeof loop, QA_PERM_READ_EXECUTE, 24);

  /* Fault on the third step: rollback and evidence must match. */
  static const uint8_t faulting[] = {0xb8, 7, 0, 0, 0, 0x89, 0x07, 0x8b, 0x04, 0x25, 0, 0, 9, 0, 0xc3};
  equivalent("memory fault", faulting, sizeof faulting, QA_PERM_READ_EXECUTE, 8);

  /* LOCK on a register operand faults through the kernel once cached. */
  static const uint8_t locked[] = {0xf0, 0x01, 0xc8, 0xc3};
  equivalent("lock register", locked, sizeof locked, QA_PERM_READ_EXECUTE, 4);

  /* SSE load from an unmapped address runs through a passthrough plan. */
  static const uint8_t sse_fault[] = {0xf3, 0x0f, 0x10, 0x04, 0x25, 0, 0, 9, 0, 0xc3};
  equivalent("sse fault", sse_fault, sizeof sse_fault, QA_PERM_READ_EXECUTE, 4);

  /* Shift and multiply passthrough steps inside a block. */
  static const uint8_t passthrough[] = {0xb8, 3, 0, 0, 0, 0xc1, 0xe0, 0x04, 0x0f, 0xaf, 0xc0, 0x89, 0x07, 0xc3};
  equivalent("passthrough", passthrough, sizeof passthrough, QA_PERM_READ_EXECUTE, 8);

  /* Call and return leave and re-enter blocks; stack stores are observed. */
  static const uint8_t calls[] = {0xe8, 2, 0, 0, 0, 0xeb, 2, 0xff, 0xc0, 0xc3, 0xc3};
  equivalent("call", calls, sizeof calls, QA_PERM_READ_EXECUTE, 8);

  /* Self-modifying store into the same block must change later execution:
   * mov byte [rip+1],0x90 replaces the int3 that follows with a nop. */
  static const uint8_t smc[] = {0xc6, 0x05, 0x00, 0x00, 0x00, 0x00, 0x90, 0xcc, 0xb8, 9, 0, 0, 0, 0xc3};
  equivalent("self modifying", smc, sizeof smc, QA_PERM_READ_WRITE_EXECUTE, 6);
  machine m;
  machine_init(&m, true, smc, sizeof smc, QA_PERM_READ_WRITE_EXECUTE);
  /* Prime the retained plans with the int3 in place, then let it be patched. */
  qa_guest_execution_stop first = qa_x64_cpu_run(&m.cpu, 8, true, A(RETURN_ADDRESS));
  CHECK(first.kind == QA_STOP_RETURN);
  CHECK(qa_integer_register_file_read(&m.state.registers, QA_REG_RAX, 64, false) == 9);
  uint8_t restore = 0xcc;
  qa_sparse_write(&m.mem, A(CODE_BASE + 7), &restore, 1);
  m.state.instruction_pointer = CODE_BASE;
  qa_integer_register_file_write(&m.state.registers, QA_REG_RSP, 64, STACK_TOP, false);
  qa_integer_register_file_write(&m.state.registers, QA_REG_RAX, 64, 0, false);
  CHECK(qa_x64_cpu_run(&m.cpu, 8, true, A(RETURN_ADDRESS)).kind == QA_STOP_RETURN);
  CHECK(qa_integer_register_file_read(&m.state.registers, QA_REG_RAX, 64, false) == 9);
  machine_destroy(&m);

  /* Budget zero and return-at-entry short circuits. */
  machine z;
  machine_init(&z, true, loop, sizeof loop, QA_PERM_READ_EXECUTE);
  CHECK(qa_x64_cpu_run(&z.cpu, 0, true, A(RETURN_ADDRESS)).kind == QA_STOP_BUDGET);
  qa_guest_execution_stop at_return = qa_x64_cpu_run(&z.cpu, 5, true, A(CODE_BASE));
  CHECK(at_return.kind == QA_STOP_RETURN && at_return.instructions == 0);
  machine_destroy(&z);
}

/* Cache limit: past 32768 entries the table clears mid-run, while a block and a
 * managed block may still name retired instructions. */
static void test_cache_limit(void) {
  size_t length = 40000;
  uint8_t *code = malloc(length);
  memset(code, 0x90, length);
  code[length - 1] = 0xc3;
  for (int managed = 0; managed < 2; managed++) {
    machine m;
    machine_init(&m, managed != 0, code, length, QA_PERM_READ_EXECUTE);
    for (int pass = 0; pass < 2; pass++) {
      m.state.instruction_pointer = CODE_BASE;
      qa_integer_register_file_write(&m.state.registers, QA_REG_RSP, 64, STACK_TOP, false);
      qa_guest_execution_stop stop = qa_x64_cpu_run(&m.cpu, (int64_t)length + 1, true, A(RETURN_ADDRESS));
      CHECK(stop.kind == QA_STOP_RETURN && stop.instructions == (int64_t)length);
    }
    machine_destroy(&m);
  }
  free(code);
}

/* ---- host entries ---- */

typedef struct host_ctx {
  unsigned sentinel;
  int calls;
} host_ctx;
static bool host_at(void *ctx, qa_guest_core_address address) {
  host_ctx *h = ctx;
  CHECK(h->sentinel == 0x51ea7);
  h->calls++;
  return address.byte_offset == CODE_BASE + 5;
}

static void count_entry(void *ctx) {
  host_ctx *h = ctx;
  CHECK(h->sentinel == 0x51ea7);
  h->calls++;
}

static void test_entry_checks(void) {
  /* An isHostCall hook forbids blocks and is consulted before every entry,
   * with the context its author gave, on cached passes too. */
  static const uint8_t nops[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xc3};
  machine m;
  machine_init(&m, false, nops, sizeof nops, QA_PERM_READ_EXECUTE);
  host_ctx h = {0x51ea7, 0};
  qa_x64_cpu_destroy(&m.cpu);
  qa_x64_cpu_init(&m.cpu, &m.state, &m.mem, host_at, &h, NULL);
  for (int pass = 0; pass < 2; pass++) {
    m.state.instruction_pointer = CODE_BASE;
    h.calls = 0;
    qa_guest_execution_stop stop = qa_x64_cpu_run(&m.cpu, 20, true, A(RETURN_ADDRESS));
    CHECK(stop.kind == QA_STOP_HOST_CALL && stop.instructions == 5);
    CHECK(stop.address.byte_offset == CODE_BASE + 5);
    CHECK(h.calls == 6);
  }
  machine_destroy(&m);

  /* A hook bound after blocks formed must stop the retained block. */
  machine c;
  machine_init(&c, true, nops, sizeof nops, QA_PERM_READ_EXECUTE);
  qa_guest_callback_table table;
  qa_guest_callback_table_init_managed(&table, &c.mem);
  qa_x64_cpu_destroy(&c.cpu);
  qa_x64_cpu_init(&c.cpu, &c.state, &c.mem, NULL, NULL, &table);
  for (int pass = 0; pass < 2; pass++) {
    c.state.instruction_pointer = CODE_BASE;
    qa_integer_register_file_write(&c.state.registers, QA_REG_RSP, 64, STACK_TOP, false);
    CHECK(qa_x64_cpu_run(&c.cpu, 20, true, A(RETURN_ADDRESS)).kind == QA_STOP_RETURN);
  }
  /* An entry observer added after blocks formed runs, with its own ctx, when
   * the retained block passes its address; removing it restores the blocks. */
  host_ctx observed_entry = {0x51ea7, 0};
  qa_guest_entry_handle watch =
      qa_guest_callback_table_observe_entry(&table, A(CODE_BASE + 3), count_entry, &observed_entry);
  CHECK(watch != 0);
  c.state.instruction_pointer = CODE_BASE;
  qa_integer_register_file_write(&c.state.registers, QA_REG_RSP, 64, STACK_TOP, false);
  CHECK(qa_x64_cpu_run(&c.cpu, 20, true, A(RETURN_ADDRESS)).kind == QA_STOP_RETURN);
  CHECK(observed_entry.calls == 1);
  qa_guest_callback_table_unobserve_entry(&table, watch);
  c.state.instruction_pointer = CODE_BASE;
  qa_integer_register_file_write(&c.state.registers, QA_REG_RSP, 64, STACK_TOP, false);
  CHECK(qa_x64_cpu_run(&c.cpu, 20, true, A(RETURN_ADDRESS)).kind == QA_STOP_RETURN);
  CHECK(observed_entry.calls == 1);
  qa_x64_cpu_destroy(&c.cpu);
  qa_guest_callback_table_destroy(&table);
  qa_sparse_guest_memory_destroy(&c.mem);
}

int main(void) {
  test_memory_locality();
  test_managed_equivalence();
  test_cache_limit();
  test_entry_checks();
  printf("test_guest_rows: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
