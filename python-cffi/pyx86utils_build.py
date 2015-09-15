from distutils.util import get_platform
from cffi import FFI

ffi = FFI()

if get_platform() == "win32":
    folder = "Win32"
elif get_platform() == "win-amd64":
    folder = "x64"
else:
    assert False # Unsupported platform
    
ffi.cdef("""
typedef struct assembler {
    ...;
} assembler_t;
assembler_t * assembler_create(int mode);
void assembler_destroy(assembler_t * asmb);
int assembler_assemble(assembler_t * asmb, const char * assembly, uint64_t address);
int assembler_result(assembler_t * asmb, unsigned char * output, size_t * output_size);
int assembler_relocs(assembler_t * asmb, unsigned char * output, size_t * output_size);

enum ud_mnemonic_code;
typedef enum ud_mnemonic_code ud_mnemonic_code_t;
enum ud_type;
typedef enum ud_type ud_type_t;
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
} operand_type_t;typedef struct operand_info {
	operand_type_t type;
	uint8_t size;
	ud_type_t reg;
	uint64_t value;
	ud_type_t segment;
	ud_type_t base;
	ud_type_t index;
	uint8_t scale;
	int64_t offset;
	uint16_t ptr_segment;
	uint32_t ptr_offset;
} operand_info_t;
typedef struct instruction_info {
	ud_mnemonic_code_t opcode;
	int mode;
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


const char * opcode_string(ud_mnemonic_code_t opcode);
const char * register_string(ud_type_t reg);

instruction_info_t * instruction_create(int mode);
void instruction_destroy(instruction_info_t * inst);

int instruction_disassemble(instruction_info_t * inst, unsigned char * input_buffer, uint32_t input_size, uint64_t address);
void operand_string(operand_info_t * op, char * output, int output_size);
void instruction_prefix_string(instruction_info_t * inst, char * output, int output_size);
void instruction_string(instruction_info_t * inst, char * output, int output_size);
int instruction_assemble(instruction_info_t * inst, unsigned char * output_buffer, size_t * output_size, uint64_t address);
""")

extra_args = dict(
     include_dirs=[r'..\x86utils',r'..\libyasm-2.1.2',r'..\udis86-1.7.2'],
     library_dirs=[r'..\%s\Release' % folder],
     libraries=['x86utils', 'modules', 'libyasm', 'libudis86']
)
ffi.set_source("_pyx86utils", "#include <x86utils.h>", **extra_args)

if __name__ == "__main__":
    ffi.compile()