#include "internal.h"
#include "native_q3_console.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void application_console_print(void *context, const qa_command_context *command,
                                 const char *text)
{
    qa_application *application = context;
    if (application != NULL && application->console_print != NULL) {
        application->console_print(application->guest_context, command, text);
        return;
    }
    if (text != NULL) fputs(text, stderr);
}

static void cvar_print(void *context, const char *text)
{
    qa_application *application = context;
    if (application->console != NULL)
        qa_console_emit(application->console, NULL, text);
    else
        application_console_print(context, NULL, text);
}

static bool read_script(void *context, const qa_command_context *command,
                         const char *path, qa_bytes *out, void **lease,
                         qa_error *error)
{
    (void)command;
    qa_application *application = context;
    const qa_launch_snapshot *snapshot = qa_application_launch(application);
    qa_vfs *mounts = snapshot == NULL ? NULL : qa_launch_snapshot_mounts(snapshot);
    qa_resource *resource = NULL;
    if (mounts == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND, "console script has no active content mounts");
    if (!qa_vfs_acquire(mounts, path, &resource, NULL, error)) return false;
    *out = qa_resource_bytes(resource);
    *lease = resource;
    return true;
}

static void release_script(void *context, void *lease)
{
    (void)context;
    qa_resource_release(lease);
}

static qa_cvars *cvar_owner(void *context, const qa_command_context *command, const char *name)
{
    qa_application *application = context;
    for (application_provider *p = application->live_providers; command && p; p = p->next_live)
        if (p->owner == command->owner && p->kind == APPLICATION_PROVIDER_Q3)
            return p->constructed && p->attached && !p->close_pending
                ? application_native_q3_cvar_owner(p, name) : NULL;
    return application->cvars;
}

static qa_cvars *visible_cvars(void *context, const qa_command_context *command, size_t index)
{
    qa_application *application = context;
    qa_cvars *source = cvar_owner(context, command, "");
    return index == 0 ? source : index == 1 && source != application->cvars
        ? application->cvars : NULL;
}

bool application_console_create(qa_application *application, qa_error *error)
{
    qa_cvar_options cvars = {.dialect = QA_CONSOLE_Q3,
        .user = application, .print = cvar_print};
    application->cvars = qa_cvars_create(&cvars, error);
    if (application->cvars == NULL) return false;
    qa_console_options console = {.context = {.dialect = QA_CONSOLE_Q3,
        .origin = QA_COMMAND_LOCAL}, .cvars = application->cvars,
        .user = application, .print = application_console_print,
        .cvar_owner = cvar_owner, .visible_cvars = visible_cvars,
        .read_script = read_script, .release_script = release_script,
        .capture_context = application_command_capture,
        .context_active = application_command_active,
        .source_command = application_command_fallback};
    application->console = qa_console_create(&console, error);
    return application->console != NULL;
}

static void guest_print(void *context, const char *text)
{
    application_provider *provider = context;
    qa_command_context command = {.owner = provider->owner, .dialect = QA_CONSOLE_Q3,
        .origin = QA_COMMAND_SERVER};
    qa_console_emit(provider->application->console, &command, text);
}

static uint32_t guest_milliseconds(void *context)
{
    application_provider *provider = context;
    qa_clock_state clock;
    uint64_t time = qa_session_clock(provider->application->session,
        provider->owner, &clock) ? clock.frame.time_ns : provider->component.clock.initial_time_ns;
    return (uint32_t)(time / UINT64_C(1000000));
}

static int32_t guest_calendar(void *context, qa_q3_host_calendar *out)
{
    (void)context;
    time_t now = time(NULL);
    const struct tm *calendar = localtime(&now);
    if (calendar == NULL) {
        if (out != NULL) *out = (qa_q3_host_calendar){0};
        return -1;
    }
    if (out != NULL) *out = (qa_q3_host_calendar){calendar->tm_sec, calendar->tm_min,
        calendar->tm_hour, calendar->tm_mday, calendar->tm_mon,
        calendar->tm_year, calendar->tm_wday, calendar->tm_yday, calendar->tm_isdst};
    uint32_t bits = (uint32_t)now;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static qa_collision_geometry *guest_geometry(void *context)
{
    qa_application *application = context;
    return application->world == NULL ? NULL : qa_world_geometry(application->world);
}

static qa_actor_id guest_world_actor(void *context)
{
    application_provider *provider = context;
    qa_physics *physics = provider->application->physics;
    return physics == NULL ? (qa_actor_id){0} : physics->world_actor;
}

static bool guest_player_velocity(void *context, qa_actor_id actor,
                                    qa_vec3 velocity, qa_error *error)
{
    application_provider *provider = context;
    return application_control_velocity(provider->application, actor, velocity, error);
}

static bool guest_load_collision(void *context, const char *path, qa_error *error)
{
    qa_application *application = context;
    const char *map = qa_strings_cstr(qa_session_strings(application->session), application->current_map);
    if (path == NULL || map == NULL || application->world == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND, "guest collision map is not published");
    if (!strncmp(path, "maps/", 5)) path += 5;
    if (!strncmp(map, "maps/", 5)) map += 5;
    size_t path_length = strlen(path), map_length = strlen(map);
    if (path_length >= 4 && !strcmp(path + path_length - 4, ".bsp")) path_length -= 4;
    if (map_length >= 4 && !strcmp(map + map_length - 4, ".bsp")) map_length -= 4;
    if (path_length != map_length || memcmp(path, map, map_length))
        return application_fail(error, QA_ERROR_ARGUMENT, "guest requested collision for a different map");
    return true;
}

bool application_q3_guest_services(qa_application *application,
                                    application_provider *provider,
                                    qa_qvm_role role, uint32_t seat,
                                    uint64_t service_owner,
                                    qa_q3_host_options *out, qa_error *error)
{
    if (application == NULL || provider == NULL || out == NULL ||
        provider->application != application || !service_owner || (unsigned)role > QA_QVM_UI)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 guest service request");
    const qa_product *product = provider->product;
    const char *directory = product == NULL ? "" : product->directory;
    if (directory != NULL) {
        const char *last = strrchr(directory, '/');
        if (last != NULL) directory = last + 1;
    }
    qa_q3_host_options services = {.role = role, .service_owner = service_owner,
        .session = application->session, .world = application->world,
        .owner = provider->owner, .cvars = application->cvars,
        .console = application->console, .mounts = provider->launch->content,
        .game_directory = directory,
        .command_context = {.owner = provider->owner, .seat = seat,
            .dialect = QA_CONSOLE_Q3,
            .origin = role == QA_QVM_GAME ? QA_COMMAND_SERVER : QA_COMMAND_SEAT},
        .common = {.context = provider, .print = guest_print,
            .milliseconds = guest_milliseconds, .calendar = guest_calendar},
        .server = {.context = provider, .player_velocity = guest_player_velocity,
            .world_actor = guest_world_actor},
        .collision = {.context = application, .geometry = guest_geometry,
            .load_map = guest_load_collision}};
    for (size_t i = 0; i < qa_vfs_mount_count(services.mounts); ++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(services.mounts, i, &mount) && mount.writable) {
            services.writable_mount = mount.id;
            break;
        }
    }
    if (application->q3_services != NULL &&
        !application->q3_services(application->guest_context, application,
            provider->owner, role, seat, &services, error)) return false;
    if (role != QA_QVM_GAME && application->q3_services == NULL)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 UI/cgame needs a platform/client service owner");
    if (services.session != application->session || services.owner != provider->owner ||
        services.service_owner != service_owner ||
        services.role != role || services.cvars == NULL || services.console == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 services changed core ownership");
    *out = services;
    return true;
}

bool application_q3_guest_native_options(qa_application *application,
                                          application_provider *provider,
                                          qa_qvm_role role, uint32_t seat,
                                          qa_native_host_instance_options *out,
                                          qa_error *error)
{
    (void)seat;
    if (application == NULL || provider == NULL || out == NULL ||
        provider->application != application || (unsigned)role > QA_QVM_UI)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid native Q3 options request");
    uint64_t step = provider->launch->selection.clock.interval_ns;
    *out = (qa_native_host_instance_options){.runner = application->native_runner};
    if (step != 0) {
        uint64_t rate = UINT64_C(1000000000) / step;
        uint64_t milliseconds = step / UINT64_C(1000000);
        if (rate > UINT32_MAX || milliseconds > UINT32_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "native source clock exceeds its ABI fields");
        out->tick_rate = (uint32_t)rate;
        out->frame_milliseconds = (uint32_t)milliseconds;
        out->frame_seconds = (float)((double)step / 1000000000.0);
    }
    return true;
}
