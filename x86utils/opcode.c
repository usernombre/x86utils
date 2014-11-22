#include "opcode.h"
#include <stdio.h>
#include <udis86.h>

void main() {
//	printf("%d", sizeof(struct opcode_info));
	getchar();
}


int decode_instruction(unsigned char * input_buffer, int length, char * output) {
    ud_t ud_obj;

    ud_init(&ud_obj);
    ud_set_input_file(&ud_obj, stdin);
    ud_set_mode(&ud_obj, 64);
    ud_set_syntax(&ud_obj, UD_SYN_INTEL);

}