#include "qa/archive.h"
#include "qa/bsp.h"

#include <stdio.h>
#include <string.h>

static void usage(FILE *stream)
{
    fputs("Quake Anthology native C engine\n"
          "Usage: quake-anthology --help | --version\n"
          "       quake-anthology --inspect-bsp FILE\n"
          "       quake-anthology --list ARCHIVE\n"
          "       quake-anthology --inspect-bsp ARCHIVE MEMBER\n"
          "The baseline engine is under construction.\n", stream);
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
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("Quake Anthology %s (baseline development)\n", QA_VERSION);
        return 0;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--help") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--inspect-bsp") == 0)
        return inspect_bsp_file(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--list") == 0)
        return inspect_archive(argv[2], NULL);
    if (argc == 4 && strcmp(argv[1], "--inspect-bsp") == 0)
        return inspect_archive(argv[2], argv[3]);
    usage(stderr);
    return 2;
}
