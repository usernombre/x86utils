#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include <udis86.h>
#include <libudis86\decode.h>
#include <libudis86\syn.h>

#include "instruction.h"
#include "x86utils.h"


const char * opcode_string(ud_mnemonic_code_t mnemonic) {
	if ((uint32_t)mnemonic < UD_MAX_MNEMONIC_CODE) {
		return ud_lookup_mnemonic(mnemonic);
	}
	return "";
}

const char * register_string(ud_type_t reg) {
	if (reg >= UD_R_AL && reg <= UD_R_RIP) {
		return ud_reg_tab[reg - UD_R_AL];
	}
	return "";
}

typedef struct decode_info {
	unsigned char * buffer;
	unsigned char * buffer_start;
	unsigned char * buffer_end;
	uint64_t address;
	instruction_info_t * instruction;
} decode_info_t;

static int read_input(ud_t * ud_obj) {
	decode_info_t * input = (decode_info_t *)ud_get_user_opaque_data(ud_obj);
	if (input->buffer == input->buffer_end) {
		return -1;
	}
	return *(input->buffer++);
}

static void translate_operand(ud_t * u, ud_operand_t * o, operand_info_t * operand) {
    const uint64_t trunc_mask = 0xffffffffffffffffull >> (64 - u->opr_mode);
	operand->size = o->size;
	switch (o->type) {
	case UD_OP_REG:
		operand->type = OP_REG;
		operand->reg = o->base;
		break;
	case UD_OP_MEM:
		operand->type = OP_MEM;
		switch (operand->size) {
		case 8:
			operand->type = OP_MEM_8; break;
		case 16:
			operand->type = OP_MEM_16; break;
		case 32:
			operand->type = OP_MEM_32; break;
		case 64:
			operand->type = OP_MEM_64; break;
		}
		if (u->pfx_seg) {
			operand->segment = (ud_type_t)(u->pfx_seg);
		}
		if (o->base) {
			operand->base = o->base;
		}
		if (o->index) {
			operand->index = o->index;
		}
		if (o->scale) {
			operand->scale = o->scale;
		}
		if (o->offset) {
			if (o->base == UD_NONE && o->index == UD_NONE) {
				switch (o->offset) {
					case 16: operand->offset = o->lval.uword;  break;
					case 32: operand->offset = o->lval.udword; break;
					case 64: operand->offset = o->lval.uqword; break;
				}
			} else {
				switch (o->offset) {
					case 8 : operand->offset = o->lval.sbyte;  break;
					case 16: operand->offset = o->lval.sword;  break;
					case 32: operand->offset = o->lval.sdword; break;
				}
			}
		}
		break;
	case UD_OP_IMM:
		operand->type = OP_IMM;
		if (o == &u->operand[0]) {
			// For the case push ..., in this case we are interested in the size of the push
			operand->size = u->opr_mode;
		}
		switch (operand->size) {
		case 8:
			operand->type = OP_IMM_8;
			break;
		case 16:
			operand->type = OP_IMM_16;
			break;
		case 32:
			operand->type = OP_IMM_32;
			break;
		case 64:
			operand->type = OP_IMM_64;
			break;
		}
		if (o->_oprcode == OP_sI && o->size != u->opr_mode) {
			if (o->size == 8) {
				operand->value = (int64_t)o->lval.sbyte;
			} else {
				operand->value = (int64_t)o->lval.sdword;
			}
			if (u->opr_mode < 64) {
				operand->value &= ((1ull << u->opr_mode) - 1ull);
			}
		} else {
			switch (o->size) {
			case 8 : operand->value = o->lval.ubyte;  break;
			case 16: operand->value = o->lval.uword;  break;
			case 32: operand->value = o->lval.udword; break;
			case 64: operand->value = o->lval.uqword; break;
			}
		}
		break;
	case UD_OP_JIMM:
		operand->type = OP_JIMM;
		switch (o->size) {
		case 8:
			operand->value = (u->pc + o->lval.sbyte) & trunc_mask; break;
		case 16:
			operand->value = (u->pc + o->lval.sword) & trunc_mask; break;
		case 32:
			operand->value = (u->pc + o->lval.sdword) & trunc_mask; break;
		}
		break;
	case UD_OP_PTR:
		operand->type = OP_PTR;
		switch (o->size) {
		case 32:
			operand->ptr_segment = o->lval.ptr.seg;
			operand->ptr_offset = o->lval.ptr.off & 0xFFFF;
		case 48:
			operand->ptr_segment = o->lval.ptr.seg;
			operand->ptr_offset = o->lval.ptr.off;
		}
		break;
	case UD_OP_CONST:
		// Const is only 1 or 3, so let's treat it as imm8.
		operand->type = OP_IMM_8;
		operand->value = o->lval.udword;
		operand->size = 8;
		break;
	}
}

static void translate_instruction(ud_t * u)
{
	decode_info_t * decode_info = (decode_info_t *)ud_get_user_opaque_data(u);
	instruction_info_t * inst = decode_info->instruction;
	inst->operation_size_prefix = 0;
	inst->address_size_prefix = 0;

	if (!P_OSO(u->itab_entry->prefix) && u->pfx_opr) {
		switch (u->dis_mode) {
		case 16: inst->operation_size_prefix = 32; break;
		case 32:
		case 64: inst->operation_size_prefix = 16; break;
		}
	}

	if (!P_ASO(u->itab_entry->prefix) && u->pfx_adr) {
		switch (u->dis_mode) {
		case 16:
		case 64: inst->address_size_prefix = 32;
		case 32: inst->address_size_prefix = 16; break;
		}
	}
	
	inst->lock = u->pfx_lock;
	inst->rep = u->pfx_rep;
	inst->repe = u->pfx_repe;
	inst->repne = u->pfx_repne;

	inst->opcode = u->mnemonic;
	inst->size = (uint8_t)(u->pc - decode_info->address);

	inst->far = u->br_far;
	inst->near = u->br_near;
	
#ifdef OPTIMIZE_MEMORY_SIZE
	inst->operands[0].has_operand_size = 0;
	inst->operands[1].has_operand_size = 0;
	inst->operands[2].has_operand_size = 0;
#endif

	if (u->pfx_seg && u->operand[0].type != UD_OP_MEM && u->operand[1].type != UD_OP_MEM ) {
	    inst->segment = (ud_type_t)(u->pfx_seg);
	}

	if (u->operand[0].type != UD_NONE) {
#ifdef OPTIMIZE_MEMORY_SIZE
		if (u->operand[0].type == UD_OP_MEM) {
			if (u->operand[1].type == UD_OP_IMM   ||
				u->operand[1].type == UD_OP_CONST ||
				u->operand[1].type == UD_NONE     ||
				(u->operand[0].size != u->operand[1].size && 
				u->operand[1].type != UD_OP_REG)) {
					inst->operands[0].has_operand_size = 1;
			} else if (u->operand[1].type == UD_OP_REG &&
				u->operand[1].base == UD_R_CL) {
					switch (u->mnemonic) {
					case UD_Ircl:
					case UD_Irol:
					case UD_Iror:
					case UD_Ircr:
					case UD_Ishl:
					case UD_Ishr:
					case UD_Isar:
						inst->operands[0].has_operand_size = 1;
						break;
					default: break;
					}
			}
		}
#endif
		translate_operand(u, &u->operand[0], &inst->operands[0]);
	} else {
		inst->operands[0].type = NONE;
	}

	if (u->operand[1].type != UD_NONE) {
#ifdef OPTIMIZE_MEMORY_SIZE
		if (u->operand[1].type == UD_OP_MEM &&
			u->operand[0].size != u->operand[1].size && 
			!ud_opr_is_sreg(&u->operand[0])) {
			inst->operands[1].has_operand_size = 1;
		}
#endif
		translate_operand(u, &u->operand[1], &inst->operands[1]);
	} else {
		inst->operands[1].type = NONE;
	}

	if (u->operand[2].type != UD_NONE) {
		translate_operand(u, &u->operand[2], &inst->operands[2]);
	} else {
		inst->operands[2].type = NONE;
	}

}

instruction_info_t * instruction_create(int mode) {
	instruction_info_t * result = (instruction_info_t *)malloc(sizeof(instruction_info_t));
	if (result == NULL) {
		return NULL;
	}
	instruction_init(result, mode);
	return result;
}

void instruction_init(instruction_info_t * inst, int mode) {
	memset(inst, 0, sizeof(*inst));
	inst->opcode = UD_Iinvalid;
	inst->mode = mode;
	inst->operands[0].type = NONE;
	inst->operands[1].type = NONE;
	inst->operands[2].type = NONE;
}

void instruction_destroy(instruction_info_t * inst) {
	free(inst);
}

int instruction_disassemble(instruction_info_t * inst, unsigned char * input_buffer, uint32_t input_size, uint64_t address) {
    ud_t ud_obj;
	decode_info_t decode_info;
	decode_info.buffer = input_buffer;
	decode_info.buffer_start = input_buffer;
	decode_info.buffer_end = input_buffer + input_size;
	decode_info.address = address;
	decode_info.instruction = inst;

    ud_init(&ud_obj);
    ud_set_user_opaque_data(&ud_obj, (void *)&decode_info);
	ud_set_syntax(&ud_obj, translate_instruction);
    ud_set_mode(&ud_obj, inst->mode);
	ud_set_vendor(&ud_obj, UD_VENDOR_AMD);
	ud_set_pc(&ud_obj, address);
	ud_set_input_hook(&ud_obj, read_input);
	ud_disassemble(&ud_obj);

	if (inst->opcode == UD_Iinvalid) {
		return -1;
	}
	return 0;
}

void operand_string(operand_info_t * op, char * output, int output_size) {
	
	switch (op->type & ~0xf) {
	case OP_REG:
		strncat(output, register_string(op->reg), output_size - 1);
		break;
	case OP_MEM:
#ifdef OPTIMIZE_MEMORY_SIZE
		if (op->has_operand_size) {
#endif
			switch (op->size) {
			case 8:		strncat(output, "byte ", output_size - 1); break;
			case 16:	strncat(output, "word ", output_size - 1); break;
			case 32:	strncat(output, "dword ", output_size - 1); break;
			case 64:	strncat(output, "qword ", output_size - 1); break;
			case 40:	strncat(output, "pword ", output_size - 1); break;
			case 80:	strncat(output, "tword ", output_size - 1); break;
			}
#ifdef OPTIMIZE_MEMORY_SIZE
		}
#endif
			
		if (op->segment) {
			strncat(output, register_string(op->segment), output_size - 1); 
			strncat(output, ":", output_size - 1); 
		}

		strncat(output, "[", output_size - 1); 
		if (op->base) {
			strncat(output, register_string(op->base), output_size - 1); 
		}
		if (op->index) {
			if (op->base) {
				strncat(output, "+", output_size - 1); 
			}
			strncat(output, register_string(op->index), output_size - 1); 
			if (op->scale) {
				_snprintf(&output[strlen(output)], output_size - strlen(output) - 1, "*%d", op->scale);
			}
		}
		if (op->offset) {
			int64_t value = op->offset;
			if (op->base || op->index) {
				if (value >= 0) {
					strncat(output, "+", output_size - 1); 
				}
			}
			if (value < 0) {
				_snprintf(&output[strlen(output)], output_size - strlen(output) - 1, "-0x%llx", -value);
			} else {
				_snprintf(&output[strlen(output)], output_size - strlen(output) - 1, "0x%llx", value);
			}
		}
		strncat(output, "]", output_size - 1); 
		break;
	case OP_IMM:
	case OP_JIMM:
		_snprintf(&output[strlen(output)], output_size - strlen(output) - 1, "0x%llx", op->value);
		break;
	case OP_PTR:
		_snprintf(&output[strlen(output)], output_size - strlen(output) - 1, "0x%x:0x%x", op->ptr_segment, op->ptr_offset);
		break;
	}
}
void instruction_prefix_string(instruction_info_t * inst, char * output, int output_size) {
	switch (inst->operation_size_prefix) {
	case 16:
		strncat(output, "o16 ", output_size - 1); break;
	case 32:
		strncat(output, "o32 ", output_size - 1); break;
	}

	switch (inst->address_size_prefix) {
	case 16:
		strncat(output, "a16 ", output_size - 1); break;
	case 32:
		strncat(output, "a32 ", output_size - 1); break;
	}

	if (inst->segment) {
		strncat(output, register_string(inst->segment), output_size - 1);
		strncat(output, " ", output_size - 1);
	}
	
	if (inst->lock) {
		strncat(output, "lock ", output_size - 1);
	} else if (inst->rep) {
		strncat(output, "rep ", output_size - 1);
	} else if (inst->repe) {
		strncat(output, "repe ", output_size - 1);
	} else if (inst->repne) {
		strncat(output, "repne ", output_size - 1);
	}
}

void instruction_string(instruction_info_t * inst, char * output, int output_size) {
	instruction_prefix_string(inst, output, output_size);

	strncat(output, opcode_string(inst->opcode), output_size - 1);
	
	if (inst->operands[0].type != OP_NONE) {
		strncat(output, " ", output_size - 1);
		if (inst->operands[0].type == OP_IMM_16) {
			// For push
			strncat(output, "word ", output_size - 1);
		}
#ifdef OPTIMIZE_MEMORY_SIZE
		if ((inst->operands[0].type & 0xF0) == OP_MEM) {
			if ((inst->operands[1].type & 0xF0) == OP_IMM   ||
				inst->operands[1].type == NONE     ||
				(inst->operands[0].size != inst->operands[1].size && 
				inst->operands[1].type != OP_REG)) {
					inst->operands[0].has_operand_size = 1;
			} else if (inst->operands[1].type == UD_OP_REG &&
				inst->operands[1].reg == UD_R_CL) {
					switch (inst->opcode) {
					case UD_Ircl:
					case UD_Irol:
					case UD_Iror:
					case UD_Ircr:
					case UD_Ishl:
					case UD_Ishr:
					case UD_Isar:
						inst->operands[0].has_operand_size = 1;
						break;
					default: break;
					}
			}
		}
#endif
		operand_string(&inst->operands[0], output, output_size);
	}
	if (inst->operands[1].type != OP_NONE) {
		strncat(output, ", ", output_size - 1);
#ifdef OPTIMIZE_MEMORY_SIZE
		if ((inst->operands[1].type & 0xF0) == OP_MEM &&
			inst->operands[0].size != inst->operands[1].size && 
			!(inst->operands[0].type == UD_OP_REG && inst->operands[0].reg >= UD_R_ES && inst->operands[0].reg <= UD_R_GS)) {
			inst->operands[1].has_operand_size = 1;
		}
#endif
		operand_string(&inst->operands[1], output, output_size);
	}
	if (inst->operands[2].type != OP_NONE) {
		strncat(output, ", ", output_size - 1);
		operand_string(&inst->operands[2], output, output_size);
	}
}

int instruction_assemble(instruction_info_t * inst, unsigned char * output_buffer, size_t * output_size, uint64_t address) {
	char instruction[100] = {0};
	assembler_t * assembler;
	if (inst->opcode == UD_Iinvalid) {
		return -1;
	}
	instruction_string(inst, instruction, sizeof(instruction));
	assembler = assembler_create(inst->mode);
	if (assembler_assemble(assembler, instruction, address)) {
		assembler_destroy(assembler);
		return -1;
	}
	if (assembler_result(assembler, output_buffer, output_size)) {
		assembler_destroy(assembler);
		return -1;
	}
	assembler_destroy(assembler);
	return 0;
}

/*
void main() {
//	printf("%d", sizeof(struct opcode_info));
	unsigned char code[] = {0xE8, 0x55, 0x11, 0x00, 0x00};
	instruction_info_t inst;
	instruction_init(&inst);
	instruction_decode(&inst, code, sizeof(code), 0);
	yasm_load_preproc("str");

	getchar();
}*/
/*
void main() {
	char instr[100] = {0};
	assembler_t * asmb;
	instruction_info_t * inst;
	asmb = assembler_create(64);
	if (assembler_assemble(asmb, "jmp 0x545443", 0x550)) {
		printf("Error");
	}
	inst = instruction_create(64);
	instruction_disassemble(inst, asmb->output.buffer, asmb->output.size, 0x550);
	instruction_string(inst, instr, sizeof(instr));
	assembler_destroy(asmb);
	getchar();
}*/