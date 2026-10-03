#include "internal.h"
#include "qa/source_save.h"

struct qa_restart_controls {
    qa_cvars *vars;
    qa_restart_service services[3];
    qa_restart_kind pending[3];
    size_t count;
    bool running, registered[3];
    qa_console *console;
    uint64_t owner;
};
static const char *const commands[] = {"vid_restart", "in_restart", "snd_restart"};
qa_restart_controls *qa_restart_create(qa_cvars *vars, const qa_restart_service services[3],
                                       qa_error *e) {
    if (!vars || !services) {
        settings_fail(e, "Missing restart owners");
        return NULL;
    }
    qa_restart_controls *controls = calloc(1, sizeof(*controls));
    if (!controls) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating restart controls");
        return NULL;
    }
    controls->vars = vars;
    for (size_t i = 0; i < 3; ++i) {
        if (!services[i].restart || (services[i].latched_count && !services[i].latched) ||
            services[i].latched_count > SIZE_MAX / sizeof(char *)) {
            settings_fail(e, "Incomplete restart service");
            qa_restart_destroy(controls, NULL);
            return NULL;
        }
        size_t count = services[i].latched_count;
        char **names = count ? calloc(count, sizeof(*names)) : NULL;
        if (count && !names) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating restart cvar list");
            qa_restart_destroy(controls, NULL);
            return NULL;
        }
        controls->services[i] = services[i];
        controls->services[i].latched = (const char *const *)names;
        for (size_t j = 0; j < count; ++j) {
            const char *name = services[i].latched[j];
            if (!name || !*name) {
                settings_fail(e, "Missing latched cvar name");
                qa_restart_destroy(controls, NULL);
                return NULL;
            }
            size_t n = strlen(name) + 1;
            names[j] = malloc(n);
            if (!names[j]) {
                qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating restart cvar name");
                qa_restart_destroy(controls, NULL);
                return NULL;
            }
            memcpy(names[j], name, n);
        }
    }
    return controls;
}
bool qa_restart_destroy(qa_restart_controls *controls, qa_error *e) {
    if (!controls)
        return true;
    if (controls->running)
        return settings_fail(e, "Cannot destroy active restart controls");
    for (size_t i = 0; i < 3; ++i) {
        if (controls->registered[i])
            qa_console_unregister(controls->console, commands[i], controls->owner);
        for (size_t j = 0; j < controls->services[i].latched_count; ++j)
            free((void *)controls->services[i].latched[j]);
        free((void *)controls->services[i].latched);
    }
    free(controls);
    return true;
}
bool qa_restart_request(qa_restart_controls *controls, qa_restart_kind kind, qa_error *e) {
    if (!controls || kind < QA_RESTART_VIDEO || kind > QA_RESTART_AUDIO)
        return settings_fail(e, "Unknown restart service");
    for (size_t i = 0; i < controls->count; ++i)
        if (controls->pending[i] == kind)
            return true;
    controls->pending[controls->count++] = kind;
    return true;
}
static bool drain(qa_restart_controls *controls,bool one,qa_error *e) {
    if (!controls || controls->running)
        return settings_fail(e, "Restart controls already active");
    controls->running = true;
    bool ok = true;
    while (ok && controls->count) {
        qa_restart_kind kind = controls->pending[0];
        --controls->count;
        memmove(controls->pending, controls->pending + 1,
                controls->count * sizeof(*controls->pending));
        qa_restart_service *service = &controls->services[kind];
        for (size_t i = 0; ok && i < service->latched_count; ++i)
            ok = qa_cvars_apply_latched(controls->vars, service->latched[i], e);
        if (ok)
            ok = service->restart(service->context, e);
        if (one) break;
    }
    controls->running = false;
    return ok;
}
bool qa_restart_drain(qa_restart_controls *controls,qa_error *error)
{ return drain(controls,false,error); }
bool qa_restart_drain_one(qa_restart_controls *controls,qa_error *error)
{ return drain(controls,true,error); }
static bool command(void *context, const qa_command_invocation *invocation, qa_error *e) {
    qa_restart_controls *controls = context;
    if (invocation->context.origin==QA_COMMAND_REMOTE) {
        qa_console_emit(invocation->console,&invocation->context,"Device restart is a local client command.\n");
        return true;
    }
    for (size_t i = 0; i < 3; ++i) {
        const unsigned char *a = (const unsigned char *)invocation->argv[0];
        const unsigned char *b = (const unsigned char *)commands[i];
        while (*a && (*a >= 'A' && *a <= 'Z' ? *a + 'a' - 'A' : *a) == *b) {
            ++a;
            ++b;
        }
        if (!*a && !*b)
            return qa_restart_request(controls, (qa_restart_kind)i, e);
    }
    return settings_fail(e, "Unknown restart command");
}
bool qa_restart_register(qa_restart_controls *controls, qa_console *console, uint64_t owner,
                         qa_error *e) {
    if (!controls || !console || controls->console || controls->running)
        return settings_fail(e, "Restart command owner already bound");
    qa_command_context context = {.owner = owner};
    controls->console = console;
    controls->owner = owner;
    for (size_t i = 0; i < 3; ++i) {
        if (qa_console_find(console, &context, commands[i]))
            continue;
        if (!qa_console_register(console, commands[i], "Restart native device resources", owner,
                                 true, command, controls, e)) {
            for (size_t j = 0; j < i; ++j)
                if (controls->registered[j]) {
                    qa_console_unregister(console, commands[j], owner);
                    controls->registered[j] = false;
                }
            controls->console = NULL;
            return false;
        }
        controls->registered[i] = true;
    }
    return true;
}
bool qa_restart_pending(const qa_restart_controls *controls,qa_restart_kind kind)
{
    if (!controls || kind<QA_RESTART_VIDEO || kind>QA_RESTART_AUDIO) return false;
    for (size_t i=0;i<controls->count;++i) if (controls->pending[i]==kind) return true;
    return false;
}
static bool pending_fields(qa_source_save_io *io,qa_restart_kind pending[3],size_t *count)
{
    uint8_t magic[4]={'Q','A','R','C'};
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QARC",4) ||
        !qa_source_save_count(io,count,3)) return false;
    for (size_t i=0;i<*count;++i) {
        uint32_t kind=pending[i];
        if (!qa_source_save_u32(io,&kind) || kind>QA_RESTART_AUDIO) return false;
        pending[i]=(qa_restart_kind)kind;
        for (size_t j=0;j<i;++j) if (pending[j]==pending[i]) return false;
    }
    return true;
}
bool qa_restart_checkpoint(const qa_restart_controls *controls,qa_buffer *out,qa_error *error)
{
    if (!controls || controls->running || !out || out->data || out->size)
        return settings_fail(error,"Restart capture requires its returned actual controls");
    qa_restart_kind pending[3]; memcpy(pending,controls->pending,sizeof(pending)); size_t count=controls->count;
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && pending_fields(&io,pending,&count) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool qa_restart_restore(qa_restart_controls *controls,qa_bytes bytes,qa_error *error)
{
    if (!controls || controls->running || controls->count)
        return settings_fail(error,"Restart import requires its empty detached controls");
    qa_restart_kind pending[3]={0}; size_t count=0; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && pending_fields(&io,pending,&count) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok) { memcpy(controls->pending,pending,sizeof(pending)); controls->count=count; }
    else if (!error || error->code==QA_OK) settings_fail(error,"Invalid actual restart pending order");
    return ok;
}
