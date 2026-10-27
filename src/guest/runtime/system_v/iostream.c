/* Port of src/guest/runtime/system-v/iostream.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/system_v/iostream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/guest/runtime/common/memory.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Guest runtime out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  char *copy = xmalloc(strlen(text) + 1);
  strcpy(copy, text);
  return copy;
}
static void plain_error(const char *message) { qa_range_error("%s", message); }
static void type_error(const char *message) { qa_range_error("%s", message); }
static qa_guest_address slot(qa_system_v_iostreams *s, qa_guest_address a, int64_t o) {
  return qa_cxx_abi_slot(&s->abi, a, o);
}
static void write_ptr(qa_system_v_iostreams *s, qa_guest_address a, bool has, qa_guest_address v) {
  qa_gmem_write_pointer(s->host->memory, a, has, v);
}
static bool read_ptr(qa_system_v_iostreams *s, qa_guest_address a, qa_guest_address *out) {
  return qa_gmem_read_pointer(s->host->memory, a, out);
}

qa_system_v_ios_layout qa_system_v_ios_layout_for(int pointer_bytes, bool wide) {
  if (pointer_bytes == 8) {
    qa_system_v_ios_layout layout = {264, 8, 16, 24, 28, 32, 40, 64, 192, 200,
                                     208, 216, 224, wide ? 228 : 225, 232, 240, 248, 256};
    return layout;
  }
  qa_system_v_ios_layout layout = {wide ? 140 : 136, 4, 8, 12, 16, 20, 24, 36, 100, 104,
                                   108, 112, 116, wide ? 120 : 117, wide ? 124 : 120,
                                   wide ? 128 : 124, wide ? 132 : 128, wide ? 136 : 132};
  return layout;
}

static void initialize(qa_system_v_iostreams *streams);
static qa_guest_address make_buffer(qa_system_v_iostreams *streams, qa_guest_address file,
                                    bool wide, qa_system_v_classic_locale *locale);
static void construct(qa_system_v_iostreams *streams, const qa_system_v_standard_stream *stream,
                      qa_guest_address buffer, qa_system_v_classic_locale *locale);
static qa_guest_address ios_of(qa_system_v_iostreams *streams, qa_guest_address stream);

typedef struct ios_state {
  qa_system_v_iostreams *streams;
  int operation; /* 0 init-ctor, 1 init-dtor, 2 clear, 3 flush */
  bool wide;
} ios_state;
static qa_call_result ios_impl(void *ctx, const qa_guest_call_context *context,
                               const qa_call_value *args, size_t count) {
  ios_state *state = ctx;
  qa_system_v_iostreams *streams = state->streams;
  qa_mapped_guest_memory *m = streams->host->memory;
  switch (state->operation) {
    case 0: initialize(streams); return qa_call_void();
    case 1: {
      int32_t previous = qa_gmem_read_i32(m, streams->refcount);
      qa_gmem_write_i32(m, streams->refcount, previous - 1);
      if (previous == 2)
        for (size_t i = 0; i < streams->stream_count; i++)
          if (!streams->streams[i].input)
            qa_system_v_iostreams_flush(streams, streams->streams[i].address,
                                        streams->streams[i].wide, context);
      return qa_call_void();
    }
    case 2:
      qa_system_v_iostreams_clear(streams, qa_guest_required_pointer(args, count, 0),
                                  state->wide, (int)qa_guest_integer(args, count, 1), context);
      return qa_call_void();
    case 3: {
      qa_guest_address stream = qa_guest_required_pointer(args, count, 0);
      qa_system_v_iostreams_flush(streams, stream, state->wide, context);
      return qa_call_pointer(true, stream);
    }
    default: break;
  }
  return qa_call_void();
}
static qa_guest_address abi_function(qa_system_v_iostreams *streams, const char *name,
                                     const qa_abi_storage *parameters, size_t parameter_count,
                                     bool result_is_void, qa_abi_storage result, int operation,
                                     bool wide) {
  ios_state *state = xmalloc(sizeof *state);
  state->streams = streams; state->operation = operation; state->wide = wide;
  qa_system_v_implementation impl = {state, ios_impl};
  return qa_cxx_abi_function(&streams->abi, name, parameters, parameter_count, result_is_void,
                             result, impl, NULL);
}

void qa_system_v_iostreams_init(qa_system_v_iostreams *streams, qa_system_v_service_host *host) {
  memset(streams, 0, sizeof *streams);
  streams->host = host;
  qa_mapped_guest_memory *m = host->memory;
  int p = m->pointer_bytes;
  qa_cxx_abi_data_init(&streams->abi, host);
  qa_system_v_stdio_init(&streams->stdio, host);
  streams->locale = xmalloc(sizeof *streams->locale);
  qa_system_v_classic_locale_init(streams->locale, &streams->abi);
  streams->refcount = qa_cxx_abi_define_data(&streams->abi, "_ZNSt8ios_base4Init11_S_refcountE", 4, NULL);
  streams->synchronized = qa_cxx_abi_define_data(&streams->abi,
                                          "_ZNSt8ios_base4Init20_S_synced_with_stdioE", 1, NULL);
  qa_memory_write_u8(m, streams->synchronized, 1);
  static const char *const names[] = {"cin", "cout", "cerr", "clog",
                                      "wcin", "wcout", "wcerr", "wclog"};
  streams->stream_count = 8;
  for (size_t i = 0; i < 8; i++) {
    bool wide = names[i][0] == 'w';
    bool input = strcmp(names[i] + strlen(names[i]) - 3, "cin") == 0;
    int prefix = (input ? 2 : 1) * p;
    char *data_name = xmalloc(strlen(names[i]) + 8);
    (void)snprintf(data_name, strlen(names[i]) + 8, "_ZSt%zu%s", strlen(names[i]), names[i]);
    qa_guest_address address = qa_cxx_abi_define_data(
        &streams->abi, data_name,
        (size_t)prefix + (size_t)qa_system_v_ios_layout_for(p, wide).size, NULL);
    free(data_name);
    streams->streams[i].name = names[i];
    streams->streams[i].wide = wide;
    streams->streams[i].input = input;
    streams->streams[i].address = address;
    streams->streams[i].ios = slot(streams, address, prefix);
  }
  qa_abi_storage ptr = QA_STORAGE_POINTER, i32 = QA_STORAGE_INT32;
  const char *ctors[] = {"_ZNSt8ios_base4InitC1Ev", "_ZNSt8ios_base4InitC2Ev"};
  for (size_t i = 0; i < 2; i++)
    (void)abi_function(streams, ctors[i], &ptr, 1, true, i32, 0, false);
  const char *dtors[] = {"_ZNSt8ios_base4InitD1Ev", "_ZNSt8ios_base4InitD2Ev"};
  for (size_t i = 0; i < 2; i++)
    (void)abi_function(streams, dtors[i], &ptr, 1, true, i32, 1, false);
  for (int w = 0; w < 2; w++) {
    bool wide = w == 1;
    char c = wide ? 'w' : 'c';
    char clear_name[96];
    (void)snprintf(clear_name, sizeof clear_name,
                   "_ZNSt9basic_iosI%cSt11char_traitsI%cEE5clearESt12_Ios_Iostate", c, c);
    qa_abi_storage clear_params[] = {ptr, i32};
    (void)abi_function(streams, xstrdup(clear_name), clear_params, 2, true, i32, 2, wide);
    char flush_name[96];
    (void)snprintf(flush_name, sizeof flush_name, "_ZNS%s5flushEv",
                   wide ? "t13basic_ostreamIwSt11char_traitsIwEE" : "o");
    (void)abi_function(streams, xstrdup(flush_name), &ptr, 1, false, ptr, 3, wide);
  }
}
void qa_system_v_iostreams_destroy(qa_system_v_iostreams *streams) {
  qa_cxx_abi_data_destroy(&streams->abi);
  free(streams->locale);
  streams->locale = NULL;
}

static void initialize(qa_system_v_iostreams *streams) {
  qa_mapped_guest_memory *m = streams->host->memory;
  int32_t previous = qa_gmem_read_i32(m, streams->refcount);
  qa_gmem_write_i32(m, streams->refcount, previous + 1);
  if (previous != 0) return;
  qa_memory_write_u8(m, streams->synchronized, 1);
  qa_system_v_classic_locale *locale = streams->locale;
  if (locale == NULL) plain_error("Classic locale was not constructed");
  for (int w = 0; w < 2; w++) {
    bool wide = w == 1;
    qa_guest_address input_buffer = make_buffer(streams, streams->stdio.stdin_, wide, locale);
    qa_guest_address output_buffer = make_buffer(streams, streams->stdio.stdout_, wide, locale);
    qa_guest_address error_buffer = make_buffer(streams, streams->stdio.stderr_, wide, locale);
    for (size_t i = 0; i < streams->stream_count; i++) {
      const qa_system_v_standard_stream *stream = &streams->streams[i];
      if (stream->wide != wide) continue;
      bool is_cout = strcmp(stream->name + strlen(stream->name) - 4, "cout") == 0;
      construct(streams, stream,
                stream->input ? input_buffer : is_cout ? output_buffer : error_buffer, locale);
    }
    const qa_system_v_standard_stream *output = NULL;
    for (size_t i = 0; i < streams->stream_count; i++)
      if (streams->streams[i].wide == wide &&
          strcmp(streams->streams[i].name + strlen(streams->streams[i].name) - 4, "cout") == 0)
        output = &streams->streams[i];
    if (output == NULL) plain_error("Missing standard output stream");
    for (size_t i = 0; i < streams->stream_count; i++) {
      const qa_system_v_standard_stream *stream = &streams->streams[i];
      if (stream->wide != wide) continue;
      bool is_cerr = strcmp(stream->name + strlen(stream->name) - 4, "cerr") == 0;
      if (!stream->input && !is_cerr) continue;
      qa_system_v_ios_layout layout = qa_system_v_ios_layout_for(m->pointer_bytes, wide);
      write_ptr(streams, slot(streams, stream->ios, layout.tie), true, output->address);
      if (!stream->input)
        qa_memory_write_u32(m, slot(streams, stream->ios, layout.flags), 0x3002);
    }
  }
  qa_gmem_write_i32(m, streams->refcount, qa_gmem_read_i32(m, streams->refcount) + 1);
}

static void construct(qa_system_v_iostreams *streams, const qa_system_v_standard_stream *stream,
                      qa_guest_address buffer, qa_system_v_classic_locale *locale) {
  qa_cxx_abi_data *abi = &streams->abi;
  qa_mapped_guest_memory *m = streams->host->memory;
  int p = m->pointer_bytes;
  qa_system_v_ios_layout layout = qa_system_v_ios_layout_for(p, stream->wide);
  qa_guest_address ios = stream->ios;
  char c = stream->wide ? 'w' : 'c';
  qa_guest_address ios_base = qa_cxx_abi_type(abi, "St8ios_base", NULL, 0);
  char basic_ios_name[80];
  (void)snprintf(basic_ios_name, sizeof basic_ios_name, "St9basic_iosI%cSt11char_traitsI%cEE",
                 c, c);
  qa_cxx_rtti_base basic_base = {ios_base, 0, 2};
  qa_guest_address basic_ios = qa_cxx_abi_type(abi, xstrdup(basic_ios_name), &basic_base, 1);
  char name_buffer[96];
  const char *name;
  if (stream->wide) {
    (void)snprintf(name_buffer, sizeof name_buffer, "St13basic_%sstreamIwSt11char_traitsIwEE",
                   stream->input ? "i" : "o");
    name = xstrdup(name_buffer);
  } else {
    name = stream->input ? "Si" : "So";
  }
  qa_cxx_rtti_base type_base = {basic_ios, -3 * p, 3};
  qa_guest_address type = qa_cxx_abi_type(abi, name, &type_base, 1);
  int prefix = (stream->input ? 2 : 1) * p;
  /* Primary address point: virtual-base offset, offset-to-top, RTTI, then two destructors. */
  char *table_name = xmalloc(strlen(name) + 8);
  (void)snprintf(table_name, strlen(name) + 8, "_ZTV%s", name);
  qa_guest_address table = {0};
  bool has_table = qa_system_v_resolve_address(streams->host, "libstdc++.so.6", table_name,
                                               "GLIBCXX_3.4", &table);
  if (!has_table) {
    table = qa_cxx_abi_define_data(abi, table_name, (size_t)(10 * p), NULL);
    qa_write_unsigned(m, table, p, (uint64_t)prefix);
    write_ptr(streams, slot(streams, table, 2 * p), true, type);
    char *unsupported_name = xmalloc(strlen(name) + 32);
    (void)snprintf(unsupported_name, strlen(name) + 32, "__guest_%s_destructor", name);
    write_ptr(streams, slot(streams, table, 3 * p), true,
              qa_cxx_abi_unsupported(abi, unsupported_name));
    char *deleting_name = xmalloc(strlen(name) + 40);
    (void)snprintf(deleting_name, strlen(name) + 40, "__guest_%s_deleting_destructor", name);
    write_ptr(streams, slot(streams, table, 4 * p), true,
              qa_cxx_abi_unsupported(abi, deleting_name));
    qa_write_unsigned(m, slot(streams, table, 5 * p), p, (uint64_t)(-(int64_t)prefix));
    qa_write_unsigned(m, slot(streams, table, 6 * p), p, (uint64_t)(-(int64_t)prefix));
    write_ptr(streams, slot(streams, table, 7 * p), true, type);
    char *virtual_name = xmalloc(strlen(name) + 40);
    (void)snprintf(virtual_name, strlen(name) + 40, "__guest_%s_virtual_destructor", name);
    write_ptr(streams, slot(streams, table, 8 * p), true,
              qa_cxx_abi_unsupported(abi, virtual_name));
    char *virtual_deleting = xmalloc(strlen(name) + 48);
    (void)snprintf(virtual_deleting, strlen(name) + 48,
                   "__guest_%s_virtual_deleting_destructor", name);
    write_ptr(streams, slot(streams, table, 9 * p), true,
              qa_cxx_abi_unsupported(abi, virtual_deleting));
  }
  free(table_name);
  write_ptr(streams, stream->address, true, slot(streams, table, 3 * p));
  write_ptr(streams, ios, true, slot(streams, table, 8 * p));
  qa_write_unsigned(m, slot(streams, ios, layout.precision), p, 6);
  qa_memory_write_u32(m, slot(streams, ios, layout.flags), 0x1002);
  qa_gmem_write_i32(m, slot(streams, ios, layout.word_size), 8);
  write_ptr(streams, slot(streams, ios, layout.words), true,
            slot(streams, ios, layout.local_words));
  write_ptr(streams, slot(streams, ios, layout.locale), true,
            qa_system_v_classic_locale_retain(locale));
  write_ptr(streams, slot(streams, ios, layout.buffer), true, buffer);
  int index = stream->wide ? 1 : 0;
  write_ptr(streams, slot(streams, ios, layout.ctype), true, locale->ctype[index]);
  write_ptr(streams, slot(streams, ios, layout.num_put), true, locale->num_put[index]);
  write_ptr(streams, slot(streams, ios, layout.num_get), true, locale->num_get[index]);
}

/* streambuf virtual closures. */
typedef struct buf_state {
  qa_system_v_iostreams *streams;
  bool wide;
  int operation; /* 0 imbue, 1 setbuf, 2 sync, 3 showmanyc, 4 xsgetn, 5 underflow,
                    6 uflow, 7 pbackfail, 8 xsputn, 9 overflow */
} buf_state;
static qa_guest_address stream_file(qa_system_v_iostreams *streams, qa_guest_address buffer) {
  qa_mapped_guest_memory *m = streams->host->memory;
  qa_guest_address file = {0};
  if (!read_ptr(streams, slot(streams, buffer, 8 * m->pointer_bytes), &file))
    plain_error("Missing synchronized FILE");
  return file;
}
static qa_call_result buf_result(buf_state *state, int value) {
  return state->wide ? qa_call_uint32((uint32_t)value) : qa_call_int32(value);
}
static qa_call_result signed_result(qa_system_v_iostreams *streams, size_t value) {
  return streams->host->memory->pointer_bytes == 4 ? qa_call_int32((int32_t)value)
                                                    : qa_call_int64((int64_t)value);
}
static qa_call_result buf_impl(void *ctx, const qa_guest_call_context *context,
                               const qa_call_value *args, size_t count) {
  buf_state *state = ctx;
  qa_system_v_iostreams *streams = state->streams;
  qa_mapped_guest_memory *m = streams->host->memory;
  int p = m->pointer_bytes;
  bool wide = state->wide;
  switch (state->operation) {
    case 0: return qa_call_void(); /* imbue: leave state unchanged */
    case 1: return qa_call_pointer(true, qa_guest_required_pointer(args, count, 0));
    case 2:
      return qa_call_int32(qa_system_v_stdio_flush(
          &streams->stdio, stream_file(streams, qa_guest_required_pointer(args, count, 0)),
          context));
    case 3: return signed_result(streams, 0);
    case 4: {
      qa_guest_address buffer = qa_guest_required_pointer(args, count, 0);
      qa_guest_address destination = qa_guest_required_pointer(args, count, 1);
      int64_t requested = qa_guest_integer(args, count, 2);
      size_t read_count = 0;
      int last = -1;
      while ((int64_t)read_count < requested) {
        last = qa_system_v_stdio_get(&streams->stdio, stream_file(streams, buffer), wide,
                                      context);
        if (last < 0) break;
        qa_write_unsigned(m, slot(streams, destination, (int64_t)read_count * (wide ? 4 : 1)),
                          wide ? 4 : 1, (uint64_t)(uint32_t)last);
        read_count++;
      }
      uint32_t stored = read_count > 0
          ? (wide ? qa_memory_read_u32(m, slot(streams, destination,
                                               (int64_t)(read_count - 1) * 4))
                  : qa_memory_read_u8(m, slot(streams, destination, (int64_t)(read_count - 1))))
          : 0xffffffffu;
      qa_memory_write_u32(m, slot(streams, buffer, 9 * p), stored);
      return signed_result(streams, read_count);
    }
    case 5: {
      qa_guest_address file = stream_file(streams, qa_guest_required_pointer(args, count, 0));
      int value = qa_system_v_stdio_get(&streams->stdio, file, wide, context);
      return buf_result(state, qa_system_v_stdio_unget(&streams->stdio, file, value, wide));
    }
    case 6: {
      qa_guest_address buffer = qa_guest_required_pointer(args, count, 0);
      int value = qa_system_v_stdio_get(&streams->stdio, stream_file(streams, buffer), wide,
                                         context);
      qa_memory_write_u32(m, slot(streams, buffer, 9 * p), (uint32_t)value);
      return buf_result(state, value);
    }
    case 7: {
      qa_guest_address buffer = qa_guest_required_pointer(args, count, 0);
      int64_t supplied = qa_guest_integer(args, count, 1);
      int value = supplied == -1 || supplied == 0xffffffffLL
          ? qa_gmem_read_i32(m, slot(streams, buffer, 9 * p)) : (int)supplied;
      int returned = qa_system_v_stdio_unget(&streams->stdio, stream_file(streams, buffer),
                                              value, wide);
      qa_gmem_write_i32(m, slot(streams, buffer, 9 * p), -1);
      return buf_result(state, returned);
    }
    case 8: {
      qa_guest_address file = stream_file(streams, qa_guest_required_pointer(args, count, 0));
      qa_guest_address source = qa_guest_required_pointer(args, count, 1);
      int64_t requested = qa_guest_integer(args, count, 2);
      size_t written = 0;
      for (; (int64_t)written < requested; written++) {
        int value = wide ? (int)qa_memory_read_u32(m, slot(streams, source, (int64_t)written * 4))
                         : qa_memory_read_u8(m, slot(streams, source, (int64_t)written));
        if (qa_system_v_stdio_put(&streams->stdio, file, value, wide, context) < 0) break;
      }
      return signed_result(streams, written);
    }
    case 9: {
      qa_guest_address file = stream_file(streams, qa_guest_required_pointer(args, count, 0));
      int64_t value = qa_guest_integer(args, count, 1);
      return buf_result(state, value == -1 || value == 0xffffffffLL
          ? (qa_system_v_stdio_flush(&streams->stdio, file, context) == 0 ? 0 : -1)
          : qa_system_v_stdio_put(&streams->stdio, file, (int)value, wide, context));
    }
    default: break;
  }
  return qa_call_void();
}
static qa_guest_address buf_function(qa_system_v_iostreams *streams, const char *name,
                                     const qa_abi_storage *parameters, size_t parameter_count,
                                     bool result_is_void, qa_abi_storage result, bool wide,
                                     int operation) {
  buf_state *state = xmalloc(sizeof *state);
  state->streams = streams; state->wide = wide; state->operation = operation;
  qa_system_v_implementation impl = {state, buf_impl};
  return qa_cxx_abi_function(&streams->abi, name, parameters, parameter_count, result_is_void,
                             result, impl, NULL);
}

static qa_guest_address make_buffer(qa_system_v_iostreams *streams, qa_guest_address file,
                                    bool wide, qa_system_v_classic_locale *locale) {
  qa_cxx_abi_data *abi = &streams->abi;
  qa_mapped_guest_memory *m = streams->host->memory;
  int p = m->pointer_bytes;
  char c = wide ? 'w' : 'c';
  char name_buffer[96];
  (void)snprintf(name_buffer, sizeof name_buffer,
                 "N9__gnu_cxx18stdio_sync_filebufI%cSt11char_traitsI%cEEE", c, c);
  char *name = xstrdup(name_buffer);
  qa_guest_address address = qa_system_v_allocate(streams->host, (size_t)(10 * p));
  char base_name[80];
  (void)snprintf(base_name, sizeof base_name, "St15basic_streambufI%cSt11char_traitsI%cEE", c, c);
  qa_guest_address base_type = qa_cxx_abi_type(abi, xstrdup(base_name), NULL, 0);
  qa_cxx_rtti_base base = {base_type, 0, 2};
  qa_guest_address type = qa_cxx_abi_type(abi, name, &base, 1);
  char *table_name = xmalloc(strlen(name) + 8);
  (void)snprintf(table_name, strlen(name) + 8, "_ZTV%s", name);
  qa_guest_address table = {0};
  bool has_table = qa_system_v_resolve_address(streams->host, "libstdc++.so.6", table_name,
                                               "GLIBCXX_3.4", &table);
  if (!has_table) {
    qa_abi_storage signed_storage = streams->host->signed_pointer_storage;
    qa_abi_storage character = wide ? QA_STORAGE_UINT32 : QA_STORAGE_INT32;
    qa_abi_storage ptr = QA_STORAGE_POINTER;
    qa_guest_address entries[14];
    char *fn = xmalloc(strlen(name) + 40);
    (void)snprintf(fn, strlen(name) + 40, "__guest_%s_destructor", name);
    entries[0] = qa_cxx_abi_unsupported(abi, fn);
    fn = xmalloc(strlen(name) + 48);
    (void)snprintf(fn, strlen(name) + 48, "__guest_%s_deleting_destructor", name);
    entries[1] = qa_cxx_abi_unsupported(abi, fn);
    /* These two inherited basic_streambuf virtual methods intentionally
     * leave its state unchanged (TS comment). */
    qa_abi_storage two_ptrs[] = {ptr, ptr};
    fn = xmalloc(strlen(name) + 24);
    (void)snprintf(fn, strlen(name) + 24, "__guest_%s_imbue", name);
    entries[2] = buf_function(streams, fn, two_ptrs, 2, true, QA_STORAGE_INT32, wide, 0);
    qa_abi_storage setbuf_params[] = {ptr, ptr, signed_storage};
    fn = xmalloc(strlen(name) + 24);
    (void)snprintf(fn, strlen(name) + 24, "__guest_%s_setbuf", name);
    entries[3] = buf_function(streams, fn, setbuf_params, 3, false, ptr, wide, 1);
    fn = xmalloc(strlen(name) + 24);
    (void)snprintf(fn, strlen(name) + 24, "__guest_%s_seekoff", name);
    entries[4] = qa_cxx_abi_unsupported(abi, fn);
    fn = xmalloc(strlen(name) + 24);
    (void)snprintf(fn, strlen(name) + 24, "__guest_%s_seekpos", name);
    entries[5] = qa_cxx_abi_unsupported(abi, fn);
    fn = xmalloc(strlen(name) + 24);
    (void)snprintf(fn, strlen(name) + 24, "__guest_%s_sync", name);
    entries[6] = buf_function(streams, fn, &ptr, 1, false, QA_STORAGE_INT32, wide, 2);
    fn = xmalloc(strlen(name) + 32);
    (void)snprintf(fn, strlen(name) + 32, "__guest_%s_showmanyc", name);
    entries[7] = buf_function(streams, fn, &ptr, 1, false, signed_storage, wide, 3);
    qa_abi_storage transfer_params[] = {ptr, ptr, signed_storage};
    fn = xmalloc(strlen(name) + 24);
    (void)snprintf(fn, strlen(name) + 24, "__guest_%s_xsgetn", name);
    entries[8] = buf_function(streams, fn, transfer_params, 3, false, signed_storage, wide, 4);
    fn = xmalloc(strlen(name) + 32);
    (void)snprintf(fn, strlen(name) + 32, "__guest_%s_underflow", name);
    entries[9] = buf_function(streams, fn, &ptr, 1, false, character, wide, 5);
    fn = xmalloc(strlen(name) + 24);
    (void)snprintf(fn, strlen(name) + 24, "__guest_%s_uflow", name);
    entries[10] = buf_function(streams, fn, &ptr, 1, false, character, wide, 6);
    qa_abi_storage char_params[] = {ptr, character};
    fn = xmalloc(strlen(name) + 32);
    (void)snprintf(fn, strlen(name) + 32, "__guest_%s_pbackfail", name);
    entries[11] = buf_function(streams, fn, char_params, 2, false, character, wide, 7);
    fn = xmalloc(strlen(name) + 24);
    (void)snprintf(fn, strlen(name) + 24, "__guest_%s_xsputn", name);
    entries[12] = buf_function(streams, fn, transfer_params, 3, false, signed_storage, wide, 8);
    fn = xmalloc(strlen(name) + 32);
    (void)snprintf(fn, strlen(name) + 32, "__guest_%s_overflow", name);
    entries[13] = buf_function(streams, fn, char_params, 2, false, character, wide, 9);
    (void)qa_cxx_abi_vtable(abi, name, type, entries, 14);
    if (!qa_system_v_resolve_address(streams->host, "libstdc++.so.6", table_name,
                                     "GLIBCXX_3.4", &table))
      plain_error("Missing constructed streambuf vtable");
  }
  free(table_name);
  write_ptr(streams, address, true, slot(streams, table, 2 * p));
  write_ptr(streams, slot(streams, address, 7 * p), true,
            qa_system_v_classic_locale_retain(locale));
  write_ptr(streams, slot(streams, address, 8 * p), true, file);
  qa_gmem_write_i32(m, slot(streams, address, 9 * p), -1);
  return address;
}

static qa_guest_address ios_of(qa_system_v_iostreams *streams, qa_guest_address stream) {
  qa_mapped_guest_memory *m = streams->host->memory;
  qa_guest_address vptr = {0};
  if (!read_ptr(streams, stream, &vptr)) type_error("Unconstructed guest stream");
  int64_t offset = m->pointer_bytes == 4
      ? qa_gmem_read_i32(m, slot(streams, vptr, -3 * m->pointer_bytes))
      : qa_gmem_read_i64(m, slot(streams, vptr, -3 * m->pointer_bytes));
  return qa_memory_offset(m, stream, offset);
}
void qa_system_v_iostreams_clear(qa_system_v_iostreams *streams, qa_guest_address ios, bool wide,
                                 int state, const qa_guest_call_context *context) {
  (void)context;
  qa_mapped_guest_memory *m = streams->host->memory;
  qa_system_v_ios_layout layout = qa_system_v_ios_layout_for(m->pointer_bytes, wide);
  qa_guest_address buffer = {0};
  if (!read_ptr(streams, slot(streams, ios, layout.buffer), &buffer)) state |= 1;
  qa_memory_write_u32(m, slot(streams, ios, layout.state), (uint32_t)state);
  if (((uint32_t)state & qa_memory_read_u32(m, slot(streams, ios, layout.exceptions))) != 0)
    qa_unsupported_system_v_service("libstdc++.so.6", "__throw_ios_failure", "GLIBCXX_3.4",
                                    "guest iostream exception propagation is not implemented");
}
void qa_system_v_iostreams_flush(qa_system_v_iostreams *streams, qa_guest_address stream,
                                 bool wide, const qa_guest_call_context *context) {
  qa_mapped_guest_memory *m = streams->host->memory;
  int p = m->pointer_bytes;
  qa_system_v_ios_layout layout = qa_system_v_ios_layout_for(p, wide);
  qa_guest_address ios = ios_of(streams, stream);
  if (qa_memory_read_u32(m, slot(streams, ios, layout.state)) != 0) return;
  qa_guest_address tied = {0};
  if (read_ptr(streams, slot(streams, ios, layout.tie), &tied))
    qa_system_v_iostreams_flush(streams, tied, wide, context);
  qa_guest_address buffer = {0};
  if (!read_ptr(streams, slot(streams, ios, layout.buffer), &buffer)) return;
  qa_guest_address vptr = {0};
  if (!read_ptr(streams, buffer, &vptr)) plain_error("Stream buffer has no vtable");
  qa_guest_address sync = {0};
  if (!read_ptr(streams, slot(streams, vptr, 6 * p), &sync))
    plain_error("Stream buffer has no sync method");
  for (int round = 0; round < 2; round++) {
    if (round == 1 && (qa_memory_read_u32(m, slot(streams, ios, layout.flags)) & 0x2000u) == 0)
      break;
    qa_abi_storage params[] = {QA_STORAGE_POINTER};
    qa_call_value invoke_args[] = {qa_call_pointer(true, buffer)};
    qa_call_result result = qa_system_v_invoke(streams->host, context, sync, params, 1, false,
                                               QA_STORAGE_INT32, invoke_args, 1);
    if (result.kind != QA_CALL_INT32) type_error("Stream buffer sync returned wrong type");
    if (result.number == -1)
      qa_system_v_iostreams_clear(
          streams, ios, wide,
          (int)(qa_memory_read_u32(m, slot(streams, ios, layout.state)) | 1u), context);
  }
}
