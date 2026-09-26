#ifndef QA_Q3_HUFFMAN_INTERNAL_H
#define QA_Q3_HUFFMAN_INTERNAL_H

#include "qa/network_q3.h"

/* Code bits are transmitted least significant first. */
void qa_q3_huffman_code(uint8_t symbol, uint32_t *bits, unsigned *width);
uint32_t qa_q3_huffman_symbol(qa_net_reader *);

#endif
