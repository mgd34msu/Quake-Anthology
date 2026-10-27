/*
 * Port of src/guest/runtime/system-v/iostream.ts: GNU libstdc++ standard
 * streams over the System V stdio FILEs. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_SYSTEM_V_IOSTREAM_H
#define QA_GUEST_RUNTIME_SYSTEM_V_IOSTREAM_H

#include "qa/guest/runtime/system_v/locale.h"
#include "qa/guest/runtime/system_v/stdio.h"

typedef struct qa_system_v_ios_layout {
  int size, precision, width, flags, exceptions, state, callbacks, local_words;
  int word_size, words, locale, tie, fill, fill_initialized, buffer, ctype, num_put, num_get;
} qa_system_v_ios_layout;
qa_system_v_ios_layout qa_system_v_ios_layout_for(int pointer_bytes, bool wide);

typedef struct qa_system_v_standard_stream {
  const char *name;
  qa_guest_address address;
  qa_guest_address ios;
  bool wide, input;
} qa_system_v_standard_stream;

typedef struct qa_system_v_iostreams {
  qa_system_v_service_host *host;
  qa_system_v_stdio stdio;
  qa_cxx_abi_data abi;
  qa_system_v_standard_stream streams[8];
  size_t stream_count;
  qa_guest_address refcount;
  qa_guest_address synchronized;
  qa_system_v_classic_locale *locale; /* owned, NULL before construction */
} qa_system_v_iostreams;

void qa_system_v_iostreams_init(qa_system_v_iostreams *streams, qa_system_v_service_host *host);
void qa_system_v_iostreams_destroy(qa_system_v_iostreams *streams);
void qa_system_v_iostreams_clear(qa_system_v_iostreams *streams, qa_guest_address ios, bool wide,
                                 int state, const qa_guest_call_context *context);
void qa_system_v_iostreams_flush(qa_system_v_iostreams *streams, qa_guest_address stream,
                                 bool wide, const qa_guest_call_context *context);

#endif
