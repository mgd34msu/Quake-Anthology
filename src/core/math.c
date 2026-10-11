#include "qa/math.h"

void qa_angle_vectors(qa_vec3 angles,qa_vec3 *forward,qa_vec3 *right,qa_vec3 *up,qa_angle_math_rule rule)
{
    const float radians=0.01745329251994329577f;
    const double native_radians=0.017453292519943295769236907684886;
    float yaw,pitch,roll;
    if(rule==QA_ANGLE_NATIVE_MATH) {
        yaw=(float)(angles.y*native_radians);
        pitch=(float)(angles.x*native_radians);
        roll=(float)(angles.z*native_radians);
    } else {
        yaw=angles.y*radians;pitch=angles.x*radians;roll=angles.z*radians;
    }
    float sy,cy,sp,cp,sr,cr;
    if(rule==QA_ANGLE_FLOAT_MATH) {
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
