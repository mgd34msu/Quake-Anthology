#include "value_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct reader { const qa_json_document *json; qa_json_id id; qa_error *error; } reader;
typedef bool (*check_fn)(reader);

static reader field(reader r, const char *name) { r.id=qa_json_get(r.json,r.id,name); return r; }
static bool fail(reader r, const char *why) { qa_error_set(r.error,QA_ERROR_FORMAT,0,"unified schema: %s",why); return false; }
static bool type(reader r, qa_json_kind kind) { return qa_json_type(r.json,r.id)==kind || fail(r,"wrong field type"); }
static bool absent(reader r) { return r.id==QA_JSON_NONE; }
static bool null_value(reader r) { return qa_json_type(r.json,r.id)==QA_JSON_NULL; }
static bool record(reader r) {
    return type(r,QA_JSON_OBJECT) && (qa_json_get(r.json,r.id,"$qts")==QA_JSON_NONE || fail(r,"tagged value is not a record"));
}
static bool boolean(reader r) { return type(r,QA_JSON_BOOL); }
static bool string(reader r) { return type(r,QA_JSON_STRING); }
static bool literal(reader r, const char *text) { return qa_json_string_equal(r.json,r.id,text) || fail(r,"unknown field variant"); }
static bool is(reader r, const char *text) { return qa_json_string_equal(r.json,r.id,text); }
static bool number(reader r, double *out) {
    if (qa_json_type(r.json,r.id)==QA_JSON_NUMBER) return qa_json_number(r.json,r.id,out,r.error);
    if (is(field(r,"$qts"),"number")) {
        reader v=field(r,"value");
        if (is(v,"-0")) *out=-0.0;
        else if (is(v,"NaN")) *out=NAN;
        else if (is(v,"Infinity")) *out=INFINITY;
        else if (is(v,"-Infinity")) *out=-INFINITY;
        else return fail(r,"invalid numeric tag");
        return true;
    }
    return fail(r,"expected number");
}
static bool finite(reader r) { double value; return number(r,&value) && (isfinite(value) || fail(r,"non-finite number")); }
static bool numeric(reader r) { double value; return number(r,&value); }
static bool integer(reader r, double low, double high) {
    double value;
    return number(r,&value) && ((isfinite(value) && floor(value)==value && value>=low && value<=high) || fail(r,"integer outside range"));
}
static bool natural(reader r) { return integer(r,0,(double)QA_UNIFIED_SAFE_INTEGER); }
static bool signed_integer(reader r) { return integer(r,-(double)QA_UNIFIED_SAFE_INTEGER,(double)QA_UNIFIED_SAFE_INTEGER); }
static bool optional(reader r, check_fn check) { return absent(r) || check(r); }
static bool nullable(reader r, check_fn check) { return null_value(r) || check(r); }
static bool list(reader r, size_t minimum, size_t maximum, check_fn check) {
    if (!type(r,QA_JSON_ARRAY)) return false;
    size_t count=qa_json_size(r.json,r.id);
    if (count<minimum || count>maximum) return fail(r,"list extent outside range");
    for (size_t i=0;i<count;++i) {
        reader element={r.json,qa_json_at(r.json,r.id,i),r.error};
        if (check && !check(element)) return false;
    }
    return true;
}
static bool fields(reader r, const char *names, check_fn check) {
    if (!record(r)) return false;
    while (*names) {
        const char *end=strchr(names,' '); size_t count=end?(size_t)(end-names):strlen(names);
        char key[64];
        if (count>=sizeof(key)) return fail(r,"internal field name too long");
        memcpy(key,names,count); key[count]=0;
        if (!check(field(r,key))) return false;
        if (!end) break;
        names=end+1;
    }
    return true;
}
static bool choices(reader r, const char *values) {
    while (*values) {
        const char *end=strchr(values,' '); size_t count=end?(size_t)(end-values):strlen(values);
        char value[64];
        if (count>=sizeof(value)) return fail(r,"internal variant too long");
        memcpy(value,values,count); value[count]=0;
        if (is(r,value)) return true;
        if (!end) break;
        values=end+1;
    }
    return fail(r,"unknown variant");
}
static bool bounded_string(reader r, size_t maximum, bool no_zero) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    /* The donor counts UTF-16 code units. */
    size_t units=0;
    for (size_t i=0;i<text.size;++i) {
        uint8_t c=text.data[i];
        if ((c&0xc0u)!=0x80u) units+=c>=0xf0u?2u:1u;
    }
    bool ok=units<=maximum && (!no_zero || !memchr(text.data,0,text.size));
    qa_buffer_free(&text);
    return ok || fail(r,"invalid protocol string");
}
static bool protocol_string(reader r) { return bounded_string(r,8192,true); }
static bool namespaced(reader r) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    const uint8_t *colon=memchr(text.data,':',text.size);
    bool ok=colon && colon!=text.data && colon!=text.data+text.size-1;
    qa_buffer_free(&text); return ok || fail(r,"invalid namespaced identity");
}
static bool content(reader r) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    bool ok=text.size>=8 && text.data[0]=='q' && text.data[1]>='1' && text.data[1]<='3' && text.data[2]==':';
    size_t colons=0,start=0;
    for (size_t i=0;i<text.size;++i) if (text.data[i]==':') { if (i==start) ok=false; colons++; start=i+1; }
    ok=ok && colons==3 && start<text.size && !memchr(text.data,0,text.size);
    qa_buffer_free(&text); return ok || fail(r,"invalid content identity");
}
static bool digest(reader r) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    bool ok=text.size==71 && !memcmp(text.data,"sha256:",7);
    for (size_t i=7;ok && i<text.size;++i) ok=(text.data[i]>='0' && text.data[i]<='9') || (text.data[i]>='a' && text.data[i]<='f');
    qa_buffer_free(&text); return ok || fail(r,"invalid content digest");
}
static bool token(reader r) {
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    bool ok=text.size==32;
    for (size_t i=0;ok && i<text.size;++i) ok=(text.data[i]>='0' && text.data[i]<='9') || (text.data[i]>='a' && text.data[i]<='f');
    qa_buffer_free(&text); return ok || fail(r,"invalid lowercase connection token");
}
static bool bytes(reader r) { return literal(field(r,"$qts"),"bytes"); }
static bool actor(reader r) { return fields(r,"slot generation",natural); }
static bool connection_actor(reader r) { return actor(r) && integer(field(r,"slot"),0,1048575); }
static bool vector(reader r) { return fields(r,"x y z",finite); }
static bool color(reader r) { return fields(r,"x y z w",finite); }
static bool bounds(reader r) { return fields(r,"min max",vector); }
static bool provider(reader r) { return record(r) && namespaced(field(r,"provider")) && content(field(r,"content")); }
static bool owner(reader r) { return record(r) && namespaced(field(r,"provider")) && integer(field(r,"generation"),1,(double)QA_UNIFIED_SAFE_INTEGER); }
static bool source_time(reader r) { return record(r) && choices(field(r,"kind"),"seconds milliseconds") && finite(field(r,"value")); }
static bool frame_context(reader r) {
    return record(r) && natural(field(r,"frame")) && fields(r,"time elapsed",source_time) &&
        choices(field(r,"phase"),"frame-entry client-command entity-prethink entity-physics entity-think client-end-frame frame-exit");
}
static bool hit(reader r) {
    reader kind=field(r,"kind");
    if (is(kind,"none")) return true;
    if (is(kind,"world")) return natural(field(r,"model"));
    return literal(kind,"actor") && actor(field(r,"actor"));
}
static bool resource(reader r) {
    if (!record(r) || !content(field(r,"content")) || !digest(field(r,"digest")) || !natural(field(r,"byteLength"))) return false;
    qa_buffer path={0}; reader path_reader=field(r,"path");
    if (!qa_json_string(r.json,path_reader.id,&path,r.error)) return false;
    bool ok=path.size && path.data[0]!='/' && !memchr(path.data,'\\',path.size) && !memchr(path.data,0,path.size);
    if (path.size>=2 && path.data[1]==':' && ((path.data[0]>='A' && path.data[0]<='Z') || (path.data[0]>='a' && path.data[0]<='z'))) ok=false;
    size_t start=0;
    for (size_t i=0;i<=path.size;++i) if (i==path.size || path.data[i]=='/') {
        size_t length=i-start;
        if (!length || (path.data[start]=='.' && (length==1 || (length==2 && path.data[start+1]=='.')))) ok=false;
        start=i+1;
    }
    qa_buffer_free(&path); return ok || fail(r,"invalid relative resource path");
}
static bool control_resource(reader r) { return resource(r) && bounded_string(field(r,"path"),1024,true) && integer(field(r,"byteLength"),0,2147483647); }
static bool arsenal(reader r) {
    return record(r) && namespaced(field(r,"provider")) && nullable(field(r,"weapon"),namespaced) && boolean(field(r,"useHoldable")) &&
        (absent(field(r,"impulse")) || integer(field(r,"impulse"),0,255));
}
static bool angle_word(reader r) { return integer(r,-2147483648.0,2147483647.0); }
static bool move_value(reader r) { return integer(r,-32768,32767); }
static bool command(reader r) {
    reader kind=field(r,"kind");
    if (!record(r) || !integer(field(r,"buttons"),0,4294967295.0)) return false;
    if (is(kind,"q3")) return integer(field(r,"serverTimeMilliseconds"),0,2147483647) &&
        list(field(r,"angleWords"),3,3,angle_word) && integer(field(r,"weapon"),0,255) && fields(r,"forwardMove rightMove upMove",move_value);
    if (is(kind,"q2-rerelease")) return integer(field(r,"milliseconds"),0,1000) && vector(field(r,"angles")) &&
        fields(r,"forwardMove sideMove",finite) && integer(field(r,"serverFrame"),-1,2147483647);
    if (!fields(r,"forwardMove sideMove upMove",move_value) || !integer(field(r,"impulse"),0,255)) return false;
    if (is(kind,"q1-netquake")) return finite(field(r,"acknowledgedServerTimeSeconds")) && vector(field(r,"viewAngles"));
    if (!integer(field(r,"milliseconds"),0,255)) return false;
    if (is(kind,"q1-quakeworld")) return vector(field(r,"angles"));
    return literal(kind,"q2-classic") && list(field(r,"angleShorts"),3,3,angle_word) && integer(field(r,"lightLevel"),0,255);
}
static bool input(reader r) { return record(r) && natural(field(r,"sequence")) && command(field(r,"command")) && optional(field(r,"arsenal"),arsenal); }
static bool inventory(reader r) {
    reader policy=field(r,"countPolicy");
    return record(r) && namespaced(field(r,"item")) && fields(r,"count capacity",numeric) &&
        (absent(policy) || is(field(policy,"kind"),"stack") || (literal(field(policy,"kind"),"source-counter") && choices(field(policy,"arithmetic"),"binary32 binary64 int32")));
}
static bool body(reader r) { return fields(r,"origin angles velocity",vector) && bounds(field(r,"bounds")) && nullable(field(r,"ground"),actor); }
static bool movement(reader r) {
    reader kind=field(r,"kind");
    if (is(kind,"q1-netquake")) return fields(r,"origin velocity angles oldOrigin angularVelocity viewAngles punchAngles waterJumpDirection",vector) &&
        fields(r,"moveType flags waterLevel waterType teleportTimeSeconds idealPitch health",finite) && hit(field(r,"ground")) && boolean(field(r,"fixAngle"));
    if (is(kind,"q1-quakeworld")) return fields(r,"origin velocity angles",vector) && fields(r,"oldButtons waterJumpTimeSeconds spectator",finite) &&
        boolean(field(r,"dead")) && hit(field(r,"ground"));
    if (is(kind,"q2-classic")) return fields(r,"type flags timeEightMilliseconds gravity",finite) &&
        list(field(r,"originEighths"),3,3,finite) && list(field(r,"velocityEighths"),3,3,finite) && list(field(r,"deltaAngleShorts"),3,3,finite);
    if (is(kind,"q2-rerelease")) return fields(r,"type flags timeMilliseconds gravity viewHeight",finite) && fields(r,"origin velocity deltaAngles",vector);
    return literal(kind,"q3") && fields(r,"commandTimeMilliseconds movementType bobCycle movementFlags movementTimeMilliseconds gravity speed movementDirection flags viewHeight predictableEventSequence movementFrame jumpPadFrame",finite) &&
        fields(r,"origin velocity grapplePoint viewAngles",vector) && list(field(r,"deltaAngleWords"),3,3,finite) && hit(field(r,"ground")) && nullable(field(r,"jumpPad"),actor);
}
static bool clock_profile(reader r) {
    reader kind=field(r,"kind");
    if (is(kind,"q1-netquake")) return fields(r,"minimumFrameSeconds maximumFrameSeconds",finite) && nullable(field(r,"fixedFrameSeconds"),finite);
    if (is(kind,"q1-quakeworld")) return finite(field(r,"maximumCommandMilliseconds"));
    if (is(kind,"q2-classic")) return integer(field(r,"frameMilliseconds"),100,100);
    if (is(kind,"q2-rerelease")) return finite(field(r,"frameMilliseconds")) && literal(field(r,"preparation"),"before-frame");
    return literal(kind,"q3") && finite(field(r,"serverFrameMilliseconds")) && nullable(field(r,"fixedMovementMilliseconds"),finite) && integer(field(r,"maximumCommandMilliseconds"),200,200);
}
static bool numeric_profile(reader r) {
    reader a=field(r,"arithmetic"),kind=field(a,"kind");
    if (!namespaced(field(r,"id")) || !literal(field(r,"scalarStorage"),"binary32") ||
        !choices(field(r,"floatToInt"),"qvm-indefinite x86-indefinite checked-c-truncation") || !literal(field(r,"integerOverflow"),"wrap32")) return false;
    if (is(kind,"binary32")) return literal(field(a,"round"),"each-operation");
    if (is(kind,"donor-binary64")) return choices(field(a,"source"),"q1-ts q2-ts");
    if (!choices(field(a,"rounding"),"nearest-even toward-zero toward-positive toward-negative")) return false;
    if (is(kind,"sse")) return fields(a,"flushToZero denormalsAreZero",boolean);
    double precision;
    return literal(kind,"x87") && number(field(a,"precisionBits"),&precision) && ((precision==24 || precision==53 || precision==64) || fail(r,"invalid x87 precision"));
}
static bool profile(reader r) {
    reader kind=field(r,"kind"),clock=field(r,"clock");
    if (!namespaced(field(r,"id")) || !clock_profile(clock) || !numeric_profile(field(r,"numeric"))) return false;
    qa_buffer name={0};
    if (!qa_json_string(r.json,kind.id,&name,r.error)) return false;
    bool matched=is(field(clock,"kind"),(const char *)name.data); qa_buffer_free(&name);
    if (!matched) return fail(r,"movement clock differs from profile");
    if (is(kind,"q1-netquake") || is(kind,"q1-quakeworld")) {
        if (!fields(field(r,"parameters"),"gravity stopSpeed maxSpeed spectatorMaxSpeed accelerate airAccelerate waterAccelerate friction waterFriction entityGravity",finite)) return false;
        return is(kind,"q1-quakeworld") || (choices(field(r,"edition"),"classic rerelease quake64") && finite(field(r,"edgeFriction")) && boolean(field(r,"noClipAngleHack")));
    }
    if (is(kind,"q2-classic")) return finite(field(r,"airAccelerate")) && boolean(field(r,"snapInitial")) && optional(field(r,"strafejumpHack"),boolean);
    if (is(kind,"q2-rerelease")) return finite(field(r,"airAccelerate")) && boolean(field(r,"n64Physics"));
    return literal(kind,"q3") && choices(field(r,"product"),"baseq3 missionpack") && nullable(field(r,"fixedMilliseconds"),finite) && boolean(field(r,"noFootsteps"));
}
static bool weapon_state(reader r) {
    reader kind=field(r,"kind");
    if (is(kind,"q1")) return fields(r,"frame attackFinishedSeconds sourceWeapon",finite);
    if (is(kind,"q2")) return fields(r,"gunFrame state machinegunShots",finite) && nullable(field(r,"pendingWeapon"),namespaced) && source_time(field(r,"grenadeTime")) && boolean(field(r,"grenadeBlewUp"));
    return literal(kind,"q3") && fields(r,"sourceWeapon state timeMilliseconds",finite);
}
static bool animation(reader r) {
    reader kind=field(r,"kind");
    if (is(kind,"q1")) return fields(r,"frame nextFrameSeconds",finite);
    if (is(kind,"q2")) return fields(r,"frame endFrame priority",finite) && fields(r,"duck run",boolean);
    return literal(kind,"q3") && fields(r,"legs torso legsTimerMilliseconds torsoTimerMilliseconds",finite);
}
static bool collision(reader r) {
    reader b=field(r,"body"),c=field(r,"collision"),shape=field(c,"shape");
    return actor(field(b,"actor")) && body(field(b,"state")) && natural(field(b,"linkCount")) && bounds(field(b,"absoluteBounds")) &&
        choices(field(c,"family"),"q1 q2 q3") && choices(field(shape,"kind"),"box capsule model") &&
        (!is(field(shape,"kind"),"model") || natural(field(shape,"model"))) && signed_integer(field(c,"contents")) && nullable(field(c,"owner"),actor) &&
        choices(field(c,"role"),"solid trigger") && fields(c,"monster deadMonster",boolean) &&
        optional(field(c,"q1Corpse"),boolean) && (absent(field(c,"q3Owner")) || fields(field(c,"q3Owner"),"entityNumber ownerNumber",signed_integer));
}
static bool client_outputs(reader r) {
    return optional(field(r,"viewOffset"),vector) && optional(field(r,"stance"),boolean) && optional(field(r,"bodyBounds"),bounds) &&
        (absent(field(r,"mode")) || choices(field(r,"mode"),"normal noclip freeze"));
}
static bool contact(reader r) { return hit(field(r,"ground")) && fields(r,"waterLevel waterType",signed_integer); }
static bool prediction(reader r) {
    if (!literal(field(r,"schema"),"qts-unified-prediction") || !integer(field(r,"version"),1,1) ||
        !actor(field(r,"actor")) || !integer(field(r,"sequence"),-1,(double)QA_UNIFIED_SAFE_INTEGER) || !finite(field(r,"commandTimeMilliseconds")) ||
        !movement(field(r,"state")) || !profile(field(r,"profile"))) return false;
    qa_buffer kind={0}; reader state_kind=field(field(r,"state"),"kind");
    if (!qa_json_string(r.json,state_kind.id,&kind,r.error)) return false;
    bool same=is(field(field(r,"profile"),"kind"),(const char *)kind.data); qa_buffer_free(&kind);
    if (!same) return fail(r,"prediction movement differs from profile");
    reader arsenal_state=field(r,"arsenal"),anim=field(r,"animation"),environment=field(r,"environment");
    return namespaced(field(arsenal_state,"provider")) && nullable(field(arsenal_state,"activeWeapon"),namespaced) &&
        weapon_state(field(arsenal_state,"state")) && list(field(arsenal_state,"ammo"),0,SIZE_MAX,inventory) &&
        namespaced(field(anim,"provider")) && animation(field(anim,"state")) && fields(r,"standingBounds bounds",bounds) &&
        fields(r,"standingViewHeight viewHeight",finite) && fields(r,"viewAngles viewOffset",vector) &&
        fields(environment,"health gravityMultiplier",finite) && fields(environment,"flight haste invulnerable",boolean) &&
        optional(field(environment,"clientOutputs"),client_outputs) && nullable(field(r,"contact"),contact) &&
        list(field(r,"collisions"),0,SIZE_MAX,collision);
}

static bool pose(reader r) {
    reader kind=field(r,"kind");
    if (is(kind,"frame")) return fields(r,"frame previousFrame",signed_integer) && finite(field(r,"backLerp"));
    if (!literal(kind,"skeleton") || !type(field(r,"joints"),QA_JSON_ARRAY)) return false;
    reader joints=field(r,"joints"); size_t count=qa_json_size(r.json,joints.id);
    for (size_t i=0;i<count;++i) {
        reader j={r.json,qa_json_at(r.json,joints.id,i),r.error};
        if (!vector(field(j,"position")) || !color(field(j,"orientation")) || !finite(field(j,"scale"))) return false;
    }
    return true;
}
static bool scene_entity(reader r, unsigned depth) {
    if (depth>32) return fail(r,"scene attachment nesting exceeds limit");
    reader transform=field(r,"transform"),flags=field(r,"flags"),attachments=field(r,"attachments");
    if (!nullable(field(r,"actor"),actor) || !resource(field(r,"resource")) || !nullable(field(r,"brushModel"),natural) ||
        !fields(transform,"origin scale",vector) || !list(field(transform,"axis"),3,3,vector) ||
        !fields(r,"previousOrigin lightingOrigin",vector) || !pose(field(r,"pose")) || !signed_integer(field(r,"skin")) ||
        !color(field(r,"color")) || !source_time(field(r,"shaderTime")) || !choices(field(flags,"kind"),"q1 q2 q3") ||
        !signed_integer(field(flags,"bits")) || !finite(field(r,"shadowPlane")) || !optional(field(r,"opacity"),finite) || !type(attachments,QA_JSON_ARRAY)) return false;
    size_t count=qa_json_size(r.json,attachments.id);
    for (size_t i=0;i<count;++i) {
        reader a={r.json,qa_json_at(r.json,attachments.id,i),r.error};
        if (!string(field(a,"tag")) || !scene_entity(field(a,"entity"),depth+1)) return false;
    }
    return true;
}
static bool entity(reader r) { return scene_entity(r,0); }
static bool light(reader r) {
    reader p=field(r,"profile");
    if (!fields(r,"origin color",vector) || !finite(field(r,"radius")) || !boolean(field(r,"additive")) || !choices(field(p,"kind"),"q1 q2 q3")) return false;
    if (!is(field(p,"kind"),"q2")) return true;
    reader cone=field(p,"cone"),shadow=field(p,"shadow");
    return finite(field(p,"scale")) && (null_value(cone) || (vector(field(cone,"direction")) && finite(field(cone,"cosHalfAngle")))) &&
        choices(field(shadow,"kind"),"none cast") && (!is(field(shadow,"kind"),"cast") || integer(field(shadow,"resolution"),1,(double)QA_UNIFIED_SAFE_INTEGER));
}
static bool particle(reader r) {
    if (!vector(field(r,"origin")) || !finite(field(r,"size"))) return false;
    if (is(field(r,"kind"),"indexed")) return natural(field(r,"paletteIndex")) && finite(field(r,"alpha"));
    return literal(field(r,"kind"),"rgba") && color(field(r,"color")) && finite(field(r,"rotation"));
}
static bool light_style(reader r) {
    if (!natural(field(r,"style"))) return false;
    if (is(field(r,"kind"),"q1")) return finite(field(r,"value"));
    return literal(field(r,"kind"),"q2") && vector(field(r,"rgb")) && finite(field(r,"white"));
}
static bool scene(reader r) {
    return source_time(field(r,"time")) && nullable(field(r,"world"),resource) && list(field(r,"entities"),0,SIZE_MAX,entity) &&
        list(field(r,"lights"),0,SIZE_MAX,light) && list(field(r,"particles"),0,SIZE_MAX,particle) && list(field(r,"lightStyles"),0,SIZE_MAX,light_style) && nullable(field(r,"areaBits"),bytes);
}
static bool actor_record(reader r) { return actor(field(r,"id")) && fields(r,"owner definition",namespaced); }
static bool actor_body(reader r) { return actor(field(r,"actor")) && body(field(r,"body")); }
static bool actor_inventory(reader r) { return actor(field(r,"actor")) && list(field(r,"entries"),0,SIZE_MAX,inventory); }
static bool actor_configuration(reader r) {
    return actor(field(r,"actor")) && fields(r,"movement inventory",provider) && fields(field(r,"character"),"definition appearance",provider) &&
        list(field(r,"weapons"),0,SIZE_MAX,provider);
}
static bool player_view(reader r) {
    return fields(r,"origin angles",vector) && finite(field(r,"viewHeight")) && optional(field(r,"blend"),color) && optional(field(r,"damageBlend"),color) &&
        optional(field(r,"kickAngles"),vector) && optional(field(r,"fieldOfView"),finite) && optional(field(r,"clientViewOffsetDelta"),vector) &&
        optional(field(r,"foreignCharacterDeath"),boolean) && (absent(field(r,"pitchDrift")) ||
        (fields(field(r,"pitchDrift"),"grounded disabled",boolean) && finite(field(field(r,"pitchDrift"),"idealPitch"))));
}
static bool model(reader r) {
    return actor(field(r,"actor")) && content(field(r,"content")) && choices(field(r,"family"),"q1 q2 q3") && string(field(r,"path")) &&
        fields(r,"frame oldFrame skin effects renderFlags",signed_integer) && fields(r,"origin angles",vector) && finite(field(r,"scale")) &&
        fields(r,"visible viewWeapon",boolean) && optional(field(r,"alpha"),finite) && optional(field(r,"previousOrigin"),vector) &&
        optional(field(r,"backLerp"),finite);
}
static bool character(reader r) {
    return actor(field(r,"actor")) && fields(r,"origin angles velocity",vector) && fields(r,"movementDirection sourceFlags powerups",signed_integer) &&
        literal(field(field(r,"animation"),"kind"),"q3") && animation(field(r,"animation")) &&
        (null_value(field(r,"team")) || choices(field(r,"team"),"red blue")) && color(field(r,"color")) &&
        optional(field(r,"scale"),finite) && optional(field(r,"opacity"),finite);
}
static bool world_text(reader r) {
    reader orientation=field(r,"orientation");
    return content(field(r,"content")) && string(field(r,"text")) && vector(field(r,"origin")) && color(field(r,"color")) && finite(field(r,"cellSize")) &&
        choices(field(orientation,"kind"),"billboard fixed") && (!is(field(orientation,"kind"),"fixed") || vector(field(orientation,"angles"))) &&
        boolean(field(r,"depthTest")) && choices(field(r,"font"),"classic selected") && optional(field(r,"distanceCullFactor"),finite);
}
static bool simulation_event(reader r) {
    reader audience=field(r,"audience"),payload=field(r,"payload");
    if (!finite(field(r,"sequence")) || !source_time(field(r,"time")) || !choices(field(audience,"kind"),"world seat client") ||
        !choices(field(payload,"kind"),"sound damage transition message")) return false;
    if (is(field(audience,"kind"),"seat") && !natural(field(field(audience,"seat"),"index"))) return false;
    if (is(field(audience,"kind"),"client") && !actor(field(audience,"client"))) return false;
    return true;
}
static bool presentation_event(reader r) {
    return choices(field(r,"kind"),"debug-graph presentation-owner q1 q1-fog q1-sky q1-client q1-session music q1-composition q1-level q2 q2-weapon view-reset q2-composition q2-rerelease q2-player q3-character q3-ballistics q3-source") &&
        finite(field(r,"sequence")) && content(field(r,"content")) && finite(field(r,"seconds")) &&
        (is(field(r,"kind"),"view-reset")?(choices(field(r,"reason"),"spawn teleport freeze source") && actor(field(r,"actor")) && vector(field(r,"angles"))):record(field(r,"event"))) &&
        optional(field(r,"owner"),owner) && optional(field(r,"recipient"),actor) &&
        (absent(field(r,"sourceEntity")) || nullable(field(r,"sourceEntity"),finite));
}
static bool component_header(reader r) {
    reader sources=field(r,"sources"),native=field(r,"native");
    return record(r) && natural(field(r,"revision")) && list(sources,0,256,record) && (absent(native) || list(native,0,256,record)) &&
        (qa_json_size(r.json,sources.id)+qa_json_size(r.json,native.id)<=256 || fail(r,"too many component owners"));
}
static bool frame(reader r) {
    reader output=field(r,"output"),snapshot=field(output,"snapshot"),player=field(r,"player");
    return literal(field(r,"schema"),"qts-unified-frame") && integer(field(r,"version"),2,9) && integer(field(r,"epoch"),1,(double)QA_UNIFIED_SAFE_INTEGER) &&
        integer(field(r,"acknowledgedInput"),-1,(double)QA_UNIFIED_SAFE_INTEGER) && bytes(field(r,"prediction")) &&
        frame_context(field(snapshot,"frame")) && list(field(snapshot,"actors"),0,SIZE_MAX,actor_record) && list(field(snapshot,"bodies"),0,SIZE_MAX,actor_body) &&
        list(field(snapshot,"inventories"),0,SIZE_MAX,actor_inventory) && list(field(snapshot,"configurations"),0,SIZE_MAX,actor_configuration) && scene(field(snapshot,"scene")) &&
        list(field(output,"events"),0,65536,simulation_event) && list(field(r,"models"),0,SIZE_MAX,model) && list(field(r,"characters"),0,SIZE_MAX,character) &&
        list(field(r,"worldText"),0,SIZE_MAX,world_text) && actor(field(player,"actor")) && player_view(field(player,"view")) && record(field(player,"ui")) &&
        optional(field(r,"components"),component_header) && (absent(field(r,"nativeCamera")) ||
        (owner(field(field(r,"nativeCamera"),"owner")) && natural(field(field(r,"nativeCamera"),"generation")) && player_view(field(field(r,"nativeCamera"),"view"))));
}
static bool command_name(reader r) {
    if (!bounded_string(r,128,true)) return false;
    qa_buffer text={0};
    if (!qa_json_string(r.json,r.id,&text,r.error)) return false;
    bool ok=text.size>0;
    for (size_t i=0;ok && i<text.size;++i) {
        uint8_t c=text.data[i];
        ok=(c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' || c=='+' || (i>0 && (c=='-' || (c>='0' && c<='9')));
    }
    qa_buffer_free(&text); return ok || fail(r,"invalid command name");
}
static bool control(reader r) {
    reader kind=field(r,"kind");
    if (is(kind,"disconnect")) return bounded_string(field(r,"reason"),1024,true);
    if (!integer(field(r,"epoch"),1,4294967295.0)) return false;
    if (is(kind,"offer")) return record(field(r,"composition")) && choices(field(r,"mode"),"singleplayer coop deathmatch") && integer(field(r,"maxClients"),1,256);
    if (is(kind,"ready")) return digest(field(r,"composition")) && protocol_string(field(r,"userinfo"));
    if (is(kind,"admitted")) return fields(r,"client actor",connection_actor) && natural(field(r,"sourceEntity"));
    if (is(kind,"resources")) return list(field(r,"resources"),0,32768,control_resource);
    if (is(kind,"events")) return natural(field(r,"frame")) && fields(r,"payload simulation",bytes);
    if (is(kind,"components")) return component_header(field(r,"update")) && integer(field(field(r,"update"),"revision"),1,(double)QA_UNIFIED_SAFE_INTEGER);
    if (is(kind,"component-command")) return owner(field(r,"owner")) && natural(field(r,"generation")) && list(field(r,"args"),1,128,protocol_string);
    if (is(kind,"command")) return command_name(field(r,"name")) && list(field(r,"args"),0,128,protocol_string);
    return literal(kind,"userinfo") && protocol_string(field(r,"value"));
}

bool qa_unified_schema_check(qa_unified_document_kind kind, const qa_json_document *json, qa_error *error) {
    reader r={json,qa_json_root(json),error};
    if (kind==QA_UNIFIED_CHECKPOINT) return true;
    if (kind==QA_UNIFIED_FRAME_DOCUMENT) return frame(r);
    if (kind==QA_UNIFIED_PREDICTION_DOCUMENT) return prediction(r);
    if (kind==QA_UNIFIED_EVENTS_DOCUMENT) return list(r,0,65536,presentation_event);
    const char *schema=kind==QA_UNIFIED_CONTROL_DOCUMENT?"qts-control":kind==QA_UNIFIED_INPUT_DOCUMENT?"qts-input":"qts-connect";
    if (!literal(field(r,"schema"),schema) || !integer(field(r,"version"),1,1)) return false;
    r=field(r,"value");
    if (kind==QA_UNIFIED_CONTROL_DOCUMENT) return control(r);
    if (kind==QA_UNIFIED_INPUT_DOCUMENT) return integer(field(r,"epoch"),1,4294967295.0) && list(field(r,"commands"),0,64,input);
    reader variant=field(r,"kind");
    return choices(variant,"hello challenge connect") && token(field(r,"nonce")) && (is(variant,"hello") || token(field(r,"token")));
}
