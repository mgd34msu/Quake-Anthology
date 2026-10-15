#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "qa/archive.h"
#include "qa/bsp.h"
#include "qa/frontend.h"
#include "qa/filesystem.h"
#include "qa/hash.h"
#include "qa/json_writer.h"
#include "qa/navigation.h"
#include "qa/platform_services.h"
#include "qa/native_process_platform.h"
#include "compat/native/guest/host_child.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static void usage(FILE *stream)
{
    fputs("Quake Anthology native C engine\n"
          "Usage: quake-anthology [options] [+command arguments]\n"
          "       quake-anthology --help | --version\n"
          "       quake-anthology --inspect-bsp FILE\n"
          "       quake-anthology --list ARCHIVE\n"
          "       quake-anthology --inspect-bsp ARCHIVE MEMBER\n"
          "       quake-anthology --inspect-nav FILE\n"
          "       quake-anthology --inspect-nav ARCHIVE MEMBER [BSP_MEMBER]\n"
          "       quake-anthology --optimize-aas FILE OUTPUT\n"
          "       quake-anthology --optimize-aas ARCHIVE MEMBER OUTPUT\n"
          "\n"
          "  --content-root PATH      Primary data root, default executable folder\n"
          "  --game-path PATH         Additional install folder, repeatable\n"
          "  --save-game-path PATH    Remember an install folder, then exit\n"
          "  --user-content-root PATH Writable user content root\n"
          "  --list-content           List discovered products\n"
          "  --menu                   Open the startup menu, default without --game\n"
          "  --game PRODUCT           Select installed native or external game\n"
          "  --original               Use the selected game's installed original module\n"
          "  --game-type PRODUCT/COMPONENT Select authored map entities; retain preset players\n"
          "  --map-game PRODUCT       Select map content independently\n"
          "  --map NAME               Select map or authored start\n"
          "  --movement q1|qw|q2|q3|PRODUCT\n"
          "  --character q1|q2|q3|PRODUCT\n"
          "  --weapons PRODUCT[/COMPONENT] Select player weapons independently\n"
          "  --monsters PRODUCT[/COMPONENT] Select authored monster replacements\n"
          "  --mod PRODUCT/COMPONENT   Enable independent addition, repeat to combine\n"
          "  --dedicated              Run server and stdin console without a window\n"
          "  --host ADDRESS --port N  Select server endpoint\n"
          "  --connect ADDRESS        Select remote endpoint\n"
          "  --protocol NAME          Select explicit wire protocol\n"
          "  --native-runtime-root PATH Native helper and runtime directory\n"
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

typedef struct inspection_input {
    const char *path, *member;
    qa_archive *archive;
    const qa_archive_entry *entry;
    qa_bytes bytes;
} inspection_input;
typedef int (*inspection_callback)(const inspection_input *, void *);

static int inspect_bsp(const inspection_input *input, void *context)
{
    (void)context;
    const char *name = input->member ? input->member : input->path;
    qa_error error = {0};
    qa_bsp_view map;
    qa_entities entities = {0};
    if (!qa_bsp_open(input->bytes, &map, &error))
        return report_error(name, &error);
    qa_entity_syntax syntax = map.family == QA_BSP_Q3 ? QA_ENTITY_Q3 :
        map.family == QA_BSP_Q2 ? QA_ENTITY_Q2_CLASSIC : QA_ENTITY_Q1;
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

static int inspect_file(const char *path, inspection_callback inspect, void *context)
{
    qa_error error = {0};
    qa_buffer file = {0};
    if (!qa_file_read_all(path, &file, &error))
        return report_error(path, &error);
    inspection_input input = {.path = path, .bytes = {file.data, file.size}};
    int result = inspect(&input, context);
    qa_buffer_free(&file);
    return result;
}

static int inspect_member(qa_archive *archive, const char *path, const char *member,
    inspection_callback inspect, void *context)
{
    qa_error error = {0};
    const qa_archive_entry *entry = NULL;
    if (!qa_archive_find(archive, member, QA_ARCHIVE_EXACT, 0, &entry, &error))
        return report_error(path, &error);
    if (entry == NULL) {
        fprintf(stderr, "%s: archive member not found: %s\n", path, member);
        return 1;
    }
    qa_archive_data data = {0};
    int result;
    if (!qa_archive_read(archive, entry->ordinal, &data, &error))
        result = report_error(path, &error);
    else {
        inspection_input input = {.path = path, .member = member, .archive = archive,
            .entry = entry, .bytes = data.bytes};
        result = inspect(&input, context);
    }
    qa_archive_data_free(&data);
    return result;
}

static int inspect_archive(const char *path, const char *member,
    inspection_callback inspect, void *context)
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
    } else result = inspect_member(archive, path, member, inspect, context);
    qa_archive_close(archive);
    return result;
}

static int optimize_aas(const inspection_input *input, void *context)
{
    const char *output = context;
    const char *name = input->member ? input->member : input->path;
    qa_error error = {0};
    qa_nav_asset *source = NULL, *optimized = NULL;
    qa_buffer bytes = {0};
    if (!qa_nav_asset_read(input->bytes, NULL, &source, &error))
        return report_error(name, &error);
    const qa_aas_view *aas = qa_nav_asset_aas(source);
    bool okay = aas != NULL;
    if (!okay) qa_error_set(&error, QA_ERROR_UNSUPPORTED, 0, "Navigation input is not an AAS file");
    if (okay) okay = qa_aas_optimize(aas, &optimized, &error) &&
        qa_aas_write(qa_nav_asset_aas(optimized), &bytes, &error);
    qa_fs_root *root = NULL;
    char *parent = NULL;
    const char *separator = strrchr(output, '/');
#ifdef _WIN32
    const char *backslash = strrchr(output, '\\');
    if (backslash && (!separator || backslash > separator)) separator = backslash;
#endif
    const char *leaf = separator ? separator + 1 : output;
    if (okay && separator) {
        size_t length = (size_t)(separator - output) + 1;
        parent = malloc(length + 1);
        if (!parent) {
            qa_error_set(&error, QA_ERROR_MEMORY, 0, "Allocating output directory path");
            okay = false;
        } else {
            memcpy(parent, output, length);
            parent[length] = 0;
        }
    }
    uint64_t nonce = 0;
    if (okay) okay = qa_fs_root_open(parent ? parent : ".", &root, &error) &&
        qa_platform_entropy_u64(&nonce, &error) &&
        qa_fs_root_replace(root, leaf, (qa_bytes){bytes.data, bytes.size}, nonce, &error);
    if (okay) printf("%s: %zu -> %zu bytes\n", output, input->bytes.size, bytes.size);
    qa_fs_root_close(root);
    free(parent);
    qa_buffer_free(&bytes);
    qa_nav_asset_release(optimized);
    qa_nav_asset_release(source);
    return okay ? 0 : report_error(output, &error);
}

static int inspect_bsp_checksum(const inspection_input *input, void *context)
{
    uint32_t checksum = qa_block_checksum(input->bytes);
    memcpy(context, &checksum, sizeof(checksum));
    return 0;
}

static const char *travel_name(qa_nav_travel mode)
{
    switch (mode) {
    case QA_NAV_WALK: return "walk";
    case QA_NAV_CROUCH: return "crouch";
    case QA_NAV_JUMP: return "jump";
    case QA_NAV_DROP: return "drop";
    case QA_NAV_SWIM: return "swim";
    case QA_NAV_WATER_JUMP: return "water-jump";
    case QA_NAV_LADDER: return "ladder";
    case QA_NAV_TELEPORT: return "teleport";
    case QA_NAV_MOVER: return "mover";
    case QA_NAV_JUMP_PAD: return "jump-pad";
    case QA_NAV_ROCKET_JUMP: return "rocket-jump";
    case QA_NAV_BFG_JUMP: return "bfg-jump";
    case QA_NAV_GRAPPLE: return "grapple";
    case QA_NAV_DOUBLE_JUMP: return "double-jump";
    case QA_NAV_RAMP_JUMP: return "ramp-jump";
    case QA_NAV_STRAFE_JUMP: return "strafe-jump";
    case QA_NAV_UNKNOWN: case QA_NAV_TRAVEL_COUNT: return "unknown";
    }
    return "unknown";
}

static int inspect_nav(const inspection_input *input, void *context)
{
    const char *bsp_member = context;
    int32_t checksum = 0;
    if (bsp_member) {
        int result = inspect_member(input->archive, input->path, bsp_member,
            inspect_bsp_checksum, &checksum);
        if (result) return result;
    }
    qa_error error = {0};
    qa_nav_asset *asset = NULL;
    const char *name = input->member ? input->member : input->path;
    if (!qa_nav_asset_read(input->bytes, bsp_member ? &checksum : NULL, &asset, &error))
        return report_error(name, &error);
    const qa_aas_view *aas = qa_nav_asset_aas(asset);
    const qa_nav_source_view *kex = qa_nav_asset_kex(asset);
    size_t reachabilities = aas ? aas->count[QA_AAS_REACHABILITY] : kex->link_count;
    size_t modes[QA_NAV_TRAVEL_COUNT] = {0}, mode_count = 0;
    qa_nav_travel order[QA_NAV_TRAVEL_COUNT];
    for (size_t i = 0; i < reachabilities; ++i) {
        qa_nav_travel mode = aas ? qa_nav_aas_travel((uint32_t)aas->reachability[i].travel_type) :
            qa_nav_kex_travel(kex->links[i].type);
        if (!modes[mode]) order[mode_count++] = mode;
        ++modes[mode];
    }
    qa_json_writer writer = {0};
    qa_json_writer_object(&writer);
    qa_json_writer_key(&writer, "archive");
    if (input->archive) qa_json_writer_string(&writer, input->path);
    else qa_json_writer_null(&writer);
    qa_json_writer_key(&writer, "member"); qa_json_writer_string(&writer, name);
    qa_json_writer_key(&writer, "ordinal");
    if (input->entry) qa_json_writer_u64(&writer, input->entry->ordinal);
    else qa_json_writer_null(&writer);
    qa_json_writer_key(&writer, "bytes"); qa_json_writer_u64(&writer, input->bytes.size);
    qa_json_writer_key(&writer, "kind");
    qa_json_writer_string(&writer, aas ? "aas" : kex->kind == QA_NAV_NAV2 ? "nav2" : "nav3");
    qa_json_writer_key(&writer, "version"); qa_json_writer_u64(&writer, aas ? aas->version : kex->version);
    qa_json_writer_key(&writer, "nodes");
    if (aas) qa_json_writer_number(&writer, (double)aas->count[QA_AAS_AREAS] - 1);
    else qa_json_writer_u64(&writer, kex->node_count);
    qa_json_writer_key(&writer, "reachabilities"); qa_json_writer_u64(&writer, reachabilities);
    qa_json_writer_key(&writer, "travelModes"); qa_json_writer_object(&writer);
    for (size_t i = 0; i < mode_count; ++i) {
        qa_nav_travel mode = order[i];
        qa_json_writer_key(&writer, travel_name(mode)); qa_json_writer_u64(&writer, modes[mode]);
    }
    qa_json_writer_end(&writer);
    qa_json_writer_key(&writer, "source"); qa_json_writer_object(&writer);
    if (aas) {
        qa_json_writer_key(&writer, "bspChecksum"); qa_json_writer_number(&writer, aas->bsp_checksum);
        qa_json_writer_key(&writer, "checkedMap");
        if (bsp_member) qa_json_writer_string(&writer, bsp_member);
        else qa_json_writer_null(&writer);
        qa_json_writer_key(&writer, "clusters"); qa_json_writer_u64(&writer, aas->count[QA_AAS_CLUSTERS]);
        qa_json_writer_key(&writer, "portals"); qa_json_writer_u64(&writer, aas->count[QA_AAS_PORTALS]);
    } else {
        qa_json_writer_key(&writer, "traversals"); qa_json_writer_u64(&writer, kex->traversal_count);
        qa_json_writer_key(&writer, "entities"); qa_json_writer_u64(&writer, kex->entity_count);
        qa_json_writer_key(&writer, "heuristic"); qa_json_writer_number(&writer, kex->heuristic);
    }
    qa_json_writer_end(&writer);
    qa_json_writer_key(&writer, "admission");
    qa_json_writer_string(&writer, "Asset inspection only; route admission requires selected movement and live shared collision.");
    qa_json_writer_end(&writer);
    qa_buffer output = {0};
    bool ok = qa_json_writer_finish(&writer, &output, &error);
    qa_json_writer_destroy(&writer);
    if (ok && fwrite(output.data, 1, output.size, stdout) != output.size) {
        qa_error_set(&error, QA_ERROR_IO, 0, "Writing navigation inspection"); ok = false;
    }
    qa_buffer_free(&output);
    qa_nav_asset_release(asset);
    return ok ? 0 : report_error(name, &error);
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
    if (argc > 1 && strcmp(argv[1], "--inspect-bsp") == 0) {
        if (argc == 3) return inspect_file(argv[2], inspect_bsp, NULL);
        if (argc == 4) return inspect_archive(argv[2], argv[3], inspect_bsp, NULL);
        usage(stderr); return 2;
    }
    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        if (argc == 3) return inspect_archive(argv[2], NULL, NULL, NULL);
        usage(stderr); return 2;
    }
    if (argc > 1 && strcmp(argv[1], "--inspect-nav") == 0) {
        if (argc == 3) return inspect_file(argv[2], inspect_nav, NULL);
        if (argc == 4 || argc == 5)
            return inspect_archive(argv[2], argv[3], inspect_nav, argc == 5 ? argv[4] : NULL);
        usage(stderr); return 2;
    }
    if (argc > 1 && strcmp(argv[1], "--optimize-aas") == 0) {
        if (argc == 4) return inspect_file(argv[2], optimize_aas, argv[3]);
        if (argc == 5) return inspect_archive(argv[2], argv[3], optimize_aas, argv[4]);
        usage(stderr); return 2;
    }
    qa_frontend_options options;
    qa_error error = {0};
    if (!qa_frontend_options_parse(argc, argv, &options, &error)) {
        report_error("startup", &error);
        usage(stderr);
        return 2;
    }
    qa_native_process_environment environment = {0};
    if (!qa_native_process_environment_acquire(&environment, &error)) {
        qa_frontend_options_destroy(&options);
        return report_error("startup", &error);
    }
    options.native_bootstrap = environment.executable;
    environment.executable = NULL;
    qa_native_process_environment_dispose(&environment);
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
