#include "native_q1_console.h"
#include "startup_flow.h"
#include "qa/cvars_save.h"
#include "qa/console_cvars_prepare.h"
#include "qa/source_number.h"
#include "qa/network_q1_qw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct application_native_q1_console {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    size_t calls;
    /* svs.info and localinfo survive SV_Spawn; reliable_datagram does not. */
    char serverinfo[513], localinfo[32769];
    uint8_t reliable_info[1450];
    size_t reliable_info_size;
    bool info_initialized;
    qa_error info_error;
    const qa_command_context *info_context;
};

qa_cvars *application_native_q1_console_registry(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q1 && provider->native_q1_console
        ? provider->native_q1_console->cvars : NULL;
}

bool application_native_q1_console_idle(const application_provider *provider)
{
    const struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    return !owner || (!owner->calls && qa_console_idle(owner->console));
}

static qa_console_dialect dialect(const application_provider *provider)
{
    return provider->launch->selection.clock.kind == QA_CLOCK_QUAKEWORLD ? QA_CONSOLE_QW : QA_CONSOLE_Q1;
}

static bool capture(void *opaque, const qa_command_context *source,
                    qa_command_context *out, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    qa_command_context command = *source;
    if (command.owner && command.owner != owner->provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console received another source owner");
    command.owner = owner->provider->owner;
    command.dialect = dialect(owner->provider);
    return application_command_capture(owner->provider->application, &command, out, error);
}

static bool active(void *opaque, const qa_command_context *command)
{
    struct application_native_q1_console *owner = opaque;
    return command && command->owner == owner->provider->owner &&
        command->dialect == dialect(owner->provider) &&
        application_command_active(owner->provider->application, command);
}

static void print(void *opaque, const qa_command_context *command, const char *text)
{
    struct application_native_q1_console *owner = opaque;
    ++owner->calls;
    application_console_print(owner->provider->application, command, text);
    --owner->calls;
}

static void cvar_print(void *opaque, const char *text)
{
    struct application_native_q1_console *owner = opaque;
    qa_console_emit(owner->console, NULL, text);
}
void application_native_q1_source_console_print(void *opaque, const char *text)
{
    application_provider *provider = opaque;
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (owner && provider->kind == APPLICATION_PROVIDER_Q1 && text)
        qa_console_emit(owner->console, NULL, text);
}
static const qa_launch_instance *source_descriptor(application_provider *provider)
{
    if (!provider || !provider->application || !provider->launch) return NULL;
    qa_application *app = provider->application;
    const qa_launch_snapshot *snapshots[] = {app->routing_snapshot,
        qa_application_startup_candidate(app), qa_application_launch(app)};
    for (size_t i = 0; i < sizeof(snapshots) / sizeof(*snapshots); ++i) {
        const qa_launch_instance *instance = snapshots[i]
            ? qa_launch_snapshot_find(snapshots[i], provider->launch->selection.instance) : NULL;
        if (instance && instance->state == provider && instance->storage == provider->launch->storage)
            return instance;
    }
    return NULL;
}
static bool log_source(application_provider *provider,qa_application_startup_source *out)
{
    struct application_native_q1_console *owner=provider?provider->native_q1_console:NULL;
    if (!owner || provider->kind!=APPLICATION_PROVIDER_Q1 || !provider->constructed ||
        !provider->attached || provider->close_pending || !provider->launch ||
        provider->launch->selection.clock.kind!=QA_CLOCK_QUAKEWORLD) return false;
    const qa_launch_instance *descriptor = source_descriptor(provider);
    if (!descriptor) return false;
    *out=(qa_application_startup_source){.descriptor=descriptor,
        .scope={.provider=provider->owner,.kind=QA_APPLICATION_CONSOLE_Q1_GAME},
        .console=owner->console,.cvars=owner->cvars,
        .command={.owner=provider->owner,.dialect=QA_CONSOLE_QW,.origin=QA_COMMAND_SERVER},
        .declaration_owner=provider->owner};
    return true;
}
void application_native_q1_source_logfrag_write(void *opaque,const char *record)
{
    application_provider *provider=opaque; qa_application_startup_source source;
    if (!record || !log_source(provider,&source)) return;
    const qa_application_startup_hooks *hooks=provider->application->startup_hooks;
    if (!hooks || !hooks->qw_logfrag_write) return;
    ++provider->native_q1_console->calls;
    hooks->qw_logfrag_write(hooks->context,provider->application,&source,record);
    --provider->native_q1_console->calls;
}
bool application_native_q1_source_logfrag_enabled(application_provider *provider,
    bool *enabled,qa_error *error)
{
    qa_application_startup_source source;
    if (!enabled || !log_source(provider,&source))
        return application_fail(error,QA_ERROR_ARGUMENT,"QuakeWorld frag file lost its physical console owner");
    *enabled=false;
    const qa_application_startup_hooks *hooks=provider->application->startup_hooks;
    if (!hooks || !hooks->qw_logfrag_enabled) return true;
    ++provider->native_q1_console->calls;
    bool okay=hooks->qw_logfrag_enabled(hooks->context,provider->application,&source,enabled,error);
    --provider->native_q1_console->calls;
    return okay;
}


/* Keep the Source byte dictionary: filtering can create empty keys and
 * duplicates, and Info_RemoveKey removes only the first matching pair. */
static bool info_pair(const char **cursor, const char **key, size_t *key_size,
    const char **value, size_t *value_size, const char **start)
{
    const char *s = *cursor;
    *start = s;
    if (*s == '\\') ++s;
    *key = s;
    while (*s && *s != '\\') ++s;
    if (!*s) return false;
    *key_size = (size_t)(s - *key);
    *value = ++s;
    while (*s && *s != '\\') ++s;
    *value_size = (size_t)(s - *value);
    *cursor = s;
    return true;
}
static void info_value(const char *info, const char *wanted, const char **out, size_t *size)
{
    const char *cursor = info, *key, *value, *start;
    size_t key_size, value_size, wanted_size = strlen(wanted);
    while (info_pair(&cursor, &key, &key_size, &value, &value_size, &start)) {
        if (key_size == wanted_size && !memcmp(key, wanted, key_size)) {
            *out = value; *size = value_size; return;
        }
        if (!*cursor) break;
    }
    *out = ""; *size = 0;
}
static void info_remove(char *info, const char *wanted)
{
    const char *cursor = info, *key, *value, *start;
    size_t key_size, value_size, wanted_size = strlen(wanted);
    while (info_pair(&cursor, &key, &key_size, &value, &value_size, &start)) {
        if (key_size == wanted_size && !memcmp(key, wanted, key_size)) {
            memmove((char *)start, cursor, strlen(cursor) + 1); return;
        }
        if (!*cursor) break;
    }
}
static void info_print(struct application_native_q1_console *owner, const char *text)
{
    qa_console_emit(owner->console, owner->info_context, text);
}
static void info_set(struct application_native_q1_console *owner, char *info,
    size_t maximum, const char *key, const char *value, bool star)
{
    if (!star && *key == '*') { info_print(owner, "Can't set * keys\n"); return; }
    if (strchr(key, '\\') || strchr(value, '\\')) {
        info_print(owner, "Can't use keys or values with a \\\n"); return;
    }
    if (strchr(key, '"') || strchr(value, '"')) {
        info_print(owner, "Can't use keys or values with a \"\n"); return;
    }
    size_t key_size = strlen(key), value_size = strlen(value);
    if (key_size > 63 || value_size > 63) {
        info_print(owner, "Keys and values must be < 64 characters.\n"); return;
    }
    const char *old; size_t old_size;
    info_value(info, key, &old, &old_size);
    size_t size = strlen(info);
    if (old_size && size - old_size + value_size > maximum) {
        info_print(owner, "Info string length exceeded\n"); return;
    }
    info_remove(info, key);
    if (!value_size) return;
    size = strlen(info);
    if (key_size + value_size + 2 + size > maximum) {
        info_print(owner, "Info string length exceeded\n"); return;
    }
    char pair[129];
    pair[0] = '\\'; memcpy(pair + 1, key, key_size);
    pair[key_size + 1] = '\\'; memcpy(pair + key_size + 2, value, value_size);
    pair[key_size + value_size + 2] = 0;
    const qa_cvar_view *highchars = qa_cvars_find(owner->cvars, "sv_highchars");
    bool high = highchars && highchars->number != 0;
    for (const unsigned char *cursor = (const unsigned char *)pair; *cursor; ++cursor) {
        unsigned char c = *cursor;
        if (!high) { c &= 127; if (c < 32) continue; }
        if (c > 13) info[size++] = (char)c;
    }
    info[size] = 0;
}
static void info_change(struct application_native_q1_console *owner,
    const char *key, const char *value)
{
    application_provider *p = owner->provider;
    /* SV_SendServerInfoChange is inert before the actual server exists. */
    if (!p->constructed || !p->attached || !p->state.q1 || !qa_q1_game_map_name(p->state.q1)) return;
    if (owner->info_error.code != QA_OK) return;
    qa_net_writer writer;
    qa_net_writer_init(&writer, owner->reliable_info + owner->reliable_info_size,
        sizeof(owner->reliable_info) - owner->reliable_info_size, &owner->info_error);
    qa_net_write_u8(&writer, QA_QW_SERVER_INFO);
    qa_net_write_string(&writer, key);
    qa_net_write_string(&writer, value);
    owner->reliable_info_size += qa_net_writer_size(&writer);
}
static void cvar_effect(void *opaque, qa_cvar_effect_kind kind, const qa_cvar_view *variable)
{
    struct application_native_q1_console *owner = opaque;
    if (kind != QA_CVAR_EFFECT_SERVERINFO || !owner->info_initialized ||
        dialect(owner->provider) != QA_CONSOLE_QW) return;
    info_set(owner, owner->serverinfo, 512, variable->name, variable->value, false);
    info_change(owner, variable->name, variable->value);
}
static void info_list(struct application_native_q1_console *owner, const char *info)
{
    const char *cursor = info;
    if (*cursor == '\\') ++cursor;
    while (*cursor) {
        const char *key = cursor;
        while (*cursor && *cursor != '\\') ++cursor;
        size_t size = (size_t)(cursor - key), width = size < 20 ? 20 : size;
        char field[32770];
        memcpy(field, key, size); memset(field + size, ' ', width - size); field[width] = 0;
        info_print(owner, field);
        if (!*cursor) { info_print(owner, "MISSING VALUE\n"); return; }
        const char *value = ++cursor;
        while (*cursor && *cursor != '\\') ++cursor;
        size = (size_t)(cursor - value);
        memcpy(field, value, size); field[size] = '\n'; field[size + 1] = 0;
        info_print(owner, field);
        if (*cursor) ++cursor;
    }
}
static bool info_command(void *opaque, const qa_command_invocation *invocation, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    bool local = !strcmp(invocation->argv[0], "localinfo");
    char *info = local ? owner->localinfo : owner->serverinfo;
    ++owner->calls;
    owner->info_context = &invocation->context;
    bool okay = true;
    if (invocation->argc == 1) {
        info_print(owner, local ? "Local info settings:\n" : "Server info settings:\n");
        info_list(owner, info);
    } else if (invocation->argc != 3) {
        info_print(owner, local ? "usage: localinfo [ <key> <value> ]\n" :
            "usage: serverinfo [ <key> <value> ]\n");
    } else if (*invocation->argv[1] == '*') {
        info_print(owner, "Star variables cannot be changed.\n");
    } else {
        info_set(owner, info, local ? 32768 : 512, invocation->argv[1], invocation->argv[2], false);
        if (!local) {
            const qa_cvar_view *variable;
            okay = qa_console_cvar_read(owner->console, &invocation->context,
                invocation->argv[1], &variable, error);
            if (okay && variable) {
                okay = qa_console_cvar_apply(owner->console, &invocation->context,
                    &(qa_cvars_edit_command){.kind = QA_CVARS_EDIT_ASSIGN,
                        .name = invocation->argv[1], .value = invocation->argv[2], .force = true,
                        .source_dialect = QA_CONSOLE_QW}, error);
            }
            if (okay) info_change(owner, invocation->argv[1], invocation->argv[2]);
        }
    }
    owner->info_context = NULL;
    --owner->calls;
    if (okay && owner->info_error.code != QA_OK) {
        if (error) *error = owner->info_error;
        okay = false;
    }
    return okay;
}
static bool visible_gamedir_command(void *opaque, const qa_command_invocation *invocation,
    qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    (void)error;
    ++owner->calls;
    owner->info_context = &invocation->context;
    if (invocation->argc == 1) {
        const char *value; size_t size;
        info_value(owner->serverinfo, "*gamedir", &value, &size);
        char text[sizeof(owner->serverinfo) + 32];
        snprintf(text, sizeof(text), "Current *gamedir: %.*s\n", (int)size, value);
        info_print(owner, text);
    } else if (invocation->argc != 2) {
        info_print(owner, "Usage: sv_gamedir <newgamedir>\n");
    } else {
        const char *directory = invocation->argv[1];
        if (strstr(directory, "..") || strpbrk(directory, "/\\:"))
            info_print(owner, "*Gamedir should be a single filename, not a path\n");
        else info_set(owner, owner->serverinfo, 512, "*gamedir", directory, true);
    }
    owner->info_context = NULL;
    --owner->calls;
    return true;
}
bool application_native_q1_source_info(application_provider *provider, bool local,
    const char **out, qa_error *error)
{
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!owner || !out || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->launch ||
        dialect(provider) != QA_CONSOLE_QW || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld info lost its actual Source console");
    *out = local ? owner->localinfo : owner->serverinfo;
    return true;
}
bool application_native_q1_world_info(void *opaque, const char *key, qa_string_id *out, qa_error *error)
{
    application_provider *provider = opaque;
    const char *server, *local, *value; size_t size;
    if (!key || !out || !application_native_q1_source_info(provider, false, &server, error) ||
        !application_native_q1_source_info(provider, true, &local, error)) return false;
    info_value(server, key, &value, &size);
    if (!size) info_value(local, key, &value, &size);
    return qa_strings_intern(qa_session_strings(provider->application->session),
        (qa_bytes){(const uint8_t *)value, size}, out, error);
}
void application_native_q1_source_info_map_reset(void *opaque)
{
    application_provider *provider = opaque;
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!owner) return;
    owner->reliable_info_size = 0; owner->info_error = (qa_error){0};
}
bool application_native_q1_source_info_flush(application_provider *provider, qa_error *error)
{
    const char *server;
    if (!application_native_q1_source_info(provider, false, &server, error)) return false;
    struct application_native_q1_console *owner = provider->native_q1_console;
    if (owner->info_error.code != QA_OK) { if (error) *error = owner->info_error; return false; }
    if (!owner->reliable_info_size) return true;
    uint64_t time_ns; double seconds;
    if (!qa_q1_game_clock_read(provider->state.q1, &time_ns, &seconds))
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld serverinfo lost its Source clock");
    if (!application_emit_protocol(provider, &(qa_application_protocol_event){
        .provider = provider->owner, .dialect = QA_CLOCK_QUAKEWORLD, .time_ns = time_ns,
        .reliable = true, .payload = {owner->reliable_info, owner->reliable_info_size}}, error)) return false;
    owner->reliable_info_size = 0;
    return true;
}

static qa_cvars *cvar_owner(void *opaque, const qa_command_context *command, const char *name)
{
    struct application_native_q1_console *owner = opaque;
    qa_cvars *selected = application_startup_cvar_owner(owner->provider, owner->console, command, name);
    return selected ? selected : owner->cvars;
}

static qa_cvars *visible_cvars(void *opaque, const qa_command_context *command, size_t index)
{
    struct application_native_q1_console *owner = opaque;
    qa_cvars *selected = NULL;
    if (application_startup_visible_cvars(owner->provider, owner->console, command, index, &selected))
        return selected;
    return index == 0 ? owner->cvars : NULL;
}

static bool cvar_edit(void *opaque, const qa_command_context *command,
    qa_cvars *registry, qa_cvars_edit **out, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    return application_startup_cvar_edit(owner->provider, owner->console, command, registry, out, error);
}

static qa_command_result command(void *opaque, const qa_command_invocation *invocation, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    ++owner->calls;
    application_provider *p = owner->provider;
    qa_command_result result = application_command_fallback(p->application, invocation, error);
    const qa_application_startup_hooks *hooks = p->application->startup_hooks;
    if (result == QA_COMMAND_UNHANDLED && hooks && hooks->source_common_command) {
        const qa_launch_instance *descriptor = source_descriptor(p);
        if (!descriptor) {
            --owner->calls;
            application_fail(error, QA_ERROR_ARGUMENT, "Native Source command lost its actual published descriptor");
            return QA_COMMAND_FAILED;
        }
        qa_application_startup_source source = {.descriptor = descriptor,
            .scope = {.provider = p->owner, .kind = QA_APPLICATION_CONSOLE_Q1_GAME},
            .console = owner->console, .cvars = owner->cvars, .command = invocation->context,
            .declaration_owner = p->owner};
        bool handled = false;
        bool okay = hooks->source_common_command(hooks->context, p->application,
            &source, invocation, &handled, error);
        result = !okay ? QA_COMMAND_FAILED : handled ? QA_COMMAND_HANDLED : QA_COMMAND_UNHANDLED;
    }
    --owner->calls;
    return result;
}

static bool read_script(void *opaque, const qa_command_context *command,
                        const char *path, qa_bytes *out, void **lease, qa_error *error)
{
    struct application_native_q1_console *owner = opaque;
    if (!active(owner, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 script publication has retired");
    if (application_startup_source_active(owner->provider))
        return application_startup_script_read(owner->provider, command, path, out, lease, error);
    if (application_startup_source_scripts(owner->provider))
        return application_startup_source_script_read(owner->provider, owner->console,
            command, path, out, lease, error);
    qa_resource *resource = NULL;
    if (!qa_vfs_acquire(owner->provider->launch->content, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}

static void release_script(void *opaque, void *lease)
{
    struct application_native_q1_console *owner = opaque;
    if (application_startup_source_active(owner->provider))
        application_startup_script_release(owner->provider, lease);
    else if (application_startup_source_scripts(owner->provider))
        application_startup_source_script_release(owner->provider, owner->console, lease);
    else qa_resource_release(lease);
}

static void script_complete(void *opaque, const qa_command_context *command,
    const char *path, bool success)
{
    struct application_native_q1_console *owner = opaque;
    application_startup_script_complete(owner->provider, command, path, success);
}

static bool allow_command(void *opaque, const qa_command_invocation *command)
{
    struct application_native_q1_console *owner = opaque;
    return application_startup_command_allowed(owner->provider, command);
}

bool application_native_q1_console_create_restored(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->owner ||
        !provider->application || !provider->application->session || !provider->launch ||
        !provider->product || provider->product->family != QA_GAME_Q1 ||
        provider->close_pending || provider->native_q1_console)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console requires its actual source owner");
    struct application_native_q1_console *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "allocating native Q1 source console");
    owner->provider = provider;
    qa_cvar_options cvars = {.dialect = dialect(provider), .user = owner, .print = cvar_print, .effect = cvar_effect};
    owner->cvars = qa_cvars_create(&cvars, error);
    qa_console_options options = {.context = {.owner = provider->owner, .dialect = dialect(provider),
        .origin = QA_COMMAND_SERVER}, .cvars = owner->cvars, .user = owner, .print = print,
        .cvar_owner = cvar_owner, .visible_cvars = visible_cvars, .cvar_edit = cvar_edit,
        .capture_context = capture, .context_active = active, .read_script = read_script,
        .release_script = release_script, .script_complete = script_complete,
        .allow_command = allow_command, .source_command = command};
    if (owner->cvars) owner->console = qa_console_create(&options, error);
    if (!owner->console) {
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        return false;
    }
    provider->native_q1_console = owner;
    if (dialect(provider) == QA_CONSOLE_QW &&
        (!qa_console_register(owner->console, "serverinfo", NULL, provider->owner,
            false, info_command, owner, error) ||
         !qa_console_register(owner->console, "localinfo", NULL, provider->owner,
            false, info_command, owner, error) ||
         !qa_console_register(owner->console, "sv_gamedir", NULL, provider->owner,
            false, visible_gamedir_command, owner, error))) {
        qa_console_destroy(owner->console); qa_cvars_destroy(owner->cvars);
        free(owner); provider->native_q1_console = NULL; return false;
    }
    return true;
}

static bool clone_source(application_provider *provider, qa_cvars *destination,
                         bool *cloned, qa_error *error)
{
    *cloned = false;
    struct application_native_q1_console *owner = provider->native_q1_console;
    for (application_provider *previous = provider->application->live_providers; previous;
         previous = previous->next_live) {
        if (previous == provider || previous->kind != APPLICATION_PROVIDER_Q1 ||
            !previous->constructed || !previous->attached || previous->close_pending ||
            !previous->launch || !previous->native_q1_console || previous->owner != provider->owner ||
            strcmp(previous->launch->selection.instance, provider->launch->selection.instance) ||
            dialect(previous) != dialect(provider)) continue;
        if (!application_native_q1_console_idle(previous))
            return application_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld info carry requires its idle Source");
        memcpy(owner->serverinfo, previous->native_q1_console->serverinfo, sizeof(owner->serverinfo));
        memcpy(owner->localinfo, previous->native_q1_console->localinfo, sizeof(owner->localinfo));
        owner->info_initialized = previous->native_q1_console->info_initialized;
        break;
    }
    if (provider->application->startup_hooks) {
        qa_application_startup_source source = {.descriptor = provider->launch,
            .scope = {.provider = provider->owner, .kind = QA_APPLICATION_CONSOLE_Q1_GAME},
            .cvars = destination, .declaration_owner = provider->owner};
        if (!application_native_q1_console_at(provider, &source.console, NULL, &source.command))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 carry lost its fresh physical console");
        return application_startup_source_carry(provider, &source, cloned, error);
    }
    for (application_provider *previous = provider->application->live_providers; previous;
         previous = previous->next_live) {
        if (previous == provider || previous->kind != APPLICATION_PROVIDER_Q1 ||
            !previous->constructed || !previous->attached || previous->close_pending ||
            !previous->launch || strcmp(previous->launch->selection.instance, provider->launch->selection.instance) ||
            previous->owner != provider->owner) continue;
        qa_cvars *source = application_native_q1_console_registry(previous);
        if (!source || qa_cvars_dialect(source) != qa_cvars_dialect(destination)) continue;
        qa_buffer bytes = {0};
        qa_cvars_restore *ticket = NULL;
        bool okay = application_native_q1_console_idle(previous) &&
            qa_cvars_save_capture(source, &bytes, error) &&
            qa_cvars_save_prepare(destination, (qa_bytes){bytes.data, bytes.size}, &ticket, error) &&
            qa_cvars_save_commit(ticket, error);
        if (!okay) qa_cvars_save_abort(ticket);
        qa_buffer_free(&bytes);
        if (okay) *cloned = true;
        return okay;
    }
    return true;
}

bool application_native_q1_console_create(application_provider *provider,
                                          const qa_q1_options *rules, qa_error *error)
{
    if (!rules || !application_native_q1_console_create_restored(provider, error)) return false;
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    bool cloned;
    bool okay = clone_source(provider, cvars, &cloned, error);
    for (size_t i = 0; okay && !cloned && i < qa_cvars_count(provider->application->cvars); ++i) {
        const qa_cvar_view *startup = qa_cvars_at(provider->application->cvars, i);
        if (!startup || (startup->owner && startup->owner != provider->owner) ||
            (!startup->console_created && !(startup->flags & QA_CVAR_ARCHIVE) && startup->owner != provider->owner)) continue;
        okay = qa_cvars_register(cvars, startup->name,
            startup->latched_value ? startup->latched_value : startup->value,
            startup->flags & (QA_CVAR_ARCHIVE | QA_CVAR_USERINFO | QA_CVAR_SERVERINFO),
            provider->owner, startup->description, error);
    }
    static const char *const names[] = {"skill", "deathmatch", "coop", "teamplay", "sv_gravity",
        "sv_maxspeed", "samelevel", "timelimit", "fraglimit", "gamecfg", "sv_cheats", "footsteps",
        "maxclients", "registered", "developer", "sv_aim"};
    char skill[16], deathmatch[16], coop[2], teamplay[16], gravity[32], gamecfg[16], maximum[16], aim[32];
    snprintf(skill, sizeof(skill), "%u", rules->skill);
    snprintf(deathmatch, sizeof(deathmatch), "%d", rules->deathmatch);
    snprintf(coop, sizeof(coop), "%u", rules->coop ? 1u : 0u);
    snprintf(teamplay, sizeof(teamplay), "%d", rules->teamplay);
    snprintf(gravity, sizeof(gravity), "%.9g", (double)rules->gravity);
    snprintf(gamecfg, sizeof(gamecfg), "%u", rules->gamecfg);
    snprintf(maximum, sizeof(maximum), "%u", rules->quakeworld ? 8u : rules->max_clients);
    snprintf(aim, sizeof(aim), "%.9g", (double)rules->aim_threshold);
    const char *values[] = {skill, deathmatch, coop, teamplay, gravity, "320", "0", "0", "0", gamecfg,
        "0", "1", maximum, "1", "0", aim};
    for (size_t i = 0; okay && i < sizeof(names) / sizeof(*names); ++i) {
        if (!qa_cvars_find(cvars, names[i]))
            okay = qa_cvars_register(cvars, names[i], values[i], 0, provider->owner, NULL, error);
        const qa_cvar_view *startup = !cloned ? qa_cvars_find(provider->application->cvars, names[i]) : NULL;
        if (okay && startup && (!startup->owner || startup->owner == provider->owner))
            okay = qa_cvars_set(cvars, names[i], startup->latched_value ? startup->latched_value : startup->value,
                true, error);
    }
    if (rules->quakeworld) {
        static const char *const qw_names[] = {"sv_maxvelocity", "sv_stopspeed",
            "sv_spectatormaxspeed", "sv_accelerate", "sv_airaccelerate",
            "sv_wateraccelerate", "sv_friction", "sv_waterfriction",
            "maxspectators", "pausable", "sv_spectalk", "sv_mapcheck",
            "hostname", "spawn", "watervis", "sv_phs", "password", "spectator_password", "sv_highchars"};
        static const char *const qw_values[] = {"2000", "100", "500", "10", "0.7",
            "10", "4", "4", "8", "1", "1", "1", "unnamed", "0", "0", "1", "", "", "1"};
        for (size_t i = 0; okay && i < sizeof(qw_names) / sizeof(*qw_names); ++i) {
            if (!qa_cvars_find(cvars, qw_names[i]))
                okay = qa_cvars_register(cvars, qw_names[i], qw_values[i], 0,
                    provider->owner, NULL, error);
            const qa_cvar_view *startup = !cloned
                ? qa_cvars_find(provider->application->cvars, qw_names[i]) : NULL;
            if (okay && startup && (!startup->owner || startup->owner == provider->owner))
                okay = qa_cvars_set(cvars, qw_names[i],
                    startup->latched_value ? startup->latched_value : startup->value, true, error);
        }
        static const char *const info_names[] = {"fraglimit", "timelimit", "teamplay",
            "samelevel", "maxclients", "maxspectators", "hostname", "deathmatch", "spawn",
            "watervis"};
        struct application_native_q1_console *owner = provider->native_q1_console;
        bool fresh_info = !owner->info_initialized;
        for (size_t i = 0; okay && i < sizeof(info_names) / sizeof(*info_names); ++i) {
            okay = qa_cvars_add_flags(cvars, info_names[i], QA_CVAR_SERVERINFO, error);
            const qa_cvar_view *variable = qa_cvars_find(cvars, info_names[i]);
            if (okay && fresh_info) info_set(owner, owner->serverinfo, 512,
                variable->name, variable->value, false);
        }
        if (okay && fresh_info) {
            /* QW SV_InitLocal publishes VERSION (bothdefs.h: 2.40). */
            info_set(owner, owner->serverinfo, 512, "*version", "2.40", true);
            for (size_t i = 0; i < qa_cvars_count(cvars); ++i) {
                const qa_cvar_view *variable = qa_cvars_at(cvars, i);
                bool standard = false;
                for (size_t n = 0; n < sizeof(info_names) / sizeof(*info_names); ++n)
                    if (!strcmp(variable->name, info_names[n])) standard = true;
                if (!standard && (variable->flags & QA_CVAR_SERVERINFO))
                    info_set(owner, owner->serverinfo, 512, variable->name, variable->value, false);
            }
            owner->info_initialized = true;
        }
    }
    if (okay) okay = qa_cvars_set(cvars, "skill", skill, true, error) &&
        qa_cvars_set(cvars, "deathmatch", deathmatch, true, error) &&
        qa_cvars_set(cvars, "coop", coop, true, error) &&
        (rules->quakeworld || qa_cvars_set(cvars, "maxclients", maximum, true, error));
    if (!okay) application_native_q1_console_destroy(provider, NULL);
    return okay;
}

bool application_native_q1_console_destroy(application_provider *provider, qa_error *error)
{
    if (!provider || !application_native_q1_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console is borrowed");
    struct application_native_q1_console *owner = provider->native_q1_console;
    if (owner) {
        if (!application_startup_source_retire(provider, owner->console, owner->cvars, error)) return false;
        qa_console_destroy(owner->console);
        qa_cvars_destroy(owner->cvars);
        free(owner);
        provider->native_q1_console = NULL;
    }
    return true;
}

bool application_native_q1_console_at(application_provider *provider, qa_console **console,
                                      qa_cvars **cvars, qa_command_context *context)
{
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!owner || !console) return false;
    *console = owner->console;
    if (cvars) *cvars = owner->cvars;
    if (context) *context = (qa_command_context){.owner = provider->owner,
        .dialect = dialect(provider), .origin = QA_COMMAND_SERVER};
    return true;
}

bool application_native_q1_console_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    struct application_native_q1_console *owner = provider ? provider->native_q1_console : NULL;
    if (!cvars || !out || !application_native_q1_console_idle(provider) || owner->info_error.code != QA_OK)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console capture requires its idle Source owner");
    qa_buffer registry = {0};
    if (!qa_cvars_save_capture(cvars, &registry, error)) return false;
    size_t server_size = strlen(owner->serverinfo), local_size = strlen(owner->localinfo);
    if (registry.size > UINT32_MAX || registry.size > SIZE_MAX - 26 - server_size - local_size - owner->reliable_info_size) {
        qa_buffer_free(&registry);
        return application_fail(error, QA_ERROR_MEMORY, "native Q1 console checkpoint exceeds its extent");
    }
    qa_buffer bytes = {.size = 26 + registry.size + server_size + local_size + owner->reliable_info_size};
    bytes.data = malloc(bytes.size);
    if (!bytes.data) { qa_buffer_free(&registry); return application_fail(error, QA_ERROR_MEMORY, "allocating Source console checkpoint"); }
    qa_net_writer writer; qa_net_writer_init(&writer, bytes.data, bytes.size, error);
    bool okay = qa_net_write_u32(&writer, UINT32_C(0x3149514e)) && qa_net_write_u32(&writer, 1) &&
        qa_net_write_u8(&writer, (uint8_t)dialect(provider)) &&
        qa_net_write_u8(&writer, owner->info_initialized ? 1 : 0) &&
        qa_net_write_u32(&writer, (uint32_t)registry.size) && qa_net_write_u32(&writer, (uint32_t)server_size) &&
        qa_net_write_u32(&writer, (uint32_t)local_size) && qa_net_write_u32(&writer, (uint32_t)owner->reliable_info_size) &&
        qa_net_write_data(&writer, registry.data, registry.size) &&
        qa_net_write_data(&writer, owner->serverinfo, server_size) &&
        qa_net_write_data(&writer, owner->localinfo, local_size) &&
        qa_net_write_data(&writer, owner->reliable_info, owner->reliable_info_size);
    qa_buffer_free(&registry);
    if (!okay) { qa_buffer_free(&bytes); return false; }
    *out = bytes; return true;
}
static bool saved_info(qa_bytes bytes)
{
    if (bytes.size && bytes.data[0] != '\\') return false;
    /* Filtering high-bit characters can introduce delimiters/quotes and
     * incomplete pairs. They are genuine retained Source bytes. */
    for (size_t i = 0; i < bytes.size; ++i)
        if (bytes.data[i] < 14) return false;
    return true;
}
static bool saved_info_messages(qa_bytes bytes)
{
    size_t cursor = 0;
    while (cursor < bytes.size) {
        if (bytes.data[cursor++] != QA_QW_SERVER_INFO) return false;
        for (size_t n = 0; n < 2; ++n) {
            while (cursor < bytes.size && bytes.data[cursor]) ++cursor;
            if (cursor == bytes.size) return false;
            ++cursor;
        }
    }
    return true;
}
bool application_native_q1_console_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    if (!cvars || !application_native_q1_console_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 console restore requires its idle Source owner");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    uint32_t magic = qa_net_read_u32(&reader), version = qa_net_read_u32(&reader);
    uint8_t source_dialect = qa_net_read_u8(&reader), initialized = qa_net_read_u8(&reader);
    uint32_t registry_size = qa_net_read_u32(&reader), server_size = qa_net_read_u32(&reader),
        local_size = qa_net_read_u32(&reader), reliable_size = qa_net_read_u32(&reader);
    if (reader.failed) return false;
    if (magic != UINT32_C(0x3149514e) || version != 1 || source_dialect != dialect(provider) ||
        initialized > 1 || server_size > 512 || local_size > 32768 || reliable_size > 1450 ||
        (source_dialect != QA_CONSOLE_QW && (initialized || server_size || local_size || reliable_size)))
        return application_fail(error, QA_ERROR_FORMAT, "native Q1 console checkpoint changes its Source recipe");
    qa_bytes registry, server, local, reliable;
    if (!qa_net_read_bytes(&reader, registry_size, &registry) || !qa_net_read_bytes(&reader, server_size, &server) ||
        !qa_net_read_bytes(&reader, local_size, &local) || !qa_net_read_bytes(&reader, reliable_size, &reliable) ||
        !qa_net_reader_finish(&reader)) return false;
    if (!saved_info(server) || !saved_info(local) || !saved_info_messages(reliable))
        return application_fail(error, QA_ERROR_FORMAT, "native Q1 console checkpoint has invalid Source info bytes");
    qa_cvars_restore *ticket = NULL;
    bool okay = qa_cvars_save_prepare(cvars, registry, &ticket, error) && qa_cvars_save_commit(ticket, error);
    if (!okay) { qa_cvars_save_abort(ticket); return false; }
    struct application_native_q1_console *owner = provider->native_q1_console;
    memcpy(owner->serverinfo, server.data, server.size); owner->serverinfo[server.size] = 0;
    memcpy(owner->localinfo, local.data, local.size); owner->localinfo[local.size] = 0;
    memcpy(owner->reliable_info, reliable.data, reliable.size); owner->reliable_info_size = reliable.size;
    owner->info_initialized = initialized != 0; owner->info_error = (qa_error){0};
    return true;
}

bool application_native_q1_cvar(void *opaque, qa_string_id name, float *out, qa_error *error)
{
    application_provider *provider = opaque;
    qa_cvars *cvars = application_native_q1_console_registry(provider);
    const char *text = provider && provider->application && provider->application->session
        ? qa_strings_cstr(qa_session_strings(provider->application->session), name) : NULL;
    if (!cvars || !text || !out || !provider->constructed || provider->close_pending ||
        provider->application->destroy_requested)
        return application_fail(error, QA_ERROR_NOT_FOUND, "native Q1 cvar source has retired");
    const qa_cvar_view *value = qa_cvars_find(cvars, text);
    if (value && value->owner && value->owner != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q1 cvar belongs to another source");
    *out = value ? (float)qa_source_fround(value->number) : 0;
    return true;
}

bool application_native_q1_client_attack(void *opaque, qa_actor_id actor, bool *out)
{
    application_provider *provider = opaque;
    qa_application_control_view control;
    uint32_t slot;
    if (!out || !provider || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->constructed ||
        !provider->attached || provider->close_pending || provider->application->destroy_requested ||
        !qa_q1_native_client_slot_prepared(provider->state.q1, actor, &slot, NULL)) return false;
    *out = qa_application_control_read(provider->application, actor, &control) && (control.buttons & 1u) != 0;
    return true;
}
