#ifndef QA_APPLICATION_GUEST_Q3_REFERENCE_H
#define QA_APPLICATION_GUEST_Q3_REFERENCE_H
#include "qa/qvm.h"

typedef enum application_q3_reference {
    Q3_REFERENCE_BASE_GAME,
    Q3_REFERENCE_THREEWAVE_GAME,
    Q3_REFERENCE_LRCTF_GAME,
    Q3_REFERENCE_THREEWAVE_CGAME,
    Q3_REFERENCE_LRCTF_CGAME,
} application_q3_reference;
typedef struct q3_reference_instruction { uint32_t at; uint8_t opcode; int32_t operand; } q3_reference_instruction;

static inline bool application_q3_reference_image(const qa_qvm_image *image, application_q3_reference reference)
{
    qa_qvm_image_info info = qa_qvm_image_describe(image);
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    const q3_reference_instruction *expected = NULL;
    size_t length = 0;
    switch (reference) {
    case Q3_REFERENCE_BASE_GAME: {
        static const q3_reference_instruction anchors[] = {
            {65953, 3, 48},
            {103594, 3, 20},
            {103220, 3, 24},
            {103974, 3, 68},
            {129805, 3, 20},
            {129114, 3, 44},
            {104006, 8, 65953},
            {104007, 5, 0},
            {104106, 8, 103594},
            {104107, 5, 0},
            {104090, 8, 103220},
            {104091, 5, 0},
            {104339, 8, 129114},
            {104340, 5, 0},
        };
        if (info.source_bytes != 469796 || info.instruction_count != 146672 ||
            info.code_length != 436836 || info.data_length != 8684 ||
            info.literal_length != 24244 || info.bss_length != 1420188) return false;
        expected = anchors; length = sizeof(anchors) / sizeof(*anchors); break;
    }
    case Q3_REFERENCE_THREEWAVE_GAME: {
        static const q3_reference_instruction anchors[] = {
            {33648, 3, 44},
            {33226, 3, 48},
            {35535, 3, 28},
            {34707, 3, 88},
            {32561, 3, 112},
            {27646, 3, 16},
            {217003, 3, 124},
            {118339, 3, 132},
            {127849, 3, 24},
            {128463, 3, 24},
            {158347, 3, 596},
            {139391, 3, 164},
            {120292, 3, 32},
            {120383, 3, 16},
            {132015, 3, 1700},
            {168574, 3, 100},
            {211210, 3, 20},
            {20482, 3, 92},
            {210519, 3, 44},
            {167408, 3, 36},
            {166848, 3, 24},
            {166897, 3, 32},
            {210993, 3, 44},
            {217563, 3, 44},
            {215035, 3, 24},
            {215169, 3, 44},
            {177663, 3, 152},
            {16897, 3, 20},
            {29990, 3, 92},
            {162405, 3, 152},
            {197341, 3, 36},
            {168696, 8, 20482},
            {168697, 5, 0},
            {168872, 8, 20482},
            {168873, 5, 0},
            {169403, 8, 210519},
            {169404, 5, 0},
            {169093, 8, 167408},
            {169094, 5, 0},
            {169077, 8, 166848},
            {169078, 5, 0},
            {169061, 8, 166897},
            {169062, 5, 0},
        };
        if (info.source_bytes != 717404 || info.instruction_count != 217747 ||
            info.code_length != 654604 || info.data_length != 18660 ||
            info.literal_length != 44108 || info.bss_length != 1619496) return false;
        expected = anchors; length = sizeof(anchors) / sizeof(*anchors); break;
    }
    case Q3_REFERENCE_LRCTF_GAME: {
        static const q3_reference_instruction anchors[] = {
            {22369, 3, 28},
            {21620, 3, 88},
            {114858, 3, 24},
            {114917, 3, 16},
            {125027, 3, 1496},
            {178797, 3, 44},
            {179014, 3, 20},
            {183076, 3, 76},
            {183171, 3, 16},
            {151164, 3, 124},
            {183577, 3, 40},
            {5362, 3, 20},
            {15874, 3, 64},
            {184569, 3, 32},
            {140358, 3, 160},
            {169306, 3, 24},
        };
        if (info.source_bytes != 627620 || info.instruction_count != 194956 ||
            info.code_length != 582148 || info.data_length != 10776 ||
            info.literal_length != 34664 || info.bss_length != 1518352) return false;
        expected = anchors; length = sizeof(anchors) / sizeof(*anchors); break;
    }
    case Q3_REFERENCE_THREEWAVE_CGAME: {
        static const q3_reference_instruction anchors[] = {
            {36612, 3, 164},
            {62937, 3, 540},
            {36804, 3, 428},
            {38561, 3, 244},
            {39712, 3, 164},
            {39826, 3, 164},
            {39870, 3, 164},
            {39534, 3, 176},
            {40520, 3, 160},
            {61925, 3, 16},
            {106815, 3, 92},
            {105885, 3, 208},
            {26123, 3, 36},
            {103468, 3, 808},
            {10083, 3, 128},
            {13793, 3, 144},
            {63313, 8, 61925},
            {63314, 5, 0},
            {63545, 8, 61925},
            {63546, 5, 0},
            {63771, 8, 61925},
            {63772, 5, 0},
        };
        if (info.source_bytes != 461312 || info.instruction_count != 137810 ||
            info.code_length != 411608 || info.data_length != 15312 ||
            info.literal_length != 34360 || info.bss_length != 6744388) return false;
        expected = anchors; length = sizeof(anchors) / sizeof(*anchors); break;
    }
    case Q3_REFERENCE_LRCTF_CGAME: {
        static const q3_reference_instruction anchors[] = {
            {45608, 3, 164},
            {81284, 3, 856},
            {46022, 3, 348},
            {47069, 3, 196},
            {47502, 3, 316},
            {47708, 3, 164},
            {47822, 3, 340},
            {48149, 3, 164},
            {47699, 3, 12},
            {45882, 3, 160},
            {45800, 3, 160},
            {80824, 3, 12},
            {108261, 3, 76},
            {107738, 3, 208},
            {22881, 3, 36},
            {106742, 3, 540},
            {24534, 3, 96},
            {24736, 3, 76},
            {82084, 8, 80824},
            {82085, 5, 0},
            {82483, 8, 80824},
            {82484, 5, 0},
            {82780, 8, 80824},
            {82781, 5, 0},
        };
        if (info.source_bytes != 553312 || info.instruction_count != 167630 ||
            info.code_length != 498756 || info.data_length != 18132 ||
            info.literal_length != 36392 || info.bss_length != 4318480) return false;
        expected = anchors; length = sizeof(anchors) / sizeof(*anchors); break;
    }
    default: return false;
    }
    for (size_t i = 0; i < length; ++i) {
        const q3_reference_instruction *row = expected + i;
        if (row->at >= count || code[row->at].opcode != row->opcode || code[row->at].operand != row->operand) return false;
    }
    return true;
}
#endif
