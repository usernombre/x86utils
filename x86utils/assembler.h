#ifndef __ASSEMBLER_H__
#define __ASSEMBLER_H__

typedef struct output_buffer {
	unsigned char *buffer;
	unsigned long size;
} output_buffer;

typedef struct assembler {
	output_buffer output;
	int bits;
	int ready;
} assembler_t;

#endif