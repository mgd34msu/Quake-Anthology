/* Port of src/compat/q2/rerelease/native-entries.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/native_entries.h"

static const qa_guest_value_layout ptr1[] = {{false, "pointer", {NULL, 0, 0, 0, NULL, 0}}};
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_spawn_signature(void) {
  return qa_compat_q2_rerelease_signature(NULL, 0, false, qa_compat_q2_rr_pointer_layout);
}
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_free_signature(void) {
  return qa_compat_q2_rerelease_signature(ptr1, 1, true, qa_compat_q2_rr_int_layout);
}
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_damage_signature(qa_guest_value_layout *storage) {
  qa_compat_q2_native_combat_argument args[10];
  qa_compat_q2_native_combat_call call = qa_compat_q2_stock_native_combat_call(QA_Q2CO_DAMAGE, true, qa_compat_q2_rerelease_abi, args);
  return qa_compat_q2_native_combat_signature(&call, QA_Q2CO_DAMAGE, qa_compat_q2_rerelease_abi, storage);
}
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_power_armor_signature(qa_guest_value_layout *storage) {
  qa_compat_q2_native_combat_argument args[10];
  qa_compat_q2_native_combat_call call = qa_compat_q2_stock_native_combat_call(QA_Q2CO_POWER_ARMOR, true, qa_compat_q2_rerelease_abi, args);
  return qa_compat_q2_native_combat_signature(&call, QA_Q2CO_POWER_ARMOR, qa_compat_q2_rerelease_abi, storage);
}
static qa_guest_address entry_at(const qa_compat_q2_memory *memory, qa_guest_address base, double rva) {
  qa_guest_address a = qa_compat_q2_mem_offset(memory, base, (int64_t)rva); memory->check(memory->ctx, a, 1, "execute"); return a;
}
static qa_guest_address data_at(const qa_compat_q2_memory *memory, qa_guest_address base, double rva, double bytes) {
  qa_guest_address a = qa_compat_q2_mem_offset(memory, base, (int64_t)rva); memory->check(memory->ctx, a, bytes, "read"); return a;
}
qa_compat_q2_rerelease_native_entries qa_compat_q2_rerelease_entries(const qa_compat_q2_memory *memory, qa_guest_address image_base,
                                                                     const qa_compat_q2_rerelease_primary_world_profile *profile) {
  qa_compat_q2_validate_rerelease_primary_world_profile(profile, memory->module.digest);
  qa_compat_q2_rerelease_native_entries out;
  out.spawn = entry_at(memory, image_base, profile->entries.spawn); out.freed = entry_at(memory, image_base, profile->entries.free_);
  out.damage = entry_at(memory, image_base, profile->entries.damage); out.power_armor = entry_at(memory, image_base, profile->entries.power_armor);
  out.regular_armor.entry = entry_at(memory, image_base, profile->entries.regular_armor.entry); out.regular_armor.join = entry_at(memory, image_base, profile->entries.regular_armor.join);
  out.armor_info_table = data_at(memory, image_base, profile->armor.table, (profile->client.inventory_count - 1) * profile->armor.stride + 8);
  out.process_pain = entry_at(memory, image_base, profile->entries.process_pain); out.time = data_at(memory, image_base, profile->entries.time, 8);
  return out;
}
