/* Port of src/world/geometry/q1-solid/index.ts. GPL-2.0-or-later. */
#include "qa/world/geometry/q1_solid/index.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
void qa_q1_solid_space_init(qa_q1_solid_space*s,const qa_q1_world_geometry*g){s->geometry=g;s->representation="bsp-leaf-cells";}
typedef struct{qa_bsp_child child;qa_convex_cell*cell;double depth;} Frame;
void qa_q1_solid_space_cells(qa_q1_solid_space*s,qa_bounds env,double model_index,qa_q1_solid_include_fn include,void*ictx,qa_q1_solid_visit_fn visit,void*vctx){
  const qa_q1_world_geometry*g=s->geometry;
  if(model_index<0||(size_t)model_index>=g->model_count)qa_range_error("Unknown Quake model %g",model_index);
  const qa_q1_world_model*model=&g->models[(size_t)model_index];
  if(model->headnodes.count==0)qa_range_error("Quake model has no drawing hull");
  double root=model->headnodes.values[0];
  qa_bsp_child first; first.is_node=root>=0; first.index=root<0?-1-root:root;
  size_t cap=16,n=0; Frame*st=malloc(cap*sizeof*st); if(!st)abort();
  st[n++]=(Frame){first,qa_box_cell(env),0};
  while(n>0){Frame fr=st[--n];
    if(!fr.child.is_node){
      if(fr.child.index<0||(size_t)fr.child.index>=g->leaf_count)qa_range_error("Unknown Quake solid-space leaf");
      const qa_q1_leaf*leaf=&g->leaves[(size_t)fr.child.index];
      int inc=include?include(ictx,leaf->contents):leaf->contents==-2;
      if(inc){qa_q1_solid_cell c={fr.cell,fr.child.index,leaf->contents};visit(vctx,c);} else qa_convex_cell_destroy(fr.cell);
      continue;
    }
    if(fr.depth>(double)g->node_count)qa_range_error("Cycle in Quake solid-space BSP");
    if(fr.child.index<0||(size_t)fr.child.index>=g->node_count)qa_range_error("Unknown Quake solid-space node/plane");
    const qa_bsp_node*node=&g->nodes[(size_t)fr.child.index];
    if(node->plane<0||(size_t)node->plane>=g->plane_count)qa_range_error("Unknown Quake solid-space node/plane");
    qa_split_result sp=qa_split_cell(fr.cell,g->planes[(size_t)node->plane].plane);
    if(sp.front==fr.cell&&sp.back==fr.cell){
      /* shared: visit both children with same cell would double-own; deep-copy for back */
      qa_convex_cell*dup=malloc(sizeof*dup); if(!dup)abort();
      dup->face_count=fr.cell->face_count; dup->faces=malloc(dup->face_count*sizeof*dup->faces); if(!dup->faces)abort();
      for(size_t i=0;i<dup->face_count;i++){dup->faces[i].plane=fr.cell->faces[i].plane;dup->faces[i].vertex_count=fr.cell->faces[i].vertex_count;dup->faces[i].vertices=malloc(dup->faces[i].vertex_count*sizeof*dup->faces[i].vertices);if(!dup->faces[i].vertices)abort();for(size_t j=0;j<dup->faces[i].vertex_count;j++)dup->faces[i].vertices[j]=fr.cell->faces[i].vertices[j];}
      if(n+2>cap){cap*=2;st=realloc(st,cap*sizeof*st);if(!st)abort();}
      st[n++]=(Frame){node->children[1],dup,fr.depth+1}; st[n++]=(Frame){node->children[0],fr.cell,fr.depth+1}; continue;
    }
    if(sp.front!=fr.cell&&sp.back!=fr.cell)qa_convex_cell_destroy(fr.cell);
    if(n+2>cap){cap*=2;st=realloc(st,cap*sizeof*st);if(!st)abort();}
    if(sp.back)st[n++]=(Frame){node->children[1],sp.back,fr.depth+1};
    if(sp.front)st[n++]=(Frame){node->children[0],sp.front,fr.depth+1};
  }
  free(st);
}
