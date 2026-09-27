#include "internal.h"
#include <stdio.h>

typedef struct stock_product {
    const char *key, *title, *campaign, *directory, *base, *witness;
    qa_game_family family;
    qa_product_edition edition;
    unsigned pak_count;
} stock_product;
#define Q1(key,title,campaign,directory,base,edition,paks,witness) \
    {key,title,campaign,directory,base,witness,QA_GAME_Q1,edition,paks}
#define Q2(key,title,campaign,directory,base,edition,witness) \
    {key,title,campaign,directory,base,witness,QA_GAME_Q2,edition,1}
#define Q3(key,title,campaign,base,edition) \
    {key,title,campaign,"q3a/" campaign,base,NULL,QA_GAME_Q3,edition,1}
static const stock_product stock[] = {
    Q1("q1-demo-id1", "Quake Shareware", "id1", "q1/id1", NULL, QA_EDITION_DEMO, 1, "maps/e1m1.bsp"),
    Q1("q1-classic-id1", "Quake", "id1", "q1/id1", NULL, QA_EDITION_CLASSIC, 2, NULL),
    Q1("q1-classic-hipnotic", "Scourge of Armagon", "hipnotic", "q1/hipnotic", "q1-classic-id1", QA_EDITION_CLASSIC, 1, NULL),
    Q1("q1-classic-rogue", "Dissolution of Eternity", "rogue", "q1/rogue", "q1-classic-id1", QA_EDITION_CLASSIC, 1, NULL),
    Q1("q1-classic-ctf", "Threewave Capture the Flag", "ctf", "q1/ctf", "q1-classic-id1", QA_EDITION_CLASSIC, 2, "maps/ctfstart.bsp"),
    Q1("q1-rerelease-id1", "Quake", "id1", "q1/rerelease/id1", NULL, QA_EDITION_RERELEASE, 1, NULL),
    Q1("q1-rerelease-hipnotic", "Scourge of Armagon", "hipnotic", "q1/rerelease/hipnotic", "q1-rerelease-id1", QA_EDITION_RERELEASE, 1, NULL),
    Q1("q1-rerelease-rogue", "Dissolution of Eternity", "rogue", "q1/rerelease/rogue", "q1-rerelease-id1", QA_EDITION_RERELEASE, 1, NULL),
    Q1("q1-rerelease-dopa", "Dimension of the Past", "dopa", "q1/rerelease/dopa", "q1-rerelease-id1", QA_EDITION_RERELEASE, 1, NULL),
    Q1("q1-rerelease-mg1", "Dimension of the Machine", "mg1", "q1/rerelease/mg1", "q1-rerelease-id1", QA_EDITION_RERELEASE, 1, NULL),
    Q1("q1-rerelease-mg3", "Dawn of the Machine", "mg3", "q1/rerelease/mg3", "q1-rerelease-id1", QA_EDITION_RERELEASE, 1, NULL),
    Q1("q1-rerelease-ctf", "Capture the Flag", "ctf", "q1/rerelease/ctf", "q1-rerelease-id1", QA_EDITION_RERELEASE, 1, NULL),
    Q1("q1-quakeworld", "QuakeWorld", "id1", "q1/qw", "q1-classic-id1", QA_EDITION_QUAKEWORLD, 0, NULL),
    Q1("q1-rerelease-quake64", "Quake 64", "quake64", "q1/rerelease/q64", "q1-rerelease-id1", QA_EDITION_RERELEASE, 0, "maps/start.bsp"),
    Q2("q2-classic-baseq2", "Quake II", "baseq2", "q2/baseq2", NULL, QA_EDITION_CLASSIC, "maps/base1.bsp"),
    Q2("q2-classic-xatrix", "The Reckoning", "xatrix", "q2/xatrix", "q2-classic-baseq2", QA_EDITION_CLASSIC, "maps/xswamp.bsp"),
    Q2("q2-classic-rogue", "Ground Zero", "rogue", "q2/rogue", "q2-classic-baseq2", QA_EDITION_CLASSIC, "maps/rmine1.bsp"),
    Q2("q2-classic-ctf", "Capture the Flag", "ctf", "q2/ctf", "q2-classic-baseq2", QA_EDITION_CLASSIC, "maps/q2ctf1.bsp"),
    Q2("q2-classic-lmctf", "Loki's Minions CTF", "lmctf", "q2/lmctf", "q2-classic-baseq2", QA_EDITION_CLASSIC, "maps/lmctf09.bsp"),
    Q2("q2-rerelease-baseq2", "Quake II", "baseq2", "q2/rerelease/baseq2", NULL, QA_EDITION_RERELEASE, "maps/base1.bsp"),
    Q2("q2-rerelease-xatrix", "The Reckoning", "xatrix", "q2/rerelease/baseq2", "q2-rerelease-baseq2", QA_EDITION_RERELEASE, "maps/xswamp.bsp"),
    Q2("q2-rerelease-rogue", "Ground Zero", "rogue", "q2/rerelease/baseq2", "q2-rerelease-baseq2", QA_EDITION_RERELEASE, "maps/rmine1.bsp"),
    Q2("q2-rerelease-ctf", "Capture the Flag", "ctf", "q2/rerelease/baseq2", "q2-rerelease-baseq2", QA_EDITION_RERELEASE, "maps/q2ctf1.bsp"),
    Q2("q2-rerelease-mg2", "Call of the Machine", "mg2", "q2/rerelease/baseq2", "q2-rerelease-baseq2", QA_EDITION_RERELEASE, "maps/mguhub.bsp"),
    Q2("q2-rerelease-n64", "Quake II 64", "n64", "q2/rerelease/baseq2", "q2-rerelease-baseq2", QA_EDITION_RERELEASE, "maps/q64/rtest.bsp"),
    Q3("q3-baseq3", "Quake III Arena", "baseq3", NULL, QA_EDITION_CLASSIC),
    Q3("q3-missionpack", "Team Arena", "missionpack", "q3-baseq3", QA_EDITION_CLASSIC),
    Q3("q3-demota", "Quake III restricted demo content", "demota", NULL, QA_EDITION_DEMO)
};
#undef Q1
#undef Q2
#undef Q3

bool catalog_stock(qa_catalog *catalog, qa_error *error)
{
    static const char *families[] = { "q1", "q2", "q3" };
    static const char *editions[] = { "classic", "rerelease", "quakeworld", "demo" };
    for (size_t i = 0; i < sizeof(stock) / sizeof(stock[0]); ++i) {
        const stock_product *s = &stock[i];
        char identity[160];
        int n = snprintf(identity, sizeof(identity), "%s:%s:%s:installed", families[s->family], editions[s->edition], s->campaign);
        if (n < 0 || (size_t)n >= sizeof(identity)) return false;
        const qa_product *base = s->base ? qa_catalog_find(catalog, s->base) : NULL;
        qa_product view = { .key = s->key, .identity = catalog_string(catalog, identity, error),
            .title = s->title, .campaign = s->campaign, .directory = s->directory,
            .base = base ? base->id : 0, .family = s->family, .edition = s->edition,
            .builtin = true, .program_kind = QA_PROGRAM_BUILTIN };
        catalog_product *p;
        if (!view.identity || !catalog_add_product(catalog, &view, &p, error)) return false;
        p->view.program_product = p->view.id;
        p->witness = s->witness;
        for (unsigned pak = 0; pak < s->pak_count; ++pak) {
            char path[192];
            n = snprintf(path, sizeof(path), "%s/pak%u.%s", s->directory, pak, s->family == QA_GAME_Q3 ? "pk3" : "pak");
            if (n < 0 || (size_t)n >= sizeof(path)) return false;
            const char *required = catalog_string(catalog, path, error);
            if (!required) return false;
            p->required[p->required_count++] = required;
        }
    }
    return true;
}
