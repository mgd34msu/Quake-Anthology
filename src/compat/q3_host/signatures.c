#include "internal.h"

#define I QA_NATIVE_I32
#define P QA_NATIVE_ADDRESS
#define S(n, ...) {n, {__VA_ARGS__}}
#define Z {0, {I}}

static const q3_signature game[46] = {
    [0]=S(1,P), [1]=S(1,P), [2]=Z, [3]=S(4,P,P,P,I), [4]=S(1,P),
    [5]=S(2,P,P), [6]=S(1,P), [7]=S(3,P,P,I), [8]=Z, [9]=S(3,I,P,I),
    [10]=S(3,P,P,I), [11]=S(3,P,I,I), [12]=S(3,P,I,I), [13]=S(1,I),
    [14]=S(2,I,P), [15]=S(5,P,I,I,P,I), [16]=S(2,I,P), [17]=S(2,I,P),
    [18]=S(2,I,P), [19]=S(3,I,P,I), [20]=S(3,I,P,I), [21]=S(2,I,P),
    [22]=S(2,P,I), [23]=S(2,P,P), [24]=S(7,P,P,P,P,P,I,I),
    [25]=S(2,P,I), [26]=S(2,P,P), [27]=S(2,P,P), [28]=S(2,P,I),
    [29]=S(2,I,I), [30]=S(1,P), [31]=S(1,P), [32]=S(4,P,P,P,I),
    [33]=S(3,P,P,P), [34]=Z, [35]=S(1,I), [36]=S(2,I,P), [37]=S(2,P,I),
    [38]=S(4,P,P,P,I), [39]=S(3,I,I,P), [40]=S(1,I), [41]=S(1,P),
    [42]=S(1,P), [43]=S(7,P,P,P,P,P,I,I), [44]=S(3,P,P,P), [45]=S(3,I,I,I)
};

static const q3_signature cgame[90] = {
    [0]=S(1,P), [1]=S(1,P), [2]=Z, [3]=S(4,P,P,P,I), [4]=S(1,P),
    [5]=S(2,P,P), [6]=S(3,P,P,I), [7]=Z, [8]=S(3,I,P,I), [9]=S(2,P,I),
    [10]=S(3,P,P,I), [11]=S(3,P,I,I), [12]=S(3,P,I,I), [13]=S(1,I),
    [14]=S(1,P), [15]=S(1,P), [16]=S(1,P), [17]=Z, [18]=S(1,P), [19]=Z,
    [20]=S(1,I), [21]=S(1,P), [22]=S(2,P,P), [23]=S(2,P,I), [24]=S(4,P,I,P,P),
    [25]=S(7,P,P,P,P,P,I,I), [26]=S(9,P,P,P,P,P,I,I,P,P),
    [27]=S(7,I,P,P,I,P,I,P), [28]=S(4,P,I,I,I), [29]=S(2,I,I), [30]=S(1,I),
    [31]=S(4,I,P,P,I), [32]=S(2,I,P), [33]=S(4,I,P,P,I), [34]=S(2,P,I),
    [35]=S(2,P,P), [36]=S(1,P), [37]=S(1,P), [38]=S(1,P), [39]=S(1,P),
    [40]=Z, [41]=S(1,P), [42]=S(3,I,I,P), [43]=S(5,P,I,I,I,I), [44]=S(1,P),
    [45]=S(1,P), [46]=S(9,I,I,I,I,I,I,I,I,I), [47]=S(3,I,P,P),
    [48]=S(6,P,I,I,I,I,P), [49]=S(1,P), [50]=S(1,P), [51]=S(2,P,P),
    [52]=S(2,I,P), [53]=S(1,I), [54]=Z, [55]=S(2,I,P), [56]=S(2,I,I),
    [57]=S(1,P), [Q3_CGAME_MEMORY_REMAINING]=Z, [Q3_CGAME_REGISTER_FONT]=S(3,P,I,P), [60]=S(1,I), [61]=Z, [62]=S(1,I),
    [63]=S(1,P), [64]=S(1,P), [65]=S(1,P), [66]=S(1,I), [67]=S(2,I,P),
    [68]=S(3,I,P,P), [69]=Z, [70]=S(1,P), [71]=S(1,P), [72]=S(1,P),
    [73]=S(4,P,P,P,P), [74]=S(6,P,I,I,I,I,I), [75]=S(1,I), [76]=S(1,I),
    [77]=S(1,I), [78]=S(5,I,I,I,I,I), [79]=S(3,P,P,P), [80]=S(4,I,P,P,I),
    [81]=S(1,I), [82]=S(2,P,P), [83]=S(7,P,P,P,P,P,I,I),
    [84]=S(9,P,P,P,P,P,I,I,P,P), [85]=S(5,P,I,I,I,I), [86]=S(2,P,I),
    [87]=S(4,I,I,P,I), [88]=S(2,P,P), [89]=S(3,I,I,I)
};

static const q3_signature ui[88] = {
    [0]=S(1,P), [1]=S(1,P), [2]=Z, [3]=S(2,P,P), [4]=S(1,P), [5]=S(3,P,P,I),
    [6]=S(2,P,I), [7]=S(1,P), [8]=S(3,P,P,I), [9]=S(3,I,P,I), [10]=Z,
    [11]=S(3,I,P,I), [12]=S(2,I,P), [13]=S(3,P,P,I), [14]=S(3,P,I,I),
    [15]=S(3,P,I,I), [16]=S(1,I), [17]=S(4,P,P,P,I), [18]=S(1,P), [19]=S(1,P),
    [20]=S(1,P), [21]=Z, [22]=S(1,P), [23]=S(3,I,I,P), [24]=S(5,P,I,I,I,I),
    [25]=S(1,P), [26]=S(1,P), [27]=S(9,I,I,I,I,I,I,I,I,I), [28]=Z,
    [29]=S(6,P,I,I,I,I,P), [30]=S(1,P), [31]=S(2,P,I), [32]=S(2,I,I),
    [33]=S(3,I,P,I), [34]=S(3,I,P,I), [35]=S(2,I,P), [36]=S(1,I), [37]=Z,
    [38]=S(1,I), [39]=Z, [40]=Z, [41]=S(1,I), [42]=S(2,P,I), [43]=S(1,P),
    [44]=S(1,P), [45]=S(3,I,P,I), [46]=Z, [47]=S(1,I), [48]=S(4,I,P,I,P),
    [49]=S(3,I,P,I), [50]=S(4,P,P,P,I), [51]=S(1,P), [Q3_UI_MEMORY_REMAINING]=Z, [53]=S(2,P,I),
    [54]=S(1,P), [Q3_UI_REGISTER_FONT]=S(3,P,I,P), [56]=S(3,I,P,P), [57]=S(1,P), [58]=S(1,P),
    [59]=S(1,I), [60]=S(2,I,P), [61]=S(3,I,P,P), [62]=Z, [63]=S(2,P,P),
    [64]=S(1,P), [65]=S(1,I), [66]=S(4,I,I,P,I), [67]=S(4,I,I,P,I),
    [68]=S(3,I,I,I), [69]=S(1,I), [70]=S(1,I), [71]=Z, [72]=Z,
    [73]=S(3,I,P,P), [74]=S(2,I,P), [75]=S(6,P,I,I,I,I,I), [76]=S(1,I),
    [77]=S(1,I), [78]=S(1,I), [79]=S(5,I,I,I,I,I), [80]=S(3,P,P,P),
    [81]=S(2,P,P), [82]=S(3,P,P,I), [83]=S(2,I,I), [84]=S(2,I,I),
    [85]=S(5,I,I,I,I,I), [86]=S(3,I,I,I), [87]=S(1,I)
};

static const q3_signature legacy_browser[4] = {Z,S(3,I,P,I),Z,S(3,I,P,I)};
static const q3_signature intrinsic_memory[3] = {S(3,P,I,I),S(3,P,P,I),S(3,P,P,I)};
static const q3_signature intrinsic_scalar[2] = {S(1,I),S(2,I,I)};
static const q3_signature intrinsic_vectors[4] = {S(1,P),S(2,P,P),S(3,P,P,P),S(4,P,P,P,P)};
static const q3_signature elementary[24] = {
    S(2,I,P),S(2,I,P),S(2,I,P),S(2,I,I),S(1,I),S(1,I),S(1,I),S(1,I),
    S(1,I),S(1,I),S(1,I),S(1,I),S(1,I),S(1,I),S(1,I),S(1,I),
    S(2,I,I),S(1,I),S(1,I),S(3,I,P,I),S(2,I,P),S(2,I,I),S(3,I,I,P),S(1,I)
};
static const q3_signature bot_library[12] = {
    Z,Z,S(2,P,P),S(3,P,P,I),S(1,P),S(1,I),S(1,P),S(2,I,P),Z,
    S(2,I,I),S(3,I,P,I),S(2,I,P)
};
static const q3_signature bot_navigation[19] = {
    S(2,I,I),S(4,P,P,P,I),S(2,I,P),S(2,I,P),Z,S(3,I,P,P),Z,S(1,P),
    S(5,P,P,P,P,I),S(1,P),S(1,I),S(4,I,P,P,I),S(3,I,P,P),S(3,I,P,P),
    S(3,I,P,P),S(1,I),S(4,I,P,I,I),S(1,P),S(13,P,I,P,I,I,P,P,I,I,I,I,I,I)
};
static const q3_signature bot_ai[82] = {
    [0]=S(2,P,I),[1]=S(1,I),[2]=S(2,I,I),[3]=S(4,I,I,I,I),
    [4]=S(2,I,I),[5]=S(4,I,I,I,I),[6]=S(4,I,I,P,I),[7]=Z,[8]=S(1,I),
    [9]=S(3,I,I,P),[10]=S(2,I,I),[11]=S(2,I,P),[12]=S(1,I),
    [13]=S(11,I,P,I,P,P,P,P,P,P,P,P),[14]=S(12,I,P,I,I,P,P,P,P,P,P,P,P),
    [15]=S(1,I),[16]=S(3,I,I,I),[17]=S(3,P,P,I),[18]=S(3,P,P,I),
    [19]=S(4,P,I,P,I),[20]=S(1,P),[21]=S(2,P,I),[22]=S(3,I,P,P),
    [23]=S(2,I,I),[24]=S(3,I,P,I),[25]=S(1,I),[26]=S(1,I),[27]=S(2,I,P),
    [28]=S(1,I),[29]=S(1,I),[30]=S(1,I),[31]=S(1,I),[32]=S(3,I,P,I),
    [33]=S(2,I,P),[34]=S(2,I,P),[35]=S(4,I,P,P,I),[36]=S(6,I,P,P,I,P,I),
    [37]=S(2,P,P),[38]=S(4,I,P,P,P),[39]=S(3,I,P,P),[40]=S(2,I,I),
    [41]=Z,[42]=Z,[43]=S(2,I,P),[44]=S(1,I),[45]=S(2,I,P),[46]=S(1,I),
    [47]=S(1,I),[48]=S(1,I),[49]=S(4,P,I,P,I),[50]=S(4,I,P,I,I),
    [51]=S(1,I),[52]=S(1,I),[53]=S(2,P,I),[54]=S(5,I,P,I,I,P),[55]=Z,
    [56]=S(1,I),[57]=S(2,I,P),[58]=S(2,I,P),[59]=S(3,I,I,P),[60]=S(2,I,P),
    [61]=Z,[62]=S(1,I),[63]=S(1,I),[64]=S(5,I,P,P,P,P),[65]=S(3,I,I,I),
    [66]=S(2,I,I),[67]=S(2,I,P),[68]=S(2,P,P),[69]=S(2,I,P),[70]=S(3,I,P,I),
    [71]=S(2,I,I),[72]=S(5,P,I,P,I,P),[73]=S(3,I,I,I),[74]=S(4,I,P,I,I),
    [75]=S(8,P,I,P,I,I,P,I,I),[76]=S(11,P,I,P,I,I,I,I,I,I,I,I),[77]=S(1,P),
    [78]=S(1,P),[79]=S(1,I),[80]=S(2,I,P),[81]=S(3,I,P,P)
};
static const q3_signature legacy_sound = S(1,P), legacy_clear_loops = Z;
static const q3_signature legacy_chat_name = S(2,I,P);

bool q3_signature_find(qa_qvm_role role, qa_qvm_abi abi, int32_t source,
                        const q3_signature **out, int32_t *canonical, qa_error *error)
{
    bool engine;
    if (!out || !canonical || !qa_qvm_classify_syscall(role, abi, source, canonical, &engine, error)) return false;
    size_t count; uint32_t pointers; bool address_result;
    if (qa_q3_abi_intrinsic_signature(role, abi, source, &count, &pointers, &address_result)) {
        *out = source >= 100 && source <= 102 ? &intrinsic_memory[source - 100] :
                 pointers ? &intrinsic_vectors[count - 1] : &intrinsic_scalar[count - 1];
        return true;
    }
    if (role == QA_QVM_UI && abi == QA_QVM_Q3_116N && source >= 46 && source <= 49) {
        *out = &legacy_browser[source - 46]; return true;
    }
    if (abi == QA_QVM_Q3_116N && ((role == QA_QVM_CGAME && source == 34) ||
                                  (role == QA_QVM_UI && source == 31))) {
        *out = &legacy_sound; return true;
    }
    if (abi == QA_QVM_Q3_116N && role == QA_QVM_CGAME && source == 30) {
        *out = &legacy_clear_loops; return true;
    }
    if (role == QA_QVM_GAME && abi == QA_QVM_Q3_116N && source >= 402 && source <= 405) {
        *out = &elementary[0]; return true;
    }
    if (role == QA_QVM_GAME && engine && *canonical >= 400 && *canonical <= 423) {
        *out = &elementary[*canonical - 400]; return true;
    }
    if (role == QA_QVM_GAME && engine) {
        if (*canonical >= 200 && *canonical <= 211) {
            *out = &bot_library[*canonical - 200]; return true;
        }
        if (*canonical >= 300 && *canonical <= 318) {
            *out = &bot_navigation[*canonical - 300]; return true;
        }
        if (*canonical >= 500 && *canonical <= 581) {
            *out = abi == QA_QVM_Q3_116N && *canonical == 524 ? &legacy_chat_name :
                       &bot_ai[*canonical - 500];
            return true;
        }
    }
    size_t limit = role == QA_QVM_GAME ? 46u : role == QA_QVM_CGAME ? 90u : 88u;
    if (engine && *canonical >= 0 && (size_t)*canonical < limit) {
        *out = (role == QA_QVM_GAME ? game : role == QA_QVM_CGAME ? cgame : ui) + *canonical;
        return true;
    }
    return q3_fail(error, QA_ERROR_UNSUPPORTED, (size_t)(uint32_t)source,
                    "Q3 service has no source signature");
}

bool q3_ql_service(int32_t source, int32_t *canonical, qa_error *error)
{
    /* Values are semantic service plus one, leaving unused import slots zero. */
    static const uint16_t services[206] = {
        [0]=15,[2]=1,[4]=13,[6]=12,[8]=11,[9]=14,[10]=2,[11]=7,[12]=5,
        [13]=8,[15]=6,[17]=4,[18]=10,[20]=9,[22]=16,[23]=17,[24]=18,
        [25]=19,[26]=20,[28]=21,[29]=22,[30]=23,[31]=24,[32]=25,[33]=44,
        [34]=26,[37]=29,[39]=31,[40]=32,[41]=33,[42]=34,[43]=35,[44]=36,
        [45]=37,[46]=38,[49]=201,[50]=202,[51]=203,[53]=205,[54]=206,[55]=207,
        [56]=208,[58]=210,[60]=212,[63]=304,[64]=305,[66]=307,[67]=308,
        [69]=309,[70]=306,[71]=578,[72]=302,[73]=303,[74]=310,[75]=311,
        [76]=312,[77]=313,[78]=314,[79]=315,[80]=316,[81]=317,[82]=301,
        [83]=577,[84]=576,[85]=318,[86]=319,[87]=403,[88]=401,[89]=402,
        [90]=404,[91]=405,[92]=406,[93]=407,[94]=408,[95]=409,[96]=411,
        [97]=412,[98]=413,[99]=414,[100]=415,[101]=416,[102]=410,[103]=417,
        [104]=418,[105]=419,[106]=420,[107]=421,[108]=422,[109]=423,[110]=424,
        [111]=501,[112]=502,[113]=503,[114]=504,[115]=505,[116]=506,[117]=507,
        [118]=508,[119]=509,[120]=510,[121]=511,[122]=512,[123]=513,[124]=514,
        [125]=570,[126]=515,[127]=516,[128]=517,[129]=571,[130]=518,[131]=519,
        [132]=520,[133]=521,[134]=522,[135]=524,[136]=525,[137]=523,
        [138]=526,[139]=527,[140]=572,[141]=528,[142]=529,[143]=530,
        [144]=531,[145]=532,[146]=533,[147]=534,[148]=535,[149]=536,[150]=537,
        [151]=538,[152]=539,[153]=540,[154]=568,[155]=569,[156]=541,[157]=574,
        [158]=542,[159]=543,[160]=544,[161]=545,[162]=566,[163]=546,[164]=567,
        [165]=547,[166]=548,[167]=549,[168]=550,[169]=551,[170]=552,[171]=553,
        [172]=554,[173]=555,[174]=573,[175]=556,[176]=557,[177]=558,[178]=575,
        [179]=559,[180]=560,[181]=561,[182]=562,[183]=563,[184]=564,[185]=565,
        [186]=211,[187]=204,[188]=209,[189]=579,[190]=580,[191]=581,[192]=582,
        [200]=1001,[202]=1002,[203]=1003,[204]=1004,[205]=1005
    };
    if (source < 0 || source >= 206 || !services[source])
        return q3_fail(error, QA_ERROR_UNSUPPORTED, (size_t)(uint32_t)source,
                        "unknown Quake Live API 10 service");
    *canonical = (int32_t)services[source] - 1; return true;
}

#undef S
#undef Z
#undef I
#undef P
