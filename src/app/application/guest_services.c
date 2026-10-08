#include "qa/platform_services.h"
#include "internal.h"
#include "native_q3_console.h"
#include "native_q1_console.h"
#include "guest_q3_console.h"
#include "guest_q3_private.h"
#include "guest_q3_client_console.h"
#include "startup_flow.h"

#include <stdio.h>
#include <string.h>

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

static qa_command_result forward(void *context,const qa_command_invocation *command,qa_error *error)
{
    qa_application *application=context;
    return application->console_forward ?
        application->console_forward(application->guest_context,command,error) : QA_COMMAND_UNHANDLED;
}

bool application_console_create(qa_application *application, qa_error *error)
{
    qa_cvar_options cvars = {.dialect = QA_CONSOLE_Q3,
        .side = QA_CVAR_SIDE_UNSPECIFIED, .role = QA_CVAR_ROLE_ENGINE,
        .user = application, .print = cvar_print};
    application->cvars = qa_cvars_create(&cvars, error);
    if (application->cvars == NULL) return false;
    if (!qa_cvars_register(application->cvars, "sv_cheats", "", 0, 0, NULL, error)) return false;
    qa_console_options console = {.context = {.dialect = QA_CONSOLE_Q3,
        .origin = QA_COMMAND_LOCAL}, .cvars = application->cvars,
        .user = application, .print = application_console_print,
        .read_script = read_script, .release_script = release_script,
        .capture_context = application_command_capture,
        .context_active = application_command_active,
        .source_command = application_command_fallback, .forward = forward};
    console.context.cvar_view = qa_cvars_view_identity(application->cvars);
    application->console = qa_console_create(&console, error);
    return application->console != NULL;
}

static void guest_print(void *context, const char *text)
{
    application_provider *provider = context;
    qa_command_context command = {.owner = provider->owner, .dialect = QA_CONSOLE_Q3,
        .origin = QA_COMMAND_SERVER};
    qa_console *console = application_guest_q3_console_owner(provider);
    qa_console_emit(console ? console : provider->application->console, &command, text);
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
    qa_platform_timespec now;
    qa_platform_calendar_fields date;
    if (!qa_platform_clock_read(QA_PLATFORM_CLOCK_REALTIME,&now,NULL) ||
        !qa_platform_calendar(now.seconds*INT64_C(1000),true,&date,NULL)) {
        if (out) *out=(qa_q3_host_calendar){0};
        return -1;
    }
    if (out) *out=(qa_q3_host_calendar){date.second,date.minute,date.hour,date.day,
        date.month-1,date.year-1900,date.weekday,date.yearday,date.daylight};
    uint32_t bits=(uint32_t)now.seconds; int32_t result;
    memcpy(&result,&bits,sizeof(result)); return result;
}

static qa_collision_geometry *guest_geometry(void *context)
{
    application_provider *provider = context;
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_world *world = engine ? engine->world : provider->application->world;
    return world ? qa_world_geometry(world) : NULL;
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
    application_provider *provider = context;
    qa_application *application = provider->application;
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_cvars *cvars = application_guest_q3_console_registry(provider);
    const qa_cvar_view *selected = cvars ? qa_cvars_find(cvars, "mapname") : NULL;
    const char *map = selected ? selected->value :
        qa_strings_cstr(qa_session_strings(application->session), application->current_map);
    qa_world *world = engine ? engine->world : application->world;
    if (path == NULL || map == NULL || world == NULL || !qa_world_geometry(world))
        return application_fail(error, QA_ERROR_NOT_FOUND, "guest collision map is not published");
    if (!strncmp(path, "maps/", 5)) path += 5;
    size_t path_length = strlen(path), map_length = strlen(map);
    if (path_length > 4 && !strcmp(path + path_length - 4, ".bsp")) path_length -= 4;
    if (path_length != map_length || memcmp(path, map, map_length))
        return application_fail(error, QA_ERROR_ARGUMENT, "guest requested collision for a different map");
    return true;
}

bool application_q3_guest_services_descriptor(qa_application *application,
                                    application_provider *provider,
                                    const qa_launch_instance *descriptor,
                                    qa_qvm_role role, uint32_t seat,
                                    uint64_t service_owner,
                                    qa_q3_host_options *out, qa_error *error)
{
    if (application == NULL || provider == NULL || out == NULL ||
        provider->application != application || !service_owner || (unsigned)role > QA_QVM_UI)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 guest service request");
    const qa_product *product = descriptor ? qa_catalog_product(qa_launch_instance_catalog(descriptor),
        descriptor->selection.product) : NULL;
    if (!descriptor || !product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 services lack their real retained descriptor");
    qa_fs_root *write_root = qa_catalog_product_write_root(qa_launch_instance_catalog(descriptor),
        descriptor->selection.product);
    const char *directory = product == NULL ? "" : product->directory;
    if (directory != NULL) {
        const char *last = strrchr(directory, '/');
        if (last != NULL) directory = last + 1;
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_q3_host_options services = {.role = role, .service_owner = service_owner,
        .session = application->session, .world = engine ? engine->world : application->world,
        .owner = provider->owner, .cvars = application->cvars,
        .console = application->console, .mounts = descriptor->content,
        .write_view = {.root = write_root},
        .game_directory = directory,
        .command_context = {.owner = provider->owner, .seat = role == QA_QVM_GAME ? 0 : seat,
            .dialect = QA_CONSOLE_Q3,
            .origin = role == QA_QVM_GAME ? QA_COMMAND_SERVER : QA_COMMAND_SEAT},
        .common = {.context = provider, .print = guest_print,
            .milliseconds = guest_milliseconds, .calendar = guest_calendar},
        .server = {.context = provider, .player_velocity = guest_player_velocity,
            .world_actor = guest_world_actor},
        .collision = {.context = provider, .geometry = guest_geometry,
            .load_map = guest_load_collision}};
    if (role == QA_QVM_GAME) {
        services.cvars = application_guest_q3_console_registry(provider);
        services.console = application_guest_q3_console_owner(provider);
        services.engine_cvars = application->cvars;
        if (!services.cvars || !services.console)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original GAME has no private console owner");
    } else if (!application_guest_q3_client_console_at(engine, role, seat, &services.console, &services.cvars)) {
        return application_fail(error, QA_ERROR_ARGUMENT, "Original CLIENT has no retained private seat console");
    }
    for (size_t i = 0; i < qa_vfs_mount_count(services.mounts); ++i) {
        qa_vfs_mount_info mount;
        if (qa_vfs_mount_at(services.mounts, i, &mount) && mount.writable && !mount.is_archive &&
            qa_fs_root_same_object(write_root, qa_vfs_mount_root(services.mounts, mount.id))) {
            services.writable_mount = mount.id;
            break;
        }
    }
    if (application->q3_services != NULL) {
        if (engine) engine->constructing_services = &services;
        bool prepared = application->q3_services(application->guest_context, application,
            provider->owner, role, seat, &services, error);
        if (engine) engine->constructing_services = NULL;
        if (!prepared) return false;
    }
    if (role != QA_QVM_GAME && application->q3_services == NULL)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 UI/cgame needs a platform/client service owner");
    if (role != QA_QVM_GAME && descriptor != provider->launch &&
        (!services.collision.geometry || !services.collision.load_map ||
         services.collision.geometry == guest_geometry || services.collision.load_map == guest_load_collision))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Remote replacement requires its actual private map and collision services");
    if (services.session != application->session || services.owner != provider->owner ||
        services.service_owner != service_owner ||
        services.role != role || services.cvars == NULL || services.console == NULL ||
        services.mounts != descriptor->content ||
        (services.write_view.root != write_root &&
            !qa_fs_root_same_object(services.write_view.root, write_root)) ||
        (role == QA_QVM_GAME &&
            (services.cvars != application_guest_q3_console_registry(provider) ||
             services.console != application_guest_q3_console_owner(provider) ||
             services.engine_cvars != application->cvars)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 services changed core ownership");
    bool writable_owner = services.writable_mount == 0;
    for (size_t i = 0; !writable_owner && i < qa_vfs_mount_count(services.mounts); ++i) {
        qa_vfs_mount_info mount;
        writable_owner = qa_vfs_mount_at(services.mounts, i, &mount) &&
            mount.id == services.writable_mount && mount.writable && !mount.is_archive &&
            qa_fs_root_same_object(write_root, qa_vfs_mount_root(services.mounts, mount.id));
    }
    if (!writable_owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 services changed their selected write-root authority");
    services.command_context.cvar_view = qa_cvars_view_identity(services.cvars);
    *out = services;
    return true;
}

bool application_q3_guest_services(qa_application *application, application_provider *provider,
    qa_qvm_role role, uint32_t seat, uint64_t service_owner,
    qa_q3_host_options *out, qa_error *error)
{
    return application_q3_guest_services_descriptor(application, provider,
        provider ? provider->launch : NULL, role, seat, service_owner, out, error);
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
