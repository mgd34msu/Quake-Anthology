#include "qa/bot_perception.h"

uint32_t qa_bot_random_word(qa_bot_random *random)
{
    uint32_t x=random->state; x^=x<<13; x^=x>>17; x^=x<<5; random->state=x; return x;
}
void qa_bot_random_seed(qa_bot_random *random,uint32_t seed)
{
    random->state=seed==0 ? UINT32_C(0x1a2b3c4d) : seed;
    for (unsigned i=0;i<8;++i) (void)qa_bot_random_word(random);
}
double qa_bot_random_unit(qa_bot_random *random) { return (double)qa_bot_random_word(random)/4294967296.0; }
uint32_t qa_bot_random_index(qa_bot_random *random,uint32_t count)
{
    if (count==0) return 0;
    return (uint32_t)((uint64_t)qa_bot_random_word(random)*count>>32);
}
bool qa_bot_random_chance(qa_bot_random *random,float percent)
{
    if (percent<=0) return false;
    if (percent>=100) return true;
    return qa_bot_random_unit(random)*100<percent;
}
bool qa_bot_random_restore(qa_bot_random *random,uint32_t state,qa_error *e)
{
    if (random==NULL || state==0) { qa_error_set(e,QA_ERROR_ARGUMENT,0,"Bot random checkpoint requires a nonzero state"); return false; }
    random->state=state; return true;
}
