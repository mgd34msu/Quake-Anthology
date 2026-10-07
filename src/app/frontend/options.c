#include "internal.h"
#include "install_locations.h"
#include "qa/text.h"
#include <errno.h>
#include <stdio.h>

bool frontend_fail(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error, status, 0, "%s", message);
    return false;
}
void qa_frontend_options_default(qa_frontend_options *options)
{
    if (!options) return;
    *options = (qa_frontend_options){.seats = 1, .gamma = 1, .audio = true,
        .network_protocol = {QA_NET_UNIFIED_1, 0, 0}, .network_port = 27960,
        .font_directory = "/usr/share/fonts/truetype/dejavu", .font_file = "DejaVuSans.ttf"};
    qa_application_options_default(&options->application);
    options->application.content_root = NULL;
    qa_display_options_default(&options->display);
    options->display.title = "Quake Anthology";
}
static bool push(const char ***array, size_t *count, const char *value, qa_error *error)
{
    if (*count >= SIZE_MAX / sizeof(**array)) return frontend_fail(error, QA_ERROR_MEMORY, "too many startup arguments");
    const char **next = realloc((void *)*array, (*count + 1) * sizeof(*next));
    if (!next) return frontend_fail(error, QA_ERROR_MEMORY, "retaining startup arguments");
    next[(*count)++] = value;
    *array = next;
    return true;
}
static bool integer(const char *text, uint64_t low, uint64_t high, uint64_t *out, qa_error *error)
{
    char *end;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    if (!*text || *text == '-' || errno || *end || value < low || value > high)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "numeric startup argument outside its range");
    *out = (uint64_t)value;
    return true;
}
bool frontend_protocol(const char *text, qa_net_protocol_id *out, qa_error *error)
{
    static const struct { const char *name; qa_net_protocol kind; uint32_t revision; } choices[] = {
        {"nq15", QA_NET_NQ15, 0}, {"fitz666", QA_NET_FITZ666, 0}, {"rmq999", QA_NET_RMQ999, 0},
        {"qw28", QA_NET_QW28, 0}, {"qw29", QA_NET_QW29, 0}, {"q2-34", QA_NET_Q2_34, 0},
        {"r1q2-35", QA_NET_R1Q2_35, 1905}, {"q2pro-36", QA_NET_Q2PRO_36, 1026},
        {"q2repro-1038", QA_NET_Q2REPRO_1038, 0}, {"q2kex-2023", QA_NET_Q2KEX_2023, 0},
        {"q3-68", QA_NET_Q3_68, 0}, {"unified-1", QA_NET_UNIFIED_1, 0}
    };
    for (size_t i = 0; i < sizeof(choices) / sizeof(*choices); ++i) if (!strcmp(text, choices[i].name)) {
        *out = (qa_net_protocol_id){choices[i].kind, choices[i].revision, 0};
        return qa_net_protocol_valid(*out, error);
    }
    return frontend_fail(error, QA_ERROR_ARGUMENT, "unknown network protocol");
}
/* Each shell argv element is a source token. Quote token arguments explicitly
 * so semicolons, whitespace and embedded quotes cannot become another command. */
static bool startup(int argc, char *const argv[], int *index, qa_frontend_options *options, qa_error *error)
{
    int begin = *index, end = begin + 1;
    const char *name = argv[begin] + 1;
    if (!*name) return frontend_fail(error, QA_ERROR_ARGUMENT, "startup command needs a name");
    while (end < argc && strncmp(argv[end], "--", 2) && argv[end][0] != '+') ++end;
    size_t bytes = strlen(argv[begin] + 1) + 1;
    for (int i = begin + 1; i < end; ++i) {
        size_t n = strlen(argv[i]);
        if (n > (SIZE_MAX - bytes - 4) / 2) return frontend_fail(error, QA_ERROR_MEMORY, "startup command too long");
        bytes += n * 2 + 4;
    }
    char *command = malloc(bytes);
    if (!command) return frontend_fail(error, QA_ERROR_MEMORY, "retaining startup command");
    size_t at = strlen(argv[begin] + 1);
    memcpy(command, argv[begin] + 1, at);
    for (int i = begin + 1; i < end; ++i) {
        command[at++] = ' '; command[at++] = '"';
        for (const char *p = argv[i]; *p; ++p) {
            /* Source tokenizers do not interpret C string escapes. Embedded
             * double quotes are rejected instead of silently changing syntax. */
            if (*p == '"' || *p == '\n' || *p == '\r') {
                free(command); return frontend_fail(error, QA_ERROR_ARGUMENT, "startup token contains an unsupported quote or newline");
            }
            command[at++] = *p;
        }
        command[at++] = '"';
    }
    command[at] = 0;
    if (!push(&options->startup, &options->startup_count, command, error)) { free(command); return false; }
    *index = end - 1;
    return true;
}
static bool native_startup(qa_frontend_options *options,qa_error *error)
{
    const struct { const char *name; double value; bool specified; } settings[]={
        {"r_mode",-1,options->width_specified || options->height_specified},
        {"r_customwidth",options->display.width,options->width_specified},
        {"r_customheight",options->display.height,options->height_specified},
        {"r_gamma",options->gamma,options->gamma_specified}};
    size_t original=options->startup_count;
    for (size_t i=0;i<sizeof(settings)/sizeof(*settings);++i) {
        if (!settings[i].specified) continue;
        char value[64];
        if (!qa_format_number(settings[i].value,value,error)) return false;
        char *arguments[]={"quake-anthology","+set",(char *)settings[i].name,value};
        int at=1;
        if (!startup(4,arguments,&at,options,error)) return false;
    }
    size_t added=options->startup_count-original;
    if (added) {
        const char *first[4];
        memcpy(first,options->startup+original,added*sizeof(*first));
        memmove(options->startup+added,options->startup,original*sizeof(*first));
        memcpy(options->startup,first,added*sizeof(*first));
    }
    return true;
}
bool qa_frontend_options_parse(int argc, char *const argv[], qa_frontend_options *options, qa_error *error)
{
    if (!options || argc < 1 || !argv) return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid startup arguments");
    qa_frontend_options_default(options);
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (*arg == '+') { if (!startup(argc, argv, &i, options, error)) goto fail; continue; }
        if (!strcmp(arg, "--menu")) { options->menu = true; continue; }
        if (!strcmp(arg, "--dedicated")) { options->dedicated = true; continue; }
        if (!strcmp(arg, "--original")) { options->original = true; continue; }
        if (!strcmp(arg, "--hidden")) { options->display.hidden = true; continue; }
        if (!strcmp(arg, "--no-audio")) { options->audio = false; continue; }
        if (!strcmp(arg, "--list-content")) continue;
        if (i + 1 == argc) { frontend_fail(error, QA_ERROR_ARGUMENT, "startup option needs a value"); goto fail; }
        const char *value = argv[++i];
        if (!strcmp(arg, "--content-root")) options->application.content_root = value;
        else if (!strcmp(arg, "--game-path")) {
            if (!*value) { frontend_fail(error,QA_ERROR_ARGUMENT,"--game-path needs a directory"); goto fail; }
            if (!push(&options->game_paths,&options->game_path_count,value,error)) goto fail;
        }
        else if (!strcmp(arg, "--save-game-path")) {
            if (!*value) { frontend_fail(error,QA_ERROR_ARGUMENT,"--save-game-path needs a directory"); goto fail; }
            if (!push(&options->save_game_paths,&options->save_game_path_count,value,error)) goto fail;
        }
        else if (!strcmp(arg, "--user-content-root")) options->application.user_root = value;
        else if (!strcmp(arg, "--native-runtime-root")) options->native_runtime_root = value;
        else if (!strcmp(arg, "--native-wine")) options->native_wine = value;
        else if (!strcmp(arg, "--native-backend")) {
            qa_native_process_resource_policy *policy = &options->application.native_process_policy;
            if (!strcmp(value, "host")) {
                policy->backend = QA_NATIVE_GUEST_HOST_X86_64; policy->instruction_budget = 0;
            } else if (!strcmp(value, "emulated")) {
                policy->backend = QA_NATIVE_GUEST_EMULATED; policy->instruction_budget = 50000000u;
            } else { frontend_fail(error, QA_ERROR_ARGUMENT, "native backend must be host or emulated"); goto fail; }
        }
        else if (!strcmp(arg, "--native-stack-bytes") || !strcmp(arg, "--native-backing-bytes") ||
            !strcmp(arg, "--native-image-bytes") || !strcmp(arg, "--native-trap-bytes") ||
            !strcmp(arg, "--native-instruction-budget")) {
            uint64_t number;
            bool budget = !strcmp(arg, "--native-instruction-budget");
            if (!integer(value, budget ? 0 : 1, SIZE_MAX, &number, error)) goto fail;
            qa_native_process_resource_policy *policy = &options->application.native_process_policy;
            if (budget) policy->instruction_budget = (size_t)number;
            else if (!strcmp(arg, "--native-stack-bytes")) policy->stack_bytes = (size_t)number;
            else if (!strcmp(arg, "--native-backing-bytes")) policy->maximum_backing_bytes = (size_t)number;
            else if (!strcmp(arg, "--native-image-bytes")) policy->maximum_image_bytes = (size_t)number;
            else policy->runtime_trap_bytes = (size_t)number;
        }
        else if (!strcmp(arg, "--game")) options->game = value;
        else if (!strcmp(arg, "--game-type")) {
            if (!qa_catalog_mod_key(value)) { frontend_fail(error, QA_ERROR_ARGUMENT, "--game-type needs PRODUCT/COMPONENT"); goto fail; }
            options->game_type = value;
        }
        else if (!strcmp(arg, "--map-game")) options->map_game = value;
        else if (!strcmp(arg, "--map")) options->map = value;
        else if (!strcmp(arg, "--movement")) options->movement = value;
        else if (!strcmp(arg, "--character")) { options->character = value; options->character_model = NULL; }
        else if (!strcmp(arg, "--weapons") || !strcmp(arg, "--monsters")) {
            if (!*value) { frontend_fail(error, QA_ERROR_ARGUMENT, "role selection needs PRODUCT or PRODUCT/COMPONENT"); goto fail; }
            if (!strcmp(arg, "--weapons")) options->weapons = value;
            else options->monsters = value;
        }
        else if (!strcmp(arg, "--model") || !strcmp(arg, "--character-model")) {
            if (!*value) { frontend_fail(error, QA_ERROR_ARGUMENT, "character model needs a name"); goto fail; }
            for (const unsigned char *p=(const unsigned char *)value;*p;++p)
                if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || *p=='_' || *p=='-')) {
                    frontend_fail(error, QA_ERROR_ARGUMENT, "invalid character model name"); goto fail;
                }
            options->character_model=value;
        }
        else if (!strcmp(arg, "--font-directory")) options->font_directory = value;
        else if (!strcmp(arg, "--font")) options->font_file = value;
        else if (!strcmp(arg, "--host")) options->network_host = value;
        else if (!strcmp(arg, "--connect")) options->network_connect = value;
        else if (!strcmp(arg, "--protocol")) { if (!frontend_protocol(value, &options->network_protocol, error)) goto fail; }
        else if (!strcmp(arg, "--port")) {
            uint64_t number;
            if (!integer(value, 1, UINT16_MAX, &number, error)) goto fail;
            options->network_port = (uint16_t)number;
        }
        else if (!strcmp(arg, "--mod")) { if (!push(&options->mods, &options->mod_count, value, error)) goto fail; }
        else if (!strcmp(arg, "--renderer")) {
            if (!strcmp(value, "gl")) options->display.backend = QA_DISPLAY_OPENGL;
            else if (!strcmp(value, "cpu")) options->display.backend = QA_DISPLAY_CPU;
            else { frontend_fail(error, QA_ERROR_ARGUMENT, "renderer must be cpu or gl"); goto fail; }
        } else if (!strcmp(arg, "--gamma")) {
            char *end; errno = 0;
            options->gamma = strtof(value, &end);
            if (!*value || *end || errno || !isfinite(options->gamma) || options->gamma < .5f || options->gamma > 3) {
                frontend_fail(error, QA_ERROR_ARGUMENT, "gamma must be between 0.5 and 3"); goto fail;
            }
            options->gamma_specified = true;
        } else {
            uint64_t number;
            uint64_t high = !strcmp(arg, "--seats") ? 4 : !strcmp(arg, "--frames") ? UINT64_MAX : 16384;
            uint64_t low = !strcmp(arg, "--frames") ? 0 : 1;
            if (strcmp(arg, "--width") && strcmp(arg, "--height") && strcmp(arg, "--seats") && strcmp(arg, "--frames")) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "unknown startup option: %s", arg); goto fail;
            }
            if (!integer(value, low, high, &number, error)) goto fail;
            if (!strcmp(arg, "--width")) { options->display.width = (uint32_t)number; options->width_specified = true; }
            else if (!strcmp(arg, "--height")) { options->display.height = (uint32_t)number; options->height_specified = true; }
            else if (!strcmp(arg, "--seats")) options->seats = (unsigned)number;
            else options->frame_limit = number;
        }
    }
    if (options->original && options->game_type) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "select --original or --game-type"); goto fail;
    }
    if (!options->game && (options->original || options->game_type || options->map || options->map_game || options->movement || options->character || options->character_model || options->weapons || options->monsters || options->mod_count)) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "explicit source selections require --game"); goto fail;
    }
    if (options->dedicated && !options->game) { frontend_fail(error, QA_ERROR_ARGUMENT, "dedicated startup requires --game"); goto fail; }
    if (options->network_host && options->network_connect) { frontend_fail(error, QA_ERROR_ARGUMENT, "select one host or remote connection"); goto fail; }
    const qa_native_process_resource_policy *policy = &options->application.native_process_policy;
    if (policy->stack_bytes % QA_NATIVE_GUEST_PAGE || policy->runtime_trap_bytes % QA_NATIVE_GUEST_PAGE ||
        (policy->backend == QA_NATIVE_GUEST_EMULATED ? !policy->instruction_budget : policy->instruction_budget != 0)) {
        frontend_fail(error, QA_ERROR_ARGUMENT, "native stack/trap extents require page multiples and execution requires its matching budget"); goto fail;
    }
    if (!native_startup(options,error)) goto fail;
    options->menu |= !options->game;
    return true;
fail:
    qa_frontend_options_destroy(options);
    return false;
}
void qa_frontend_options_destroy(qa_frontend_options *options)
{
    if (!options) return;
    for (size_t i = 0; i < options->startup_count; ++i) free((void *)options->startup[i]);
    free((void *)options->startup); free((void *)options->mods);
    free((void *)options->game_paths); free((void *)options->save_game_paths);
    options->game_paths=NULL; options->save_game_paths=NULL;
    options->game_path_count=options->save_game_path_count=0;
    frontend_install_locations_destroy(options->install_locations);
    options->install_locations=NULL;
    options->startup = NULL; options->startup_count = 0;
    options->mods = NULL; options->mod_count = 0;
    free(options->native_bootstrap);
    options->native_bootstrap = NULL;
}
bool qa_frontend_list_content(const qa_frontend_options *options, FILE *stream, qa_application **retained, qa_error *error)
{
    if (!options || !stream || !retained || *retained)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid content listing");
    qa_application *application = NULL;
    qa_application_options construction=options->application;
    construction.startup_commands=options->startup;
    construction.startup_command_count=options->startup_count;
    construction.initial_product_key=options->game;
    if (!qa_application_create(&construction, &application, error)) { *retained=application; return false; }
    qa_catalog *catalog = qa_application_catalog(application);
    for (size_t i = 0; i < qa_catalog_count(catalog); ++i) {
        const qa_product *product = qa_catalog_at(catalog, i);
        const qa_catalog_mount *mount=qa_catalog_product_loose_mount(catalog,product->id);
        fprintf(stream, "%s\t%s\t%s\t%s\n", product->key,
            product->availability == QA_CONTENT_INSTALLED ? "installed" : "unavailable",
            product->title,mount?mount->path:"");
    }
    fputs("To add an installation: --game-path PATH --list-content\n"
        "To remember it: --save-game-path PATH (uses your user settings directory)\n",stream);
    bool written = !ferror(stream);
    if (!written) frontend_fail(error, QA_ERROR_IO, "writing content listing");
    qa_error cleanup={0};
    bool closed = qa_application_destroy(application, &cleanup);
    if (!closed) { *retained=application; if (written && error) *error=cleanup; }
    return written && closed;
}
