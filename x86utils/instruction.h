#ifndef __INSTRUCTION_H__
#define __INSTRUCTION_H__

#include <udis86.h>
#include <stdint.h>

typedef enum operand_type
{
	NONE = 0x0,
	OP_REG = 0x10,
	OP_MEM = 0x20,
	OP_MEM_8 = 0x21,
	OP_MEM_16 = 0x22,
	OP_MEM_32 = 0x23,
	OP_MEM_64 = 0x24,
	OP_IMM = 0x30,
	OP_IMM_8 = 0x31,
	OP_IMM_16 = 0x32,
	OP_IMM_32 = 0x33,
	OP_IMM_64 = 0x34,
	OP_PTR = 0x40,
	OP_JIMM = 0x50,
} operand_type_t;

/*
enum REGISTER_TYPES
{
    RT_REG8 = 0x10,
    RT_REG16 = 0x20,
    RT_REG32 = 0x30,
    RT_SEG = 0x40,
    RT_ST = 0x50,
    RT_CR = 0x60,
    RT_DR = 0x70,
    RT_MM = 0x80,
    RT_XMM = 0x90,
    RT_REG8S = 0xa4,
    RT_REG64 = 0xb0,
    RT_RIP = 0xc0,
};*/

/*
enum REGISTER
{
	R_NONE = 0x0,

    R_AL = 0x10,
    R_CL = 0x11,
    R_DL = 0x12,
    R_BL = 0x13,
    R_AH = 0x14,
    R_CH = 0x15,
    R_DH = 0x16,
    R_BH = 0x17,
    R_R8B = 0x18,
    R_R9B = 0x19,
    R_R10B = 0x1a,
    R_R11B = 0x1b,
    R_R12B = 0x1c,
    R_R13B = 0x1d,
    R_R14B = 0x1e,
    R_R15B = 0x1f,

    R_AX = 0x20,
    R_CX = 0x21,
    R_DX = 0x22,
    R_BX = 0x23,
    R_SP = 0x24,
    R_BP = 0x25,
    R_SI = 0x26,
    R_DI = 0x27,
    R_R8W = 0x28,
    R_R9W = 0x29,
    R_R10W = 0x2a,
    R_R11W = 0x2b,
    R_R12W = 0x2c,
    R_R13W = 0x2d,
    R_R14W = 0x2e,
    R_R15W = 0x2f,

    R_EAX = 0x30,
    R_ECX = 0x31,
    R_EDX = 0x32,
    R_EBX = 0x33,
    R_ESP = 0x34,
    R_EBP = 0x35,
    R_ESI = 0x36,
    R_EDI = 0x37,
    R_R8D = 0x38,
    R_R9D = 0x39,
    R_R10D = 0x3a,
    R_R11D = 0x3b,
    R_R12D = 0x3c,
    R_R13D = 0x3d,
    R_R14D = 0x3e,
    R_R15D = 0x3f,

    R_ES = 0x40,
    R_CS = 0x41,
    R_SS = 0x42,
    R_DS = 0x43,
    R_FS = 0x44,
    R_GS = 0x45,
	
    R_ST0 = 0x50,
    R_ST1 = 0x51,
    R_ST2 = 0x52,
    R_ST3 = 0x53,
    R_ST4 = 0x54,
    R_ST5 = 0x55,
    R_ST6 = 0x56,
    R_ST7 = 0x57,
	
    R_CR0 = 0x60,
    R_CR1 = 0x61,
    R_CR2 = 0x62,
    R_CR3 = 0x63,
    R_CR4 = 0x64,
    R_CR5 = 0x65,
    R_CR6 = 0x66,
    R_CR7 = 0x67,
    R_CR8 = 0x68,
    R_CR9 = 0x69,
    R_CR10 = 0x6a,
    R_CR11 = 0x6b,
    R_CR12 = 0x6c,
    R_CR13 = 0x6d,
    R_CR14 = 0x6e,
    R_CR15 = 0x6f,
	
    R_DR0 = 0x70,
    R_DR1 = 0x71,
    R_DR2 = 0x72,
    R_DR3 = 0x73,
    R_DR4 = 0x74,
    R_DR5 = 0x75,
    R_DR6 = 0x76,
    R_DR7 = 0x77,
    R_DR8 = 0x78,
    R_DR9 = 0x79,
    R_DR10 = 0x7a,
    R_DR11 = 0x7b,
    R_DR12 = 0x7c,
    R_DR13 = 0x7d,
    R_DR14 = 0x7e,
    R_DR15 = 0x7f,
	
    R_MM0 = 0x80,
    R_MM1 = 0x81,
    R_MM2 = 0x82,
    R_MM3 = 0x83,
    R_MM4 = 0x84,
    R_MM5 = 0x85,
    R_MM6 = 0x86,
    R_MM7 = 0x87,
	
    R_XMM0 = 0x90,
    R_XMM1 = 0x91,
    R_XMM2 = 0x92,
    R_XMM3 = 0x93,
    R_XMM4 = 0x94,
    R_XMM5 = 0x95,
    R_XMM6 = 0x96,
    R_XMM7 = 0x97,
    R_XMM8 = 0x98,
    R_XMM9 = 0x99,
    R_XMM10 = 0x9a,
    R_XMM11 = 0x9b,
    R_XMM12 = 0x9c,
    R_XMM13 = 0x9d,
    R_XMM14 = 0x9e,
    R_XMM15 = 0x9f,

    //R__AH = 0xa0,
    //R__CH = 0xa1,
    //R__DH = 0xa2,
    //R__BH = 0xa3,
    R_SPL = 0xa4,
    R_BPL = 0xa5,
    R_SIL = 0xa6,
    R_DIL = 0xa7,
    //R__R8B = 0xa8,
    //R__R9B = 0xa9,
    //R__R10B = 0xaa,
    //R__R11B = 0xab,
    //R__R12B = 0xac,
    //R__R13B = 0xad,
    //R__R14B = 0xae,
    //R__R15B = 0xaf,

    R_RAX = 0xb0,
    R_RCX = 0xb1,
    R_RDX = 0xb2,
    R_RBX = 0xb3,
    R_RSP = 0xb4,
    R_RBP = 0xb5,
    R_RSI = 0xb6,
    R_RDI = 0xb7,
    R_R8 = 0xb8,
    R_R9 = 0xb9,
    R_R10 = 0xba,
    R_R11 = 0xbb,
    R_R12 = 0xbc,
    R_R13 = 0xbd,
    R_R14 = 0xbe,
    R_R15 = 0xbf,

    R_RIP = 0xc0,
};
*/

enum OPERAND_TYPE_INFO
{
  NOP_NONE = 0x0,
  NOP_REG8 = 0x1,
  NOP_REG16 = 0x2,
  NOP_REG32 = 0x3,
  NOP_REG64 = 0x4,
  NOP_REG_SP = 0x5,
  NOP_REG_ESP = 0x6,
  NOP_REG_SEG = 0x7,
  NOP_IMM_8 = 0x8,
  NOP_IMM_16 = 0x9,
  NOP_IMM_32 = 0xA,
  NOP_IMM_64 = 0xB,
  NOP_MEM_8 = 0xC,
  NOP_MEM_16 = 0xD,
  NOP_MEM_32 = 0xE,
  NOP_MEM_64 = 0xF,
  NOP_MEM_8_SP = 0x13,
  NOP_MEM_16_SP = 0x14,
  NOP_MEM_32_SP = 0x15,
  NOP_MEM_64_SP = 0x16,
  NOP_REG_RSP = 0x17,
};

typedef struct operand_info {
	operand_type_t type;
	uint8_t size;
#ifdef OPTIMIZE_MEMORY_SIZE
	uint8_t has_operand_size;
#endif

	// for OP_REG
	ud_type_t reg;

	// for OP_IMM/OP_CONST/OP_JIMM
	uint64_t value;

	// for OP_MEM
	ud_type_t segment;
	ud_type_t base;
	ud_type_t index;
	uint8_t scale;
	int64_t offset;

	// for OP_PTR
	uint16_t ptr_segment;
	uint32_t ptr_offset;
} operand_info_t;

typedef struct instruction_info {
	ud_mnemonic_code_t opcode;
	int mode; // 32/64 bits
	uint8_t size;
	uint8_t operation_size_prefix;
	uint8_t address_size_prefix;
	uint8_t lock;
	uint8_t rep;
	uint8_t repe;
	uint8_t repne;
	uint8_t near;
	uint8_t far;
	ud_type_t segment;
	operand_info_t operands[3];
} instruction_info_t;

#endif