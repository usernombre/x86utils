#ifndef __X86UTILS_H__
#define __X86UTILS_H__

#ifdef _WIN32
#    ifdef LIBRARY_EXPORTS
#        define LIBRARY_API __declspec(dllexport)
#    else
#        define LIBRARY_API __declspec(dllimport)
#    endif
#else
#    define LIBRARY_API
#endif

#include "instruction.h"

LIBRARY_API instruction_info_t * instruction_create(int mode);
LIBRARY_API void instruction_free(instruction_info_t * inst);



#include "assembler.h"

LIBRARY_API assembler_t * assembler_create(int mode);
LIBRARY_API void assembler_destroy(assembler_t * asmb);

LIBRARY_API int assembler_assemble(assembler_t * asmb, const char * assembly, uint64_t address);
LIBRARY_API int assembler_result(assembler_t * asmb, unsigned char * output, size_t * output_size);

#endif