#ifndef __ASSEMBLER_H__
#define __ASSEMBLER_H__

typedef struct _buffer {
	unsigned char *buffer;
	unsigned long size;
} buffer;

typedef struct output_info {
	buffer output;
	buffer relocs;
} output_info;

typedef struct assembler {
	output_info output;
	int bits;
	int ready;
} assembler_t;

#endif