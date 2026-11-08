#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <libyasm.h>
#include <libyasm/bitvect.h>

#include "assembler.h"
#include "x86utils.h"

#ifndef _MSC_VER
#define _snprintf snprintf
#endif


static int BitVector_initialized = 0;
static int libyasm_initialized = 0;
static int assembler_loaded = 0;
static const yasm_arch_module * cur_arch_module = NULL;
static const yasm_parser_module * cur_parser_module = NULL;
static const yasm_preproc_module * cur_preproc_module = NULL;
static char *objfmt_keyword = NULL;
static const yasm_objfmt_module * cur_objfmt_module = NULL;
static const yasm_dbgfmt_module * cur_dbgfmt_module = NULL;

static int load_assembler() {
	if (!libyasm_initialized) {
		if (!BitVector_initialized) {
			/* Initialize BitVector (needed for intnum/floatnum). */
			if (BitVector_Boot() != ErrCode_Ok) {
				return -1;
			}
			BitVector_initialized = 1;
		}
		yasm_errwarn_initialize();

		/* Initialize intnum and floatnum */
		yasm_intnum_initialize();
		yasm_floatnum_initialize();
		libyasm_initialized = 1;
	}

    if (!cur_objfmt_module) {
        if (!objfmt_keyword)
            objfmt_keyword = yasm__xstrdup("mem");
        cur_objfmt_module = (const yasm_objfmt_module *)yasm_load_objfmt(objfmt_keyword);
        if (!cur_objfmt_module) {
			return -1;
        }
    }

    if (!cur_arch_module) {
        cur_arch_module = (yasm_arch_module *)yasm_load_arch("x86");
        if (!cur_arch_module) {
			return -1;
        }
    }

    if (!cur_parser_module) {
        cur_parser_module = (yasm_parser_module *)yasm_load_parser("tasm");
        if (!cur_parser_module) {
			return -1;
        }
    }
	
    if (!cur_preproc_module) {
        cur_preproc_module = (yasm_preproc_module *)yasm_load_preproc("str");
        if (!cur_preproc_module) {
			return -1;
        }
    }
    if (!cur_dbgfmt_module) {
        cur_dbgfmt_module = (yasm_dbgfmt_module *)yasm_load_dbgfmt("null");
        if (!cur_dbgfmt_module) {
			return -1;
        }
    }
	assembler_loaded = 1;
	return 0;
}

static void unload_assembler() {
	if (objfmt_keyword) {
		yasm_xfree(objfmt_keyword);
		objfmt_keyword = NULL;
	}
	if (libyasm_initialized) {
		yasm_errwarn_cleanup();
		yasm_intnum_cleanup();
		yasm_floatnum_cleanup();
		libyasm_initialized = false;
	}
	if (BitVector_initialized) {
		BitVector_Shutdown();
		BitVector_initialized = false;
	}
}

assembler_t * assembler_create(int mode) {
	assembler_t * result = (assembler_t *)malloc(sizeof(assembler_t));
	if (result == NULL) {
		return NULL;
	}
	memset(result, 0, sizeof(*result));
	result->bits = mode;
	return result;
}

void assembler_destroy(assembler_t * asmb) {
	if (asmb->output.output.buffer) {
		yasm_xfree(asmb->output.output.buffer);
	}
	if (asmb->output.relocs.buffer) {
		yasm_xfree(asmb->output.relocs.buffer);
	}
	free(asmb);
}

int assembler_assemble(assembler_t * asmb, const char * assembly, uint64_t address) {
	int err = 0;
	yasm_arch * arch = NULL;
	yasm_object * object = NULL;
	yasm_arch_create_error arch_error;
	yasm_preproc * preproc = NULL;
	yasm_linemap * linemap = NULL;
	char * machine_name = NULL;
	yasm_errwarns *errwarns = NULL;
	char * assembly_line = NULL;
	
	if (asmb->output.output.buffer) {
		yasm_xfree(asmb->output.output.buffer);
		asmb->output.output.buffer = NULL;
		asmb->output.output.size = 0;
	}
	if (asmb->output.relocs.buffer) {
		yasm_xfree(asmb->output.relocs.buffer);
		asmb->output.relocs.buffer = NULL;
		asmb->output.relocs.size = 0;
	}
	asmb->ready = 0;

	if (load_assembler()) {
		goto error;
	}

	if (address) {
		assembly_line = yasm_xmalloc(strlen(assembly) + 26); // Max length of org ...
		if (_snprintf(assembly_line, strlen(assembly) + 25, "[org 0x%llx]\n%s", address, assembly) < 0) {
			goto error;
		}
	} else {
		assembly_line = yasm__xstrdup(assembly);
	}

	if (!assembler_loaded) {
		goto error;
	}

	errwarns = yasm_errwarns_create();
	if (!errwarns) {
		goto error;
	}

	// TODO: make warnings thread safe
	// Enable all warnings
	yasm_warn_enable(YASM_WARN_GENERAL);
	yasm_warn_enable(YASM_WARN_UNREC_CHAR);
	yasm_warn_enable(YASM_WARN_PREPROC);
	yasm_warn_enable(YASM_WARN_ORPHAN_LABEL);
	yasm_warn_enable(YASM_WARN_UNINIT_CONTENTS);
	yasm_warn_enable(YASM_WARN_SIZE_OVERRIDE);
	yasm_warn_enable(YASM_WARN_IMPLICIT_SIZE_OVERRIDE);

    linemap = yasm_linemap_create();
	if (!linemap) {
		goto error;
	}
    yasm_linemap_set(linemap, NULL, 0, 1, 1);

	switch (asmb->bits) {
	case 32:
		machine_name = yasm__xstrdup(cur_arch_module->default_machine_keyword);
		break;
	case 64:
		machine_name = yasm__xstrdup("amd64");
		break;
	default:
		goto error;
	}
	
    arch = yasm_arch_create(cur_arch_module, machine_name, cur_parser_module->keyword, &arch_error);
    if (!arch) {
		goto error;
	}
	object = yasm_object_create("dummy", "dummy", arch, cur_objfmt_module, cur_dbgfmt_module);
	if (!object) {
		goto error;
	}

	preproc = yasm_preproc_create(cur_preproc_module, assembly_line, object->symtab, linemap, errwarns);
	if (!preproc) {
		goto error;
	}
	
	yasm_arch_set_var(arch, "mode_bits", asmb->bits);
    //yasm_arch_set_var(cur_arch, "force_strict", 0);
	cur_parser_module->do_parse(object, preproc, 0, linemap, errwarns);
    if (yasm_errwarns_num_errors(errwarns, 1) > 0) {
		goto error;
	}

    yasm_object_finalize(object, errwarns);
    if (yasm_errwarns_num_errors(errwarns, 1) > 0) {
		goto error;
	}

    yasm_object_optimize(object, errwarns, address);
    if (yasm_errwarns_num_errors(errwarns, 1) > 0) {
		goto error;
	}
	
    yasm_objfmt_output(object, (FILE*)&(asmb->output), 0, errwarns);
    if (yasm_errwarns_num_errors(errwarns, 1) > 0) {
		goto error;
	}
	asmb->ready = 1;

	goto clean;
error:
	err = -1;
clean:	
    if (preproc) {
        yasm_preproc_destroy(preproc);
	}
    if (object) {
        yasm_object_destroy(object);
	} else if (arch) { // arch is released by object
        yasm_arch_destroy(arch);
	}
	if (assembly_line) {
		yasm_xfree(assembly_line);
	}
	if (machine_name) {
		yasm_xfree(machine_name);
	}
	if (linemap) {
		yasm_linemap_destroy(linemap);
	}
	if (errwarns) {
		yasm_errwarns_destroy(errwarns);
	}
	return err;
}

static int buffer_result(buffer * buffer, unsigned char * output, size_t * output_size) {
	if (output == NULL) {
		// Just update the size
		*output_size = buffer->size;
		return 0;
	}
	if (*output_size < buffer->size) {
		return -1;
	}
	memcpy(output, buffer->buffer, buffer->size);
	*output_size = buffer->size;
	return 0;
}

int assembler_result(assembler_t * asmb, unsigned char * output, size_t * output_size) {
	if (!asmb->ready) {
		return -1;
	}
	return buffer_result(&asmb->output.output, output, output_size);
}

int assembler_relocs(assembler_t * asmb, unsigned char * output, size_t * output_size) {
	if (!asmb->ready) {
		return -1;
	}
	return buffer_result(&asmb->output.relocs, output, output_size);
}