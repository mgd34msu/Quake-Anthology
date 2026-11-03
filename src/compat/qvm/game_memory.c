/* See include/qa/compat/qvm/game_memory.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/game_memory.h"

bool qa_qvm_game_memory_pointer(const qa_qvm_game_memory *memory, int64_t pointer, qa_qvm_game_region *out) {
  return memory->ops->pointer(memory->ctx, pointer, out);
}
qa_qvm_game_region qa_qvm_game_memory_span(const qa_qvm_game_memory *memory, int64_t pointer, int64_t length,
                                         int64_t relative_offset) {
  return memory->ops->span(memory->ctx, pointer, length, relative_offset);
}
void qa_qvm_game_memory_view(const qa_qvm_game_memory *memory, int64_t pointer, int64_t length, int64_t relative_offset,
                            qa_qvm_writable_view *out) {
  memory->ops->view(memory->ctx, pointer, length, relative_offset, out);
}
char *qa_qvm_game_memory_read_string(const qa_qvm_game_memory *memory, int64_t pointer) {
  return memory->ops->read_string(memory->ctx, pointer);
}
void qa_qvm_game_memory_write_string(const qa_qvm_game_memory *memory, int64_t pointer, const char *text,
                                    int64_t capacity) {
  memory->ops->write_string(memory->ctx, pointer, text, capacity);
}
bool qa_qvm_game_memory_address(const qa_qvm_game_memory *memory, int64_t pointer, int64_t *out) {
  return memory->ops->address(memory->ctx, pointer, out);
}
int64_t qa_qvm_game_memory_pointer_word(const qa_qvm_game_memory *memory, bool has_address, int64_t address) {
  return memory->ops->pointer_word(memory->ctx, has_address, address);
}
void qa_qvm_game_memory_view_at(const qa_qvm_game_memory *memory, int64_t address, int64_t length,
                               qa_qvm_writable_view *out) {
  memory->ops->view_at(memory->ctx, address, length, out);
}
void qa_qvm_game_memory_region_view(const qa_qvm_game_memory *memory, const qa_qvm_game_region *region, int64_t offset,
                                   int64_t length, qa_qvm_writable_view *out) {
  memory->ops->region_view(memory->ctx, region, offset, length, out);
}
void qa_qvm_game_memory_region_write(const qa_qvm_game_memory *memory, const qa_qvm_game_region *region,
                                    const uint8_t *bytes, size_t length) {
  memory->ops->region_write(memory->ctx, region, bytes, length);
}
void qa_qvm_game_memory_region_fill(const qa_qvm_game_memory *memory, const qa_qvm_game_region *region, int64_t start,
                                   int64_t length, int32_t value) {
  memory->ops->region_fill(memory->ctx, region, start, length, value);
}
