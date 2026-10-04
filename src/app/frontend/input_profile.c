#include "qa/catalog_save.h"
#include "input_profile.h"
#include "qa/application_profile.h"
#include "save_private.h"
#include "config_scripts.h"
#include "global_settings_storage.h"
#include <SDL.h>

bool frontend_input_profile_default_options(qa_frontend *f,qa_error *error)
{
    if (!f || f->application || f->default_user_root)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Default input profile root belongs to normal frontend construction");
    if (f->options.application.user_root) return true;
    f->default_user_root=SDL_GetPrefPath("quake-anthology","content");
    if (!f->default_user_root)
        return frontend_fail(error,QA_ERROR_IO,"Platform could not create the actual default user-content directory");
    f->options.application.user_root=f->default_user_root;
    return true;
}
bool frontend_input_profile_bind(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || f->capture || f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input profile binding requires its normal installed frontend owner");
    if (f->input_config) {
        if (!f->input_catalog || !qa_catalog_product(f->input_catalog,f->input_product))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Input profile lost its retained selected catalogue row");
        qa_fs_root *root=qa_application_player_profile_root(f->application);
        for (size_t i=0;i<qa_vfs_mount_count(f->input_config);++i) {
            qa_vfs_mount_info mount;
            if (!qa_vfs_mount_at(f->input_config,i,&mount)) return false;
            if (mount.writable && !mount.is_archive)
                return root==qa_vfs_mount_root(f->input_config,mount.id) ||
                    frontend_fail(error,QA_ERROR_ARGUMENT,"Input profile lost its exact retained ConfigStore directory");
        }
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Installed input ConfigStore has no actual writable directory");
    }
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    if (!publication) return true;
    const qa_launch_choices *choices=qa_launch_snapshot_choices(publication);
    if (!choices || !choices->world.preset)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input profile has no genuine selected preset product");
    qa_catalog *catalog=qa_launch_snapshot_catalog(publication);
    frontend_config_files *files=frontend_config_files_create(catalog,choices->world.preset,
        frontend_global_settings_storage_user_store(f->global_settings_storage),
        frontend_global_settings_storage_device_store(f->global_settings_storage),error);
    if (!files) return false;
    bool ok=frontend_input_profile_bind_store(f,catalog,choices->world.preset,
        frontend_config_files_store(files,false),error);
    bool destroyed=frontend_config_files_destroy(files,ok?error:NULL);
    return ok && destroyed;
}
bool frontend_input_profile_bind_store(qa_frontend *f,qa_catalog *catalog,qa_product_id product,
    qa_settings_store store,qa_error *error)
{
    if (!f || !f->application || f->capture || f->source_restoring ||
        !catalog || !qa_catalog_product(catalog,product) || !store.vfs || !store.mount)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input profile requires its actual selected ConfigStore");
    if (f->input_config) return frontend_input_profile_bind(f,error);
    qa_vfs_mount_info selected={0}; bool found=false;
    for (size_t i=0;i<qa_vfs_mount_count(store.vfs);++i) {
        qa_vfs_mount_info mount;
        if (!qa_vfs_mount_at(store.vfs,i,&mount)) return false;
        if (mount.id==store.mount) { selected=mount; found=true; break; }
    }
    qa_fs_root *root=qa_vfs_mount_root(store.vfs,store.mount);
    if (!found || selected.is_archive || !selected.writable || !root)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input ConfigStore has no true writable directory authority");
    qa_vfs *view=qa_vfs_create(qa_catalog_resources(catalog),error); qa_mount_id retained=0;
    if (!view) return false;
    if (!qa_vfs_mount_retained(view,store.vfs,store.mount,selected.comparison,true,&retained,error) ||
        qa_vfs_mount_root(view,retained)!=root ||
        !qa_application_player_profile_bind(f->application,root,error)) {
        qa_vfs_destroy(view); return false;
    }
    qa_catalog_retain(catalog);
    f->input_catalog=catalog; f->input_config=view; f->input_product=product;
    f->options.application.player_profile_root=root;
    return true;
}
const qa_vfs *frontend_input_profile_files(const qa_frontend *f)
{ return f?f->input_config:NULL; }
qa_product_id frontend_input_profile_product(const qa_frontend *f)
{ return f?f->input_product:0; }
void frontend_input_profile_destroy(qa_frontend *f)
{
    if (!f) return;
    qa_vfs_destroy(f->input_config); f->input_config=NULL; f->input_product=0;
    qa_catalog_release(f->input_catalog); f->input_catalog=NULL;
    SDL_free(f->default_user_root); f->default_user_root=NULL;
}

typedef struct input_profile_state {
    uint64_t catalog,view;
    uint32_t product;
    char *default_root;
    bool present;
} input_profile_state;
static bool input_profile_fields(qa_source_save_io *io,input_profile_state *state)
{
    uint8_t magic[4]={'Q','F','I','P'}; return qa_source_save_bytes(io,magic,sizeof(magic)) && !memcmp(magic,"QFIP",sizeof(magic)) &&
        qa_source_save_bool(io,&state->present) &&
        qa_source_save_u32(io,&state->product) && qa_source_save_u64(io,&state->catalog) && qa_source_save_u64(io,&state->view) &&
        qa_source_save_owned_text(io,&state->default_root) &&
        (state->present?(state->product && state->catalog && state->view):(!state->product && !state->catalog && !state->view)) &&
        (!state->default_root || *state->default_root);
}
static bool input_profile_decode(qa_bytes bytes,input_profile_state *state,qa_error *error)
{
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && input_profile_fields(&io,state) &&
        qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    return ok;
}
static bool input_profile_default_root(const qa_frontend *f,const input_profile_state *state)
{
    return (!!state->default_root==!!f->default_user_root) &&
        (!state->default_root || (f->options.application.user_root==f->default_user_root &&
                                !strcmp(state->default_root,f->default_user_root)));
}
bool frontend_input_profile_resolve_root(const qa_frontend *f,const qa_application_content_graph *graph,
    qa_bytes bytes,qa_fs_root **out,qa_error *error)
{
    if (!f || f->application || !graph || !out || *out || f->input_config || f->input_catalog || f->input_product)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input profile root resolution requires its empty cold constructor");
    input_profile_state state={0};
    bool ok=input_profile_decode(bytes,&state,error) && input_profile_default_root(f,&state);
    qa_fs_root *root=NULL;
    if (ok && state.present) {
        qa_vfs *view=qa_application_content_view(graph,state.view);
        qa_catalog *catalog=qa_application_content_catalog(graph,state.catalog);
        ok=view && catalog && qa_catalog_product(catalog,state.product) && qa_vfs_mount_count(view)==1;
        qa_vfs_mount_info mount={0};
        if (ok) ok=qa_vfs_mount_at(view,0,&mount) && mount.writable && !mount.is_archive &&
            (root=qa_vfs_mount_root(view,mount.id))!=NULL;
    }
    free(state.default_root);
    if (!ok) {
        if (!error || error->code==QA_OK)
            frontend_fail(error,QA_ERROR_FORMAT,"Saved input profile lacks its actual writable constructor view");
        return false;
    }
    *out=root;
    return true;
}
static bool input_profile_root(const qa_frontend *f,const qa_vfs *view,qa_error *error)
{
    qa_fs_root *root=qa_application_player_profile_root(f->application);
    for (size_t i=0;i<qa_vfs_mount_count(view);++i) {
        qa_vfs_mount_info mount;
        if (!qa_vfs_mount_at(view,i,&mount)) return false;
        if (mount.writable && !mount.is_archive)
            return root && root==qa_vfs_mount_root(view,mount.id) &&
                f->options.application.player_profile_root==root;
    }
    return frontend_fail(error,QA_ERROR_ARGUMENT,"Input continuation lacks its true retained profile root alias");
}
bool frontend_input_profile_checkpoint(const qa_frontend *f,const qa_application_content_graph *graph,
    qa_buffer *out,qa_error *error)
{
    if (!f || !f->application || f->stepping || f->preparing || f->source_restoring ||
        !graph || !out || out->data || out->size || (!!f->input_config != !!f->input_product) ||
        (!!f->input_config != !!f->input_catalog) ||
        (f->input_config && (!qa_catalog_product(f->input_catalog,f->input_product) ||
                            !input_profile_root(f,f->input_config,error))) ||
        (f->default_user_root && f->options.application.user_root!=f->default_user_root))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input profile capture requires its actual idle constructor/view owners");
    input_profile_state state={.present=f->input_config!=NULL,.product=f->input_product,
        .catalog=f->input_catalog?qa_application_content_catalog_id(graph,f->input_catalog):0,
        .view=f->input_config?qa_application_content_view_id(graph,f->input_config):0,
        .default_root=f->default_user_root};
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && input_profile_fields(&io,&state) &&
        qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Invalid actual input profile continuation");
    return ok;
}
bool frontend_input_profile_restore(qa_frontend *f,qa_application_content_graph *graph,
    qa_bytes bytes,qa_error *error)
{
    if (!f || !f->application || !graph || f->stepping || f->input_config || f->input_product || f->input_catalog)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input profile import requires its empty detached owner");
    input_profile_state state={0};
    bool ok=input_profile_decode(bytes,&state,error) && input_profile_default_root(f,&state);
    qa_vfs *view=ok && state.present?qa_application_content_view(graph,state.view):NULL;
    qa_catalog *catalog=ok && state.present?qa_application_content_catalog(graph,state.catalog):NULL;
    if (ok && state.present) ok=view &&
        catalog && qa_catalog_product(catalog,state.product) && input_profile_root(f,view,error);
    qa_catalog *retained=NULL;
    if (ok && state.present) ok=qa_application_content_retain_catalog(graph,state.catalog,&retained,error) &&
        qa_application_content_claim_view(graph,state.view,&f->input_config,error);
    if (ok) { f->input_product=state.product; f->input_catalog=retained; }
    else qa_catalog_release(retained);
    free(state.default_root);
    if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Saved input profile leaves its actual constructor/root graph");
    return ok;
}
