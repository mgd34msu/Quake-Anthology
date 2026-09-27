#include "qa/campaign_q1.h"
#include <string.h>

const char *qa_q1_finale_text(bool rerelease, const char *key) {
    static const struct {
        const char *key, *text;
    } entries[] = {
        {"$qc_finale_e1_shareware",
         "As the corpse of the monstrous entity\nChthon sinks back into the lava whence\nit rose, "
         "you grip the Rune of Earth\nMagic tightly. Now that you have\nconquered the Dimension of "
         "the Doomed,\nrealm of Earth Magic, you are ready to\ncomplete your task in the other "
         "three\nhaunted lands of Quake. Or are you? If\nyou don't register Quake, you'll "
         "never\nknow what awaits you in the Realm of\nBlack Magic, the Netherworld, and "
         "the\nElder World!"},
        {"$qc_finale_e1",
         "As the corpse of the monstrous entity\nChthon sinks back into the lava whence\nit rose, "
         "you grip the Rune of Earth\nMagic tightly. Now that you have\nconquered the Dimension of "
         "the Doomed,\nrealm of Earth Magic, you are ready to\ncomplete your task. A Rune of "
         "magic\npower lies at the end of each haunted\nland of Quake. Go forth, seek "
         "the\ntotality of the four Runes!"},
        {"$qc_finale_e2",
         "The Rune of Black Magic throbs evilly in\nyour hand and whispers dark thoughts\ninto "
         "your brain. You learn the inmost\nlore of the Hell-Mother; Shub-Niggurath!\nYou now know "
         "that she is behind all the\nterrible plotting which has led to so\nmuch death and "
         "horror. But she is not\ninviolate! Armed with this Rune, you\nrealize that once all four "
         "Runes are\ncombined, the gate to Shub-Niggurath's\nPit will open, and you can face "
         "the\nWitch-Goddess herself in her frightful\notherworld cathedral."},
        {"$qc_finale_e3",
         "The charred viscera of diabolic horrors\nbubble viscously as you seize the Rune\nof Hell "
         "Magic. Its heat scorches your\nhand, and its terrible secrets blight\nyour mind. "
         "Gathering the shreds of your\ncourage, you shake the devil's shackles\nfrom your soul, "
         "and become ever more\nhard and determined to destroy the\nhideous creatures whose mere "
         "existence\nthreatens the souls and psyches of all\nthe population of Earth."},
        {"$qc_finale_e4",
         "Despite the awful might of the Elder\nWorld, you have achieved the Rune of\nElder Magic, "
         "capstone of all types of\narcane wisdom. Beyond good and evil,\nbeyond life and death, "
         "the Rune\npulsates, heavy with import. Patient and\npotent, the Elder Being "
         "Shub-Niggurath\nweaves her dire plans to clear off all\nlife from the Earth, and bring "
         "her own\nfoul offspring to our world! For all the\ndwellers in these nightmare "
         "dimensions\nare her descendants! Once all Runes of\nmagic power are united, the "
         "energy\nbehind them will blast open the Gateway\nto Shub-Niggurath, and you can "
         "travel\nthere to foil the Hell-Mother's plots\nin person."},
        {"$qc_finale_all_runes",
         "Now, you have all four Runes. You sense\ntremendous invisible forces moving to\nunseal "
         "ancient barriers. Shub-Niggurath\nhad hoped to use the Runes Herself to\nclear off the "
         "Earth, but now instead,\nyou will use them to enter her home and\nconfront her as an "
         "avatar of avenging\nEarth-life. If you defeat her, you will\nbe remembered forever as "
         "the savior of\nthe planet. If she conquers, it will be\nas if you had never been born."},
        {"$qc_finale_end",
         "Congratulations and well done! You have\nbeaten the hideous Shub-Niggurath, and\nher "
         "hundreds of ugly changelings and\nmonsters. You have proven that your\nskill and your "
         "cunning are greater than\nall the powers of Quake. You are the\nmaster now. Id Software "
         "salutes you."},
        {"$qc_finale_r1",
         "Victory! The Overlord's mangled\nremains are the evidence.  His evil\nWrath army?  Cast "
         "out to wander\naimlessly throughout time.\n\nAs the Slipgate fog surrounds "
         "you,\nthoughts rage into your consciousness:\nHas Quake's oppressive reign ended?\nIs it "
         "Salvation, or Damnation, which\nwaits beyond the Vortex?\n\nAnother thought, not quite "
         "your own,\nrazors through the haze.  \"Forgiveness\ncan yet be granted; Our Master "
         "remains\nto absolve your sins against his Chosen.\nFall down upon your knees-pray "
         "for\nQuake's mercy.\""},
        {"$qc_finale_hip1",
         "Deep within the bowels of the\nResearch Facility, you discover the\npassage that the "
         "followers of Quake\nhave used to enter our world.\nThe bastards used some type "
         "of\ngigantic teleporter to overload\none of our own slipgates!  As long as\nthis portal "
         "exists, Earth will never\nbe safe from Quake's cruel minions."},
        {"$qc_finale_hip2",
         "After destroying the power generator,\nyou pass beyond the gate of Mortum's\nKeep.  A "
         "wave of nausea suddenly flows\nover you and you find yourself cast\nout into a liquid "
         "void.  You float\nlifelessly, yet aware, in a lavender\nsea of energy."},
        {"$qc_finale_hipend",
         "After the last echoes of Armagon's\ndeath yell fade away, you breathe a\nheavy sigh of "
         "relief.  With the loss\nof his magic, Armagon's fortress\nbegins to collapse.  The rift "
         "he\ncreated to send his grisly troops\nthrough time slowly closes and seals\nitself "
         "forever.  In the chaos that\nensues, a wall collapses, revealing\none remaining time "
         "portal.  With your\nchances to escape rapidly growing\nslim, you race for the "
         "portal,\nmindless of your destination.  In a\nflash of light, you find yourself\nback at "
         "Command HQ, safe and sound."},
        {"$qc_finale_hip1m4", "If you can find the source of the\nportal's power, you can shut "
                              "it\ndown--possibly forever!  With only a\nmoment's consideration "
                              "for your own\nsafety, you re-enter the dark domain,\nknowing Hell "
                              "would be a better fate\nthan experiencing the reign of Quake."},
        {"$qc_finale_hip2m5",
         "After what seems like an eternity,\nyou feel the presence of a diabolical\nintelligence. "
         " You are held helpless\nfor a moment as your mind is open to\nthat of Armagon--Quake's "
         "General and\nmaster of this realm.  Recognizing\nyou as the one who foiled his\nattempt "
         "to conquer Earth, a hellish\nhowl fills your mind and blots out\nall consciousness.  "
         "When you awake,\nyou find yourself on the shores of\nreality, but in a time and "
         "place\nunknown to you."},
        {"$qc_finale_hipend2", "Congratulations!  You are victorious!\nThe minions of Quake have "
                               "once again\nfallen before your mighty hand.\nIs this the last you "
                               "will see of\nQuake's hellions?\n\nOnly time will tell..."},
        {"$qc_finale_coop", "You have destroyed Quake's\nTemporal Teleporter. His assault\non Time "
                            "has been defeated."},
        {"$qc_finale_rogue_end",
         "\nFinally, Quake's Temporal Teleporter\nyields to your assault. A high\npitched scream "
         "emits from the\ndevastated device as stressed steel\nblasts outward to rock the "
         "cavern.\nThe machine is devoured by molten lava.\n\nThe ground shudders as reality "
         "shifts\nback to its predestined path.\n\nYou run to enter the charged time "
         "pod,\nscrambling in as the chamber closes.\nYour consciousness fades as you realize\nyou "
         "have halted Quake's plans for...\n\nThe Dissolution of Eternity."},
    };
    if (!key || rerelease)
        return key;
    for (size_t i = 0; i < sizeof(entries) / sizeof(*entries); ++i)
        if (!strcmp(key, entries[i].key))
            return entries[i].text;
    return key;
}
