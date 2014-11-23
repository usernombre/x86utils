/*
 * Raw preprocessor (preforms NO preprocessing)
 *
 *  Copyright (C) 2001-2007  Peter Johnson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND OTHER CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR OTHER CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <util.h>

#include <libyasm.h>

typedef struct yasm_preproc_str {
    yasm_preproc_base preproc;   /* base structure */

    char *buf;
    char *current;
} yasm_preproc_str;

yasm_preproc_module yasm_str_LTX_preproc;

static yasm_preproc *
str_preproc_create(const char *in_filename, yasm_symtab *symtab,
                   yasm_linemap *lm, yasm_errwarns *errwarns)
{
    yasm_preproc_str *preproc_str = yasm_xmalloc(sizeof(yasm_preproc_str));
	
    preproc_str->preproc.module = &yasm_str_LTX_preproc;
    preproc_str->buf = yasm__xstrdup(in_filename);
	preproc_str->current = preproc_str->buf;

    return (yasm_preproc *)preproc_str;
}

static void
str_preproc_destroy(yasm_preproc *preproc)
{
    yasm_preproc_str *preproc_str = (yasm_preproc_str *)preproc;
    yasm_xfree(preproc_str->buf);
    yasm_xfree(preproc);
}

static char *
str_preproc_get_line(yasm_preproc *preproc)
{
    yasm_preproc_str *preproc_str = (yasm_preproc_str *)preproc;

    /* Loop to ensure entire line is read (don't want to limit line length). */
	char *line_start = preproc_str->current;
	char *line_end = strchr(line_start, '\n');
	char *buf;
	if (line_end == NULL) {
		line_end = line_start + strlen(line_start);
	} else {
		line_end += 1; /* Skip \n */
	}
	if (line_start == line_end) {
		return NULL; /* EOF */
	}
	preproc_str->current = line_end;
	buf = yasm_xmalloc(line_end - line_start + 1);
	buf[line_end - line_start] = '\0';
	memcpy(buf, line_start, line_end - line_start);

    /* Strip the line ending */
    buf[strcspn(buf, "\r\n")] = '\0';

    return buf;
}

static size_t
str_preproc_get_included_file(yasm_preproc *preproc, char *buf,
                              size_t max_size)
{
    /* no included files */
    return 0;
}

static void
str_preproc_add_include_file(yasm_preproc *preproc, const char *filename)
{
    /* no pre-include files */
}

static void
str_preproc_predefine_macro(yasm_preproc *preproc, const char *macronameval)
{
    /* no pre-defining macros */
}

static void
str_preproc_undefine_macro(yasm_preproc *preproc, const char *macroname)
{
    /* no undefining macros */
}

static void
str_preproc_define_builtin(yasm_preproc *preproc, const char *macronameval)
{
    /* no builtin defines */
}

static void
str_preproc_add_standard(yasm_preproc *preproc, const char **macros)
{
    /* no standard macros */
}


/* Define preproc structure -- see preproc.h for details */
yasm_preproc_module yasm_str_LTX_preproc = {
    "Disable preprocessing",
    "str",
    str_preproc_create,
    str_preproc_destroy,
    str_preproc_get_line,
    str_preproc_get_included_file,
    str_preproc_add_include_file,
    str_preproc_predefine_macro,
    str_preproc_undefine_macro,
    str_preproc_define_builtin,
    str_preproc_add_standard
};
