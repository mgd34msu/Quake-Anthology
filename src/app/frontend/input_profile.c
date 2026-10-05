#include "qa/catalog_save.h"
#include "input_profile.h"
#include "qa/application_profile.h"
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
    return frontend_input_profile_bind_product(f,catalog,choices->world.preset,error);
}
bool frontend_input_profile_bind_product(qa_frontend *f,qa_catalog *catalog,qa_product_id product,
    qa_error *error)
{
    if (f->input_config) return frontend_input_profile_bind(f,error);
    frontend_config_files *files=frontend_config_files_create(catalog,product,
        frontend_global_settings_storage_user_store(f->global_settings_storage),
        frontend_global_settings_storage_device_store(f->global_settings_storage),error);
    if (!files) return false;
    bool ok=frontend_input_profile_bind_store(f,catalog,product,
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
