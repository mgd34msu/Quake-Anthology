#include "qa/math.h"
#include <string.h>

void qa_angle_vectors(qa_vec3 angles,qa_vec3 *forward,qa_vec3 *right,qa_vec3 *up,qa_angle_math_rule rule)
{
    const float radians=0.01745329251994329577f;
    const double native_radians=0.017453292519943295769236907684886;
    float yaw,pitch,roll;
    if(rule==QA_ANGLE_NATIVE_MATH || rule==QA_ANGLE_FLOAT_NATIVE_MATH) {
        yaw=(float)(angles.y*native_radians);
        pitch=(float)(angles.x*native_radians);
        roll=(float)(angles.z*native_radians);
    } else {
        yaw=angles.y*radians;pitch=angles.x*radians;roll=angles.z*radians;
    }
    float sy,cy,sp,cp,sr,cr;
    if(rule==QA_ANGLE_FLOAT_MATH || rule==QA_ANGLE_FLOAT_NATIVE_MATH) {
        sy=sinf(yaw);cy=cosf(yaw);sp=sinf(pitch);cp=cosf(pitch);sr=sinf(roll);cr=cosf(roll);
    } else {
        sy=(float)sin((double)yaw);cy=(float)cos((double)yaw);
        sp=(float)sin((double)pitch);cp=(float)cos((double)pitch);
        sr=(float)sin((double)roll);cr=(float)cos((double)roll);
    }
    if(forward) *forward=qa_v3(cp*cy,cp*sy,-sp);
    if(right) *right=qa_v3(-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cp);
    if(up) *up=qa_v3(cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp);
}
void qa_angle_axis(qa_vec3 angles,qa_vec3 axis[3],qa_angle_math_rule rule)
{
    qa_angle_vectors(angles,axis,axis+1,axis+2,rule);
    axis[1]=qa_vec_scale(axis[1],-1);
}

qa_vec3 qa_vec_rotate_euler_xyz(qa_vec3 v,qa_vec3 degrees)
{
    const float radians=.01745329251994329577f;
    float p=degrees.x*radians,r=degrees.y*radians,y=degrees.z*radians;
    qa_vec3 x=qa_v3(v.x,v.y*cosf(p)-v.z*sinf(p),v.y*sinf(p)+v.z*cosf(p));
    qa_vec3 z=qa_v3(x.x*cosf(r)+x.z*sinf(r),x.y,-x.x*sinf(r)+x.z*cosf(r));
    return qa_v3(z.x*cosf(y)-z.y*sinf(y),z.x*sinf(y)+z.y*cosf(y),z.z);
}

qa_vec3 qa_axes_angles(const qa_vec3 axis[3],qa_axes_projection_rule rule)
{
    if(rule==QA_AXES_DOUBLE_BASIS) {
        const double degrees=57.295779513082320876;
        qa_vec3 angles=qa_v3((float)(atan2(-axis[0].z,hypot(axis[0].x,axis[0].y))*degrees),
                             (float)(atan2(axis[0].y,axis[0].x)*degrees),0);
        qa_vec3 right,up;
        qa_angle_vectors(qa_v3(angles.x,angles.y,0),NULL,&right,&up,QA_ANGLE_FLOAT_MATH);
        angles.z=(float)(atan2(qa_vec_dot(axis[1],up),-qa_vec_dot(axis[1],right))*degrees);
        return angles;
    }
    const float degrees=57.29577951308232f;
    float horizontal=sqrtf(axis[0].x*axis[0].x+axis[0].y*axis[0].y);
    return qa_v3(atan2f(-axis[0].z,horizontal)*degrees,
                 atan2f(axis[0].y,axis[0].x)*degrees,
                 atan2f(axis[1].z,axis[2].z)*degrees);
}

void qa_axis_multiply(const qa_vec3 left[3],const qa_vec3 right[3],qa_vec3 out[3])
{
    qa_vec3 columns[3]={{right[0].x,right[1].x,right[2].x},
        {right[0].y,right[1].y,right[2].y},{right[0].z,right[1].z,right[2].z}},result[3];
    for(unsigned i=0;i<3;++i) result[i]=qa_axis_to_local(left[i],columns);
    memcpy(out,result,sizeof(result));
}
qa_vec3 qa_vec_perpendicular(qa_vec3 v,qa_perpendicular_math_rule rule)
{
    if(rule==QA_PERPENDICULAR_DIRECT_PROJECTION) {
        qa_vec3 axis;
        float x=fabsf(v.x),y=fabsf(v.y),z=fabsf(v.z);
        if(x<=y && x<=z) axis=qa_v3(1,0,0);
        else if(y<=z) axis=qa_v3(0,1,0);
        else axis=qa_v3(0,0,1);
        return qa_vec_normalize(qa_vec_sub(axis,qa_vec_scale(v,qa_vec_dot(axis,v))));
    }
    float minimum=1;qa_vec3 axis={1,0,0};
    if(fabsf(v.x)<minimum) minimum=fabsf(v.x);
    if(fabsf(v.y)<minimum) { minimum=fabsf(v.y);axis=qa_v3(0,1,0); }
    if(fabsf(v.z)<minimum) axis=qa_v3(0,0,1);
    float inverse=1/qa_vec_dot(v,v),distance=qa_vec_dot(v,axis)*inverse;
    return qa_vec_normalize(qa_vec_sub(axis,qa_vec_scale(qa_vec_scale(v,inverse),distance)));
}
qa_vec3 qa_vec_rotate_around(qa_vec3 axis,qa_vec3 point,float degrees,qa_rotation_math_rule rule)
{
    if(rule==QA_ROTATION_RODRIGUES) {
        float radians=(float)((double)degrees*3.14159265358979323846/180.0);
        float sine=(float)sin(radians),cosine=(float)cos(radians);
        return qa_vec_add(qa_vec_add(qa_vec_scale(point,cosine),
                                   qa_vec_scale(qa_vec_cross(axis,point),sine)),
                          qa_vec_scale(axis,qa_vec_dot(axis,point)*(1.0f-cosine)));
    }
    qa_vec3 radial=qa_vec_perpendicular(axis,QA_PERPENDICULAR_RECIPROCAL_PROJECTION),vertical=qa_vec_cross(radial,axis);
    float radians=(degrees*3.14159274101257324219f)/180;
    float cosine=(float)cos((double)radians),sine=(float)sin((double)radians);
    qa_vec3 basis[3]={{radial.x,vertical.x,axis.x},{radial.y,vertical.y,axis.y},{radial.z,vertical.z,axis.z}};
    qa_vec3 rotation[3]={{cosine,sine,0},{-sine,cosine,0},{0,0,1}},inverse[3]={radial,vertical,axis},first[3],last[3];
    qa_axis_multiply(basis,rotation,first);qa_axis_multiply(first,inverse,last);
    return qa_axis_to_local(point,last);
}
float qa_inverse_sqrt(float square)
{
    uint32_t bits;memcpy(&bits,&square,sizeof(bits));
    bits=UINT32_C(0x5f3759df)-(bits>>1);
    float inverse;memcpy(&inverse,&bits,sizeof(inverse));
    float product=(square*.5f)*inverse;product*=inverse;
    return inverse*(1.5f-product);
}
static double direction_degrees(double y,double x,qa_direction_math_rule rule)
{
    const float pi=3.14159274101257324219f;
    switch(rule) {
    case QA_DIRECTION_FLOAT:return atan2f((float)y,(float)x)*57.29577951308232f;
    case QA_DIRECTION_FLOAT_RATIO:return atan2f((float)y,(float)x)*(180.f/pi);
    case QA_DIRECTION_FLOAT_DIVIDE:return atan2f((float)y,(float)x)*180.f/pi;
    case QA_DIRECTION_VM:return (float)atan2(y,x)*180.f/pi;
    case QA_DIRECTION_GEAR:return (float)((float)atan2(y,x)*180.f/(double)pi);
    case QA_DIRECTION_NATIVE_RATIO:return atan2(y,x)*(180.0/(double)pi);
    default: {
        double degrees=atan2(y,x)*180/3.14159265358979323846264338327950288;
        if(rule==QA_DIRECTION_INTEGER) {
            double integral=trunc(degrees);
            return integral==0?0:integral;
        }
        if(rule==QA_DIRECTION_NATIVE_FLOAT) return (float)degrees;
        return degrees;
    }
    }
}
static double direction_wrap(double angle,qa_direction_math_rule rule)
{
    if(angle<0) return rule==QA_DIRECTION_NATIVE || rule==QA_DIRECTION_NATIVE_RATIO?
        angle+360:(float)((float)angle+360);
    return angle;
}
float qa_vec_yaw(qa_vec3 v,qa_direction_math_rule rule,bool wrap)
{
    double yaw=direction_degrees(v.y,v.x,rule);
    return (float)(wrap?direction_wrap(yaw,rule):yaw);
}
qa_vec3 qa_vec_angles(qa_vec3 v,qa_direction_math_rule rule,unsigned flags)
{
    bool vertical=v.x==0 && v.y==0;
    double pitch;
    if(flags&QA_VECTOR_POLAR_PITCH) pitch=acosf(fminf(1,fmaxf(-1,v.z)))*57.29577951308232f;
    else {
        double horizontal=flags&QA_VECTOR_SQUARED_HORIZONTAL?
            (flags&QA_VECTOR_DOUBLE_SQRT?(float)sqrt((double)(v.x*v.x+v.y*v.y)):sqrtf(v.x*v.x+v.y*v.y)):
            flags&QA_VECTOR_WIDE_HORIZONTAL?hypot((double)v.x,v.y):hypotf(v.x,v.y);
        pitch=vertical && (flags&QA_VECTOR_VERTICAL_CARDINAL)?(v.z>0?90:270):
            vertical && (flags&QA_VECTOR_VERTICAL_SIGNED)?(v.z>0?90:-90):direction_degrees(v.z,horizontal,rule);
    }
    if(flags&QA_VECTOR_NEGATE_BEFORE_WRAP) pitch=-pitch;
    if(flags&QA_VECTOR_WRAP_PITCH) pitch=direction_wrap(pitch,rule);
    if((flags&QA_VECTOR_NEGATIVE_PITCH) && !(flags&QA_VECTOR_NEGATE_BEFORE_WRAP)) pitch=-pitch;
    float yaw=vertical && (flags&(QA_VECTOR_VERTICAL_CARDINAL|QA_VECTOR_VERTICAL_SIGNED))?0:
        qa_vec_yaw(v,rule,(flags&QA_VECTOR_WRAP_YAW)!=0);
    return qa_v3((float)pitch,yaw,0);
}
