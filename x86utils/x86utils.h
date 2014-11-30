#ifndef __X86UTILS_H__
#define __X86UTILS_H__

#ifdef _WIN32
#	 ifdef X86UTILS_DLL_EXPORT
#		 define X86UTILS_API __declspec(dllexport)
#	 elseif X86UTILS_DLL_IMPORT
#	     define X86UTILS_API __declspec(dllimport)
#	 endif
#endif

#ifndef X86UTILS_API
#define X86UTILS_API
#endif

#include "instruction.h"

X86UTILS_API const char * opcode_string(ud_mnemonic_code_t opcode);
X86UTILS_API const char * register_string(ud_type_t reg);

X86UTILS_API instruction_info_t * instruction_create(int mode);
X86UTILS_API void instruction_init(instruction_info_t * inst, int mode);
X86UTILS_API void instruction_destroy(instruction_info_t * inst);

X86UTILS_API int instruction_disassemble(instruction_info_t * inst, unsigned char * input_buffer, uint32_t input_size, uint64_t address);
X86UTILS_API void operand_string(operand_info_t * op, char * output, int output_size);
X86UTILS_API void instruction_prefix_string(instruction_info_t * inst, char * output, int output_size);
X86UTILS_API void instruction_string(instruction_info_t * inst, char * output, int output_size);
X86UTILS_API int instruction_assemble(instruction_info_t * inst, unsigned char * output_buffer, size_t * output_size, uint64_t address);



#include "assembler.h"

X86UTILS_API assembler_t * assembler_create(int mode);
X86UTILS_API void assembler_destroy(assembler_t * asmb);

X86UTILS_API int assembler_assemble(assembler_t * asmb, const char * assembly, uint64_t address);
X86UTILS_API int assembler_result(assembler_t * asmb, unsigned char * output, size_t * output_size);

#endif