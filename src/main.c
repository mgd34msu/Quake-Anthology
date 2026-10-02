#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "qa/archive.h"
#include "qa/bsp.h"
#include "qa/frontend.h"
#include "compat/native/guest/host_child.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

static bool executable_path(char **out, qa_error *error)
{
    size_t capacity = 256;
    for (;;) {
#if defined(_WIN32)
        if (capacity > UINT32_MAX / sizeof(wchar_t)) break;
        wchar_t *wide = malloc(capacity * sizeof(*wide));
        if (!wide) break;
        DWORD length = GetModuleFileNameW(NULL, wide, (DWORD)capacity);
        if (!length) { free(wide); break; }
        if ((size_t)length < capacity) {
            if (length > INT_MAX) { free(wide); break; }
            int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
                (int)length, NULL, 0, NULL, NULL);
            char *path = bytes > 0 ? malloc((size_t)bytes + 1) : NULL;
            if (path && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide,
                (int)length, path, bytes, NULL, NULL) == bytes) {
                path[bytes] = 0; *out = path; free(wide); return true;
            }
            free(path); free(wide); break;
        }
        free(wide);
#elif defined(__APPLE__)
        if (capacity > UINT32_MAX) break;
        char *path = malloc(capacity);
        if (!path) break;
        uint32_t bytes = (uint32_t)capacity;
        if (!_NSGetExecutablePath(path, &bytes)) { *out = path; return true; }
        free(path);
        if (bytes > capacity) { capacity = bytes; continue; }
#elif defined(__linux__)
        char *path = malloc(capacity);
        if (!path) break;
        ssize_t length = readlink("/proc/self/exe", path, capacity - 1);
        if (length < 0) { free(path); break; }
        if ((size_t)length < capacity - 1) {
            path[length] = 0; *out = path; return true;
        }
        free(path);
#else
        break;
#endif
        if (capacity > SIZE_MAX / 2) break;
        capacity *= 2;
    }
    qa_error_set(error, QA_ERROR_IO, 0, "Cannot retain the actual executable bootstrap path");
    return false;
}

static void usage(FILE *stream)
{
    fputs("Quake Anthology native C engine\n"
          "Usage: quake-anthology [options] [+command arguments]\n"
          "       quake-anthology --help | --version\n"
          "       quake-anthology --inspect-bsp FILE\n"
          "       quake-anthology --list ARCHIVE\n"
          "       quake-anthology --inspect-bsp ARCHIVE MEMBER\n"
          "\n"
          "  --content-root PATH      Primary data root, default executable folder\n"
          "  --game-path PATH         Additional install folder, repeatable\n"
          "  --save-game-path PATH    Remember an install folder and list content\n"
          "  --user-content-root PATH Writable user content root\n"
          "  --list-content           List discovered products\n"
          "  --menu                   Open the startup menu, default without --game\n"
          "  --game PRODUCT           Select installed native or external game\n"
          "  --map-game PRODUCT       Select map content independently\n"
          "  --map NAME               Select map or authored start\n"
          "  --movement q1|qw|q2|q3|PRODUCT\n"
          "  --character q1|q2|q3|PRODUCT\n"
          "  --mod PRODUCT/COMPONENT   Enable independent addition, repeat to combine\n"
          "  --dedicated              Run server and stdin console without a window\n"
          "  --host ADDRESS --port N  Select server endpoint\n"
          "  --connect ADDRESS        Select remote endpoint\n"
          "  --protocol NAME          Select explicit wire protocol\n"
          "  --native-runtime-root PATH Native helper and runtime directory\n"
          "  --native-wine FILE       Wine launcher override\n"
          "  --native-backend host|emulated  External native execution policy\n"
          "  --native-stack-bytes N --native-backing-bytes N\n"
          "  --native-image-bytes N --native-trap-bytes N\n"
          "  --native-instruction-budget N  Emulated call limit; host requires zero\n"
          "  --renderer cpu|gl        Select native output\n"
          "  --width N --height N     Set window dimensions\n"
          "  --seats 1..4             Local player seats\n"
          "  --gamma 0.5..3           Output brightness\n"
          "  --frames N               Stop after N frames, zero is unlimited\n"
          "  --hidden --no-audio      Select window visibility and audio delivery\n"
          "  --font-directory PATH --font FILE  Native menu font resource\n"
          "\n"
          "First use: put the executable beside your installed games, or run\n"
          "  quake-anthology --save-game-path \"/path/to/your/games\"\n"
          "Nearby folders, Steam libraries and saved paths are searched. Saved\n"
          "paths live under --user-content-root or the platform user settings.\n"
          "Use --list-content to find installed product IDs before --game.\n", stream);
}

static int report_error(const char *source, const qa_error *error)
{
    fprintf(stderr, "%s:%zu: %s\n", source, error->offset, error->message);
    return 1;
}

static int inspect_bsp(const char *name, qa_bytes bytes)
{
    qa_error error = {0};
    qa_bsp_view map;
    qa_entities entities = {0};
    if (!qa_bsp_open(bytes, &map, &error))
        return report_error(name, &error);
    qa_entity_syntax syntax = map.family == QA_BSP_Q3 ? QA_ENTITY_Q3 : QA_ENTITY_Q1;
    if (!qa_entities_parse(map.lumps[QA_BSP_ENTITIES].bytes, syntax,
                           &entities, &error))
        return report_error(name, &error);
    printf("format: %s\nentities: %zu\n", qa_bsp_format_name(map.format), entities.count);
    for (int kind = 0; kind < QA_BSP_LUMP_COUNT; ++kind) {
        const qa_bsp_lump *lump = &map.lumps[kind];
        if (lump->present)
            printf("%s: %zu bytes, %zu records\n",
                   qa_bsp_lump_name((qa_bsp_lump_kind)kind), lump->bytes.size,
                   qa_bsp_record_count(&map, (qa_bsp_lump_kind)kind));
    }
    printf("extensions: %u\ndiagnostics: %u\n", map.extension_count, map.diagnostics);
    qa_entities_free(&entities);
    return 0;
}

static int inspect_bsp_file(const char *path)
{
    qa_error error = {0};
    qa_buffer file = {0};
    if (!qa_file_read_all(path, &file, &error))
        return report_error(path, &error);
    int result = inspect_bsp(path, (qa_bytes){file.data, file.size});
    qa_buffer_free(&file);
    return result;
}

static int inspect_archive(const char *path, const char *member)
{
    qa_error error = {0};
    qa_archive *archive = NULL;
    if (!qa_archive_open_file(path, QA_ARCHIVE_AUTO, &archive, &error))
        return report_error(path, &error);
    int result = 0;
    if (member == NULL) {
        for (size_t i = 0; i < qa_archive_count(archive); ++i) {
            const qa_archive_entry *entry = qa_archive_entry_at(archive, i);
            printf("%zu\t%zu\t%s\n", entry->ordinal, entry->size, entry->path);
        }
    } else {
        const qa_archive_entry *entry = NULL;
        if (!qa_archive_find(archive, member, QA_ARCHIVE_EXACT, 0, &entry, &error)) {
            result = report_error(path, &error);
        } else if (entry == NULL) {
            fprintf(stderr, "%s: archive member not found: %s\n", path, member);
            result = 1;
        } else {
            qa_archive_data data = {0};
            if (!qa_archive_read(archive, entry->ordinal, &data, &error))
                result = report_error(path, &error);
            else
                result = inspect_bsp(member, data.bytes);
            qa_archive_data_free(&data);
        }
    }
    qa_archive_close(archive);
    return result;
}

int main(int argc, char **argv)
{
    qa_error child_error = {0};
    bool child_handled = false;
    int child_status = 0;
    if (!guest_host_child_bootstrap(argc, argv, &child_handled, &child_status,
                                    &child_error))
        return report_error("native child", &child_error);
    if (child_handled) return child_status;
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Quake Anthology %s (baseline development)\n", QA_VERSION);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--inspect-bsp") == 0)
        return inspect_bsp_file(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--list") == 0)
        return inspect_archive(argv[2], NULL);
    if (argc == 4 && strcmp(argv[1], "--inspect-bsp") == 0)
        return inspect_archive(argv[2], argv[3]);
    qa_frontend_options options;
    qa_error error = {0};
    if (!qa_frontend_options_parse(argc, argv, &options, &error)) {
        report_error("startup", &error);
        usage(stderr);
        return 2;
    }
    if (!executable_path(&options.native_bootstrap, &error)) {
        qa_frontend_options_destroy(&options);
        return report_error("startup", &error);
    }
    options.application.native_bootstrap = options.native_bootstrap;
    if (!qa_frontend_options_resolve_locations(&options,&error)) {
        qa_frontend_options_destroy(&options);
        return report_error("game locations",&error);
    }
    bool list = false;
    for (int i = 1; i < argc; ++i) list |= strcmp(argv[i], "--list-content") == 0;
    if (options.save_game_path_count) {
        printf("Saved game search locations in %s\n",qa_frontend_options_locations_file(&options));
        if (!list) { qa_frontend_options_destroy(&options); return 0; }
    }
    bool ok, owners_released = true;
    if (list) {
        qa_application *retained = NULL;
        ok = qa_frontend_list_content(&options, stdout, &retained, &error);
        if (retained) {
            qa_error cleanup = {0};
            if (!qa_application_destroy(retained, &cleanup)) {
                if (ok) error = cleanup;
                else fprintf(stderr, "shutdown: %s\n", cleanup.message);
                owners_released = false;
                ok = false;
            }
        }
    } else {
        qa_frontend *frontend = NULL;
        ok = qa_frontend_create(&options, &frontend, &error);
        if (ok) ok = qa_frontend_run(&frontend, &error);
        if (frontend) {
            qa_error cleanup = {0};
            if (!qa_frontend_shutdown(&frontend, &cleanup)) {
                if (ok) error = cleanup;
                else fprintf(stderr, "shutdown: %s\n", cleanup.message);
                owners_released = frontend == NULL;
                ok = false;
            }
        }
    }
    if (owners_released) qa_frontend_options_destroy(&options);
    return ok ? 0 : report_error("application", &error);
}
