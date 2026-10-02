#include "root_restore.h"
#include "visual_restore.h"
#include "capture.h"

bool frontend_roots_attach_restored(qa_frontend *f,frontend_world_inventory *roots,
    frontend_model_inventory *models,qa_error *error)
{
    if (!f || !f->application || f->stepping || !f->source_restoring || !roots || !models ||
        f->scene_world || f->map_resource || f->map_name || !frontend_owners_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Scene root adoption requires genuine empty candidate map/cache owners");
    frontend_world_source map_source={0}; size_t map_key=0;
    for (size_t i=0;i<frontend_world_inventory_world_count(roots);++i) {
        frontend_world_source source; frontend_scene_owner owner;
        if (!frontend_world_inventory_world_at(roots,i,&source,&owner)) return false;
        if (owner.kind!=FRONTEND_SCENE_OWNER_FRONTEND) continue;
        if (map_key || source.files!=f->mounts || source.images!=f->images || source.materials!=f->materials ||
            !frontend_world_owner_ready(roots,i+1,FRONTEND_SCENE_OWNER_FRONTEND,0,error))
            return frontend_fail(error,QA_ERROR_FORMAT,"Frontend map root leaves its actual unique content/resource heaps");
        map_source=source; map_key=i+1;
    }
    qa_application_map_view map={0}; char *name=NULL;
    if (map_key) {
        if (!qa_application_map_read(f->application,&map) || !map.name || !map.resource ||
            map.resource!=map_source.resource || map.revision!=f->map_revision ||
            qa_application_configuration_generation(f->application)!=f->configuration)
            return frontend_fail(error,QA_ERROR_FORMAT,"Frontend map root differs from the actual restored application map");
        size_t length=strlen(map.name);
        if (length==SIZE_MAX || !(name=malloc(length+1)))
            return frontend_fail(error,QA_ERROR_MEMORY,"Retaining genuine restored render map name");
        memcpy(name,map.name,length+1);
    } else if (f->mounts || f->images || f->materials || f->sounds || f->map_revision) {
        return frontend_fail(error,QA_ERROR_FORMAT,"Restored frontend map heaps omit their actual world root");
    }
    /* Preflight every physical appearance-owner row before the map or any
     * cache acquires destructor authority. Root dictionary order need not be
     * the cache's physical linked order; the recorded row determines it. */
    size_t owners=frontend_visual_owner_count(f),count=frontend_world_inventory_model_count(roots);
    for (size_t owner=0;owner<owners;++owner) {
        frontend_visual_owner_view actual;
        if (!frontend_visual_owner_read(f,owner,&actual) || frontend_visual_model_count(f,owner)) {
            free(name); return frontend_fail(error,QA_ERROR_FORMAT,"Appearance cache is not its genuine empty prepared owner");
        }
    }
    for (size_t i=0;i<count;++i) {
        frontend_scene_root_view root;
        if (!frontend_world_inventory_model_at(roots,i,&root)) { free(name); return false; }
        if (root.owner.kind!=FRONTEND_SCENE_OWNER_VISUAL) continue;
        frontend_visual_owner_view owner;
        if (!root.owner.owner || root.owner.owner>owners || !root.owner.row || root.owner.row>count ||
            !frontend_visual_owner_read(f,(size_t)root.owner.owner-1,&owner) ||
            root.source.files!=owner.mounts || !root.visual_path || !*root.visual_path ||
            !frontend_scene_root_owner_ready(roots,i+1,FRONTEND_SCENE_OWNER_VISUAL,root.owner.owner,error)) {
            free(name); return frontend_fail(error,QA_ERROR_FORMAT,"Appearance scene root leaves its saved destructor/cache scope");
        }
        bool previous=root.owner.row==1;
        for (size_t j=0;j<count;++j) {
            frontend_scene_root_view other;
            if (!frontend_world_inventory_model_at(roots,j,&other)) { free(name); return false; }
            if (other.owner.kind!=FRONTEND_SCENE_OWNER_VISUAL || other.owner.owner!=root.owner.owner || j==i) continue;
            if (other.owner.row==root.owner.row) {
                free(name); return frontend_fail(error,QA_ERROR_FORMAT,"Appearance roots repeat a physical cache row");
            }
            if (other.owner.row==root.owner.row-1) previous=true;
        }
        if (!previous) { free(name); return frontend_fail(error,QA_ERROR_FORMAT,"Appearance roots omit a preceding physical cache row"); }
    }
    if (map_key) {
        qa_resource_retain(map.resource); f->map_resource=map.resource; f->map_name=name; name=NULL;
        f->scene_world=(qa_scene_world *)map_source.world; frontend_world_adopt(roots,map_key);
    }
    for (size_t owner=0;owner<owners;++owner) {
        for (size_t row=1;row<=count;++row) {
            bool found=false;
            for (size_t i=0;i<count;++i) {
                frontend_scene_root_view root;
                if (!frontend_world_inventory_model_at(roots,i,&root)) return false;
                if (root.owner.kind!=FRONTEND_SCENE_OWNER_VISUAL || root.owner.owner!=owner+1 || root.owner.row!=row) continue;
                if (!frontend_visual_model_attach_restored(f,owner,root.visual_path,root.source.resource,
                    root.source.model,(qa_scene_model *)root.scene,models,error)) return false;
                frontend_scene_root_adopt(roots,i+1); found=true; break;
            }
            if (!found) break;
        }
    }
    return frontend_visual_model_receipts_ready(f,error);
}
