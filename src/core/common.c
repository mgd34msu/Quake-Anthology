#include "qa/common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <sys/sysinfo.h>
#endif

void qa_error_set(qa_error *error, qa_status code, size_t offset,
                  const char *format, ...)
{
    if (error == NULL) {
        return;
    }
    error->code = code;
    error->offset = offset;
    error->message[0] = '\0';
    if (format != NULL) {
        va_list args;
        va_start(args, format);
        int written = vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
        if (written < 0) {
            error->message[0] = '\0';
        }
    }
}

void qa_buffer_free(qa_buffer *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        *buffer = (qa_buffer){0};
    }
}

int32_t qa_memory_available(void)
{
    uint64_t available = 0;
#ifdef _WIN32
    MEMORYSTATUSEX status = {.dwLength = sizeof(status)};
    if (GlobalMemoryStatusEx(&status)) available = status.ullAvailPhys;
#elif defined(__APPLE__)
    mach_port_t host = mach_host_self();
    vm_size_t page = 0;
    vm_statistics64_data_t statistics;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_page_size(host, &page) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&statistics, &count) == KERN_SUCCESS)
        available = (uint64_t)statistics.free_count * (uint64_t)page;
    mach_port_deallocate(mach_task_self(), host);
#else
    struct sysinfo information;
    if (sysinfo(&information) == 0) available = (uint64_t)information.freeram * information.mem_unit;
#endif
    return available > INT32_MAX ? INT32_MAX : (int32_t)available;
}
