/* Port of src/guest/core/memory.ts. GPL-2.0-or-later. */
#include "qa/guest/core/memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

uint64_t qa_wrap_guest_pointer(uint64_t value, int pointer_bytes) {
  return pointer_bytes == 4 ? value & 0xffffffffull : value;
}
int64_t qa_signed_guest_pointer(uint64_t value, int pointer_bytes) {
  if (pointer_bytes == 4) {
    uint32_t v = (uint32_t)value;
    return (v & 0x80000000u) != 0 ? (int64_t)v - 0x100000000ll : (int64_t)v;
  }
  return (int64_t)value;
}
uint64_t qa_add_guest_pointer(uint64_t value, int64_t displacement, int pointer_bytes) {
  return qa_wrap_guest_pointer(value + (uint64_t)displacement, pointer_bytes);
}

struct qa_sparse_write_observer {
  uint64_t id;
  bool removed;
  qa_guest_write_observer_fn notify;
  void *ctx;
  /* watched chunks in backing coordinates */
  struct qa_sparse_chunk_ref { qa_sparse_backing *backing; size_t offset; size_t byte_length; } *chunks;
  size_t chunk_count;
  struct qa_sparse_write_observer *next;
};

typedef struct chunk {
  qa_sparse_mapping *mapping;
  size_t offset; /* within the mapping */
  size_t byte_length;
} chunk;

static void *xmalloc(size_t n) {
  void *p = malloc(n == 0 ? 1 : n);
  if (p == NULL) qa_range_error("Guest memory allocation failed");
  return p;
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest memory allocation failed");
  return p;
}
static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n == 0 ? 1 : n);
  if (q == NULL) qa_range_error("Guest memory allocation failed");
  return q;
}
static char *dup_str(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = (char *)xmalloc(n);
  memcpy(p, s, n);
  return p;
}
static uint64_t load_le(const uint8_t *p, int n) {
  uint64_t v = 0;
  for (int i = n - 1; i >= 0; i--) v = (v << 8) | (uint64_t)p[i];
  return v;
}
static void store_le(uint8_t *p, uint64_t v, int n) {
  for (int i = 0; i < n; i++) { p[i] = (uint8_t)(v & 0xffu); v >>= 8; }
}
/* mapping end (base+len, possibly 2^64) comparisons */
static bool end_le(const qa_sparse_mapping *m, uint64_t address) {
  return !m->end_wraps && m->end <= address;
}
static bool addr_lt_end(uint64_t address, const qa_sparse_mapping *m) {
  return m->end_wraps || address < m->end;
}

_Noreturn static void fault(qa_sparse_guest_memory *mem, qa_guest_memory_fault_reason reason,
                            uint64_t address, size_t byte_length, qa_guest_fault_access access, const char *detail) {
  qa_guest_throw_memory(mem->throw_ctx, reason, &mem->module, address, byte_length, access, detail);
}
static qa_guest_fault_access fault_access_of(qa_guest_access access) {
  return access == QA_ACCESS_READ ? QA_FAULT_ACCESS_READ
         : access == QA_ACCESS_WRITE ? QA_FAULT_ACCESS_WRITE : QA_FAULT_ACCESS_EXECUTE;
}

static void range_check(qa_sparse_guest_memory *mem, uint64_t base, size_t byte_length, qa_guest_fault_access access) {
  if (byte_length > (size_t)QA_MAX_SAFE_INTEGER)
    fault(mem, QA_FAULT_INVALID_LENGTH, base, byte_length, access, "length must be a nonnegative safe integer");
  if (base == 0)
    fault(mem, QA_FAULT_NULL_ADDRESS, base, byte_length, access, "null is not a mapped address");
  bool over;
  if (mem->base.pointer_bytes == 4) {
    over = base >= 0x100000000ull || (byte_length != 0 && base + (uint64_t)byte_length > 0x100000000ull);
  } else {
    /* limit is 2^64: fault iff base + len > 2^64 (len > 0). */
    over = byte_length != 0 && base > 0ull - (uint64_t)byte_length;
  }
  if (over) {
    char detail[64];
    (void)snprintf(detail, sizeof detail, "range exceeds %d-bit address space", mem->base.pointer_bytes * 8);
    fault(mem, QA_FAULT_ADDRESS_OVERFLOW, base, byte_length, access, detail);
  }
}

static size_t first_end_after(const qa_sparse_guest_memory *mem, uint64_t address) {
  size_t low = 0, high = mem->mapping_count;
  while (low < high) {
    size_t middle = (low + high) / 2;
    if (end_le(mem->mappings[middle], address)) low = middle + 1;
    else high = middle;
  }
  return low;
}

static void available(qa_sparse_guest_memory *mem, uint64_t base, size_t byte_length) {
  range_check(mem, base, byte_length, QA_FAULT_ACCESS_MAP);
  if (byte_length == 0)
    fault(mem, QA_FAULT_INVALID_LENGTH, base, byte_length, QA_FAULT_ACCESS_MAP, "mapping must contain at least one byte");
  size_t index = first_end_after(mem, base);
  if (index < mem->mapping_count) {
    /* overlapping.base < base+len, with 2^64 end handled: range already
     * proved base+len <= limit, so the sum fits unless it is 2^64. */
    uint64_t end = base + (uint64_t)byte_length;
    bool end_is_limit = mem->base.pointer_bytes == 8 && end == 0 && byte_length != 0;
    if (end_is_limit || mem->mappings[index]->base < end)
      fault(mem, QA_FAULT_OVERLAP, base, byte_length, QA_FAULT_ACCESS_MAP, "mapping overlaps an existing guest range");
  }
}

static qa_sparse_backing *new_backing(qa_sparse_guest_memory *mem, uint8_t *bytes, size_t byte_length) {
  qa_sparse_backing *backing = (qa_sparse_backing *)xcalloc(1, sizeof *backing);
  backing->bytes = bytes;
  backing->byte_length = byte_length;
  if (mem->backing_count == mem->backing_cap) {
    mem->backing_cap = mem->backing_cap == 0 ? 8 : mem->backing_cap * 2;
    mem->backings = (qa_sparse_backing **)xrealloc(mem->backings, mem->backing_cap * sizeof *mem->backings);
  }
  mem->backings[mem->backing_count++] = backing;
  return backing;
}

static qa_sparse_mapping *new_mapping(uint64_t base, size_t byte_length, qa_guest_permissions permissions,
                                      const char *label, qa_sparse_backing *backing, size_t backing_offset) {
  qa_sparse_mapping *m = (qa_sparse_mapping *)xcalloc(1, sizeof *m);
  m->base = base;
  m->byte_length = byte_length;
  m->permissions = permissions;
  m->label = dup_str(label);
  m->backing = backing;
  m->backing_offset = backing_offset;
  m->end = base + (uint64_t)byte_length;
  m->end_wraps = byte_length != 0 && m->end == 0;
  m->active = true;
  return m;
}

static void insert_mapping(qa_sparse_guest_memory *mem, qa_sparse_mapping *mapping) {
  mem->generation += 1;
  size_t at = first_end_after(mem, mapping->base);
  if (mem->mapping_count == mem->mapping_cap) {
    mem->mapping_cap = mem->mapping_cap == 0 ? 8 : mem->mapping_cap * 2;
    mem->mappings = (qa_sparse_mapping **)xrealloc(mem->mappings, mem->mapping_cap * sizeof *mem->mappings);
  }
  memmove(mem->mappings + at + 1, mem->mappings + at, (mem->mapping_count - at) * sizeof *mem->mappings);
  mem->mappings[at] = mapping;
  mem->mapping_count++;
  mem->mapped_total += mapping->byte_length;
}

/* TS #recentMappings key: read, write, execute, or "any" for access null. */
static int recent_slot(bool has_access, qa_guest_access access) {
  if (!has_access) return 3;
  return access == QA_ACCESS_READ ? 0 : access == QA_ACCESS_WRITE ? 1 : 2;
}

/* TS #remember. Retired mappings stay alive, so stale slots only need the
 * active check their readers already make. */
static void remember(qa_sparse_guest_memory *mem, int slot, qa_sparse_mapping *mapping) {
  mem->recent_mappings[slot] = mapping;
  for (size_t i = 0; i < QA_SPARSE_WORKING_SET; i++)
    if (mem->working_set[i] == mapping) return;
  mem->working_set[mem->next_working_mapping] = mapping;
  mem->next_working_mapping = (mem->next_working_mapping + 1u) & (QA_SPARSE_WORKING_SET - 1u);
}

/* True when [address, address+byte_length) lies inside mapping. */
static bool mapping_holds(const qa_sparse_mapping *mapping, uint64_t address, size_t byte_length, size_t *offset) {
  if (address < mapping->base || !addr_lt_end(address, mapping)) return false;
  size_t at = (size_t)(address - mapping->base);
  if (byte_length > mapping->byte_length - at) return false;
  *offset = at;
  return true;
}

_Noreturn static void permission_fault(qa_sparse_guest_memory *mem, const qa_sparse_mapping *mapping, uint64_t address,
                                       size_t byte_length, qa_guest_access access) {
  char detail[512];
  (void)snprintf(detail, sizeof detail, "mapping '%s' permits %s", mapping->label,
                 qa_guest_permissions_name(mapping->permissions));
  fault(mem, QA_FAULT_PERMISSION, address, byte_length, fault_access_of(access), detail);
}

/* TS #singleMapping: the mapping wholly containing the range, or NULL when
 * the range is empty or crosses mappings (callers then take chunks_of). */
static qa_sparse_mapping *single_mapping(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length,
                                         qa_guest_access access, size_t *offset) {
  int slot = recent_slot(true, access);
  uint64_t raw = address.byte_offset;
  qa_sparse_mapping *recent = mem->recent_mappings[slot];
  if (byte_length > 0 && byte_length <= (size_t)QA_MAX_SAFE_INTEGER) {
    if (recent != NULL && recent->active && mapping_holds(recent, raw, byte_length, offset)) {
      if (!qa_guest_permissions_allows(recent->permissions, access))
        permission_fault(mem, recent, raw, byte_length, access);
      return recent;
    }
    for (size_t i = 0; i < QA_SPARSE_WORKING_SET; i++) {
      qa_sparse_mapping *candidate = mem->working_set[i];
      if (candidate == NULL || candidate == recent || !candidate->active) continue;
      if (!mapping_holds(candidate, raw, byte_length, offset)) continue;
      if (!qa_guest_permissions_allows(candidate->permissions, access))
        permission_fault(mem, candidate, raw, byte_length, access);
      mem->recent_mappings[slot] = candidate;
      return candidate;
    }
  }
  range_check(mem, raw, byte_length, fault_access_of(access));
  if (byte_length == 0) return NULL;
  size_t index = first_end_after(mem, raw);
  if (index >= mem->mapping_count) return NULL;
  qa_sparse_mapping *mapping = mem->mappings[index];
  if (!mapping_holds(mapping, raw, byte_length, offset)) return NULL;
  if (!qa_guest_permissions_allows(mapping->permissions, access))
    permission_fault(mem, mapping, raw, byte_length, access);
  remember(mem, slot, mapping);
  return mapping;
}

/* TS #chunks: full checked walk. has_access=false mirrors access null.
 * Returns a malloc'd chunk array (caller frees); count via out. */
static chunk *chunks_of(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length,
                        bool has_access, qa_guest_access access, size_t *out_count) {
  range_check(mem, address.byte_offset, byte_length,
              has_access ? fault_access_of(access) : QA_FAULT_ACCESS_MAP);
  int slot = recent_slot(has_access, access);
  qa_sparse_mapping *recent = mem->recent_mappings[slot];
  size_t recent_offset = 0;
  if (byte_length > 0 && recent != NULL && recent->active &&
      mapping_holds(recent, address.byte_offset, byte_length, &recent_offset)) {
    if (has_access && !qa_guest_permissions_allows(recent->permissions, access))
      permission_fault(mem, recent, address.byte_offset, byte_length, access);
    chunk *only = (chunk *)xmalloc(sizeof *only);
    only->mapping = recent;
    only->offset = recent_offset;
    only->byte_length = byte_length;
    *out_count = 1;
    return only;
  }
  chunk *result = NULL;
  size_t count = 0, cap = 0;
  uint64_t cursor = address.byte_offset;
  size_t remaining = byte_length;
  size_t low = first_end_after(mem, cursor);
  for (size_t index = low; index < mem->mapping_count; index++) {
    qa_sparse_mapping *mapping = mem->mappings[index];
    if (remaining == 0) break;
    if (!addr_lt_end(cursor, mapping)) continue; /* cursor >= end */
    if (cursor < mapping->base) break;
    if (has_access && !qa_guest_permissions_allows(mapping->permissions, access)) {
      free(result);
      permission_fault(mem, mapping, cursor, remaining, access);
    }
    size_t offset = (size_t)(cursor - mapping->base);
    size_t length = remaining < mapping->byte_length - offset ? remaining : mapping->byte_length - offset;
    if (count == cap) {
      cap = cap == 0 ? 4 : cap * 2;
      result = (chunk *)xrealloc(result, cap * sizeof *result);
    }
    result[count].mapping = mapping;
    result[count].offset = offset;
    result[count].byte_length = length;
    count++;
    remember(mem, slot, mapping);
    remaining -= length;
    cursor += (uint64_t)length;
  }
  if (remaining != 0) {
    free(result);
    fault(mem, QA_FAULT_UNMAPPED, cursor, remaining,
          has_access ? fault_access_of(access) : QA_FAULT_ACCESS_MAP, "range includes unmapped bytes");
  }
  *out_count = count;
  return result;
}

static void ensure_page_revisions(qa_sparse_backing *backing) {
  if (backing->page_revisions != NULL) return;
  backing->page_count = (backing->byte_length + 4095u) / 4096u;
  backing->page_revisions = (uint64_t *)xcalloc(backing->page_count == 0 ? 1 : backing->page_count, sizeof(uint64_t));
}

/* TS invalidateCode over a backing-absolute range. */
static void invalidate_code(qa_sparse_backing *backing, size_t absolute_offset, size_t byte_length) {
  if (byte_length == 0 || backing->page_revisions == NULL) return;
  size_t first = absolute_offset / 4096u;
  size_t last = (absolute_offset + byte_length - 1u) / 4096u;
  if (last >= backing->page_count) last = backing->page_count - 1u;
  for (size_t i = first; i <= last; i++) backing->page_revisions[i] += 1u; /* fresh revision token */
}

/* TS #notifyWrite. TS indexes watched ranges by backing page (PERF1); the
 * result is the same as this full scan: observers in registration order,
 * ranges in watched-then-written chunk order. Every pending range is
 * collected first, then #writeDeliveries counts the store once, then the
 * observers run. */
static void notify_write(qa_sparse_guest_memory *mem, const chunk *written, size_t written_count) {
  if (mem->observers == NULL) return;
  /* Snapshot the observer list; callbacks may add/remove/write. */
  size_t count = 0, cap = 0;
  struct pending { qa_sparse_write_observer *observer; qa_guest_written_range *ranges; size_t range_count; } *pending = NULL;
  for (qa_sparse_write_observer *observer = mem->observers; observer != NULL; observer = observer->next) {
    if (observer->removed) continue;
    qa_guest_written_range *ranges = NULL;
    size_t range_count = 0, range_cap = 0;
    int64_t displacement = 0;
    for (size_t w = 0; w < observer->chunk_count; w++) {
      size_t watched_start = observer->chunks[w].offset;
      for (size_t j = 0; j < written_count; j++) {
        if (observer->chunks[w].backing != written[j].mapping->backing) continue;
        size_t other = written[j].mapping->backing_offset + written[j].offset;
        size_t first = watched_start > other ? watched_start : other;
        size_t watched_end = watched_start + observer->chunks[w].byte_length;
        size_t other_end = other + written[j].byte_length;
        size_t last = watched_end < other_end ? watched_end : other_end;
        if (first < last) {
          if (range_count == range_cap) {
            range_cap = range_cap == 0 ? 4 : range_cap * 2;
            ranges = (qa_guest_written_range *)xrealloc(ranges, range_cap * sizeof *ranges);
          }
          ranges[range_count].byte_offset = displacement + (int64_t)(first - watched_start);
          ranges[range_count].byte_length = (int64_t)(last - first);
          range_count++;
        }
      }
      displacement += (int64_t)observer->chunks[w].byte_length;
    }
    if (range_count == 0) continue;
    if (count == cap) {
      cap = cap == 0 ? 4 : cap * 2;
      pending = (struct pending *)xrealloc(pending, cap * sizeof *pending);
    }
    pending[count].observer = observer;
    pending[count].ranges = ranges;
    pending[count].range_count = range_count;
    count++;
  }
  if (count == 0) return;
  mem->write_deliveries++;
  for (size_t i = 0; i < count; i++) {
    if (!pending[i].observer->removed)
      pending[i].observer->notify(pending[i].observer->ctx, pending[i].ranges, pending[i].range_count);
  }
  for (size_t i = 0; i < count; i++) free(pending[i].ranges);
  free(pending);
}

static void commit_write(qa_sparse_guest_memory *mem, const chunk *chunks, size_t count, const uint8_t *source) {
  size_t consumed = 0;
  for (size_t i = 0; i < count; i++) {
    qa_sparse_mapping *m = chunks[i].mapping;
    size_t abs_off = m->backing_offset + chunks[i].offset;
    memmove(m->backing->bytes + abs_off, source + consumed, chunks[i].byte_length);
    invalidate_code(m->backing, abs_off, chunks[i].byte_length);
    consumed += chunks[i].byte_length;
  }
  notify_write(mem, chunks, count);
}

static void copy_chunks_into(const chunk *chunks, size_t count, uint8_t *dest) {
  size_t consumed = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_sparse_mapping *m = chunks[i].mapping;
    memcpy(dest + consumed, m->backing->bytes + m->backing_offset + chunks[i].offset, chunks[i].byte_length);
    consumed += chunks[i].byte_length;
  }
}

/* TS #contiguous: absolute backing offset of a live view, or fault. */
static size_t contiguous_offset(qa_sparse_guest_memory *mem, const chunk *chunks, size_t count,
                                qa_guest_core_address address, size_t byte_length, qa_sparse_backing **out_backing) {
  if (count == 0) { *out_backing = NULL; return 0; }
  qa_sparse_backing *backing = chunks[0].mapping->backing;
  size_t start = chunks[0].mapping->backing_offset + chunks[0].offset;
  size_t consumed = 0;
  for (size_t i = 0; i < count; i++) {
    if (chunks[i].mapping->backing != backing ||
        chunks[i].mapping->backing_offset + chunks[i].offset != start + consumed) {
      fault(mem, QA_FAULT_NONCONTIGUOUS_BORROW, address.byte_offset, byte_length, QA_FAULT_ACCESS_BORROW,
            "a live view requires contiguous backing storage; use checked loads or copy for fragmented ranges");
    }
    consumed += chunks[i].byte_length;
  }
  *out_backing = backing;
  return start;
}

static void retire_mapping(qa_sparse_guest_memory *mem, qa_sparse_mapping *mapping) {
  mapping->active = false;
  if (mem->retired_count == mem->retired_cap) {
    mem->retired_cap = mem->retired_cap == 0 ? 8 : mem->retired_cap * 2;
    mem->retired = (qa_sparse_mapping **)xrealloc(mem->retired, mem->retired_cap * sizeof *mem->retired);
  }
  mem->retired[mem->retired_count++] = mapping;
}

/* TS #replaceRange (permissions NULL = unmap). */
static void replace_range(qa_sparse_guest_memory *mem, uint64_t base, size_t byte_length,
                          const qa_guest_permissions *permissions) {
  if (byte_length == 0) return;
  uint64_t end = base + (uint64_t)byte_length;
  bool end_wraps = end == 0; /* range already proofed base+len <= limit */
  size_t first = first_end_after(mem, base);
  qa_sparse_mapping **next = NULL;
  size_t next_count = 0, next_cap = 0;
  size_t last = first;
  for (; last < mem->mapping_count; last++) {
    qa_sparse_mapping *mapping = mem->mappings[last];
    if (!end_wraps && mapping->base >= end) break;
    retire_mapping(mem, mapping);
    uint64_t cut_start = base > mapping->base ? base : mapping->base;
    size_t start_offset = (size_t)(cut_start - mapping->base);
    size_t end_offset;
    if (mapping->end_wraps && end_wraps) end_offset = mapping->byte_length;
    else if (mapping->end_wraps) end_offset = (size_t)(end - mapping->base);
    else if (end_wraps) end_offset = (size_t)(mapping->end - mapping->base);
    else end_offset = (size_t)((end < mapping->end ? end : mapping->end) - mapping->base);
    if (permissions == NULL) mem->mapped_total -= end_offset - start_offset;
    qa_sparse_mapping *pieces[3];
    int piece_count = 0;
    if (start_offset > 0)
      pieces[piece_count++] = new_mapping(mapping->base, start_offset, mapping->permissions, mapping->label,
                                          mapping->backing, mapping->backing_offset);
    if (permissions != NULL)
      pieces[piece_count++] = new_mapping(mapping->base + (uint64_t)start_offset, end_offset - start_offset,
                                          *permissions, mapping->label, mapping->backing,
                                          mapping->backing_offset + start_offset);
    if (end_offset < mapping->byte_length)
      pieces[piece_count++] = new_mapping(mapping->base + (uint64_t)end_offset, mapping->byte_length - end_offset,
                                          mapping->permissions, mapping->label, mapping->backing,
                                          mapping->backing_offset + end_offset);
    for (int p = 0; p < piece_count; p++) {
      if (next_count == next_cap) {
        next_cap = next_cap == 0 ? 4 : next_cap * 2;
        next = (qa_sparse_mapping **)xrealloc(next, next_cap * sizeof *next);
      }
      next[next_count++] = pieces[p];
    }
  }
  if (permissions == NULL) {
    qa_sparse_mapping *touched = first < mem->mapping_count ? mem->mappings[first] : NULL;
    qa_sparse_mapping *previous = first > 0 ? mem->mappings[first - 1] : NULL;
    uint64_t gap_start;
    if (touched != NULL && touched->base < base) gap_start = base;
    else if (previous == NULL) gap_start = mem->allocation_base;
    else gap_start = previous->end_wraps ? UINT64_MAX : previous->end;
    uint64_t lower = gap_start < mem->allocation_base ? mem->allocation_base : gap_start;
    for (size_t i = 0; i < mem->hint_count; i++) {
      if (lower < mem->hints[i].base) mem->hints[i].base = lower;
    }
  }
  mem->generation += 1;
  size_t removed = last - first;
  size_t new_total = mem->mapping_count - removed + next_count;
  if (new_total > mem->mapping_cap) {
    mem->mapping_cap = new_total;
    mem->mappings = (qa_sparse_mapping **)xrealloc(mem->mappings, mem->mapping_cap * sizeof *mem->mappings);
  }
  memmove(mem->mappings + first + next_count, mem->mappings + last,
          (mem->mapping_count - last) * sizeof *mem->mappings);
  if (next_count > 0) memcpy(mem->mappings + first, next, next_count * sizeof *mem->mappings);
  mem->mapping_count = new_total;
  free(next);
}

static void read_bytes(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint8_t *dest, size_t byte_length) {
  size_t single_offset = 0;
  const qa_sparse_mapping *single = single_mapping(mem, address, byte_length, QA_ACCESS_READ, &single_offset);
  if (single != NULL) {
    memcpy(dest, single->backing->bytes + single->backing_offset + single_offset, byte_length);
    return;
  }
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, true, QA_ACCESS_READ, &count);
  copy_chunks_into(chunks, count, dest);
  free(chunks);
}
static void write_bytes(qa_sparse_guest_memory *mem, qa_guest_core_address address, const uint8_t *bytes, size_t byte_length) {
  size_t single_offset = 0;
  qa_sparse_mapping *single = single_mapping(mem, address, byte_length, QA_ACCESS_WRITE, &single_offset);
  if (single != NULL) {
    chunk only = {single, single_offset, byte_length};
    commit_write(mem, &only, 1, bytes);
    return;
  }
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, true, QA_ACCESS_WRITE, &count);
  /* Source may alias guest backing: snapshot when it overlaps a dest chunk. */
  bool overlaps = false;
  for (size_t i = 0; i < count && !overlaps; i++) {
    const uint8_t *b = chunks[i].mapping->backing->bytes;
    size_t blen = chunks[i].mapping->backing->byte_length;
    if (byte_length > 0 && bytes < b + blen && b < bytes + byte_length) overlaps = true;
  }
  if (overlaps && byte_length > 0) {
    uint8_t *snapshot = (uint8_t *)xmalloc(byte_length);
    memcpy(snapshot, bytes, byte_length);
    commit_write(mem, chunks, count, snapshot);
    free(snapshot);
  } else {
    commit_write(mem, chunks, count, bytes);
  }
  free(chunks);
}

static qa_guest_core_address make_address(uint64_t byte_offset) {
  qa_guest_core_address a = {byte_offset};
  return a;
}

bool qa_sparse_pointer(qa_sparse_guest_memory *mem, uint64_t raw_value, qa_guest_core_address *out) {
  if (raw_value == 0) return false;
  range_check(mem, raw_value, 0, QA_FAULT_ACCESS_READ);
  *out = make_address(raw_value);
  return true;
}
qa_guest_core_address qa_sparse_offset(qa_sparse_guest_memory *mem, qa_guest_core_address address, int64_t displacement) {
  range_check(mem, address.byte_offset, 0, QA_FAULT_ACCESS_READ);
  uint64_t result = address.byte_offset + (uint64_t)displacement;
  /* Negative TS results fault as address-overflow in #range. */
  if ((displacement > 0 && result < address.byte_offset) ||
      (displacement < 0 && result > address.byte_offset)) {
    char detail[64];
    (void)snprintf(detail, sizeof detail, "range exceeds %d-bit address space", mem->base.pointer_bytes * 8);
    fault(mem, QA_FAULT_ADDRESS_OVERFLOW, result, 0, QA_FAULT_ACCESS_READ, detail);
  }
  range_check(mem, result, 0, QA_FAULT_ACCESS_READ);
  return make_address(result);
}

qa_guest_core_address qa_sparse_map(qa_sparse_guest_memory *mem, const qa_guest_map_options *options) {
  available(mem, options->base, options->byte_length);
  if (mem->mapped_total + (uint64_t)options->byte_length > mem->maximum_mapped_bytes) {
    char detail[96];
    (void)snprintf(detail, sizeof detail, "guest mappings would exceed %llu bytes", (unsigned long long)mem->maximum_mapped_bytes);
    fault(mem, QA_FAULT_QUOTA, options->base, options->byte_length, QA_FAULT_ACCESS_MAP, detail);
  }
  if (options->bytes != NULL && options->bytes_length > options->byte_length)
    qa_range_error("Initial guest bytes exceed the mapping length");
  uint8_t *bytes = (uint8_t *)xcalloc(options->byte_length == 0 ? 1 : options->byte_length, 1);
  if (options->bytes != NULL) memcpy(bytes, options->bytes, options->bytes_length);
  qa_sparse_backing *backing = new_backing(mem, bytes, options->byte_length);
  insert_mapping(mem, new_mapping(options->base, options->byte_length, options->permissions,
                                   options->label != NULL ? options->label : "", backing, 0));
  return make_address(options->base);
}

static uint64_t align_up_checked(qa_sparse_guest_memory *mem, uint64_t value, uint64_t alignment) {
  uint64_t rem = value & (alignment - 1u);
  if (rem == 0) return value;
  uint64_t result = value + (alignment - rem);
  if (result < value) { /* beyond 2^64: the following map range-checks */
    char detail[64];
    (void)snprintf(detail, sizeof detail, "range exceeds %d-bit address space", mem->base.pointer_bytes * 8);
    fault(mem, QA_FAULT_ADDRESS_OVERFLOW, value, 0, QA_FAULT_ACCESS_MAP, detail);
  }
  return result;
}

enum { POOL_PAGE_BYTES = 4096, POOL_PAGES_PER_SLAB = 64 };
/* TS #allocatePooledPage: small native allocations share slabs of whole pages; released pages are zeroed and reused. */
static qa_guest_core_address allocate_pooled_page(qa_sparse_guest_memory *mem, const char *label) {
  size_t page;
  if (mem->free_page_count > 0) page = mem->free_pages[--mem->free_page_count];
  else {
    size_t slab_index = mem->slab_count;
    for (size_t i = 0; i < mem->slab_count; i++) if (mem->slabs[i].next < POOL_PAGES_PER_SLAB) { slab_index = i; break; }
    if (slab_index == mem->slab_count) {
      char slab_label[256];
      snprintf(slab_label, sizeof slab_label, "pooled pages (%s)", label);
      qa_guest_allocation_options slab_options;
      memset(&slab_options, 0, sizeof slab_options);
      slab_options.byte_length = (size_t)POOL_PAGE_BYTES * POOL_PAGES_PER_SLAB;
      slab_options.alignment = POOL_PAGE_BYTES;
      slab_options.label = slab_label;
      qa_guest_core_address base = qa_sparse_allocate(mem, &slab_options);
      if (mem->slab_count == mem->slab_cap) {
        mem->slab_cap = mem->slab_cap == 0 ? 4 : mem->slab_cap * 2;
        mem->slabs = (struct qa_sparse_slab *)xrealloc(mem->slabs, mem->slab_cap * sizeof *mem->slabs);
      }
      memset(&mem->slabs[mem->slab_count], 0, sizeof *mem->slabs);
      mem->slabs[mem->slab_count].base = base.byte_offset;
      slab_index = mem->slab_count++;
    }
    page = slab_index * POOL_PAGES_PER_SLAB + mem->slabs[slab_index].next++;
  }
  struct qa_sparse_slab *slab = &mem->slabs[page / POOL_PAGES_PER_SLAB];
  slab->used[page % POOL_PAGES_PER_SLAB] = 1;
  return make_address(slab->base + (uint64_t)(page % POOL_PAGES_PER_SLAB) * POOL_PAGE_BYTES);
}
/* TS #releasePooledPage: true when the range was a pooled page (now zeroed and free). */
static bool release_pooled_page(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length) {
  const uint64_t raw = address.byte_offset;
  const uint64_t slab_bytes = (uint64_t)POOL_PAGE_BYTES * POOL_PAGES_PER_SLAB;
  for (size_t slab_index = 0; slab_index < mem->slab_count; slab_index++) {
    struct qa_sparse_slab *slab = &mem->slabs[slab_index];
    if (raw < slab->base || raw >= slab->base + slab_bytes) continue;
    if (raw == slab->base && byte_length == slab_bytes) {
      /* Guest disposal unmaps the slab's own mapping: retire the slab so none of its pages is handed out again. */
      memset(slab->used, 1, sizeof slab->used);
      slab->next = POOL_PAGES_PER_SLAB;
      const size_t first = slab_index * POOL_PAGES_PER_SLAB, last = first + POOL_PAGES_PER_SLAB;
      for (size_t free_index = mem->free_page_count; free_index-- > 0;) {
        const size_t page = mem->free_pages[free_index];
        if (page >= first && page < last) {
          memmove(&mem->free_pages[free_index], &mem->free_pages[free_index + 1], (mem->free_page_count - free_index - 1) * sizeof *mem->free_pages);
          mem->free_page_count--;
        }
      }
      return false;
    }
    const size_t index = (size_t)((raw - slab->base) / POOL_PAGE_BYTES);
    if (byte_length != POOL_PAGE_BYTES || (raw - slab->base) % POOL_PAGE_BYTES != 0)
      fault(mem, QA_FAULT_INVALID_LENGTH, raw, byte_length, QA_FAULT_ACCESS_MAP, "a pooled page can only be released whole");
    if (slab->used[index] != 1) fault(mem, QA_FAULT_UNMAPPED, raw, byte_length, QA_FAULT_ACCESS_MAP, "pooled page is not allocated");
    slab->used[index] = 0;
    size_t offset = 0;
    qa_sparse_mapping *mapping = single_mapping(mem, address, byte_length, QA_ACCESS_WRITE, &offset);
    if (mapping == NULL) qa_range_error("Pooled guest page lost its mapping");
    const size_t absolute = mapping->backing_offset + offset;
    memset(mapping->backing->bytes + absolute, 0, byte_length);
    invalidate_code(mapping->backing, absolute, byte_length);
    if (mem->free_page_count == mem->free_page_cap) {
      mem->free_page_cap = mem->free_page_cap == 0 ? 16 : mem->free_page_cap * 2;
      mem->free_pages = (size_t *)xrealloc(mem->free_pages, mem->free_page_cap * sizeof *mem->free_pages);
    }
    mem->free_pages[mem->free_page_count++] = slab_index * POOL_PAGES_PER_SLAB + index;
    return true;
  }
  return false;
}
qa_guest_core_address qa_sparse_allocate(qa_sparse_guest_memory *mem, const qa_guest_allocation_options *options) {
  if (options->pooled && options->byte_length == POOL_PAGE_BYTES && (options->alignment == 0 ? 16 : options->alignment) <= POOL_PAGE_BYTES
      && (!options->has_permissions || options->permissions == QA_PERM_READ_WRITE))
    return allocate_pooled_page(mem, options->label != NULL ? options->label : "allocation");
  uint64_t alignment = options->alignment == 0 ? 16 : options->alignment;
  if (alignment == 0 || (alignment & (alignment - 1u)) != 0)
    qa_range_error("Guest allocation alignment must be a positive power of two");
  range_check(mem, mem->allocation_base, options->byte_length, QA_FAULT_ACCESS_MAP);
  if (options->byte_length == 0)
    qa_range_error("Guest allocations must contain at least one byte");
  const struct qa_sparse_hint *hint = NULL;
  for (size_t i = 0; i < mem->hint_count; i++) {
    if (mem->hints[i].alignment == alignment) { hint = &mem->hints[i]; break; }
  }
  uint64_t start = hint != NULL && options->byte_length >= hint->byte_length ? hint->base : mem->allocation_base;
  uint64_t base = align_up_checked(mem, start, alignment);
  uint64_t length = (uint64_t)options->byte_length;
  for (size_t index = first_end_after(mem, base); index < mem->mapping_count; index++) {
    qa_sparse_mapping *mapping = mem->mappings[index];
    uint64_t sum = base + length;
    bool sum_wraps = sum < base;
    if (!sum_wraps && sum <= mapping->base) break;
    if (addr_lt_end(base, mapping) && base >= mapping->base) {
      base = align_up_checked(mem, mapping->end_wraps ? UINT64_MAX : mapping->end, alignment);
    } else if (base < mapping->base && !end_le(mapping, base)) {
      /* base < end (TS `base < end` with bigint end) */
      base = align_up_checked(mem, mapping->end_wraps ? UINT64_MAX : mapping->end, alignment);
    }
  }
  qa_guest_map_options map_options;
  memset(&map_options, 0, sizeof map_options);
  map_options.base = base;
  map_options.byte_length = options->byte_length;
  map_options.permissions = options->has_permissions ? options->permissions : QA_PERM_READ_WRITE;
  map_options.label = options->label != NULL ? options->label : "allocation";
  qa_guest_core_address result = qa_sparse_map(mem, &map_options);
  bool found = false;
  for (size_t i = 0; i < mem->hint_count; i++) {
    if (mem->hints[i].alignment == alignment) {
      mem->hints[i].base = base + length;
      mem->hints[i].byte_length = options->byte_length;
      found = true;
      break;
    }
  }
  if (!found) {
    if (mem->hint_count == mem->hint_cap) {
      mem->hint_cap = mem->hint_cap == 0 ? 4 : mem->hint_cap * 2;
      mem->hints = (struct qa_sparse_hint *)xrealloc(mem->hints, mem->hint_cap * sizeof *mem->hints);
    }
    mem->hints[mem->hint_count].alignment = alignment;
    mem->hints[mem->hint_count].base = base + length;
    mem->hints[mem->hint_count].byte_length = options->byte_length;
    mem->hint_count++;
  }
  return result;
}

qa_guest_core_address qa_sparse_map_alias(qa_sparse_guest_memory *mem, uint64_t base, size_t byte_length,
                                     qa_guest_permissions permissions, const char *label, qa_guest_core_address source) {
  available(mem, base, byte_length);
  size_t count = 0;
  chunk *chunks = chunks_of(mem, source, byte_length, false, QA_ACCESS_READ, &count);
  qa_sparse_backing *backing = NULL;
  size_t start = contiguous_offset(mem, chunks, count, source, byte_length, &backing);
  free(chunks);
  insert_mapping(mem, new_mapping(base, byte_length, permissions, label != NULL ? label : "alias", backing, start));
  return make_address(base);
}

void qa_sparse_unmap(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length) {
  if (mem->slab_count != 0 && release_pooled_page(mem, address, byte_length)) return;
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, false, QA_ACCESS_READ, &count);
  free(chunks);
  replace_range(mem, address.byte_offset, byte_length, NULL);
}
void qa_sparse_protect(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length, qa_guest_permissions permissions) {
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, false, QA_ACCESS_READ, &count);
  free(chunks);
  replace_range(mem, address.byte_offset, byte_length, &permissions);
}
size_t qa_sparse_mappings(const qa_sparse_guest_memory *mem, qa_guest_mapping *out, size_t capacity) {
  size_t n = mem->mapping_count < capacity ? mem->mapping_count : capacity;
  for (size_t i = 0; i < n; i++) {
    out[i].base = mem->mappings[i]->base;
    out[i].byte_length = mem->mappings[i]->byte_length;
    out[i].permissions = mem->mappings[i]->permissions;
    out[i].label = mem->mappings[i]->label;
  }
  return mem->mapping_count;
}
void qa_sparse_check(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length, qa_guest_access access) {
  size_t single_offset = 0;
  if (single_mapping(mem, address, byte_length, access, &single_offset) != NULL) return;
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, true, access, &count);
  free(chunks);
}
uint8_t *qa_sparse_borrow(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length) {
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, true, QA_ACCESS_WRITE, &count);
  qa_sparse_backing *backing = NULL;
  size_t start = contiguous_offset(mem, chunks, count, address, byte_length, &backing);
  for (size_t i = 0; i < count; i++) chunks[i].mapping->backing->exposed = true;
  free(chunks);
  return backing == NULL ? &mem->dummy : backing->bytes + start;
}
void qa_sparse_copy(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint8_t *dest, size_t byte_length) {
  read_bytes(mem, address, dest, byte_length);
}
void qa_sparse_copy_into(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint8_t *dest,
                         size_t dest_length, size_t dest_offset, size_t byte_length) {
  if (dest_offset > (size_t)QA_MAX_SAFE_INTEGER || byte_length > (size_t)QA_MAX_SAFE_INTEGER ||
      dest_offset + byte_length < dest_offset || dest_offset + byte_length > dest_length)
    qa_range_error("Guest copy exceeds destination storage");
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, true, QA_ACCESS_READ, &count);
  bool overlaps = false;
  for (size_t i = 0; i < count && !overlaps; i++) {
    const uint8_t *b = chunks[i].mapping->backing->bytes;
    size_t blen = chunks[i].mapping->backing->byte_length;
    const uint8_t *d = dest + dest_offset;
    if (byte_length > 0 && d < b + blen && b < d + byte_length) overlaps = true;
  }
  if (overlaps) {
    uint8_t *snapshot = (uint8_t *)xmalloc(byte_length);
    copy_chunks_into(chunks, count, snapshot);
    memcpy(dest + dest_offset, snapshot, byte_length);
    free(snapshot);
  } else {
    copy_chunks_into(chunks, count, dest + dest_offset);
  }
  free(chunks);
}
int64_t qa_sparse_find_zero(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t maximum) {
  size_t consumed = 0;
  qa_guest_core_address cursor = address;
  while (consumed < maximum) {
    /* one checked byte at a time through the chunk walk, then scan the
     * remainder of that mapping directly (TS #singleMapping fast path) */
    size_t count = 0;
    chunk *chunks = chunks_of(mem, cursor, 1, true, QA_ACCESS_READ, &count);
    qa_sparse_mapping *mapping = chunks[0].mapping;
    size_t offset = chunks[0].offset;
    free(chunks);
    size_t length = maximum - consumed < mapping->byte_length - offset
                        ? maximum - consumed : mapping->byte_length - offset;
    const uint8_t *bytes = mapping->backing->bytes + mapping->backing_offset + offset;
    for (size_t i = 0; i < length; i++) {
      if (bytes[i] == 0) return (int64_t)(consumed + i);
    }
    consumed += length;
    if (consumed < maximum) cursor = qa_sparse_offset(mem, address, (int64_t)consumed);
  }
  return -1;
}
void qa_sparse_fetch(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint8_t *dest, size_t byte_length) {
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, true, QA_ACCESS_EXECUTE, &count);
  copy_chunks_into(chunks, count, dest);
  free(chunks);
}
uint8_t qa_sparse_fetch_byte(qa_sparse_guest_memory *mem, uint64_t byte_offset) {
  qa_sparse_mapping *mapping = mem->recent_mappings[2];
  if (mapping == NULL || !mapping->active || byte_offset < mapping->base || !addr_lt_end(byte_offset, mapping)) {
    range_check(mem, byte_offset, 1, QA_FAULT_ACCESS_EXECUTE);
    size_t index = first_end_after(mem, byte_offset);
    mapping = index < mem->mapping_count ? mem->mappings[index] : NULL;
    if (mapping == NULL || byte_offset < mapping->base)
      fault(mem, QA_FAULT_UNMAPPED, byte_offset, 1, QA_FAULT_ACCESS_EXECUTE, "range includes unmapped bytes");
    remember(mem, 2, mapping);
  }
  if (!qa_guest_permissions_allows(mapping->permissions, QA_ACCESS_EXECUTE))
    permission_fault(mem, mapping, byte_offset, 1, QA_ACCESS_EXECUTE);
  return mapping->backing->bytes[mapping->backing_offset + (size_t)(byte_offset - mapping->base)];
}
void qa_sparse_write(qa_sparse_guest_memory *mem, qa_guest_core_address address, const uint8_t *bytes, size_t byte_length) {
  write_bytes(mem, address, bytes, byte_length);
}
void qa_sparse_move(qa_sparse_guest_memory *mem, qa_guest_core_address destination, qa_guest_core_address source, size_t byte_length) {
  uint8_t *snapshot = (uint8_t *)xmalloc(byte_length);
  read_bytes(mem, source, snapshot, byte_length);
  write_bytes(mem, destination, snapshot, byte_length);
  free(snapshot);
}
void qa_sparse_fill(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length, uint32_t value) {
  uint8_t byte = (uint8_t)(value & 255u);
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, true, QA_ACCESS_WRITE, &count);
  for (size_t i = 0; i < count; i++) {
    qa_sparse_mapping *m = chunks[i].mapping;
    size_t abs_off = m->backing_offset + chunks[i].offset;
    memset(m->backing->bytes + abs_off, byte, chunks[i].byte_length);
    invalidate_code(m->backing, abs_off, chunks[i].byte_length);
  }
  notify_write(mem, chunks, count);
  free(chunks);
}
qa_guest_write_observer_handle qa_sparse_observe_writes(qa_sparse_guest_memory *mem, qa_guest_core_address address,
                                                        size_t byte_length, qa_guest_write_observer_fn notify, void *ctx) {
  size_t count = 0;
  chunk *chunks = chunks_of(mem, address, byte_length, true, QA_ACCESS_READ, &count);
  qa_sparse_write_observer *observer = (qa_sparse_write_observer *)xcalloc(1, sizeof *observer);
  observer->id = ++mem->next_observer_id;
  observer->notify = notify;
  observer->ctx = ctx;
  observer->chunk_count = count;
  observer->chunks = (struct qa_sparse_chunk_ref *)xmalloc((count == 0 ? 1 : count) * sizeof *observer->chunks);
  for (size_t i = 0; i < count; i++) {
    observer->chunks[i].backing = chunks[i].mapping->backing;
    observer->chunks[i].offset = chunks[i].mapping->backing_offset + chunks[i].offset;
    observer->chunks[i].byte_length = chunks[i].byte_length;
  }
  free(chunks);
  observer->next = mem->observers;
  /* TS Set preserves insertion order; append to keep notify order. */
  observer->next = NULL;
  if (mem->observers == NULL) mem->observers = observer;
  else {
    qa_sparse_write_observer *tail = mem->observers;
    while (tail->next != NULL) tail = tail->next;
    tail->next = observer;
  }
  return observer->id;
}
void qa_sparse_unobserve_writes(qa_sparse_guest_memory *mem, qa_guest_write_observer_handle handle) {
  for (qa_sparse_write_observer *o = mem->observers; o != NULL; o = o->next) {
    if (o->id == handle) { o->removed = true; return; }
  }
}
uint64_t qa_sparse_write_deliveries(const qa_sparse_guest_memory *mem) { return mem->write_deliveries; }

/* TS #numericMapping: the recent mapping for this access, then the working
 * set (a hit there becomes the recent mapping); NULL leaves nothing changed. */
static qa_sparse_mapping *numeric_mapping(qa_sparse_guest_memory *mem, uint64_t address, size_t byte_length,
                                          qa_guest_access access, size_t *offset) {
  const int slot = recent_slot(true, access);
  qa_sparse_mapping *recent = mem->recent_mappings[slot];
  if (recent != NULL && recent->active && !recent->end_wraps && recent->end <= 0x1fffffffffffffull &&
      mapping_holds(recent, address, byte_length, offset) && qa_guest_permissions_allows(recent->permissions, access))
    return recent;
  for (size_t i = 0; i < QA_SPARSE_WORKING_SET; i++) {
    qa_sparse_mapping *candidate = mem->working_set[i];
    if (candidate == NULL || candidate == recent || !candidate->active || candidate->end_wraps ||
        candidate->end > 0x1fffffffffffffull)
      continue;
    if (!mapping_holds(candidate, address, byte_length, offset) ||
        !qa_guest_permissions_allows(candidate->permissions, access))
      continue;
    mem->recent_mappings[slot] = candidate;
    return candidate;
  }
  return NULL;
}
int64_t qa_sparse_scalar_read(qa_sparse_guest_memory *mem, uint64_t address, int byte_length) {
  size_t offset = 0;
  qa_sparse_mapping *mapping = numeric_mapping(mem, address, (size_t)byte_length, QA_ACCESS_READ, &offset);
  if (mapping == NULL) return -1;
  return (int64_t)load_le(mapping->backing->bytes + mapping->backing_offset + offset, byte_length);
}
bool qa_sparse_scalar_write(qa_sparse_guest_memory *mem, uint64_t address, int byte_length, uint32_t value) {
  size_t offset = 0;
  qa_sparse_mapping *mapping = numeric_mapping(mem, address, (size_t)byte_length, QA_ACCESS_WRITE, &offset);
  if (mapping == NULL) return false;
  const size_t absolute = mapping->backing_offset + offset;
  store_le(mapping->backing->bytes + absolute, value, byte_length);
  invalidate_code(mapping->backing, absolute, (size_t)byte_length);
  if (mem->observers != NULL) {
    chunk written = { mapping, offset, (size_t)byte_length };
    notify_write(mem, &written, 1);
  }
  return true;
}

void qa_sparse_allocator_image(const qa_sparse_guest_memory *mem, qa_guest_allocator_image *out) {
  memset(out, 0, sizeof *out);
  qa_guest_allocator_hint *hints = (qa_guest_allocator_hint *)xcalloc(mem->hint_count == 0 ? 1 : mem->hint_count, sizeof *hints);
  for (size_t i = 0; i < mem->hint_count; i++) {
    hints[i].alignment = (int64_t)mem->hints[i].alignment;
    hints[i].base = (int64_t)mem->hints[i].base;
    hints[i].byte_length = (double)mem->hints[i].byte_length;
  }
  qa_guest_allocator_slab *slabs = (qa_guest_allocator_slab *)xcalloc(mem->slab_count == 0 ? 1 : mem->slab_count, sizeof *slabs);
  for (size_t i = 0; i < mem->slab_count; i++) {
    uint8_t *used = (uint8_t *)xmalloc(POOL_PAGES_PER_SLAB);
    memcpy(used, mem->slabs[i].used, POOL_PAGES_PER_SLAB);
    slabs[i].base = (int64_t)mem->slabs[i].base;
    slabs[i].used.bytes = used;
    slabs[i].used.length = POOL_PAGES_PER_SLAB;
    slabs[i].next = (double)mem->slabs[i].next;
  }
  double *free_pages = (double *)xcalloc(mem->free_page_count == 0 ? 1 : mem->free_page_count, sizeof *free_pages);
  for (size_t i = 0; i < mem->free_page_count; i++) free_pages[i] = (double)mem->free_pages[i];
  out->hints = hints;
  out->hint_count = mem->hint_count;
  out->slabs = slabs;
  out->slab_count = mem->slab_count;
  out->free_pages = free_pages;
  out->free_page_count = mem->free_page_count;
}
void qa_guest_allocator_image_free(qa_guest_allocator_image *image) {
  for (size_t i = 0; i < image->slab_count; i++) free((void *)image->slabs[i].used.bytes);
  free((void *)image->hints);
  free((void *)image->slabs);
  free((void *)image->free_pages);
  memset(image, 0, sizeof *image);
}
static bool safe_integer(double value) { return value == trunc(value) && fabs(value) <= 9007199254740991.0; }
void qa_sparse_restore_allocator(qa_sparse_guest_memory *mem, const qa_guest_allocator_image *image) {
  for (size_t i = 0; i < image->slab_count; i++) {
    const qa_guest_allocator_slab *slab = &image->slabs[i];
    if (slab->used.length != POOL_PAGES_PER_SLAB || !safe_integer(slab->next) || slab->next < 0 || slab->next > POOL_PAGES_PER_SLAB)
      qa_range_error("Invalid guest allocator slab");
    qa_sparse_check(mem, make_address((uint64_t)slab->base), (size_t)POOL_PAGE_BYTES * POOL_PAGES_PER_SLAB, QA_ACCESS_READ);
  }
  for (size_t i = 0; i < image->free_page_count; i++) {
    const double page = image->free_pages[i];
    if (!safe_integer(page) || page < 0 || page >= (double)image->slab_count * POOL_PAGES_PER_SLAB)
      qa_range_error("Invalid guest allocator free page");
  }
  /* TS Map#set on an existing alignment keeps the first insertion's place. */
  mem->hint_count = 0;
  for (size_t i = 0; i < image->hint_count; i++) {
    size_t at = mem->hint_count;
    for (size_t j = 0; j < mem->hint_count; j++)
      if (mem->hints[j].alignment == (uint64_t)image->hints[i].alignment) { at = j; break; }
    if (at == mem->hint_count) {
      if (mem->hint_count == mem->hint_cap) {
        mem->hint_cap = mem->hint_cap == 0 ? 4 : mem->hint_cap * 2;
        mem->hints = (struct qa_sparse_hint *)xrealloc(mem->hints, mem->hint_cap * sizeof *mem->hints);
      }
      mem->hint_count++;
    }
    mem->hints[at].alignment = (uint64_t)image->hints[i].alignment;
    mem->hints[at].base = (uint64_t)image->hints[i].base;
    mem->hints[at].byte_length = (size_t)image->hints[i].byte_length;
  }
  if (image->slab_count > mem->slab_cap) {
    mem->slab_cap = image->slab_count;
    mem->slabs = (struct qa_sparse_slab *)xrealloc(mem->slabs, mem->slab_cap * sizeof *mem->slabs);
  }
  mem->slab_count = image->slab_count;
  for (size_t i = 0; i < image->slab_count; i++) {
    mem->slabs[i].base = (uint64_t)image->slabs[i].base;
    memcpy(mem->slabs[i].used, image->slabs[i].used.bytes, POOL_PAGES_PER_SLAB);
    mem->slabs[i].next = (size_t)image->slabs[i].next;
  }
  if (image->free_page_count > mem->free_page_cap) {
    mem->free_page_cap = image->free_page_count;
    mem->free_pages = (size_t *)xrealloc(mem->free_pages, mem->free_page_cap * sizeof *mem->free_pages);
  }
  mem->free_page_count = image->free_page_count;
  for (size_t i = 0; i < image->free_page_count; i++) mem->free_pages[i] = (size_t)image->free_pages[i];
}

bool qa_sparse_has_write_observers(const qa_sparse_guest_memory *mem) {
  for (const qa_sparse_write_observer *o = mem->observers; o != NULL; o = o->next) {
    if (!o->removed) return true;
  }
  return false;
}

#define READ_SCALAR(name, type, n)                                                     \
  type name(qa_sparse_guest_memory *mem, qa_guest_core_address address) {                   \
    uint8_t bytes[n];                                                                  \
    read_bytes(mem, address, bytes, n);                                                \
    return (type)load_le(bytes, n);                                                    \
  }
READ_SCALAR(qa_sparse_read_u8, uint8_t, 1)
READ_SCALAR(qa_sparse_read_i8, int8_t, 1)
READ_SCALAR(qa_sparse_read_u16, uint16_t, 2)
READ_SCALAR(qa_sparse_read_i16, int16_t, 2)
READ_SCALAR(qa_sparse_read_u32, uint32_t, 4)
READ_SCALAR(qa_sparse_read_i32, int32_t, 4)
READ_SCALAR(qa_sparse_read_u64, uint64_t, 8)
READ_SCALAR(qa_sparse_read_i64, int64_t, 8)
#undef READ_SCALAR

void qa_sparse_read_u64_words(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint32_t *low, uint32_t *high) {
  uint8_t bytes[8];
  read_bytes(mem, address, bytes, 8);
  uint32_t lo = (uint32_t)load_le(bytes, 4), hi = (uint32_t)load_le(bytes + 4, 4);
  *low = lo;
  *high = hi;
}
double qa_sparse_read_f32(qa_sparse_guest_memory *mem, qa_guest_core_address address) {
  uint8_t bytes[4];
  read_bytes(mem, address, bytes, 4);
  float value;
  uint32_t bits = (uint32_t)load_le(bytes, 4);
  memcpy(&value, &bits, 4);
  return (double)value;
}
qa_vec3 qa_sparse_read_f32_vector(qa_sparse_guest_memory *mem, qa_guest_core_address address) {
  uint8_t bytes[12];
  read_bytes(mem, address, bytes, 12);
  qa_vec3 result;
  float components[3];
  for (int i = 0; i < 3; i++) {
    uint32_t bits = (uint32_t)load_le(bytes + i * 4, 4);
    memcpy(&components[i], &bits, 4);
  }
  result.x = (double)components[0];
  result.y = (double)components[1];
  result.z = (double)components[2];
  return result;
}
double qa_sparse_read_f64(qa_sparse_guest_memory *mem, qa_guest_core_address address) {
  uint8_t bytes[8];
  read_bytes(mem, address, bytes, 8);
  double value;
  uint64_t bits = load_le(bytes, 8);
  memcpy(&value, &bits, 8);
  return value;
}
bool qa_sparse_read_pointer(qa_sparse_guest_memory *mem, qa_guest_core_address address, qa_guest_core_address *out) {
  return qa_sparse_pointer(mem, mem->base.pointer_bytes == 4 ? qa_sparse_read_u32(mem, address)
                                                             : qa_sparse_read_u64(mem, address), out);
}
#define WRITE_SCALAR(name, type, n)                                                    \
  void name(qa_sparse_guest_memory *mem, qa_guest_core_address address, type value) {       \
    uint8_t bytes[n];                                                                  \
    store_le(bytes, (uint64_t)value, n);                                               \
    write_bytes(mem, address, bytes, n);                                               \
  }
WRITE_SCALAR(qa_sparse_write_u8, uint32_t, 1)
WRITE_SCALAR(qa_sparse_write_i8, int32_t, 1)
WRITE_SCALAR(qa_sparse_write_u16, uint32_t, 2)
WRITE_SCALAR(qa_sparse_write_i16, int32_t, 2)
WRITE_SCALAR(qa_sparse_write_u32, uint32_t, 4)
WRITE_SCALAR(qa_sparse_write_i32, int32_t, 4)
WRITE_SCALAR(qa_sparse_write_u64, uint64_t, 8)
WRITE_SCALAR(qa_sparse_write_i64, int64_t, 8)
#undef WRITE_SCALAR

void qa_sparse_write_u64_words(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint32_t low, uint32_t high) {
  uint8_t bytes[8];
  store_le(bytes, low, 4);
  store_le(bytes + 4, high, 4);
  write_bytes(mem, address, bytes, 8);
}
void qa_sparse_write_f32(qa_sparse_guest_memory *mem, qa_guest_core_address address, double value) {
  float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, 4);
  uint8_t bytes[4];
  store_le(bytes, bits, 4);
  write_bytes(mem, address, bytes, 4);
}
void qa_sparse_write_f64(qa_sparse_guest_memory *mem, qa_guest_core_address address, double value) {
  uint64_t bits;
  memcpy(&bits, &value, 8);
  uint8_t bytes[8];
  store_le(bytes, bits, 8);
  write_bytes(mem, address, bytes, 8);
}
void qa_sparse_write_pointer(qa_sparse_guest_memory *mem, qa_guest_core_address address, bool has_value, qa_guest_core_address value) {
  if (has_value) range_check(mem, value.byte_offset, 0, QA_FAULT_ACCESS_WRITE);
  uint64_t raw = has_value ? value.byte_offset : 0;
  if (mem->base.pointer_bytes == 4) qa_sparse_write_u32(mem, address, (uint32_t)raw);
  else qa_sparse_write_u64(mem, address, raw);
}

void qa_sparse_fetch_sequence_init(qa_guest_fetch_sequence *seq, qa_sparse_guest_memory *mem, uint64_t byte_offset) {
  memset(seq, 0, sizeof *seq);
  seq->mem = mem;
  seq->start = byte_offset;
}
uint8_t qa_guest_fetch_sequence_next(qa_guest_fetch_sequence *seq) {
  qa_sparse_guest_memory *mem = seq->mem;
  if (!seq->started || seq->generation != mem->generation || seq->mapping == NULL ||
      seq->offset >= seq->mapping->byte_length) {
    uint64_t address = seq->start + seq->consumed;
    uint8_t byte = qa_sparse_fetch_byte(mem, address);
    size_t index = first_end_after(mem, address);
    seq->mapping = mem->mappings[index];
    seq->offset = (size_t)(address - seq->mapping->base) + 1u;
    seq->generation = mem->generation;
    seq->started = true;
    seq->consumed += 1u;
    return byte;
  }
  uint8_t byte = seq->mapping->backing->bytes[seq->mapping->backing_offset + seq->offset];
  seq->offset += 1u;
  seq->consumed += 1u;
  return byte;
}

static bool guard_unchanged_range(qa_guest_executable_guard *guard) {
  qa_sparse_mapping *mapping = guard->mapping;
  if (mapping == NULL || !mapping->active) return false;
  qa_sparse_backing *backing = mapping->backing;
  if (!backing->exposed && guard->has_page && guard->has_checked &&
      guard->checked == backing->page_revisions[guard->page]) return true;
  const uint8_t *live = backing->bytes + mapping->backing_offset + guard->mapping_offset;
  if (memcmp(live, guard->expected, guard->byte_length) != 0) return false;
  if (guard->has_page) {
    guard->checked = backing->page_revisions[guard->page];
    guard->has_checked = true;
  }
  return true;
}

bool qa_sparse_retain_executable_range(qa_sparse_guest_memory *mem, uint64_t byte_offset,
                                       const uint8_t *bytes, size_t byte_length, qa_guest_executable_guard *out) {
  memset(out, 0, sizeof *out);
  out->mem = mem;
  if (byte_length == 0) return false;
  size_t index = first_end_after(mem, byte_offset);
  qa_sparse_mapping *mapping = index < mem->mapping_count ? mem->mappings[index] : NULL;
  if (mapping == NULL || byte_offset < mapping->base ||
      !qa_guest_permissions_allows(mapping->permissions, QA_ACCESS_EXECUTE)) return false;
  size_t offset = (size_t)(byte_offset - mapping->base);
  if (offset + byte_length > mapping->byte_length) return false;
  out->mapping = mapping;
  out->mapping_offset = offset;
  out->byte_length = byte_length;
  out->expected = (uint8_t *)xmalloc(byte_length);
  memcpy(out->expected, bytes, byte_length);
  qa_sparse_backing *backing = mapping->backing;
  size_t absolute = mapping->backing_offset + offset;
  size_t first_page = absolute / 4096u;
  size_t last_page = (absolute + byte_length - 1u) / 4096u;
  if (!backing->exposed && first_page == last_page) {
    ensure_page_revisions(backing);
    out->has_page = true;
    out->page = first_page;
  }
  if (!guard_unchanged_range(out)) {
    qa_guest_executable_guard_free(out);
    return false;
  }
  out->valid = true;
  return true;
}
bool qa_sparse_retain_executable_bytes(qa_sparse_guest_memory *mem, uint64_t byte_offset,
                                       const uint8_t *bytes, size_t byte_length, qa_guest_executable_guard *out) {
  if (byte_length == 0 || byte_length > 15) { memset(out, 0, sizeof *out); out->mem = mem; return false; }
  return qa_sparse_retain_executable_range(mem, byte_offset, bytes, byte_length, out);
}
bool qa_sparse_retain_executable_block(qa_sparse_guest_memory *mem, uint64_t byte_offset,
                                       const uint8_t *bytes, size_t byte_length, qa_guest_executable_guard *out) {
  if (!qa_sparse_retain_executable_range(mem, byte_offset, bytes, byte_length, out)) return false;
  qa_sparse_mapping *mapping = out->mapping;
  qa_sparse_backing *backing = mapping->backing;
  ensure_page_revisions(backing);
  size_t absolute = mapping->backing_offset + out->mapping_offset;
  size_t first_page = absolute / 4096u;
  size_t last_page = (absolute + byte_length - 1u) / 4096u;
  out->block_page_count = last_page - first_page + 1u;
  out->block_pages = (size_t *)xmalloc(out->block_page_count * sizeof *out->block_pages);
  out->block_checked = (uint64_t *)xmalloc(out->block_page_count * sizeof *out->block_checked);
  for (size_t i = 0; i < out->block_page_count; i++) {
    out->block_pages[i] = first_page + i;
    out->block_checked[i] = backing->page_revisions[first_page + i];
  }
  return true;
}
bool qa_guest_executable_guard_unchanged(qa_guest_executable_guard *guard) {
  if (guard == NULL || !guard->valid) return false;
  if (!guard_unchanged_range(guard)) return false;
  if (guard->block_pages != NULL) {
    qa_sparse_backing *backing = guard->mapping->backing;
    for (size_t i = 0; i < guard->block_page_count; i++)
      guard->block_checked[i] = backing->page_revisions[guard->block_pages[i]];
  }
  return true;
}
bool qa_guest_executable_block_after_store(const qa_guest_executable_guard *guard) {
  if (guard == NULL || !guard->valid || guard->mapping == NULL || !guard->mapping->active) return false;
  qa_sparse_backing *backing = guard->mapping->backing;
  for (size_t i = 0; i < guard->block_page_count; i++) {
    if (guard->block_checked[i] != backing->page_revisions[guard->block_pages[i]]) return false;
  }
  return true;
}
void qa_guest_executable_guard_free(qa_guest_executable_guard *guard) {
  if (guard == NULL) return;
  free(guard->expected);
  free(guard->block_pages);
  free(guard->block_checked);
  memset(guard, 0, sizeof *guard);
}

bool qa_sparse_checkpoint(qa_sparse_guest_memory *mem, qa_guest_memory_snapshot *out) {
  memset(out, 0, sizeof *out);
  out->module = mem->module;
  out->pointer_bytes = mem->base.pointer_bytes;
  out->allocation_base = mem->allocation_base;
  out->backings = (uint8_t **)xcalloc(mem->mapping_count == 0 ? 1 : mem->mapping_count, sizeof *out->backings);
  out->backing_lengths = (size_t *)xcalloc(mem->mapping_count == 0 ? 1 : mem->mapping_count, sizeof *out->backing_lengths);
  out->mappings = (qa_guest_snapshot_mapping *)xcalloc(mem->mapping_count == 0 ? 1 : mem->mapping_count, sizeof *out->mappings);
  out->mapping_count = mem->mapping_count;
  for (size_t i = 0; i < mem->mapping_count; i++) {
    qa_sparse_mapping *mapping = mem->mappings[i];
    size_t backing_index = out->backing_count;
    for (size_t j = 0; j < i; j++) {
      if (mem->mappings[j]->backing == mapping->backing) { backing_index = out->mappings[j].backing; break; }
    }
    if (backing_index == out->backing_count) {
      size_t len = mapping->backing->byte_length;
      out->backings[backing_index] = (uint8_t *)xmalloc(len);
      memcpy(out->backings[backing_index], mapping->backing->bytes, len);
      out->backing_lengths[backing_index] = len;
      out->backing_count++;
    }
    out->mappings[i].base = mapping->base;
    out->mappings[i].byte_length = mapping->byte_length;
    out->mappings[i].permissions = mapping->permissions;
    out->mappings[i].label = dup_str(mapping->label);
    out->mappings[i].backing = backing_index;
    out->mappings[i].backing_offset = mapping->backing_offset;
  }
  return true;
}

static bool same_module_field(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

bool qa_sparse_guest_memory_restore(qa_sparse_guest_memory *out, const qa_guest_module_ref *module,
                                    const qa_guest_memory_snapshot *snapshot) {
  if (!same_module_field(module->id, snapshot->module.id) ||
      !same_module_field(module->digest, snapshot->module.digest) ||
      !same_module_field(module->revision, snapshot->module.revision))
    qa_range_error("Guest snapshot belongs to a different module artifact");
  qa_sparse_guest_memory_options options;
  memset(&options, 0, sizeof options);
  options.module = *module;
  options.pointer_bytes = snapshot->pointer_bytes;
  options.allocation_base = snapshot->allocation_base;
  options.has_allocation_base = true;
  qa_sparse_guest_memory_init(out, &options);
  qa_sparse_backing **backings = (qa_sparse_backing **)xcalloc(
      snapshot->backing_count == 0 ? 1 : snapshot->backing_count, sizeof *backings);
  for (size_t i = 0; i < snapshot->backing_count; i++) {
    uint8_t *bytes = (uint8_t *)xmalloc(snapshot->backing_lengths[i]);
    memcpy(bytes, snapshot->backings[i], snapshot->backing_lengths[i]);
    backings[i] = new_backing(out, bytes, snapshot->backing_lengths[i]);
  }
  for (size_t i = 0; i < snapshot->mapping_count; i++) {
    const qa_guest_snapshot_mapping *mapping = &snapshot->mappings[i];
    available(out, mapping->base, mapping->byte_length);
    if (mapping->backing >= snapshot->backing_count ||
        mapping->backing_offset + mapping->byte_length < mapping->backing_offset ||
        mapping->backing_offset + mapping->byte_length > snapshot->backing_lengths[mapping->backing]) {
      free(backings);
      qa_range_error("Invalid guest snapshot backing range");
    }
    insert_mapping(out, new_mapping(mapping->base, mapping->byte_length, mapping->permissions,
                                    mapping->label != NULL ? mapping->label : "",
                                    backings[mapping->backing], mapping->backing_offset));
  }
  free(backings);
  return true;
}

/* --- registers.h vtable wiring --- */
static void vt_copy(void *ctx, qa_guest_core_address a, uint8_t *dest, size_t len) {
  qa_sparse_copy((qa_sparse_guest_memory *)ctx, a, dest, len);
}
static void vt_write(void *ctx, qa_guest_core_address a, const uint8_t *bytes, size_t len) {
  qa_sparse_write((qa_sparse_guest_memory *)ctx, a, bytes, len);
}
static void vt_copy_into(void *ctx, qa_guest_core_address a, uint8_t *dest, size_t off, size_t len) {
  /* The vtable carries no destination length; only the written range is
   * known, so the TS destination-storage RangeError can only check the
   * safe-integer bounds here (qa_sparse_copy_into checks fully). */
  qa_sparse_copy_into((qa_sparse_guest_memory *)ctx, a, dest, off + len, off, len);
}
static uint8_t vt_read_u8(void *ctx, qa_guest_core_address a) { return qa_sparse_read_u8((qa_sparse_guest_memory *)ctx, a); }
static uint16_t vt_read_u16(void *ctx, qa_guest_core_address a) { return qa_sparse_read_u16((qa_sparse_guest_memory *)ctx, a); }
static uint32_t vt_read_u32(void *ctx, qa_guest_core_address a) { return qa_sparse_read_u32((qa_sparse_guest_memory *)ctx, a); }
static uint64_t vt_read_u64(void *ctx, qa_guest_core_address a) { return qa_sparse_read_u64((qa_sparse_guest_memory *)ctx, a); }
static void vt_write_u8(void *ctx, qa_guest_core_address a, uint8_t v) { qa_sparse_write_u8((qa_sparse_guest_memory *)ctx, a, v); }
static void vt_write_u16(void *ctx, qa_guest_core_address a, uint16_t v) { qa_sparse_write_u16((qa_sparse_guest_memory *)ctx, a, v); }
static void vt_write_u32(void *ctx, qa_guest_core_address a, uint32_t v) { qa_sparse_write_u32((qa_sparse_guest_memory *)ctx, a, v); }
static void vt_write_u64(void *ctx, qa_guest_core_address a, uint64_t v) { qa_sparse_write_u64((qa_sparse_guest_memory *)ctx, a, v); }
static bool vt_pointer(void *ctx, uint64_t raw, qa_guest_core_address *out) {
  return qa_sparse_pointer((qa_sparse_guest_memory *)ctx, raw, out);
}
static qa_guest_core_address vt_offset(void *ctx, qa_guest_core_address a, int64_t d) {
  return qa_sparse_offset((qa_sparse_guest_memory *)ctx, a, d);
}
static void vt_check(void *ctx, qa_guest_core_address a, size_t len, qa_guest_access access) {
  qa_sparse_check((qa_sparse_guest_memory *)ctx, a, len, access);
}
static void vt_read_words(void *ctx, qa_guest_core_address a, uint32_t *low, uint32_t *high) {
  qa_sparse_read_u64_words((qa_sparse_guest_memory *)ctx, a, low, high);
}
static void vt_write_words(void *ctx, qa_guest_core_address a, uint32_t low, uint32_t high) {
  qa_sparse_write_u64_words((qa_sparse_guest_memory *)ctx, a, low, high);
}

static void init_impl(qa_sparse_guest_memory *mem, const qa_sparse_guest_memory_options *options, bool managed) {
  memset(mem, 0, sizeof *mem);
  mem->module = options->module;
  mem->managed = managed;
  mem->allocation_base = options->has_allocation_base ? options->allocation_base : 0x10000ull;
  mem->maximum_mapped_bytes = options->maximum_mapped_bytes != 0 ? options->maximum_mapped_bytes : 0x40000000ull;
  mem->base.ctx = mem;
  mem->base.pointer_bytes = options->pointer_bytes;
  mem->base.managed_sparse = managed;
  mem->base.copy = vt_copy;
  mem->base.write = vt_write;
  mem->base.copy_into = vt_copy_into;
  mem->base.read_u8 = vt_read_u8;
  mem->base.read_u16 = vt_read_u16;
  mem->base.read_u32 = vt_read_u32;
  mem->base.read_u64 = vt_read_u64;
  mem->base.write_u8 = vt_write_u8;
  mem->base.write_u16 = vt_write_u16;
  mem->base.write_u32 = vt_write_u32;
  mem->base.write_u64 = vt_write_u64;
  mem->base.pointer = vt_pointer;
  mem->base.offset = vt_offset;
  mem->base.check = vt_check;
  mem->base.read_u64_words = vt_read_words;
  mem->base.write_u64_words = vt_write_words;
  range_check(mem, mem->allocation_base, 0, QA_FAULT_ACCESS_MAP);
}
void qa_sparse_guest_memory_init(qa_sparse_guest_memory *mem, const qa_sparse_guest_memory_options *options) {
  init_impl(mem, options, false);
}
void qa_sparse_guest_memory_init_managed(qa_sparse_guest_memory *mem, const qa_sparse_guest_memory_options *options) {
  init_impl(mem, options, true);
}
bool qa_sparse_guest_memory_managed(const qa_sparse_guest_memory *mem) { return mem->managed; }

static void free_mapping(qa_sparse_mapping *mapping) {
  free(mapping->label);
  free(mapping);
}
void qa_sparse_guest_memory_destroy(qa_sparse_guest_memory *mem) {
  for (size_t i = 0; i < mem->mapping_count; i++) free_mapping(mem->mappings[i]);
  free(mem->mappings);
  for (size_t i = 0; i < mem->retired_count; i++) free_mapping(mem->retired[i]);
  free(mem->retired);
  for (size_t i = 0; i < mem->backing_count; i++) {
    free(mem->backings[i]->bytes);
    free(mem->backings[i]->page_revisions);
    free(mem->backings[i]);
  }
  free(mem->backings);
  qa_sparse_write_observer *observer = mem->observers;
  while (observer != NULL) {
    qa_sparse_write_observer *next = observer->next;
    free(observer->chunks);
    free(observer);
    observer = next;
  }
  free(mem->hints);
  free(mem->slabs);
  free(mem->free_pages);
  memset(mem, 0, sizeof *mem);
}






