#include "equipment_held_stock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *copy(const char *text)
{
    size_t length = strlen(text) + 1;
    char *out = malloc(length);
    if (out) memcpy(out, text, length);
    return out;
}

static const qa_model_transform q1_gun = {
    .origin = {3.192499796549479f, -5.947635650634766f, 10.002536137898764f},
    .axes = {{.7190015912055969f, .6357764005661011f, -.280758261680603f},
        {-.608428418636322f, .7710461020469666f, .18789082765579224f},
        {.3359340727329254f, .03572748228907585f, .941207766532898f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q1_axe = {
    .origin = {-1.0801829099655151f, -16.939233779907227f, 3.4784603118896484f},
    .axes = {{.8529600501060486f, .34326276183128357f, .3932298421859741f},
        {-.49903303384780884f, .31537508964538574f, .8071582913398743f},
        {.15305252373218536f, -.8847085237503052f, .4403018355369568f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_male = {
    .origin = {-2.5316378672917685f, -9.27009121576945f, 4.795252025127411f},
    .axes = {{.9701912999153137f, -.16778743267059326f, -.17486034333705902f},
        {.16538193821907043f, .9858221411705017f, -.02834496460855007f},
        {.17713716626167297f, -.0014186727348715067f, .9841850996017456f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_male_classic = {
    .origin = {-2.198841094970703f, -9.209379196166992f, 5.1354217529296875f},
    .axes = {{.9739808440208435f, -.16668719053268433f, -.15354691445827484f},
        {.12157008051872253f, .9560694098472595f, -.26674318313598633f},
        {.19126416742801666f, .24113602936267853f, .9514575004577637f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_female = {
    .origin = {1.5657100677490234f, -5.886165618896484f, 1.9254425764083862f},
    .axes = {{.9919583797454834f, .036155037581920624f, -.12129008769989014f},
        {-.03602835536003113f, .9993454813957214f, .003238283796235919f},
        {.12132780998945236f, .0011576636461541057f, .992611825466156f}},
    .scale = {1, 1, 1}
};
static const qa_model_transform q2_female_classic = {
    .origin = {1.8566161394119263f, -5.973968029022217f, 2.301879405975342f},
    .axes = {{.99488365650177f, .030664639547467232f, -.09626106172800064f},
        {-.013402743265032768f, .9844618439674377f, .17508633434772491f},
        {.10013430565595627f, -.17290037870407104f, .9798359870910645f}},
    .scale = {1, 1, 1}
};

bool frontend_held_stock_q2_grip(const qa_resource *source, qa_model_transform *out,
    bool *found, qa_error *error)
{
    const qa_sha256_digest *digest = qa_resource_digest(source);
    if (!digest || !out || !found) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Stock Q2 grip requires an actual admitted model resource");
        return false;
    }
    static const char *const male[] = {
        "32e4effc4d859237a6b0fbd5ebd2c0854f4705fb3baa577dc63f7f47d1e60e44",
        "e66e9297814dadc07f949f4f5a6e2df8595e63391ca048656157c0cf7d91da17",
        "ae8330db3742a3c89e240027e5fde1805305937f3a8c9afabb6e2a81b3c0230f",
        "ee5610ad0175df970fde5d50f49e23f370a7e43e49233bb5f9e639fe226fc50b",
        "d7218a9f908d5cdf5686224bde01a4168c7660c0c634cc4ad7c4d36f4655fa46",
        "be2b96c10cd02ff367026b2fc8a5e3bac50ccf143e6a3b77c98ccf403454ea35",
        "83e538c9ca8664a1ace624bfae0aef47049ceff019f64e09baafe4fb9a3906b9",
        "d14e6475d4bbe7fb9db312c98d60b83b940cfe5b8eeb963575fce523e992f6a9",
        "f74348a1ceb113f0622337ec098ac014ab6c3c3c12d67469773795a462f27c29",
        "645ab70efa2aa6b1dbe83f822dc9abb4c1b86a67c64ddd5bacf88a68d3cf209f",
        "e71f1871813276ee8cb8849b1a83170f9bb0a016b6b7fc185599dc71eede65d8",
        "57762bbf8d747af13dca4c918bc7e2f216f410a188fafec21983ce7d00e93861",
        "fbfdb043b58abe5f84cb09b7ba008698be8c764cd4debda1c3007aa04614b97f",
        "1abd354dbe844d97761a4fad9f7e9b848df25300e39aa8e241e171488f3e42d7",
        "6aba815fe8748f6333b7ff80f896d4fc976c4de7003eb1b50dcf35d5d84354c6",
        "4a052927e34237e69391cd9fb98e868726cb19b9bcf63c24bae12108f5d3b929",
        "b504936c4c5419548833632a4daa9d3d39b368521453c67c299043a39348f2d6",
        "56d164f5046f6b83b620713d2d6384c581edf8e8e5beeecd0109d2ff2ebfd1f1",
        "d6df8f529b2fe188258e043475fff8d0f78c2a7834377155c7838a7ba4159cd6",
        "0090cbf7d7712ecc1715b83a07665adfb438cbf2d206b17b0e22ffc140d382f8",
        "fad6bbf39320d8d244b0f64aa4b0d6382a8bec2d6b4c9c616669dc867f1defbe",
        "bf2eee4f3d5ba00da87e6dbc229c1c7492de339fcb5e8a78fa9e196a89b3b9a8",
        "73f67c7af2853aed67b94ecda07c8a48bcc5f2452423439a6c0a1b56f355659b",
        "9823b9ccc3803f956ed182e77dba420b07fa439996773be65281834bea93dd1a",
        "9275720d065ed004833647d944b97edb7a6d400fe3b66bbf56cae4e32d09c478",
        "e2149301a4e8cb0d23f7615abc4372dc8e862b932cb6c66270d769344bfccb46",
        "0355570303c72b087534ab34fc1d679477935426f9fd7cac1a1e579dbdbf2def",
        "0a6f9063367e830fc5d9f9f9ea6c1420ceacac49587a7ae4cbf6acd3d93922c9",
        "9070c3608064899cd0e793b1569be3d203e9132c33d11d550d9d193fe7a89951",
        "6966853fb42a452e9d7d64165265f6f714429d497115a746dacdda51fbb6d8ce",
        "e389c40788620e9845c55deb3aff742f5a4460db2f4a93defb7e3858f45ebf74",
        "414a0ea1504c149bf046668fa634eb0303964ede042bf7420a63e9bd935520ba",
        "fea9f1a8d526072db78351f0816059be53b3253757ced527b164d7e0bd262005",
        "5ee925da0b9cc6098a3b4e14abf3f47bfb6712b7e2331a12808f5397f1904a4a",
        "0c915db364d5b054443cce87acf9819b8dd320009bba439a88b6191f11f3d7dc",
        "d8929d1cd5e6cb8ce16410bf53cdd2dfe19721105c4cff17ea445c9b487e28da",
        "467995c58f1fc5377a53549a80c756ea52acec6c619442eefbfab5d5b2b5b5d0",
        "e7005c4fedd0cebacac144da368e7b4310db0e7422af5e0a398cec484251568e",
        "5808e32c6c2d1c48344b8235973234fecbdf345e93ddef9b08607ec44c0a54b5"
    };
    static const char *const female[] = {
        "8d1684ad6391d5f93f4990ff18f9e862c5ccea33610cb6e77a80eba227fc3fd1",
        "8b494f82ec11808a6129d1914355b874031ba8c290b5d6a66000d68c55da9bbf",
        "7555e0430ff2ed7ffb6bea1ab65eba5e0e04c829166c2215c2a849db04ab3911",
        "c7c10642cd2204f6abdb117da3fb37f1a3c4273b6d3d586c93ffdd7b844c35e7",
        "fd0ac88955dc79d2cd6b9320c02452f3b6272c50b0fa8cb88f8bdd94d86da577",
        "f55926d7cae2d2f93fba88aa794e8b27abc345a39db6cd4e576a4dc95a757134",
        "f8d8b0a56ef0397b7ce5c50917dc00cb8ff161fa21aa6c8d9864483415249ad1",
        "62489b3793e6f754bccfe4638eb9a6c530c6341a13d10ffee05de84c43f606d1",
        "e0b5130f85d535d0990f169401467512b97d95eed72672df808ed94ce407c719",
        "80f1cfa4bce5066c5038f5482e630d9be11c45ac1a85e4346d3ea5e2c1888374",
        "ffe43152810fb211fcc4ed41270fd590c275e8d72dacd77a587f87b4ac64c636",
        "eca0849e8142930306f6d9521284af0133b7e578c7dc7f35e5411a3861782b66",
        "5a546188a6e3ab4e664ccac8457256c5c01903ed3c1156915e707458b9381026",
        "5f1ed6c46a9e2b1e1985916c0cae69f9373ce5c44ffaa6258401d3bffdd40527",
        "50ff1ccca45c74a5fbfce1b7750c0a44443be18ada7bf5d3da551f3d91d13689",
        "01f300b4460c74184b4e75a8fad50c6a7ad6234e2a7ac2acc4306db4632c1259",
        "ec3f99acf7c9bf7b0ce04c5eb1c0def21ac6e8ae34e03e96cdde88af17d381a2",
        "156e3e14c7a2d269934ad64779564ef866a59dd36f36ec402bf5d7d45f1ebded",
        "21539f5f8b7c9c7af91a278d7283e24cfa44402b6d6a9715cf110bac58ada0ba",
        "6ca26afe240beff2feaf66d51649215c69eefe8d1771a341ae145ce05a1009f5",
        "927d1e07e92eaff6dbd4121f510267746bc9033fff8573a1f269ca70f6d2eed8",
        "7ba6dfd3ff6eadfa629fb0932724a3454e9adcd97a220d92b88d8281a4d22709",
        "ce3cc8ca60f96db9b23b42ba63670d8e68085d7a954086983e411bbe78418473",
        "d298dcdb469b6eb8c4b3348763fb0c82081038a111392c93bdfd9668e80c1795",
        "22af6579f102f6d8dd326ff59556cf72dda6643161a3c1b7b42a8d5fa60a7bde",
        "ce1d4fa285a57ac379aa13d0f52ba9b112e2ccd94fb95eb46ac5015b1b57c461",
        "1c17590bafa6a01fe671fd5df9f092363665cc894e10474f26f1d96272d0af33",
        "685f089f4d2c843b246a68a59ba4492ef3eaf3318153ee41be99c552e031f7d4",
        "ddd7a9752a6e5fb0a9f3f2409fbffca71535f45b0b2ed07c921251efe5fc8004",
        "e23bba47f77f222d1c0684e879a92230bf96eacafe9993e7d8c6d45838bde9f5",
        "44480ded746b8116930a080359ef35b16c5b34f35c59c574988142205831fa27",
        "51ac2040388da250bfc471038ab9f20b8c05c9fb977b2d749b6113e51d36fa24",
        "ed70cd5a30fc113f934b440be06736c37ad0b82228c5f5a19a0a8c603c841689",
        "11d3f96add65da06a2143a2d7bb07976c650926909964ec2ccc1f811df50761d",
        "7a055147003a8b25079786c70e820604850c5f791147a6678821ab0ba56ed7eb",
        "c67fe7cb4f755cac86783ed7c60c83313fe37dfd2790febd7a84e9e243d72fa4",
        "0807bfb253b23170644d48be575d41515b2aa531c44fb1f33d608b60af024718",
        "1591ad8e838e3e697944215fe80710248cee783a3dd365e9f1ddd36cb7db6cbe",
        "4ada1e8d6874644cccb6f6d68a5016209a7a1b1009a1a9b417c1efb83c194075"
    };
    char hex[65]; qa_sha256_hex(digest, hex);
    *found = true;
    if (!strcmp(hex, "b1d3c78722594d99d40d650914c4f9bfed27e34d210743b99576d299e4b700c7") ||
        !strcmp(hex, "e8a4615b37d86a2fdaa6cc3abf7b12f93b6fc8755e86dc29dce8e3691c66eda7")) {
        *out = q2_male_classic; return true;
    }
    if (!strcmp(hex, "ce0c9f5e4431302be3b7abbfdbd4ecc96f3e8015ff5c59083c5162b47c2af44f")) {
        *out = q2_female_classic; return true;
    }
    for (size_t i = 0; i < sizeof(male) / sizeof(*male); ++i) if (!strcmp(hex, male[i])) {
        *out = q2_male;
        out->origin[0] = -2.5316379070281982f; out->origin[1] = -9.270092010498047f;
        out->origin[2] = 4.795251846313477f;
        return true;
    }
    for (size_t i = 0; i < sizeof(female) / sizeof(*female); ++i) if (!strcmp(hex, female[i])) {
        *out = q2_female; return true;
    }
    *found = false;
    return true;
}

static bool q1(const char *view, frontend_held_declaration *out, bool *found, qa_error *error)
{
    size_t length = strlen(view);
    if (length < 12 || strncmp(view, "progs/v_", 8) || strcmp(view + length - 4, ".mdl")) return true;
    static const uint32_t gun[] = {40,41,42,100,101,102,116,117,118,142,143,144,145,146,
        147,148,156,157,158,174,175,177,178,179,185,186,187,188,189,190,191,194,195,196,199,200,201,202};
    static const uint32_t axe[] = {9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24};
    bool ax = !strcmp(view, "progs/v_axe.mdl");
    const uint32_t *vertices = ax ? axe : gun;
    frontend_held_declaration result = {.reference_frame = ax ? 17 : 12,
        .part_digest_count = 2, .vertex_count = ax ? sizeof(axe) / sizeof(*axe) : sizeof(gun) / sizeof(*gun),
        .grip = ax ? q1_axe : q1_gun};
    result.path = copy("progs/player.mdl");
    result.part_digests = calloc(2, sizeof(*result.part_digests));
    result.vertices = malloc(result.vertex_count * sizeof(*result.vertices));
    if (!result.path || !result.part_digests || !result.vertices) {
        frontend_held_declaration_free(&result);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original Q1 held model declaration");
        return false;
    }
    memcpy(result.vertices, vertices, result.vertex_count * sizeof(*result.vertices));
    bool ok = qa_sha256_parse("sha256:10cecfe08d312ff17c63529e280b976c50018f683f541cb03b2048c7a681ebf9",
        result.part_digests, error) &&
        qa_sha256_parse("sha256:7bd9988aa264d27cea670bbf280d9101d712cf27d87dbe774ac41d363bdf01d2",
            result.part_digests + 1, error);
    if (!ok) { frontend_held_declaration_free(&result); return false; }
    *out = result; *found = true;
    return true;
}

static bool q2(const char *view, const char *item, frontend_held_declaration *out,
    bool *found, qa_error *error)
{
    static const struct { const char *item, *model; } models[] = {
        {"q2:weapon_blaster", "w_blaster"}, {"q2:weapon_shotgun", "w_shotgun"},
        {"q2:weapon_supershotgun", "w_sshotgun"}, {"q2:weapon_machinegun", "w_machinegun"},
        {"q2:weapon_chaingun", "w_chaingun"}, {"q2:ammo_grenades", "a_grenades"},
        {"q2:weapon_grenadelauncher", "w_glauncher"}, {"q2:weapon_rocketlauncher", "w_rlauncher"},
        {"q2:weapon_hyperblaster", "w_hyperblaster"}, {"q2:weapon_railgun", "w_railgun"},
        {"q2:weapon_bfg", "w_bfg"}, {"q2:ammo_trap", "a_trap"},
        {"q2:weapon_boomer", "w_ripper"}, {"q2:weapon_phalanx", "w_phalanx"},
        {"q2:ammo_tesla", "a_tesla"}, {"q2:weapon_proxlauncher", "w_plauncher"},
        {"q2:weapon_chainfist", "w_chainfist"}, {"q2:weapon_disintegrator", "w_disrupt"},
        {"q2:weapon_etf_rifle", "w_etfrifle"}, {"q2:weapon_plasmabeam", "w_plasma"}
    };
    const char *model = NULL;
    if (item) for (size_t i = 0; i < sizeof(models) / sizeof(*models); ++i)
        if (!strcmp(item, models[i].item)) { model = models[i].model; break; }
    if (!item) {
        static const char *const views[] = {
            "models/weapons/v_blast/tris.md2", "models/weapons/v_shotg/tris.md2",
            "models/weapons/v_shotg2/tris.md2", "models/weapons/v_machn/tris.md2",
            "models/weapons/v_chain/tris.md2", "models/weapons/v_handgr/tris.md2",
            "models/weapons/v_launch/tris.md2", "models/weapons/v_rocket/tris.md2",
            "models/weapons/v_hyperb/tris.md2", "models/weapons/v_rail/tris.md2",
            "models/weapons/v_bfg/tris.md2", "models/weapons/v_trap/tris.md2",
            "models/weapons/v_boomer/tris.md2", "models/weapons/v_shotx/tris.md2",
            "models/weapons/v_tesla/tris.md2", "models/weapons/v_launch/tris.md2",
            "models/weapons/v_chainf/tris.md2", "models/weapons/v_dist/tris.md2",
            "models/weapons/v_etf_rifle/tris.md2", "models/weapons/v_beamer/tris.md2"
        };
        for (size_t i = 0; i < sizeof(views) / sizeof(*views); ++i)
            if (!strcmp(view, views[i])) { model = models[i].model; break; }
    }
    if (!model && !strcmp(view, "models/weapons/grapple/tris.md2")) model = "w_grapple";
    if (!model) return true;
    char path[96];
    snprintf(path, sizeof(path), "players/male/%s.md2", model);
    frontend_held_declaration result = {.grip = q2_male};
    result.path = copy(path); result.fallback = copy("players/male/weapon.md2");
    if (!result.path || !result.fallback) {
        frontend_held_declaration_free(&result);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining original Q2 held model declaration");
        return false;
    }
    *out = result; *found = true;
    return true;
}

bool frontend_held_stock(qa_game_family family, const char *view, const char *item,
    frontend_held_declaration *out, bool *found, qa_error *error)
{
    if (!view || !out || !found) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Original held declaration requires actual source identity");
        return false;
    }
    *found = false;
    if (family == QA_GAME_Q1) return q1(view, out, found, error);
    if (family == QA_GAME_Q2) return q2(view, item, out, found, error);
    return true;
}
