/* Port of src/compat/q3/native/module.ts. GPL-2.0-or-later. */
#include "qa/compat/q3/native/module.h"
#include <string.h>
#include "qa/foundation/support.h"

qa_guest_call_signature qa_compat_q3_native_signature(qa_guest_native_abi abi,
  const qa_abi_storage *parameters, size_t parameter_count,
  bool has_result, qa_abi_storage result, bool variadic,
  qa_abi_value_layout *layouts, qa_abi_value_layout *result_layout) {
  for (size_t i = 0; i < parameter_count; i++) {
    memset(&layouts[i], 0, sizeof layouts[i]);
    layouts[i].is_aggregate = false; layouts[i].storage = parameters[i];
  }
  memset(result_layout, 0, sizeof *result_layout);
  result_layout->is_aggregate = false; result_layout->storage = result;
  return (qa_guest_call_signature){abi, layouts, parameter_count, !has_result, *result_layout, variadic};
}
bool qa_compat_q3_native_export(const qa_guest_image *image, const char *name, qa_guest_address *out) {
  for (size_t i = 0; i < image->export_count; i++) {
    const qa_guest_export *entry = &image->exports[i];
    if (!entry->symbol.is_ordinal && entry->symbol.name && strcmp(entry->symbol.name, name) == 0) {
      if (entry->is_forward) return false;
      *out = entry->address; return true;
    }
  }
  return false;
}
static qa_call_result invoke_call(qa_compat_q3_native_module_options *options, qa_guest_address target,
  const qa_abi_storage *parameters, size_t parameter_count, bool has_result, qa_abi_storage result,
  const qa_call_value *args, size_t arg_count, bool native_context) {
  qa_abi_value_layout layouts[13], result_layout;
  qa_guest_call_signature signature = qa_compat_q3_native_signature(options->image.abi, parameters, parameter_count, has_result, result, false, layouts, &result_layout);
  qa_guest_call_context context = options->context;
  if (native_context) {
    context.callback_kind = QA_CALLBACK_REF_NATIVE_GUEST;
    context.callback_module = options->image.module;
    context.callback_address = target;
    context.callback_abi = signature.abi;
  }
  qa_guest_call_request request = {target, signature, args, arg_count, context, options->instruction_budget};
  return qa_guest_call_runner_invoke(options->runner, &request);
}
void qa_compat_q3_quakelive_game_module_init(qa_compat_q3_quakelive_game_module *m,
  qa_compat_q3_native_module_options options, qa_guest_address imports) {
  memset(m, 0, sizeof *m); m->options = options;
  qa_guest_call_runner_options runner_options = qa_guest_call_runner_options_of(options.runner);
  m->memory = runner_options.cpu->memory;
  const qa_mapped_memory_ext *ext = runner_options.cpu->memory_ext;
  qa_guest_address found;
  if (qa_compat_q3_native_export(&options.image, "vmMain", &found) || qa_compat_q3_native_export(&options.image, "GetGameAPI", &found))
    qa_range_error("Module does not use the Quake Live table interface");
  qa_guest_address entry;
  if (!qa_compat_q3_native_export(&options.image, "dllEntry", &entry)) qa_range_error("Native module has no dllEntry export");
  qa_guest_address output = qa_gmem_allocate(ext, (size_t)options.image.abi.pointer_bytes + 4, true, 8, false, QA_PERM_NONE, "QL dllEntry output");
  qa_guest_address version = qa_memory_offset(m->memory, output, options.image.abi.pointer_bytes);
  static const qa_abi_storage params[3] = {QA_STORAGE_POINTER, QA_STORAGE_POINTER, QA_STORAGE_POINTER};
  qa_call_value args[3] = {qa_call_pointer(true, output), qa_call_pointer(true, imports), qa_call_pointer(true, version)};
  qa_call_result result = invoke_call(&m->options, entry, params, 3, false, QA_STORAGE_INT32, args, 3, true);
  qa_call_value_free(&result);
  m->api_version = qa_gmem_read_i32(m->memory, version);
  if (m->api_version != 10) qa_range_error("Quake Live game API %lld is unsupported; expected 10", (long long)m->api_version);
  qa_guest_address table;
  if (!qa_gmem_read_pointer(m->memory, output, &table)) qa_range_error("Quake Live dllEntry returned a null export table");
  qa_memory_check(m->memory, table, (size_t)(11 * options.image.abi.pointer_bytes), QA_ACCESS_READ);
  m->exports = table;
  qa_gmem_unmap(ext, output, (size_t)options.image.abi.pointer_bytes + 4);
}
static qa_call_result call_slot(qa_compat_q3_quakelive_game_module *m, int64_t slot,
  const qa_abi_storage *parameters, size_t parameter_count, bool has_result, qa_abi_storage result,
  const qa_call_value *args, size_t arg_count) {
  qa_guest_address target;
  if (!qa_gmem_read_pointer(m->memory, qa_memory_offset(m->memory, m->exports, slot * m->memory->pointer_bytes), &target))
    qa_range_error("Missing Quake Live export slot %lld", (long long)slot);
  qa_memory_check(m->memory, target, 1, QA_ACCESS_EXECUTE);
  return invoke_call(&m->options, target, parameters, parameter_count, has_result, result, args, arg_count, true);
}
void qa_compat_q3_quakelive_game_module_register_cvars(qa_compat_q3_quakelive_game_module *m) {
  qa_call_result r = call_slot(m, 2, NULL, 0, false, QA_STORAGE_INT32, NULL, 0); qa_call_value_free(&r);
}
void qa_compat_q3_quakelive_game_module_initialize(qa_compat_q3_quakelive_game_module *m, double level_time, double random_seed, bool restart) {
  static const qa_abi_storage params[3] = {QA_STORAGE_INT32, QA_STORAGE_INT32, QA_STORAGE_INT32};
  qa_call_value args[3] = {qa_call_int32((int32_t)level_time), qa_call_int32((int32_t)random_seed), qa_call_int32(restart ? 1 : 0)};
  qa_call_result r = call_slot(m, 3, params, 3, false, QA_STORAGE_INT32, args, 3); qa_call_value_free(&r);
}
void qa_compat_q3_quakelive_game_module_shutdown(qa_compat_q3_quakelive_game_module *m, bool restart) {
  static const qa_abi_storage params[1] = {QA_STORAGE_INT32};
  qa_call_value args[1] = {qa_call_int32(restart ? 1 : 0)};
  qa_call_result r = call_slot(m, 0, params, 1, false, QA_STORAGE_INT32, args, 1); qa_call_value_free(&r);
}
void qa_compat_q3_quakelive_game_module_run_frame(qa_compat_q3_quakelive_game_module *m, double time) {
  static const qa_abi_storage params[1] = {QA_STORAGE_INT32};
  qa_call_value args[1] = {qa_call_int32((int32_t)time)};
  qa_call_result r = call_slot(m, 1, params, 1, false, QA_STORAGE_INT32, args, 1); qa_call_value_free(&r);
}
bool qa_compat_q3_quakelive_game_module_console_command(qa_compat_q3_quakelive_game_module *m) {
  qa_call_result result = call_slot(m, 4, NULL, 0, true, QA_STORAGE_INT32, NULL, 0);
  if (result.kind != QA_CALL_INT32) qa_range_error("Quake Live ConsoleCommand returned the wrong ABI type");
  return result.number != 0;
}
bool qa_compat_q3_quakelive_game_module_client_connect(qa_compat_q3_quakelive_game_module *m, double client, bool first_time, bool is_bot, qa_guest_address *out) {
  static const qa_abi_storage params[3] = {QA_STORAGE_INT32, QA_STORAGE_INT32, QA_STORAGE_INT32};
  qa_call_value args[3] = {qa_call_int32((int32_t)client), qa_call_int32(first_time ? 1 : 0), qa_call_int32(is_bot ? 1 : 0)};
  qa_call_result result = call_slot(m, 8, params, 3, true, QA_STORAGE_POINTER, args, 3);
  if (result.kind != QA_CALL_POINTER) qa_range_error("Quake Live ClientConnect returned the wrong ABI type");
  if (result.pointer_has) *out = result.pointer;
  return result.pointer_has;
}
void qa_compat_q3_vm_module_init(qa_compat_q3_vm_module *m, qa_compat_q3_native_module_options options, qa_guest_address syscall) {
  memset(m, 0, sizeof *m); m->options = options;
  if (options.image.abi.pointer_bytes != 4) qa_range_error("The Q3 1.32 native profile requires i386; later intptr_t profiles need their own declared ABI");
  qa_guest_address entry, main_address;
  bool has_entry = qa_compat_q3_native_export(&options.image, "dllEntry", &entry);
  bool has_main = qa_compat_q3_native_export(&options.image, "vmMain", &main_address);
  if (!has_entry || !has_main) qa_range_error("Q3 1.32 native module requires dllEntry and vmMain exports");
  m->vm_main = main_address;
  static const qa_abi_storage params[1] = {QA_STORAGE_POINTER};
  qa_call_value args[1] = {qa_call_pointer(true, syscall)};
  qa_call_result result = invoke_call(&m->options, entry, params, 1, false, QA_STORAGE_INT32, args, 1, false);
  qa_call_value_free(&result);
}
double qa_compat_q3_vm_module_call(qa_compat_q3_vm_module *m, double command, const double *arguments, size_t argument_count) {
  if (argument_count > 12) qa_range_error("Q3 vmMain accepts at most twelve arguments");
  qa_call_value args[13];
  args[0] = qa_call_int32((int32_t)command);
  for (size_t i = 0; i < 12; i++) args[1 + i] = qa_call_int32((int32_t)(i < argument_count ? arguments[i] : 0));
  qa_abi_storage params[13];
  for (size_t i = 0; i < 13; i++) params[i] = QA_STORAGE_INT32;
  qa_call_result result = invoke_call(&m->options, m->vm_main, params, 13, true, QA_STORAGE_INT32, args, 13, true);
  if (result.kind != QA_CALL_INT32) qa_range_error("Q3 vmMain returned the wrong ABI type");
  return result.number;
}
