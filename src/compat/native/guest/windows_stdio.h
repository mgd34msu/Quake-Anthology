#ifndef QA_NATIVE_GUEST_WINDOWS_STDIO_H
#define QA_NATIVE_GUEST_WINDOWS_STDIO_H
#include "windows_runtime.h"

enum windows_stdio_operation {
    WST_OPEN = 0x1000, WST_CLOSE, WST_READ, WST_WRITE, WST_SEEK, WST_TELL,
    WST_SEEK64, WST_TELL64, WST_FLUSH, WST_EOF, WST_ERROR, WST_CLEAR,
    WST_REWIND, WST_GET, WST_PUT, WST_UNGET, WST_FILENO
};
enum { WST_FLAG_READ = 1, WST_FLAG_WRITE = 2, WST_FLAG_UNBUFFERED = 4,
    WST_FLAG_EOF = 16, WST_FLAG_ERROR = 32, WST_FLAG_UPDATE = 128 };
typedef struct windows_stdio_file {
    uint64_t address, handle, capability;
    uint32_t mode, descriptor, flags;
    uint8_t pushback, pending;
    bool legacy, binary, append, closing, has_pushback, has_pending;
} windows_stdio_file;
typedef struct windows_stdio_pending {
    guest_runtime_file_capability capability;
    char *name;
    uint32_t creation;
    bool opened;
} windows_stdio_pending;

bool windows_stdio_invoke(windows_service *, const qa_native_value *, size_t,
    qa_native_value *, qa_error *);
bool windows_stdio_file_in_use(const guest_windows *, uint64_t);
/* Retirement closes real capabilities without touching potentially terminal
 * guest RAM. FILE identities remain until the runtime owner is disposed. */
bool windows_stdio_close_files(guest_windows *, qa_error *);
bool windows_stdio_valid(const guest_windows *, qa_error *);
bool windows_stdio_lower_valid(guest_windows *, qa_error *);
#endif
