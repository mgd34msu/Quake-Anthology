#include "tools_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

static bool equal(const char *a, const char *b) {
    while (*a && *b) { unsigned x = (unsigned char)*a++, y = (unsigned char)*b++; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return false; }
    return *a == *b;
}
static bool memory(qa_tools *tools, const qa_command_context *source, qa_error *error) {
    uint64_t resident = 0, virtual_bytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX info = {0}; info.cb = sizeof info;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&info, sizeof info)) return tools_fail(error, "could not query native process memory");
    resident = (uint64_t)info.WorkingSetSize; virtual_bytes = (uint64_t)info.PrivateUsage;
#elif defined(__APPLE__)
    struct mach_task_basic_info info; mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return tools_fail(error, "could not query native process memory");
    resident = info.resident_size; virtual_bytes = info.virtual_size;
#else
    FILE *file = fopen("/proc/self/statm", "r"); uint64_t pages = 0, resident_pages = 0; long page = sysconf(_SC_PAGESIZE);
    if (!file) return tools_fail(error, "native process memory counters unavailable on this platform");
    bool read = fscanf(file, "%" SCNu64 " %" SCNu64, &pages, &resident_pages) == 2; fclose(file);
    if (!read || page <= 0 || pages > UINT64_MAX / (uint64_t)page || resident_pages > UINT64_MAX / (uint64_t)page) return tools_fail(error, "could not query native process memory");
    resident = resident_pages * (uint64_t)page; virtual_bytes = pages * (uint64_t)page;
#endif
    char text[192];
#ifdef _WIN32
    (void)snprintf(text, sizeof text, "Native process bytes: rss=%" PRIu64 " private=%" PRIu64 "\n", resident, virtual_bytes);
#else
    (void)snprintf(text, sizeof text, "Native process bytes: rss=%" PRIu64 " virtual=%" PRIu64 "\n", resident, virtual_bytes);
#endif
    tools_print(tools, source, text); return true;
}
bool tools_diagnostic_command(qa_tools *tools, const qa_command_invocation *call, qa_error *error) {
    const char *name = call->argv[0];
    if (equal(name, "dir")) {
        if (call->argc > 3) return tools_fail(error, "usage: dir [path] [extension]");
        const char *path = call->argc > 1 ? call->argv[1] : ""; if (!strcmp(path, ".")) path = "";
        qa_vfs_listing listing = {0};
        qa_vfs *files; qa_mount_id mount;
        if (!tools_files(tools, &call->context, &files, &mount, error) || !qa_vfs_list(files, path, call->argc > 2 ? call->argv[2] : "", &listing, error)) return false;
        for (size_t i = 0; i < listing.count; ++i) { tools_print(tools, &call->context, listing.names[i]); tools_print(tools, &call->context, "\n"); }
        char text[96]; (void)snprintf(text, sizeof text, "%zu files\n", listing.count); tools_print(tools, &call->context, text); qa_vfs_listing_free(&listing); return true;
    }
    if (equal(name, "touchFile")) {
        if (call->argc != 2) return tools_fail(error, "usage: touchFile <file>");
        qa_vfs *files; qa_mount_id mount; qa_resource *resource = NULL;
        bool ok = tools_files(tools, &call->context, &files, &mount, error) && qa_vfs_acquire(files, call->argv[1], &resource, NULL, error); qa_resource_release(resource); return ok;
    }
    if (equal(name, "meminfo")) { if (call->argc != 1) return tools_fail(error, "usage: meminfo"); return memory(tools, &call->context, error); }
    if (equal(name, "shaderlist")) { if (call->argc > 2) return tools_fail(error, "usage: shaderlist [sorted]"); }
    else if (call->argc != 1) return tools_fail(error, "diagnostic command takes no arguments");
    if (!tools->options.diagnostic) return tools_fail(error, "active application does not provide this diagnostic owner");
    qa_buffer text = {0}; bool ok = tools->options.diagnostic(tools->options.context, call, &text, error);
    if (ok && (!text.data || memchr(text.data, 0, text.size))) ok = tools_fail(error, "diagnostic owner returned invalid console text");
    if (ok) tools_print(tools, &call->context, (const char *)text.data);
    qa_buffer_free(&text); return ok;
}
