/* Port of src/persistence/execution.ts. GPL-2.0-or-later. */
#include "qa/persistence/execution.h"

#include <stdlib.h>
#include <string.h>

#include "qa/persistence/shared.h"

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *field_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_namespaced(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_choice_number(const qa_save_reader *reader, const char *name,
                                  const double *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_choice_number(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_literal(const qa_save_reader *reader, const char *name,
                                 const char *expected) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_literal_string(&f, expected);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_literal_number(const qa_save_reader *reader, const char *name,
                                   double expected) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_literal_number(&f, expected);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer_min(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return value;
}
static int64_t field_bigint(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  int64_t value = qa_save_reader_bigint(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_byte_view field_bytes(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  qa_byte_view view;
  view.bytes = qa_save_reader_bytes(&f, &view.length);
  qa_save_reader_destroy(&f);
  return view;
}

qa_module_identity qa_read_module(const qa_save_reader *reader) {
  qa_module_identity module;
  module.id = field_namespaced(reader, "id");
  module.artifact_path = field_string(reader, "artifactPath");
  qa_save_reader digest = field(reader, "digest");
  module.digest = qa_read_digest(&digest);
  qa_save_reader_destroy(&digest);
  module.revision = field_string(reader, "revision");
  return module;
}
qa_game_api_identity qa_read_api(const qa_save_reader *reader) {
  static const char *const kinds[] = {"q1-netquake", "q1-quakeworld", "q2-classic-game",
      "q2-rerelease-game", "q2-rerelease-cgame", "q3-qagame", "q3-cgame", "q3-ui"};
  const char *kind = field_choice(reader, "kind", kinds, 8);
  qa_game_api_identity api;
  memset(&api, 0, sizeof api);
  if (strcmp(kind, "q1-netquake") == 0 || strcmp(kind, "q1-quakeworld") == 0) {
    api.kind = QA_GAME_API_QUAKEC;
    api.as.quakec.kind = strcmp(kind, "q1-netquake") == 0
        ? QA_QUAKEC_API_Q1_NETQUAKE : QA_QUAKEC_API_Q1_QUAKEWORLD;
    api.as.quakec.program_version =
        (int64_t)field_literal_number(reader, "programVersion", 6);
    api.as.quakec.system_crc = (int64_t)field_literal_number(
        reader, "systemCrc", strcmp(kind, "q1-netquake") == 0 ? 5927 : 54730);
  } else if (strcmp(kind, "q2-classic-game") == 0 || strcmp(kind, "q2-rerelease-game") == 0) {
    api.kind = QA_GAME_API_Q2_GAME;
    api.as.q2_game.kind = strcmp(kind, "q2-classic-game") == 0
        ? QA_Q2_GAME_API_CLASSIC : QA_Q2_GAME_API_RERELEASE;
    api.as.q2_game.version = (int64_t)field_literal_number(
        reader, "version", strcmp(kind, "q2-classic-game") == 0 ? 3 : 2023);
  } else if (strcmp(kind, "q2-rerelease-cgame") == 0) {
    api.kind = QA_GAME_API_Q2_CGAME;
    api.as.q2_cgame.version = (int64_t)field_literal_number(reader, "version", 2022);
  } else {
    static const double qagame[] = {7, 8};
    static const double cgame[] = {3, 4};
    static const double ui[] = {4, 6};
    api.kind = QA_GAME_API_Q3;
    if (strcmp(kind, "q3-qagame") == 0) {
      api.as.q3.kind = QA_Q3_API_QAGAME;
      api.as.q3.version = (int64_t)field_choice_number(reader, "version", qagame, 2);
    } else if (strcmp(kind, "q3-cgame") == 0) {
      api.as.q3.kind = QA_Q3_API_CGAME;
      api.as.q3.version = (int64_t)field_choice_number(reader, "version", cgame, 2);
    } else {
      api.as.q3.kind = QA_Q3_API_UI;
      api.as.q3.version = (int64_t)field_choice_number(reader, "version", ui, 2);
    }
  }
  return api;
}
qa_native_call_abi qa_read_native_call_abi(const qa_save_reader *reader) {
  static const char *const kinds[] = {
      "windows-i386", "windows-x86-64", "linux-i386", "linux-x86-64"};
  const char *kind = field_choice(reader, "kind", kinds, 4);
  qa_native_call_abi abi;
  abi.kind = kind;
  if (strcmp(kind, "windows-i386") == 0) {
    static const char *const calls[] = {"cdecl", "stdcall", "thiscall", "fastcall"};
    abi.image = field_literal(reader, "image", "pe32");
    abi.pointer_bytes = (int64_t)field_literal_number(reader, "pointerBytes", 4);
    abi.call = field_choice(reader, "call", calls, 4);
  } else if (strcmp(kind, "windows-x86-64") == 0) {
    abi.image = field_literal(reader, "image", "pe32+");
    abi.pointer_bytes = (int64_t)field_literal_number(reader, "pointerBytes", 8);
    abi.call = field_literal(reader, "call", "microsoft-x64");
  } else if (strcmp(kind, "linux-i386") == 0) {
    abi.image = field_literal(reader, "image", "elf32");
    abi.pointer_bytes = (int64_t)field_literal_number(reader, "pointerBytes", 4);
    abi.call = field_literal(reader, "call", "system-v-i386");
  } else {
    abi.image = field_literal(reader, "image", "elf64");
    abi.pointer_bytes = (int64_t)field_literal_number(reader, "pointerBytes", 8);
    abi.call = field_literal(reader, "call", "system-v-x86-64");
  }
  return abi;
}
qa_native_abi qa_read_native_abi(const qa_save_reader *reader) {
  qa_native_abi abi = qa_read_native_call_abi(reader);
  if (strcmp(abi.kind, "windows-i386") == 0 && strcmp(abi.call, "cdecl") != 0)
    qa_save_reader_fail(reader, "module entry requires cdecl");
  return abi;
}
static qa_guest_storage read_storage(const qa_save_reader *reader) {
  static const char *const storages[] = {"int8", "uint8", "int16", "uint16", "int32",
      "uint32", "int64", "uint64", "float32", "float64", "pointer"};
  return qa_save_reader_choice_string(reader, storages, 11);
}
qa_guest_layout qa_read_layout(const qa_save_reader *reader) {
  static const double pointer_bytes[] = {4, 8};
  qa_guest_layout layout;
  memset(&layout, 0, sizeof layout);
  layout.id = field_namespaced(reader, "id");
  layout.byte_length = field_integer_min(reader, "byteLength", 0);
  layout.alignment = field_integer_min(reader, "alignment", 1);
  layout.pointer_bytes = (int64_t)field_choice_number(reader, "pointerBytes", pointer_bytes, 2);
  (void)field_literal(reader, "byteOrder", "little-endian");
  qa_save_reader fields = field(reader, "fields");
  size_t count = qa_save_reader_list_count(&fields);
  qa_guest_field_layout *values =
      count > 0 ? (qa_guest_field_layout *)malloc(count * sizeof *values) : NULL;
  if (count > 0 && values == NULL) qa_save_reader_fail(&fields, "out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&fields, i);
    values[i].name = field_string(&entry, "name");
    values[i].byte_offset = field_integer_min(&entry, "byteOffset", 0);
    qa_save_reader storage = field(&entry, "storage");
    values[i].storage = read_storage(&storage);
    qa_save_reader_destroy(&storage);
    values[i].count = field_integer_min(&entry, "count", 0);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&fields);
  layout.fields = values;
  layout.field_count = count;
  return layout;
}
static qa_guest_value_layout read_value_layout(const qa_save_reader *reader) {
  static const char *const kinds[] = {"scalar", "aggregate"};
  const char *kind = field_choice(reader, "kind", kinds, 2);
  qa_guest_value_layout layout;
  memset(&layout, 0, sizeof layout);
  if (strcmp(kind, "scalar") == 0) {
    qa_save_reader storage = field(reader, "storage");
    layout.storage = read_storage(&storage);
    qa_save_reader_destroy(&storage);
  } else {
    layout.is_aggregate = true;
    qa_save_reader nested = field(reader, "layout");
    layout.layout = qa_read_layout(&nested);
    qa_save_reader_destroy(&nested);
  }
  return layout;
}
static qa_saved_guest_callback_reference read_callback_reference(const qa_save_reader *reader) {
  static const char *const kinds[] = {"typescript", "quakec", "qvm", "native-guest"};
  const char *kind = field_choice(reader, "kind", kinds, 4);
  qa_saved_guest_callback_reference saved;
  memset(&saved, 0, sizeof saved);
  if (strcmp(kind, "typescript") == 0) {
    saved.reference.kind = QA_GUEST_CALLBACK_TYPESCRIPT;
    saved.reference.provider = field_namespaced(reader, "provider");
    saved.reference.callback = field_namespaced(reader, "callback");
  } else if (strcmp(kind, "quakec") == 0) {
    saved.reference.kind = QA_GUEST_CALLBACK_QUAKEC;
    qa_save_reader module = field(reader, "module");
    saved.reference.module = qa_read_module(&module);
    qa_save_reader_destroy(&module);
    saved.reference.function_index = field_integer_min(reader, "functionIndex", 0);
  } else if (strcmp(kind, "qvm") == 0) {
    saved.reference.kind = QA_GUEST_CALLBACK_QVM;
    qa_save_reader module = field(reader, "module");
    saved.reference.module = qa_read_module(&module);
    qa_save_reader_destroy(&module);
    saved.reference.instruction_index = field_integer_min(reader, "instructionIndex", 0);
  } else {
    saved.reference.kind = QA_GUEST_CALLBACK_NATIVE_GUEST;
    qa_save_reader module = field(reader, "module");
    saved.reference.module = qa_read_module(&module);
    qa_save_reader_destroy(&module);
    saved.byte_offset = field_bigint(reader, "byteOffset");
    qa_save_reader abi = field(reader, "abi");
    saved.reference.abi = qa_read_native_call_abi(&abi);
    qa_save_reader_destroy(&abi);
  }
  return saved;
}
static qa_saved_guest_callback_binding read_callback(const qa_save_reader *reader) {
  qa_saved_guest_callback_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.id = field_namespaced(reader, "id");
  qa_save_reader reference = field(reader, "reference");
  binding.reference = read_callback_reference(&reference);
  qa_save_reader_destroy(&reference);
  qa_save_reader parameters = field(reader, "parameters");
  size_t count = qa_save_reader_list_count(&parameters);
  qa_guest_value_layout *values =
      count > 0 ? (qa_guest_value_layout *)malloc(count * sizeof *values) : NULL;
  if (count > 0 && values == NULL) qa_save_reader_fail(&parameters, "out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&parameters, i);
    values[i] = read_value_layout(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&parameters);
  binding.parameters = values;
  binding.parameter_count = count;
  qa_save_reader result = field(reader, "result");
  const qa_save_value *raw = result.value;
  if (raw != NULL && raw->kind == QA_SAVE_STRING && strcmp(raw->text, "void") == 0)
    binding.result_is_void = true;
  else
    binding.result = read_value_layout(&result);
  qa_save_reader_destroy(&result);
  return binding;
}
static qa_guest_private_state read_private(const qa_save_reader *reader) {
  qa_guest_private_state state;
  qa_save_reader module = field(reader, "module");
  state.module = qa_read_module(&module);
  qa_save_reader_destroy(&module);
  state.format = field_namespaced(reader, "format");
  state.bytes = field_bytes(reader, "bytes");
  return state;
}
static qa_guest_private_state field_private(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  qa_guest_private_state state = read_private(&f);
  qa_save_reader_destroy(&f);
  return state;
}
static qa_game_api_identity field_api(const qa_save_reader *reader) {
  qa_save_reader f = field(reader, "api");
  qa_game_api_identity api = qa_read_api(&f);
  qa_save_reader_destroy(&f);
  return api;
}

qa_guest_checkpoint qa_read_guest(const qa_save_reader *reader) {
  static const char *const kinds[] = {"typescript", "quakec", "qvm", "native-guest"};
  qa_guest_checkpoint checkpoint;
  memset(&checkpoint, 0, sizeof checkpoint);
  qa_save_reader module = field(reader, "module");
  checkpoint.module = qa_read_module(&module);
  qa_save_reader_destroy(&module);
  {
    qa_save_reader random = field(reader, "random");
    size_t count = qa_save_reader_list_count(&random);
    qa_random_state *values =
        count > 0 ? (qa_random_state *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&random, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&random, i);
      values[i] = qa_read_random(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&random);
    checkpoint.random = values;
    checkpoint.random_count = count;
  }
  {
    qa_save_reader callbacks = field(reader, "callbacks");
    size_t count = qa_save_reader_list_count(&callbacks);
    qa_saved_guest_callback_binding *values = count > 0
        ? (qa_saved_guest_callback_binding *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&callbacks, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&callbacks, i);
      values[i] = read_callback(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&callbacks);
    checkpoint.callbacks = values;
    checkpoint.callback_count = count;
  }
  const char *kind = field_choice(reader, "kind", kinds, 4);
  if (strcmp(kind, "typescript") == 0) {
    checkpoint.kind = QA_GUEST_CHECKPOINT_TYPESCRIPT;
    checkpoint.game_api = field_api(reader);
    checkpoint.state = field_private(reader, "state");
  } else if (strcmp(kind, "quakec") == 0) {
    checkpoint.kind = QA_GUEST_CHECKPOINT_QUAKEC;
    qa_game_api_identity api = field_api(reader);
    if (api.kind != QA_GAME_API_QUAKEC)
      qa_save_reader_fail(reader, "QuakeC checkpoint requires a Q1 API");
    checkpoint.quakec_api = api.as.quakec;
    checkpoint.globals = field_bytes(reader, "globals");
    checkpoint.entities = field_bytes(reader, "entities");
    checkpoint.entity_stride_bytes = field_integer_min(reader, "entityStrideBytes", 1);
    checkpoint.entity_count = field_integer_min(reader, "entityCount", 0);
    checkpoint.strings = field_bytes(reader, "strings");
    checkpoint.statement = field_integer(reader, "statement");
    checkpoint.function_index = field_integer_min(reader, "functionIndex", 0);
    checkpoint.argument_count = field_integer_min(reader, "argumentCount", 0);
    qa_save_reader stack = field(reader, "callStack");
    size_t count = qa_save_reader_list_count(&stack);
    qa_quakec_stack_frame *frames =
        count > 0 ? (qa_quakec_stack_frame *)malloc(count * sizeof *frames) : NULL;
    if (count > 0 && frames == NULL) qa_save_reader_fail(&stack, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&stack, i);
      frames[i].statement = field_integer(&entry, "statement");
      frames[i].function_index = field_integer_min(&entry, "functionIndex", 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&stack);
    checkpoint.call_stack = frames;
    checkpoint.call_stack_count = count;
    checkpoint.locals = field_bytes(reader, "locals");
    checkpoint.host_state = field_private(reader, "hostState");
  } else if (strcmp(kind, "qvm") == 0) {
    static const char *const profiles[] = {"q3-modern", "q3-1.16n-base"};
    checkpoint.kind = QA_GUEST_CHECKPOINT_QVM;
    qa_game_api_identity api = field_api(reader);
    if (api.kind != QA_GAME_API_Q3)
      qa_save_reader_fail(reader, "QVM checkpoint requires a Q3 API");
    checkpoint.q3_api = api.as.q3;
    qa_save_reader profile = field(reader, "abiProfile");
    checkpoint.has_abi_profile = true;
    checkpoint.abi_profile = qa_save_reader_is_undefined(&profile)
        ? "q3-modern" : qa_save_reader_choice_string(&profile, profiles, 2);
    qa_save_reader_destroy(&profile);
    checkpoint.data = field_bytes(reader, "data");
    checkpoint.instruction_index = field_integer(reader, "instructionIndex");
    checkpoint.program_stack = field_integer_min(reader, "programStack", 0);
    qa_save_reader operands = field(reader, "operandStack");
    size_t count = qa_save_reader_list_count(&operands);
    double *values = count > 0 ? (double *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&operands, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&operands, i);
      values[i] = qa_save_reader_integer(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&operands);
    checkpoint.operand_stack.values = values;
    checkpoint.operand_stack.count = count;
    checkpoint.host_state = field_private(reader, "hostState");
  } else {
    static const char *const permissions[] = {
        "read", "read-write", "read-execute", "read-write-execute"};
    checkpoint.kind = QA_GUEST_CHECKPOINT_NATIVE_GUEST;
    qa_save_reader abi = field(reader, "abi");
    checkpoint.abi = qa_read_native_abi(&abi);
    qa_save_reader_destroy(&abi);
    qa_save_reader regions = field(reader, "regions");
    size_t count = qa_save_reader_list_count(&regions);
    qa_guest_memory_region *values =
        count > 0 ? (qa_guest_memory_region *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&regions, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&regions, i);
      values[i].base = field_bigint(&entry, "base");
      values[i].permissions = field_choice(&entry, "permissions", permissions, 4);
      values[i].bytes = field_bytes(&entry, "bytes");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&regions);
    checkpoint.regions = values;
    checkpoint.region_count = count;
    qa_save_reader layout = field(reader, "processorLayout");
    checkpoint.processor_layout = qa_read_layout(&layout);
    qa_save_reader_destroy(&layout);
    checkpoint.processor_state = field_bytes(reader, "processorState");
    checkpoint.runtime_state = field_private(reader, "runtimeState");
  }
  return checkpoint;
}
