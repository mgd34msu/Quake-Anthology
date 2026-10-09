#include "geometry.h"
#include <stdalign.h>
#include <string.h>

static const q1v axes[3]={{1,0,0},{0,1,0},{0,0,1}};

void *q1_alloc(q1work *w,size_t count,size_t size,size_t alignment) {
    if(w->failed || count==0) return NULL;
    if(size==0 || count>SIZE_MAX/size) {
        qa_error_set(w->error,QA_ERROR_MEMORY,0,"Quake collision allocation overflow");
        w->failed=true; return NULL;
    }
    void *p=qa_arena_alloc(w->arena,count*size,alignment,w->error);
    if(p==NULL) w->failed=true;
    return p;
}
bool q1_grow(q1work *w,void **data,size_t *capacity,size_t needed,size_t size,size_t alignment) {
    if(needed<=*capacity) return true;
    size_t cap=*capacity==0?8:*capacity;
    while(cap<needed) { if(cap>SIZE_MAX/2) { cap=needed; break; } cap*=2; }
    void *next=q1_alloc(w,cap,size,alignment);
    if(next==NULL) return false;
    if(*data!=NULL) memcpy(next,*data,*capacity*size);
    *data=next; *capacity=cap; return true;
}
bool q1_cells_push(q1work *w,q1cells *cells,q1cell *cell) {
    if(cells->count==SIZE_MAX) { w->failed=true; qa_error_set(w->error,QA_ERROR_MEMORY,0,"Too many Quake collision cells"); return false; }
    void *data=cells->items;
    if(!q1_grow(w,&data,&cells->capacity,cells->count+1,sizeof(*cells->items),alignof(q1cell *))) return false;
    cells->items=data;
    cells->items[cells->count++]=cell; return true;
}
static void cell_bounds(q1cell *cell) {
    q1bounds b={qv(INFINITY,INFINITY,INFINITY),qv(-INFINITY,-INFINITY,-INFINITY)};
    for(size_t f=0;f<cell->count;f++) for(size_t i=0;i<cell->faces[f].count;i++) {
        q1v v=cell->faces[f].vertices[i];
        b.min=qv(fmin(b.min.x,v.x),fmin(b.min.y,v.y),fmin(b.min.z,v.z));
        b.max=qv(fmax(b.max.x,v.x),fmax(b.max.y,v.y),fmax(b.max.z,v.z));
    }
    cell->bounds=b;
}
q1cell *q1_box(q1work *w,q1bounds b) {
    q1cell *c=q1_alloc(w,1,sizeof(*c),alignof(q1cell));
    q1face *f=q1_alloc(w,6,sizeof(*f),alignof(q1face));
    q1v *v=q1_alloc(w,24,sizeof(*v),alignof(q1v));
    if(w->failed) return NULL;
    q1v points[8]={qv(b.min.x,b.min.y,b.min.z),qv(b.min.x,b.min.y,b.max.z),
                  qv(b.min.x,b.max.y,b.min.z),qv(b.min.x,b.max.y,b.max.z),
                  qv(b.max.x,b.min.y,b.min.z),qv(b.max.x,b.min.y,b.max.z),
                  qv(b.max.x,b.max.y,b.min.z),qv(b.max.x,b.max.y,b.max.z)};
    static const unsigned corners[6][4]={{4,6,7,5},{0,1,3,2},{2,3,7,6},{0,4,5,1},{1,5,7,3},{0,2,6,4}};
    double d[6]={b.max.x,-b.min.x,b.max.y,-b.min.y,b.max.z,-b.min.z};
    for(size_t i=0;i<6;i++) {
        f[i]=(q1face){(q1p){qscale(axes[i/2],i%2==0?1:-1),d[i]},v+i*4,4};
        for(size_t j=0;j<4;j++) v[i*4+j]=points[corners[i][j]];
    }
    *c=(q1cell){f,6,b}; return c;
}
static bool cap_has(const q1v *cap,size_t count,q1v p) {
    for(size_t i=0;i<count;i++) if(qlength(qsub(cap[i],p))<1e-7) return true;
    return false;
}
static q1cell *close_cell(q1work *w,q1face *faces,size_t count,const q1v *cap,size_t cap_count,q1p plane) {
    if(cap_count>=3) {
        q1v *sorted=q1_alloc(w,cap_count,sizeof(*sorted),alignof(q1v));
        double *angles=q1_alloc(w,cap_count,sizeof(*angles),alignof(double));
        if(w->failed) return NULL;
        q1v center=qv(0,0,0);
        for(size_t i=0;i<cap_count;i++) center=qadd(center,cap[i]);
        center=qscale(center,1.0/(double)cap_count);
        q1v u=qcross(fabs(plane.normal.z)<0.9?axes[2]:axes[1],plane.normal);
        double length=qlength(u);
        if(length==0) { qa_error_set(w->error,QA_ERROR_FORMAT,0,"Degenerate Quake collision plane"); w->failed=true; return NULL; }
        u=qscale(u,1/length); q1v v=qcross(plane.normal,u);
        /* Stable insertion order is retained for equal winding angles. */
        for(size_t i=0;i<cap_count;i++) {
            q1v relative=qsub(cap[i],center);
            double angle=atan2(qdot(relative,v),qdot(relative,u));
            size_t j=i;
            while(j>0 && angles[j-1]>angle) { sorted[j]=sorted[j-1]; angles[j]=angles[j-1]; j--; }
            sorted[j]=cap[i]; angles[j]=angle;
        }
        faces[count++]=(q1face){plane,sorted,cap_count};
    }
    if(count<4) return NULL;
    q1cell *cell=q1_alloc(w,1,sizeof(*cell),alignof(q1cell));
    if(cell==NULL) return NULL;
    cell->faces=faces; cell->count=count; cell_bounds(cell); return cell;
}
void q1_split(q1work *w,q1cell *cell,q1p plane,q1cell **front,q1cell **back) {
    *front=NULL; *back=NULL;
    if(cell==NULL || w->failed) return;
    q1v n=plane.normal; q1bounds bounds=cell->bounds;
    double lower=(n.x<0?bounds.max.x:bounds.min.x)*n.x+(n.y<0?bounds.max.y:bounds.min.y)*n.y+(n.z<0?bounds.max.z:bounds.min.z)*n.z-plane.distance;
    double upper=(n.x<0?bounds.min.x:bounds.max.x)*n.x+(n.y<0?bounds.min.y:bounds.max.y)*n.y+(n.z<0?bounds.min.z:bounds.max.z)*n.z-plane.distance;
    if(upper<-1e-8) { *back=cell; return; }
    if(lower>1e-8) { *front=cell; return; }
    if(lower>=-1e-8 && upper<=1e-8) { *front=cell; *back=cell; return; }
    bool outside=false,inside=false;
    size_t total=0;
    for(size_t f=0;f<cell->count;f++) {
        if(cell->faces[f].count>SIZE_MAX-total) { qa_error_set(w->error,QA_ERROR_MEMORY,0,"Quake winding size overflow"); w->failed=true; return; }
        total+=cell->faces[f].count;
        for(size_t i=0;i<cell->faces[f].count;i++) {
            double d=qdot(cell->faces[f].vertices[i],n)-plane.distance;
            outside|=d>1e-8; inside|=d<-1e-8;
        }
    }
    if(!outside || !inside) { *front=inside?NULL:cell; *back=outside?NULL:cell; return; }
    q1face *ff=q1_alloc(w,cell->count+1,sizeof(*ff),alignof(q1face));
    q1face *bf=q1_alloc(w,cell->count+1,sizeof(*bf),alignof(q1face));
    q1v *cap=q1_alloc(w,total,sizeof(*cap),alignof(q1v));
    if(w->failed) return;
    size_t nf=0,nb=0,nc=0;
    for(size_t f=0;f<cell->count;f++) {
        const q1face *face=&cell->faces[f];
        /* A convex winding needs at most n+1 points. Reserve two per edge so
         * even roundoff around nearly collinear edges cannot overrun it. */
        if(face->count>SIZE_MAX/2) { qa_error_set(w->error,QA_ERROR_MEMORY,0,"Quake winding size overflow"); w->failed=true; return; }
        q1v *fv=q1_alloc(w,face->count*2,sizeof(*fv),alignof(q1v));
        q1v *bv=q1_alloc(w,face->count*2,sizeof(*bv),alignof(q1v));
        if(w->failed) return;
        size_t fn=0,bn=0;
        for(size_t i=0;i<face->count;i++) {
            q1v a=face->vertices[i],b=face->vertices[(i+1)%face->count];
            double da=qdot(a,n)-plane.distance,db=qdot(b,n)-plane.distance;
            if(da>=0) fv[fn++]=a;
            if(da<=0) bv[bn++]=a;
            if((da<0 && db>0)||(da>0 && db<0)) {
                q1v p=qlerp(a,b,da/(da-db)); fv[fn++]=p; bv[bn++]=p;
                if(!cap_has(cap,nc,p)) cap[nc++]=p;
            } else if(da==0 && !cap_has(cap,nc,a)) cap[nc++]=a;
        }
        if(fn>=3) ff[nf++]=(q1face){face->plane,fv,fn};
        if(bn>=3) bf[nb++]=(q1face){face->plane,bv,bn};
    }
    *front=close_cell(w,ff,nf,cap,nc,qnegate(plane));
    *back=close_cell(w,bf,nb,cap,nc,plane);
}
q1cell *q1_clip(q1work *w,q1cell *cell,q1p plane) {
    q1cell *front,*back; q1_split(w,cell,plane,&front,&back); return back;
}
static bool insert_normal(q1work *w,q1planes *out,q1v normal) {
    double length=qlength(normal);
    if(length<1e-8) return true;
    normal=qscale(normal,1/length);
    for(size_t i=0;i<out->count;i++) if(qdot(out->items[i].normal,normal)>1-1e-10) return true;
    void *data=out->items;
    if(!q1_grow(w,&data,&out->capacity,out->count+1,sizeof(*out->items),alignof(q1p))) return false;
    out->items=data;
    out->items[out->count++]=(q1p){normal,-INFINITY}; return true;
}
bool q1_separating_planes(q1work *w,const q1cell *cell,const q1v shape_axes[3],q1planes *out) {
    out->count=0;
    for(size_t f=0;f<cell->count;f++) {
        const q1face *face=&cell->faces[f];
        if(!insert_normal(w,out,face->plane.normal)) return false;
        for(size_t i=0;i<face->count;i++) {
            q1v edge=qsub(face->vertices[(i+1)%face->count],face->vertices[i]);
            for(size_t a=0;a<3;a++) {
                q1v n=qcross(edge,shape_axes[a]);
                if(!insert_normal(w,out,n)||!insert_normal(w,out,qscale(n,-1))) return false;
            }
        }
    }
    for(size_t a=0;a<3;a++) if(!insert_normal(w,out,shape_axes[a])||!insert_normal(w,out,qscale(shape_axes[a],-1))) return false;
    for(size_t p=0;p<out->count;p++) for(size_t f=0;f<cell->count;f++) for(size_t i=0;i<cell->faces[f].count;i++)
        out->items[p].distance=fmax(out->items[p].distance,qdot(out->items[p].normal,cell->faces[f].vertices[i]));
    return true;
}
double q1_shape_support(const q1shape *shape,q1v n) {
    if(shape->capsule) return shape->radius+fabs(qdot(n,shape->axes[2]))*shape->half_segment;
    return fabs(qdot(n,shape->axes[0]))*shape->extents.x+fabs(qdot(n,shape->axes[1]))*shape->extents.y+fabs(qdot(n,shape->axes[2]))*shape->extents.z;
}
static bool sweep_box(q1work *w,const q1cell *cell,q1v start,q1v end,const q1shape *shape,double epsilon,q1interval *out) {
    q1planes planes={0};
    if(!q1_separating_planes(w,cell,shape->axes,&planes)) return false;
    q1interval interval={.enter=-INFINITY,.exit=INFINITY,.contact=-INFINITY};
    for(size_t i=0;i<planes.count;i++) {
        q1p p=planes.items[i]; double distance=p.distance+q1_shape_support(shape,p.normal);
        double a=qdot(start,p.normal)-distance,b=qdot(end,p.normal)-distance;
        if(a>0 && b>0) return false;
        if(a<=0 && b<=0) continue;
        double fraction=a/(a-b);
        if(a>b) {
            if(fraction>interval.enter) { interval.enter=fraction; interval.plane=p; }
            interval.contact=fmax(interval.contact,(a-epsilon)/(a-b));
        } else interval.exit=fmin(interval.exit,fraction);
        if(interval.enter>interval.exit) return false;
    }
    if(interval.enter>1 || interval.exit<0) return false;
    *out=interval; return true;
}

typedef struct closest { q1v a,b; double squared; } closest;
static closest pair(q1v a,q1v b) { q1v d=qsub(a,b); return (closest){a,b,qdot(d,d)}; }
static closest nearest(closest a,closest b) { return a.squared<=b.squared?a:b; }
static double clamp(double f) { return fmax(0,fmin(1,f)); }
static closest point_triangle(q1v p,q1v a,q1v b,q1v c) {
    q1v ab=qsub(b,a),ac=qsub(c,a),ap=qsub(p,a);
    double d1=qdot(ab,ap),d2=qdot(ac,ap);
    if(d1<=0 && d2<=0) return pair(p,a);
    q1v bp=qsub(p,b); double d3=qdot(ab,bp),d4=qdot(ac,bp);
    if(d3>=0 && d4<=d3) return pair(p,b);
    double vc=d1*d4-d3*d2;
    if(vc<=0 && d1>=0 && d3<=0) return pair(p,qadd(a,qscale(ab,d1/(d1-d3))));
    q1v cp=qsub(p,c); double d5=qdot(ab,cp),d6=qdot(ac,cp);
    if(d6>=0 && d5<=d6) return pair(p,c);
    double vb=d5*d2-d1*d6;
    if(vb<=0 && d2>=0 && d6<=0) return pair(p,qadd(a,qscale(ac,d2/(d2-d6))));
    double va=d3*d6-d5*d4;
    if(va<=0 && d4-d3>=0 && d5-d6>=0) return pair(p,qlerp(b,c,(d4-d3)/(d4-d3+d5-d6)));
    double sum=va+vb+vc;
    if(fabs(sum)<1e-25) return nearest(nearest(pair(p,a),pair(p,b)),pair(p,c));
    return pair(p,qadd(a,qadd(qscale(ab,vb/sum),qscale(ac,vc/sum))));
}
static closest segments(q1v p,q1v q,q1v a,q1v b) {
    q1v u=qsub(q,p),v=qsub(b,a),w=qsub(p,a);
    double uu=qdot(u,u),uv=qdot(u,v),vv=qdot(v,v),uw=qdot(u,w),vw=qdot(v,w),den=uu*vv-uv*uv;
    double s=uu==0?0:clamp((uv*vw-vv*uw)/(den==0?1:den));
    double t=vv==0?0:(uv*s+vw)/vv;
    if(t<0) { t=0; s=uu==0?0:clamp(-uw/uu); }
    else if(t>1) { t=1; s=uu==0?0:clamp((uv-uw)/uu); }
    return pair(qlerp(p,q,s),qlerp(a,b,t));
}
static closest segment_cell(const q1cell *cell,q1v p,q1v q) {
    double enter=0,exit=1;
    for(size_t f=0;f<cell->count;f++) {
        q1p plane=cell->faces[f].plane;
        double a=qdot(p,plane.normal)-plane.distance,b=qdot(q,plane.normal)-plane.distance;
        if(a>0 && b>0) { enter=INFINITY; break; }
        if(a>b && a>0) enter=fmax(enter,a/(a-b));
        if(a<b && b>0) exit=fmin(exit,a/(a-b));
    }
    if(enter<=exit) { q1v at=qlerp(p,q,enter); return pair(at,at); }
    closest result={p,p,INFINITY};
    for(size_t f=0;f<cell->count;f++) {
        const q1face *face=&cell->faces[f];
        if(face->count==0) continue;
        q1v a=face->vertices[0];
        for(size_t i=1;i+1<face->count;i++) {
            q1v b=face->vertices[i],c=face->vertices[i+1];
            if(qlength(qcross(qsub(b,a),qsub(c,a)))<1e-12) continue;
            result=nearest(result,point_triangle(p,a,b,c)); result=nearest(result,point_triangle(q,a,b,c));
            result=nearest(result,segments(p,q,a,b)); result=nearest(result,segments(p,q,b,c)); result=nearest(result,segments(p,q,c,a));
        }
    }
    return result;
}
static bool capsule_entrance(q1work *w,const q1cell *cell,q1v start,q1v end,const q1shape *shape,double margin,double *out_fraction,q1p *plane) {
    q1v offset=qscale(shape->axes[2],shape->half_segment),movement=qsub(end,start);
    double fraction=0;
    for(unsigned iteration=0;iteration<96;iteration++) {
        q1v center=qlerp(start,end,fraction);
        closest c=segment_cell(cell,qsub(center,offset),qadd(center,offset));
        double distance=sqrt(c.squared),separation=distance-shape->radius-margin;
        q1v normal=distance>1e-12?qscale(qsub(c.a,c.b),1/distance):qv(0,0,0);
        if(separation<=1e-7) { *out_fraction=fraction; *plane=(q1p){normal,qdot(normal,c.b)}; return true; }
        double closing=-qdot(movement,normal);
        if(closing<=0) return false;
        double step=separation/closing;
        if(fraction+step>1) return false;
        fraction+=step;
    }
    qa_error_set(w->error,QA_ERROR_FORMAT,0,"Quake solid-cell capsule sweep did not converge"); w->failed=true; return false;
}
bool q1_sweep_cell(q1work *w,const q1cell *cell,q1v start,q1v end,const q1shape *shape,double epsilon,q1interval *out) {
    if(!shape->capsule) return sweep_box(w,cell,start,end,shape,epsilon,out);
    double entrance,reverse,contact; q1p plane,unused;
    if(!capsule_entrance(w,cell,start,end,shape,0,&entrance,&plane)) return false;
    bool reverse_hit=capsule_entrance(w,cell,end,start,shape,0,&reverse,&unused);
    if(w->failed) return false;
    bool contact_hit=capsule_entrance(w,cell,start,end,shape,epsilon,&contact,&unused);
    if(w->failed) return false;
    *out=(q1interval){.enter=entrance,.exit=reverse_hit?1-reverse:entrance,.contact=contact_hit?contact:entrance,.plane=plane}; return true;
}
