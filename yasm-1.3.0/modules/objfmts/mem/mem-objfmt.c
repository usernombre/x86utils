/*
 * Flat-format binary object format
 *
 *  Copyright (C) 2002-2007  Peter Johnson
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

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <libyasm.h>


#define REGULAR_OUTBUF_SIZE     1024

typedef struct mem_section_data {
    int bss;                    /* aka nobits */

    /* User-provided alignment */
    yasm_intnum *align, *valign;

    /* User-provided starts */
    /*@null@*/ /*@owned@*/ yasm_expr *start, *vstart;

    /* User-provided follows */
    /*@null@*/ /*@owned@*/ char *follows, *vfollows;

    /* Calculated (final) starts, used only during output() */
    /*@null@*/ /*@owned@*/ yasm_intnum *istart, *ivstart;

    /* Calculated (final) length, used only during output() */
    /*@null@*/ /*@owned@*/ yasm_intnum *length;
} mem_section_data;

typedef struct yasm_objfmt_mem {
    yasm_objfmt_base objfmt;            /* base structure */

    /*@null@*/ /*@only@*/ yasm_expr *org;
} yasm_objfmt_mem;

/* symrec data is used only for the special symbols section<sectname>.start,
 * section<sectname>.vstart, and section<sectname>.length
 */
typedef struct mem_symrec_data {
    yasm_section *section;          /* referenced section */
    enum mem_ssym {
        SSYM_START,
        SSYM_VSTART,
        SSYM_LENGTH
    } which;
} mem_symrec_data;

static void mem_section_data_destroy(/*@only@*/ void *d);
static void mem_section_data_print(void *data, FILE *f, int indent_level);

static const yasm_assoc_data_callback mem_section_data_cb = {
    mem_section_data_destroy,
    mem_section_data_print
};

static void mem_symrec_data_destroy(/*@only@*/ void *d);
static void mem_symrec_data_print(void *data, FILE *f, int indent_level);

static const yasm_assoc_data_callback mem_symrec_data_cb = {
    mem_symrec_data_destroy,
    mem_symrec_data_print
};

yasm_objfmt_module yasm_mem_LTX_objfmt;


static yasm_objfmt *
mem_objfmt_create(yasm_object *object)
{
    yasm_objfmt_mem *objfmt_mem = yasm_xmalloc(sizeof(yasm_objfmt_mem));
    objfmt_mem->objfmt.module = &yasm_mem_LTX_objfmt;

    objfmt_mem->org = NULL;

    return (yasm_objfmt *)objfmt_mem;
}

typedef TAILQ_HEAD(mem_group_head, mem_group) mem_groups;

typedef struct mem_group {
    TAILQ_ENTRY(mem_group) link;
    yasm_section *section;
    mem_section_data *bsd;

    /* Groups that (in parallel) logically come immediately after this
     * group's section.
     */
    mem_groups follow_groups;
} mem_group;

/* Recursive function to find group containing named section. */
static mem_group *
find_group_by_name(mem_groups *groups, const char *name)
{
    mem_group *group, *found;
    TAILQ_FOREACH(group, groups, link) {
        if (strcmp(yasm_section_get_name(group->section), name) == 0)
            return group;
        /* Recurse to loop through follow groups */
        found = find_group_by_name(&group->follow_groups, name);
        if (found)
            return found;
    }
    return NULL;
}

/* Recursive function to find group.  Returns NULL if not found. */
static mem_group *
find_group_by_section(mem_groups *groups, yasm_section *section)
{
    mem_group *group, *found;
    TAILQ_FOREACH(group, groups, link) {
        if (group->section == section)
            return group;
        /* Recurse to loop through follow groups */
        found = find_group_by_section(&group->follow_groups, section);
        if (found)
            return found;
    }
    return NULL;
}

#if 0
/* Debugging function */
static void
print_groups(const mem_groups *groups, int indent_level)
{
    mem_group *group;
    TAILQ_FOREACH(group, groups, link) {
        printf("%*sSection `%s':\n", indent_level, "",
               yasm_section_get_name(group->section));
        mem_section_data_print(group->bsd, stdout, indent_level+1);
        if (!TAILQ_EMPTY(&group->follow_groups)) {
            printf("%*sFollowing groups:\n", indent_level, "");
            print_groups(&group->follow_groups, indent_level+1);
        }
    }
}
#endif

static void
mem_group_destroy(/*@only@*/ mem_group *group)
{
    mem_group *follow, *group_temp;
    TAILQ_FOREACH_SAFE(follow, &group->follow_groups, link, group_temp)
        mem_group_destroy(follow);
    yasm_xfree(group);
}

typedef struct buffer {
	unsigned char *buffer;
	unsigned long size;
} buffer;

typedef struct output_info {
	buffer output;
	buffer relocs;
} output_info;

typedef struct mem_objfmt_output_info {
    yasm_object *object;
    yasm_errwarns *errwarns;
    /*@only@*/ output_info *output;
    /*@only@*/ unsigned char *buf;
    /*@observer@*/ const yasm_section *sect;
	unsigned long write_position;

    yasm_intnum *origin;
    yasm_intnum *tmp_intn;      /* temporary working intnum */

    mem_groups lma_groups, vma_groups;
} mem_objfmt_output_info;

static int
mem_objfmt_check_sym(yasm_symrec *sym, /*@null@*/ void *d)
{
    /*@null@*/ mem_objfmt_output_info *info = (mem_objfmt_output_info *)d;
    yasm_sym_vis vis = yasm_symrec_get_visibility(sym);
    assert(info != NULL);

    /* Don't check internally-generated symbols.  Only internally generated
     * symbols have symrec data, so simply check for its presence.
     */
    if (yasm_symrec_get_data(sym, &mem_symrec_data_cb))
        return 0;

    if (vis & YASM_SYM_EXTERN) {
        yasm_warn_set(YASM_WARN_GENERAL,
            N_("memory object format does not support extern variables"));
        yasm_errwarn_propagate(info->errwarns, yasm_symrec_get_decl_line(sym));
    } else if (vis & YASM_SYM_GLOBAL) {
        yasm_warn_set(YASM_WARN_GENERAL,
            N_("memory object format does not support global variables"));
        yasm_errwarn_propagate(info->errwarns, yasm_symrec_get_decl_line(sym));
    } else if (vis & YASM_SYM_COMMON) {
        yasm_error_set(YASM_ERROR_TYPE,
            N_("memory object format does not support common variables"));
        yasm_errwarn_propagate(info->errwarns, yasm_symrec_get_decl_line(sym));
    }
    return 0;
}

static int
mem_lma_create_group(yasm_section *sect, /*@null@*/ void *d)
{
    mem_objfmt_output_info *info = (mem_objfmt_output_info *)d;
    mem_section_data *bsd = yasm_section_get_data(sect, &mem_section_data_cb);
    unsigned long align = yasm_section_get_align(sect);
    mem_group *group;

    assert(info != NULL);
    assert(bsd != NULL);

    group = yasm_xmalloc(sizeof(mem_group));
    group->section = sect;
    group->bsd = bsd;
    TAILQ_INIT(&group->follow_groups);

    /* Determine section alignment as necessary. */
    if (!bsd->align)
        bsd->align = yasm_intnum_create_uint(align > 1 ? align : 1);
    else {
        yasm_intnum *align_intn = yasm_intnum_create_uint(align);
        if (yasm_intnum_compare(align_intn, bsd->align) > 0) {
            yasm_warn_set(YASM_WARN_GENERAL,
                N_("section `%s' internal align of %lu is greater than `%s' of %lu; using `%s'"),
                yasm_section_get_name(sect),
                yasm_intnum_get_uint(align_intn),
                N_("align"),
                yasm_intnum_get_uint(bsd->align),
                N_("align"));
            yasm_errwarn_propagate(info->errwarns, 0);
        }
        yasm_intnum_destroy(align_intn);
    }

    /* Calculate section integer start. */
    if (bsd->start) {
        bsd->istart = yasm_expr_get_intnum(&bsd->start, 0);
        if (!bsd->istart) {
            yasm_error_set(YASM_ERROR_TOO_COMPLEX,
                           N_("start expression is too complex"));
            yasm_errwarn_propagate(info->errwarns, bsd->start->line);
            return 1;
        } else
            bsd->istart = yasm_intnum_copy(bsd->istart);
    } else
        bsd->istart = NULL;

    /* Calculate section integer vstart. */
    if (bsd->vstart) {
        bsd->ivstart = yasm_expr_get_intnum(&bsd->vstart, 0);
        if (!bsd->ivstart) {
            yasm_error_set(YASM_ERROR_TOO_COMPLEX,
                           N_("vstart expression is too complex"));
            yasm_errwarn_propagate(info->errwarns, bsd->vstart->line);
            return 1;
        } else
            bsd->ivstart = yasm_intnum_copy(bsd->ivstart);
    } else
        bsd->ivstart = NULL;

    /* Calculate section integer length. */
    bsd->length = yasm_calc_bc_dist(yasm_section_bcs_first(sect),
                                    yasm_section_bcs_last(sect));

    TAILQ_INSERT_TAIL(&info->lma_groups, group, link);
    return 0;
}

static int
mem_vma_create_group(yasm_section *sect, /*@null@*/ void *d)
{
    mem_objfmt_output_info *info = (mem_objfmt_output_info *)d;
    mem_section_data *bsd = yasm_section_get_data(sect, &mem_section_data_cb);
    mem_group *group;

    assert(info != NULL);
    assert(bsd != NULL);

    group = yasm_xmalloc(sizeof(mem_group));
    group->section = sect;
    group->bsd = bsd;
    TAILQ_INIT(&group->follow_groups);

    TAILQ_INSERT_TAIL(&info->vma_groups, group, link);
    return 0;
}

/* Calculates new start address based on alignment constraint.
 * Start is modified (rounded up) to the closest aligned value greater than
 * what was passed in.
 * Align must be a power of 2.
 */
static void
mem_objfmt_align(yasm_intnum *start, const yasm_intnum *align)
{
    /* Because alignment is always a power of two, we can use some bit
     * trickery to do this easily.
     */
    yasm_intnum *align_intn =
        yasm_intnum_create_uint(yasm_intnum_get_uint(align)-1);
    yasm_intnum_calc(align_intn, YASM_EXPR_AND, start);
    if (!yasm_intnum_is_zero(align_intn)) {
        /* start = (start & ~(align-1)) + align; */
        yasm_intnum_set_uint(align_intn, yasm_intnum_get_uint(align)-1);
        yasm_intnum_calc(align_intn, YASM_EXPR_NOT, NULL);
        yasm_intnum_calc(align_intn, YASM_EXPR_AND, start);
        yasm_intnum_set(start, align);
        yasm_intnum_calc(start, YASM_EXPR_ADD, align_intn);
    }
    yasm_intnum_destroy(align_intn);
}

/* Recursive function to assign start addresses.
 * Updates start, last, and vdelta parameters as it goes along.
 * The tmp parameter is just a working intnum so one doesn't have to be
 * locally allocated for this purpose.
 */
static void
group_assign_start_recurse(mem_group *group, yasm_intnum *start,
                           yasm_intnum *last, yasm_intnum *vdelta,
                           yasm_intnum *tmp, yasm_errwarns *errwarns)
{
    mem_group *follow_group;

    /* Determine LMA */
    if (group->bsd->istart) {
        yasm_intnum_set(group->bsd->istart, start);
        if (group->bsd->align) {
            mem_objfmt_align(group->bsd->istart, group->bsd->align);
            if (yasm_intnum_compare(start, group->bsd->istart) != 0) {
                yasm_warn_set(YASM_WARN_GENERAL,
                    N_("start inconsistent with align; using aligned value"));
                yasm_errwarn_propagate(errwarns, group->bsd->start->line);
            }
        }
    } else {
        group->bsd->istart = yasm_intnum_copy(start);
        if (group->bsd->align != 0)
            mem_objfmt_align(group->bsd->istart, group->bsd->align);
    }

    /* Determine VMA if either just valign specified or if no v* specified */
    if (!group->bsd->vstart) {
        if (!group->bsd->vfollows && !group->bsd->valign) {
            /* No v* specified, set VMA=LMA+vdelta. */
            group->bsd->ivstart = yasm_intnum_copy(group->bsd->istart);
            yasm_intnum_calc(group->bsd->ivstart, YASM_EXPR_ADD, vdelta);
        } else if (!group->bsd->vfollows) {
            /* Just valign specified: set VMA=LMA+vdelta, align VMA, then add
             * delta between unaligned and aligned to vdelta parameter.
             */
            group->bsd->ivstart = yasm_intnum_copy(group->bsd->istart);
            yasm_intnum_calc(group->bsd->ivstart, YASM_EXPR_ADD, vdelta);
            yasm_intnum_set(tmp, group->bsd->ivstart);
            mem_objfmt_align(group->bsd->ivstart, group->bsd->valign);
            yasm_intnum_calc(vdelta, YASM_EXPR_ADD, group->bsd->ivstart);
            yasm_intnum_calc(vdelta, YASM_EXPR_SUB, tmp);
        }
    }

    /* Find the maximum end value */
    yasm_intnum_set(tmp, group->bsd->istart);
    yasm_intnum_calc(tmp, YASM_EXPR_ADD, group->bsd->length);
    if (yasm_intnum_compare(tmp, last) > 0)     /* tmp > last */
        yasm_intnum_set(last, tmp);

    /* Recurse for each following group. */
    TAILQ_FOREACH(follow_group, &group->follow_groups, link) {
        /* Following sections have to follow this one,
         * so add length to start.
         */
        yasm_intnum_set(start, group->bsd->istart);
        yasm_intnum_calc(start, YASM_EXPR_ADD, group->bsd->length);

        group_assign_start_recurse(follow_group, start, last, vdelta, tmp,
                                   errwarns);
    }
}

/* Recursive function to assign start addresses.
 * Updates start parameter as it goes along.
 * The tmp parameter is just a working intnum so one doesn't have to be
 * locally allocated for this purpose.
 */
static void
group_assign_vstart_recurse(mem_group *group, yasm_intnum *start,
                            yasm_errwarns *errwarns)
{
    mem_group *follow_group;

    /* Determine VMA section alignment as necessary.
     * Default to LMA alignment if not specified.
     */
    if (!group->bsd->valign)
        group->bsd->valign = yasm_intnum_copy(group->bsd->align);
    else {
        unsigned long align = yasm_section_get_align(group->section);
        yasm_intnum *align_intn = yasm_intnum_create_uint(align);
        if (yasm_intnum_compare(align_intn, group->bsd->valign) > 0) {
            yasm_warn_set(YASM_WARN_GENERAL,
                N_("section `%s' internal align of %lu is greater than `%s' of %lu; using `%s'"),
                yasm_section_get_name(group->section),
                yasm_intnum_get_uint(align_intn),
                N_("valign"),
                yasm_intnum_get_uint(group->bsd->valign),
                N_("valign"));
            yasm_errwarn_propagate(errwarns, 0);
        }
        yasm_intnum_destroy(align_intn);
    }

    /* Determine VMA */
    if (group->bsd->ivstart) {
        yasm_intnum_set(group->bsd->ivstart, start);
        if (group->bsd->valign) {
            mem_objfmt_align(group->bsd->ivstart, group->bsd->valign);
            if (yasm_intnum_compare(start, group->bsd->ivstart) != 0) {
                yasm_error_set(YASM_ERROR_VALUE,
                               N_("vstart inconsistent with valign"));
                yasm_errwarn_propagate(errwarns, group->bsd->vstart->line);
            }
        }
    } else {
        group->bsd->ivstart = yasm_intnum_copy(start);
        if (group->bsd->valign)
            mem_objfmt_align(group->bsd->ivstart, group->bsd->valign);
    }

    /* Recurse for each following group. */
    TAILQ_FOREACH(follow_group, &group->follow_groups, link) {
        /* Following sections have to follow this one,
         * so add length to start.
         */
        yasm_intnum_set(start, group->bsd->ivstart);
        yasm_intnum_calc(start, YASM_EXPR_ADD, group->bsd->length);

        group_assign_vstart_recurse(follow_group, start, errwarns);
    }
}

static /*@null@*/ const yasm_intnum *
get_ssym_value(yasm_symrec *sym)
{
    mem_symrec_data *bsymd = yasm_symrec_get_data(sym, &mem_symrec_data_cb);
    mem_section_data *bsd;

    if (!bsymd)
        return NULL;

    bsd = yasm_section_get_data(bsymd->section, &mem_section_data_cb);
    assert(bsd != NULL);

    switch (bsymd->which) {
        case SSYM_START: return bsd->istart;
        case SSYM_VSTART: return bsd->ivstart;
        case SSYM_LENGTH: return bsd->length;
    }
    return NULL;
}

static /*@only@*/ yasm_expr *
mem_objfmt_expr_xform(/*@returned@*/ /*@only@*/ yasm_expr *e,
                      /*@unused@*/ /*@null@*/ void *d)
{
    int i;
    for (i=0; i<e->numterms; i++) {
        /*@dependent@*/ yasm_section *sect;
        /*@dependent@*/ /*@null@*/ yasm_bytecode *precbc;
        /*@null@*/ yasm_intnum *dist;
        /*@null@*/ const yasm_intnum *ssymval;

        /* Transform symrecs or precbcs that reference sections into
         * vstart + intnum(dist).
         */
        if (((e->terms[i].type == YASM_EXPR_SYM &&
             yasm_symrec_get_label(e->terms[i].data.sym, &precbc)) ||
            (e->terms[i].type == YASM_EXPR_PRECBC &&
             (precbc = e->terms[i].data.precbc))) &&
            (sect = yasm_bc_get_section(precbc)) &&
            (dist = yasm_calc_bc_dist(yasm_section_bcs_first(sect), precbc))) {
            mem_section_data *bsd;
            bsd = yasm_section_get_data(sect, &mem_section_data_cb);
            assert(bsd != NULL);
            yasm_intnum_calc(dist, YASM_EXPR_ADD, bsd->ivstart);
            e->terms[i].type = YASM_EXPR_INT;
            e->terms[i].data.intn = dist;
        }

        /* Transform our special symrecs into the appropriate value */
        if (e->terms[i].type == YASM_EXPR_SYM &&
            (ssymval = get_ssym_value(e->terms[i].data.sym))) {
            e->terms[i].type = YASM_EXPR_INT;
            e->terms[i].data.intn = yasm_intnum_copy(ssymval);
        }
    }

    return e;
}

/* Check for LMA overlap using a simple N^2 algorithm. */
static int
check_lma_overlap(yasm_section *sect, /*@null@*/ void *d)
{
    mem_section_data *bsd, *bsd2;
    yasm_section *other = (yasm_section *)d;
    yasm_intnum *overlap;

    if (!d)
        return yasm_object_sections_traverse(yasm_section_get_object(sect),
                                             sect, check_lma_overlap);
    if (sect == other)
        return 0;

    bsd = yasm_section_get_data(sect, &mem_section_data_cb);
    bsd2 = yasm_section_get_data(other, &mem_section_data_cb);

    if (yasm_intnum_is_zero(bsd->length) ||
        yasm_intnum_is_zero(bsd2->length))
        return 0;

    if (yasm_intnum_compare(bsd->istart, bsd2->istart) <= 0) {
        overlap = yasm_intnum_copy(bsd->istart);
        yasm_intnum_calc(overlap, YASM_EXPR_ADD, bsd->length);
        yasm_intnum_calc(overlap, YASM_EXPR_SUB, bsd2->istart);
    } else {
        overlap = yasm_intnum_copy(bsd2->istart);
        yasm_intnum_calc(overlap, YASM_EXPR_ADD, bsd2->length);
        yasm_intnum_calc(overlap, YASM_EXPR_SUB, bsd->istart);
    }

    if (yasm_intnum_sign(overlap) > 0) {
        yasm_error_set(YASM_ERROR_GENERAL,
                       N_("sections `%s' and `%s' overlap by %lu bytes"),
                       yasm_section_get_name(sect),
                       yasm_section_get_name(other),
                       yasm_intnum_get_uint(overlap));
        yasm_intnum_destroy(overlap);
        return -1;
    }

    yasm_intnum_destroy(overlap);
    return 0;
}

static int
mem_objfmt_output_value(yasm_value *value, unsigned char *buf,
                        unsigned int destsize,
                        /*@unused@*/ unsigned long offset, yasm_bytecode *bc,
                        int warn, /*@null@*/ void *d)
{
    /*@null@*/ mem_objfmt_output_info *info = (mem_objfmt_output_info *)d;
    /*@dependent@*/ /*@null@*/ yasm_bytecode *precbc;
    /*@dependent@*/ yasm_section *sect;

    assert(info != NULL);

    /* Binary objects we need to resolve against object, not against section. */
    if (value->rel) {
        unsigned int rshift = (unsigned int)value->rshift;
        yasm_expr *syme;
        /*@null@*/ const yasm_intnum *ssymval;

        if (yasm_symrec_is_abs(value->rel)) {
            syme = yasm_expr_create_ident(yasm_expr_int(
                yasm_intnum_create_uint(0)), bc->line);
        } else if (yasm_symrec_get_label(value->rel, &precbc)
                   && (sect = yasm_bc_get_section(precbc))) {
            syme = yasm_expr_create_ident(yasm_expr_sym(value->rel), bc->line);
        } else if ((ssymval = get_ssym_value(value->rel))) {
            syme = yasm_expr_create_ident(yasm_expr_int(
                yasm_intnum_copy(ssymval)), bc->line);
        } else
            goto done;

        /* Handle PC-relative */
        if (value->curpos_rel) {
            yasm_expr *sube;
            sube = yasm_expr_create(YASM_EXPR_SUB, yasm_expr_precbc(bc),
                yasm_expr_int(yasm_intnum_create_uint(bc->len*bc->mult_int)),
                bc->line);
            syme = yasm_expr_create(YASM_EXPR_SUB, yasm_expr_expr(syme),
                                    yasm_expr_expr(sube), bc->line);
            value->curpos_rel = 0;
            value->ip_rel = 0;
        }

        if (value->rshift > 0)
            syme = yasm_expr_create(YASM_EXPR_SHR, yasm_expr_expr(syme),
                yasm_expr_int(yasm_intnum_create_uint(rshift)), bc->line);

        /* Add into absolute portion */
        if (!value->abs)
            value->abs = syme;
        else
            value->abs =
                yasm_expr_create(YASM_EXPR_ADD, yasm_expr_expr(value->abs),
                                 yasm_expr_expr(syme), bc->line);
        value->rel = NULL;
        value->rshift = 0;
    }
done:
    /* Simplify absolute portion of value, transforming symrecs */
    if (value->abs)
        value->abs = yasm_expr__level_tree
            (value->abs, 1, 1, 1, 0, mem_objfmt_expr_xform, NULL);

	if (value->rel_addr) {
		if (yasm__strcasecmp(yasm_arch_get_machine(info->object->arch), "x86") == 0) {
			info->output->relocs.size += 4;
			info->output->relocs.buffer = yasm_xrealloc(info->output->relocs.buffer, info->output->relocs.size);
			*(int *)(info->output->relocs.buffer + info->output->relocs.size - 4) = bc->offset + offset;
		} else if (yasm__strcasecmp(yasm_arch_get_machine(info->object->arch), "amd64") == 0) {
			yasm_error_set(YASM_ERROR_GENERAL,
				N_("Rellocation in 64bit not supported yet"));
		}
	}

    /* Output */
    switch (yasm_value_output_basic(value, buf, destsize, bc, warn,
                                    info->object->arch)) {
        case -1:
            return 1;
        case 0:
            break;
        default:
            return 0;
    }

    /* Couldn't output, assume it contains an external reference. */
    yasm_error_set(YASM_ERROR_GENERAL,
        N_("memory object format does not support external references"));
    return 1;
}

static int
mem_objfmt_output_bytecode(yasm_bytecode *bc, /*@null@*/ void *d)
{
    /*@null@*/ mem_objfmt_output_info *info = (mem_objfmt_output_info *)d;
    /*@null@*/ /*@only@*/ unsigned char *bigbuf;
    unsigned long size = REGULAR_OUTBUF_SIZE;
    int gap;

    assert(info != NULL);

    bigbuf = yasm_bc_tobytes(bc, info->buf, &size, &gap, info,
                             mem_objfmt_output_value, NULL);

    /* Don't bother doing anything else if size ended up being 0. */
    if (size == 0) {
        if (bigbuf)
            yasm_xfree(bigbuf);
        return 0;
    }
	
	if (info->write_position + size > info->output->output.size) {
		unsigned long new_size = info->write_position + size;
		info->output->output.buffer = yasm_xrealloc(info->output->output.buffer, new_size);
		// zero the new allocation
		memset(info->output->output.buffer + info->output->output.size, 0, new_size - info->output->output.size);
		info->output->output.size = new_size;
	}

    /* Warn that gaps are converted to 0 and write out the 0's. */
    if (gap) {
        yasm_warn_set(YASM_WARN_UNINIT_CONTENTS,
            N_("uninitialized space declared in code/data section: zeroing"));
        memset(info->output->output.buffer + info->write_position, 0, size);
    } else {
        /* Output buf (or bigbuf if non-NULL) to file */
		memcpy(info->output->output.buffer + info->write_position, bigbuf ? bigbuf : info->buf, (size_t)size);
    }
	info->write_position += size;

    /* If bigbuf was allocated, free it */
    if (bigbuf)
        yasm_xfree(bigbuf);

    return 0;
}

/* Check to ensure bytecode is res* (for BSS sections) */
static int
mem_objfmt_no_output_bytecode(yasm_bytecode *bc, /*@null@*/ void *d)
{
    /*@null@*/ mem_objfmt_output_info *info = (mem_objfmt_output_info *)d;
    /*@null@*/ /*@only@*/ unsigned char *bigbuf;
    unsigned long size = REGULAR_OUTBUF_SIZE;
    int gap;

    assert(info != NULL);

    bigbuf = yasm_bc_tobytes(bc, info->buf, &size, &gap, info,
                             mem_objfmt_output_value, NULL);

    /* If bigbuf was allocated, free it */
    if (bigbuf)
        yasm_xfree(bigbuf);

    /* Don't bother doing anything else if size ended up being 0. */
    if (size == 0)
        return 0;

    /* Warn if not a gap. */
    if (!gap) {
        yasm_warn_set(YASM_WARN_GENERAL,
            N_("initialized space declared in nobits section: ignoring"));
    }

    return 0;
}

static int
mem_objfmt_output_section(yasm_section *sect, /*@null@*/ void *d)
{
    mem_section_data *bsd = yasm_section_get_data(sect, &mem_section_data_cb);
    /*@null@*/ mem_objfmt_output_info *info = (mem_objfmt_output_info *)d;

    assert(bsd != NULL);
    assert(info != NULL);

    if (bsd->bss) {
        yasm_section_bcs_traverse(sect, info->errwarns,
                                  info, mem_objfmt_no_output_bytecode);
    } else {
        yasm_intnum_set(info->tmp_intn, bsd->istart);
        yasm_intnum_calc(info->tmp_intn, YASM_EXPR_SUB, info->origin);
        if (yasm_intnum_sign(info->tmp_intn) < 0) {
            yasm_error_set(YASM_ERROR_VALUE,
                           N_("section `%s' starts before origin (ORG)"),
                           yasm_section_get_name(sect));
            yasm_errwarn_propagate(info->errwarns, 0);
            return 0;
        }
        if (!yasm_intnum_check_size(info->tmp_intn, sizeof(long)*8, 0, 1)) {
            yasm_error_set(YASM_ERROR_VALUE,
                           N_("section `%s' start value too large"),
                           yasm_section_get_name(sect));
            yasm_errwarn_propagate(info->errwarns, 0);
            return 0;
        }
		info->write_position = yasm_intnum_get_int(info->tmp_intn);
        yasm_section_bcs_traverse(sect, info->errwarns,
                                  info, mem_objfmt_output_bytecode);
    }

    return 0;
}

static void
mem_objfmt_cleanup(mem_objfmt_output_info *info)
{
    mem_group *group, *group_temp;

    yasm_xfree(info->buf);
    yasm_intnum_destroy(info->origin);
    yasm_intnum_destroy(info->tmp_intn);

    TAILQ_FOREACH_SAFE(group, &info->lma_groups, link, group_temp)
        mem_group_destroy(group);

    TAILQ_FOREACH_SAFE(group, &info->vma_groups, link, group_temp)
        mem_group_destroy(group);
}

static void
mem_objfmt_output(yasm_object *object, FILE *f, /*@unused@*/ int all_syms,
                  yasm_errwarns *errwarns)
{
    yasm_objfmt_mem *objfmt_mem = (yasm_objfmt_mem *)object->objfmt;
    mem_objfmt_output_info info;
    mem_group *group, *lma_group, *vma_group, *group_temp;
    yasm_intnum *start, *last, *vdelta;
    mem_groups unsorted_groups, bss_groups;

    /* Set ORG to 0 unless otherwise specified */
    if (objfmt_mem->org) {
        info.origin = yasm_expr_get_intnum(&objfmt_mem->org, 0);
        if (!info.origin) {
            yasm_error_set(YASM_ERROR_TOO_COMPLEX,
                           N_("ORG expression is too complex"));
            yasm_errwarn_propagate(errwarns, objfmt_mem->org->line);
            return;
        }
        if (yasm_intnum_sign(info.origin) < 0) {
            yasm_error_set(YASM_ERROR_VALUE, N_("ORG expression is negative"));
            yasm_errwarn_propagate(errwarns, objfmt_mem->org->line);
            return;
        }
        info.origin = yasm_intnum_copy(info.origin);
    } else
        info.origin = yasm_intnum_create_uint(0);

    info.object = object;
    info.errwarns = errwarns;
	// Hack to pass buffer
	info.output = (output_info *)f;
    info.buf = yasm_xmalloc(REGULAR_OUTBUF_SIZE);
    info.tmp_intn = yasm_intnum_create_uint(0);
    TAILQ_INIT(&info.lma_groups);
    TAILQ_INIT(&info.vma_groups);

    /* Check symbol table */
    yasm_symtab_traverse(object->symtab, &info, mem_objfmt_check_sym);

    /* Create section groups */
    if (yasm_object_sections_traverse(object, &info, mem_lma_create_group)) {
        mem_objfmt_cleanup(&info);
        return;     /* error detected */
    }

    /* Determine section order according to LMA.
     * Sections can be ordered either by (priority):
     *  - follows
     *  - start
     *  - progbits/nobits setting
     *  - order in the input file
     */

    /* Look at each group with follows specified, and find the section
     * that group is supposed to follow.
     */
    TAILQ_FOREACH_SAFE(lma_group, &info.lma_groups, link, group_temp) {
        if (lma_group->bsd->follows) {
            mem_group *found;
            /* Need to find group containing section this section follows. */
            found =
                find_group_by_name(&info.lma_groups, lma_group->bsd->follows);
            if (!found) {
                yasm_error_set(YASM_ERROR_VALUE,
                               N_("section `%s' follows an invalid or unknown section `%s'"),
                               yasm_section_get_name(lma_group->section),
                               lma_group->bsd->follows);
                yasm_errwarn_propagate(errwarns, 0);
                mem_objfmt_cleanup(&info);
                return;
            }

            /* Check for loops */
            if (lma_group->section == found->section ||
                find_group_by_section(&lma_group->follow_groups,
                                      found->section)) {
                yasm_error_set(YASM_ERROR_VALUE,
                               N_("follows loop between section `%s' and section `%s'"),
                               yasm_section_get_name(lma_group->section),
                               yasm_section_get_name(found->section));
                yasm_errwarn_propagate(errwarns, 0);
                mem_objfmt_cleanup(&info);
                return;
            }

            /* Remove this section from main lma groups list */
            TAILQ_REMOVE(&info.lma_groups, lma_group, link);
            /* Add it after the section it's supposed to follow. */
            TAILQ_INSERT_TAIL(&found->follow_groups, lma_group, link);
        }
    }

    /* Sort the top-level groups according to their start address.
     * Use Shell sort for ease of implementation.
     * If no start address is specified for a section, don't change the order,
     * and move BSS sections to a separate list so they can be moved to the
     * end of the lma list after all other sections are sorted.
     */
    unsorted_groups = info.lma_groups;  /* structure copy */
    TAILQ_INIT(&info.lma_groups);
    TAILQ_INIT(&bss_groups);
    TAILQ_FOREACH_SAFE(lma_group, &unsorted_groups, link, group_temp) {
        mem_group *before;

        if (!lma_group->bsd->istart) {
            if (lma_group->bsd->bss)
                TAILQ_INSERT_TAIL(&bss_groups, lma_group, link);
            else
                TAILQ_INSERT_TAIL(&info.lma_groups, lma_group, link);
            continue;
        }

        before = NULL;
        TAILQ_FOREACH(group, &info.lma_groups, link) {
            if (!group->bsd->istart)
                continue;
            if (yasm_intnum_compare(group->bsd->istart,
                                    lma_group->bsd->istart) > 0) {
                before = group;
                break;
            }
        }
        if (before)
            TAILQ_INSERT_BEFORE(before, lma_group, link);
        else
            TAILQ_INSERT_TAIL(&info.lma_groups, lma_group, link);
    }

    /* Move the pure-BSS sections to the end of the LMA list. */
    TAILQ_FOREACH_SAFE(group, &bss_groups, link, group_temp)
        TAILQ_INSERT_TAIL(&info.lma_groups, group, link);
    TAILQ_INIT(&bss_groups);    /* For sanity */

    /* Assign a LMA start address to every section.
     * Also assign VMA=LMA unless otherwise specified.
     *
     * We need to assign VMA=LMA here (while walking the tree) for the case:
     *  sect1 start=0 (size=0x11)
     *  sect2 follows=sect1 valign=16 (size=0x104)
     *  sect3 follows=sect2 valign=16
     * Where the valign of sect2 will result in a sect3 vaddr higher than a
     * naive segment-by-segment interpretation (where sect3 and sect2 would
     * have a VMA overlap).
     *
     * Algorithm for VMA=LMA setting:
     * Start with delta=0.
     * If there's no virtual attributes, we simply set VMA = LMA+delta.
     * If there's only valign specified, we set VMA = aligned LMA, and add
     * any new alignment difference to delta.
     *
     * We could do the LMA start and VMA=LMA steps in two separate steps,
     * but it's easier to just recurse once.
     */
    start = yasm_intnum_copy(info.origin);
    last = yasm_intnum_copy(info.origin);
    vdelta = yasm_intnum_create_uint(0);
    TAILQ_FOREACH(lma_group, &info.lma_groups, link) {
        if (lma_group->bsd->istart)
            yasm_intnum_set(start, lma_group->bsd->istart);
        group_assign_start_recurse(lma_group, start, last, vdelta,
                                   info.tmp_intn, errwarns);
        yasm_intnum_set(start, last);
    }
    yasm_intnum_destroy(last);
    yasm_intnum_destroy(vdelta);

    /*
     * Determine section order according to VMA
     */

    /* Create section groups */
    if (yasm_object_sections_traverse(object, &info, mem_vma_create_group)) {
        yasm_intnum_destroy(start);
        mem_objfmt_cleanup(&info);
        return;     /* error detected */
    }

    /* Look at each group with vfollows specified, and find the section
     * that group is supposed to follow.
     */
    TAILQ_FOREACH_SAFE(vma_group, &info.vma_groups, link, group_temp) {
        if (vma_group->bsd->vfollows) {
            mem_group *found;
            /* Need to find group containing section this section follows. */
            found = find_group_by_name(&info.vma_groups,
                                       vma_group->bsd->vfollows);
            if (!found) {
                yasm_error_set(YASM_ERROR_VALUE,
                               N_("section `%s' vfollows an invalid or unknown section `%s'"),
                               yasm_section_get_name(vma_group->section),
                               vma_group->bsd->vfollows);
                yasm_errwarn_propagate(errwarns, 0);
                yasm_intnum_destroy(start);
                mem_objfmt_cleanup(&info);
                return;
            }

            /* Check for loops */
            if (vma_group->section == found->section ||
                find_group_by_section(&vma_group->follow_groups,
                                      found->section)) {
                yasm_error_set(YASM_ERROR_VALUE,
                               N_("vfollows loop between section `%s' and section `%s'"),
                               yasm_section_get_name(vma_group->section),
                               yasm_section_get_name(found->section));
                yasm_errwarn_propagate(errwarns, 0);
                mem_objfmt_cleanup(&info);
                return;
            }

            /* Remove this section from main lma groups list */
            TAILQ_REMOVE(&info.vma_groups, vma_group, link);
            /* Add it after the section it's supposed to follow. */
            TAILQ_INSERT_TAIL(&found->follow_groups, vma_group, link);
        }
    }

    /* Due to the combination of steps above, we now know that all top-level
     * groups have integer ivstart:
     * Vstart Vfollows Valign   Handled by
     *     No       No     No   group_assign_start_recurse()
     *     No       No    Yes   group_assign_start_recurse()
     *     No      Yes    -     vfollows loop (above)
     *    Yes      -      -     mem_lma_create_group()
     */
    TAILQ_FOREACH(vma_group, &info.vma_groups, link) {
        yasm_intnum_set(start, vma_group->bsd->ivstart);
        group_assign_vstart_recurse(vma_group, start, errwarns);
    }

    /* Ensure we don't have overlapping progbits LMAs.
     * Use a dumb O(N^2) algorithm as the number of sections is essentially
     * always low.
     */
    if (yasm_object_sections_traverse(object, NULL, check_lma_overlap)) {
        yasm_errwarn_propagate(errwarns, 0);
        yasm_intnum_destroy(start);
        mem_objfmt_cleanup(&info);
        return;
    }

    /* Output sections */
    yasm_object_sections_traverse(object, &info, mem_objfmt_output_section);

    /* Clean up */
    yasm_intnum_destroy(start);
    mem_objfmt_cleanup(&info);
}

static void
mem_objfmt_destroy(yasm_objfmt *objfmt)
{
    yasm_objfmt_mem *objfmt_mem = (yasm_objfmt_mem *)objfmt;
    yasm_expr_destroy(objfmt_mem->org);
    yasm_xfree(objfmt);
}

static void
define_section_symbol(yasm_symtab *symtab, yasm_section *sect,
                      const char *sectname, const char *suffix,
                      enum mem_ssym which, unsigned long line)
{
    yasm_symrec *sym;
    mem_symrec_data *bsymd = yasm_xmalloc(sizeof(mem_symrec_data));
    char *symname = yasm_xmalloc(8+strlen(sectname)+strlen(suffix)+1);

    strcpy(symname, "section.");
    strcat(symname, sectname);
    strcat(symname, suffix);

    bsymd->section = sect;
    bsymd->which = which;

    sym = yasm_symtab_declare(symtab, symname, YASM_SYM_EXTERN, line);
    yasm_xfree(symname);
    yasm_symrec_add_data(sym, &mem_symrec_data_cb, bsymd);
}

static void
mem_objfmt_init_new_section(yasm_section *sect, unsigned long line)
{
    yasm_object *object = yasm_section_get_object(sect);
    const char *sectname = yasm_section_get_name(sect);
    /*yasm_objfmt_mem *objfmt_mem = (yasm_objfmt_mem *)object->objfmt;*/
    mem_section_data *data;

    data = yasm_xmalloc(sizeof(mem_section_data));
    data->bss = 0;
    data->align = NULL;
    data->valign = NULL;
    data->start = NULL;
    data->vstart = NULL;
    data->follows = NULL;
    data->vfollows = NULL;
    data->istart = NULL;
    data->ivstart = NULL;
    data->length = NULL;
    yasm_section_add_data(sect, &mem_section_data_cb, data);

    define_section_symbol(object->symtab, sect, sectname, ".start",
                          SSYM_START, line);
    define_section_symbol(object->symtab, sect, sectname, ".vstart",
                          SSYM_VSTART, line);
    define_section_symbol(object->symtab, sect, sectname, ".length",
                          SSYM_LENGTH, line);
}

static yasm_section *
mem_objfmt_add_default_section(yasm_object *object)
{
    yasm_section *retval;
    int isnew;

    retval = yasm_object_get_general(object, ".text", 0, 1, 0, &isnew, 0);
    if (isnew)
        yasm_section_set_default(retval, 1);
    return retval;
}

/* GAS-style flags */
static int
mem_helper_gasflags(void *obj, yasm_valparam *vp, unsigned long line, void *d,
                    /*@unused@*/ uintptr_t arg)
{
    /* TODO */
    return 0;
}

static /*@observer@*/ /*@null@*/ yasm_section *
mem_objfmt_section_switch(yasm_object *object, yasm_valparamhead *valparams,
                          /*@unused@*/ /*@null@*/
                          yasm_valparamhead *objext_valparams,
                          unsigned long line)
{
    yasm_valparam *vp;
    yasm_section *retval;
    int isnew;
    int flags_override = 0;
    const char *sectname;
    mem_section_data *bsd = NULL;

    struct mem_section_switch_data {
        /*@only@*/ /*@null@*/ char *follows;
        /*@only@*/ /*@null@*/ char *vfollows;
        /*@only@*/ /*@null@*/ yasm_expr *start;
        /*@only@*/ /*@null@*/ yasm_expr *vstart;
        /*@only@*/ /*@null@*/ yasm_intnum *align;
        /*@only@*/ /*@null@*/ yasm_intnum *valign;
        unsigned long bss;
        unsigned long code;
    } data;

    static const yasm_dir_help help[] = {
        { "follows", 1, yasm_dir_helper_string,
          offsetof(struct mem_section_switch_data, follows), 0 },
        { "vfollows", 1, yasm_dir_helper_string,
          offsetof(struct mem_section_switch_data, vfollows), 0 },
        { "start", 1, yasm_dir_helper_expr,
          offsetof(struct mem_section_switch_data, start), 0 },
        { "vstart", 1, yasm_dir_helper_expr,
          offsetof(struct mem_section_switch_data, vstart), 0 },
        { "align", 1, yasm_dir_helper_intn,
          offsetof(struct mem_section_switch_data, align), 0 },
        { "valign", 1, yasm_dir_helper_intn,
          offsetof(struct mem_section_switch_data, valign), 0 },
        { "nobits", 0, yasm_dir_helper_flag_set,
          offsetof(struct mem_section_switch_data, bss), 1 },
        { "progbits", 0, yasm_dir_helper_flag_set,
          offsetof(struct mem_section_switch_data, bss), 0 },
        { "code", 0, yasm_dir_helper_flag_set,
          offsetof(struct mem_section_switch_data, code), 1 },
        { "data", 0, yasm_dir_helper_flag_set,
          offsetof(struct mem_section_switch_data, code), 0 },
        { "execute", 0, yasm_dir_helper_flag_set,
          offsetof(struct mem_section_switch_data, code), 1 },
        { "noexecute", 0, yasm_dir_helper_flag_set,
          offsetof(struct mem_section_switch_data, code), 0 },
        { "gasflags", 1, mem_helper_gasflags, 0, 0 }
    };

    vp = yasm_vps_first(valparams);
    sectname = yasm_vp_string(vp);
    if (!sectname)
        return NULL;
    vp = yasm_vps_next(vp);

    retval = yasm_object_find_general(object, sectname);
    if (retval) {
        bsd = yasm_section_get_data(retval, &mem_section_data_cb);
        assert(bsd != NULL);
        data.follows = bsd->follows;
        data.vfollows = bsd->vfollows;
        data.start = bsd->start;
        data.vstart = bsd->vstart;
        data.align = NULL;
        data.valign = NULL;
        data.bss = bsd->bss;
        data.code = yasm_section_is_code(retval);
    } else {
        data.follows = NULL;
        data.vfollows = NULL;
        data.start = NULL;
        data.vstart = NULL;
        data.align = NULL;
        data.valign = NULL;
        data.bss = strcmp(sectname, ".bss") == 0;
        data.code = strcmp(sectname, ".text") == 0;
    }

    flags_override = yasm_dir_helper(object, vp, line, help, NELEMS(help),
                                     &data, yasm_dir_helper_valparam_warn);
    if (flags_override < 0)
        return NULL;    /* error occurred */

    if (data.start && data.follows) {
        yasm_error_set(YASM_ERROR_GENERAL,
            N_("cannot combine `start' and `follows' section attributes"));
        return NULL;
    }

    if (data.vstart && data.vfollows) {
        yasm_error_set(YASM_ERROR_GENERAL,
            N_("cannot combine `vstart' and `vfollows' section attributes"));
        return NULL;
    }

    if (data.align) {
        unsigned long align = yasm_intnum_get_uint(data.align);

        /* Alignments must be a power of two. */
        if (!is_exp2(align)) {
            yasm_error_set(YASM_ERROR_VALUE,
                           N_("argument to `%s' is not a power of two"),
                           "align");
            return NULL;
        }
    } else
        data.align = bsd ? bsd->align : NULL;

    if (data.valign) {
        unsigned long valign = yasm_intnum_get_uint(data.valign);

        /* Alignments must be a power of two. */
        if (!is_exp2(valign)) {
            yasm_error_set(YASM_ERROR_VALUE,
                           N_("argument to `%s' is not a power of two"),
                           "valign");
            return NULL;
        }
    } else
        data.valign = bsd ? bsd->valign : NULL;

    retval = yasm_object_get_general(object, sectname, 0, (int)data.code,
                                     (int)data.bss, &isnew, line);

    bsd = yasm_section_get_data(retval, &mem_section_data_cb);

    if (isnew || yasm_section_is_default(retval)) {
        yasm_section_set_default(retval, 0);
    }

    /* Update section flags */
    bsd->bss = data.bss;
    bsd->align = data.align;
    bsd->valign = data.valign;
    bsd->start = data.start;
    bsd->vstart = data.vstart;
    bsd->follows = data.follows;
    bsd->vfollows = data.vfollows;

    return retval;
}

static /*@observer@*/ /*@null@*/ yasm_symrec *
mem_objfmt_get_special_sym(yasm_object *object, const char *name,
                           const char *parser)
{
    return NULL;
}

static void
mem_objfmt_dir_org(yasm_object *object,
                   /*@null@*/ yasm_valparamhead *valparams,
                   /*@unused@*/ /*@null@*/
                   yasm_valparamhead *objext_valparams, unsigned long line)
{
    yasm_objfmt_mem *objfmt_mem = (yasm_objfmt_mem *)object->objfmt;
    yasm_valparam *vp;

    /* We only allow a single ORG in a program. */
    if (objfmt_mem->org) {
        yasm_error_set(YASM_ERROR_GENERAL, N_("program origin redefined"));
        return;
    }

    /* ORG takes just a simple expression as param */
    vp = yasm_vps_first(valparams);
    objfmt_mem->org = yasm_vp_expr(vp, object->symtab, line);
    if (!objfmt_mem->org) {
        yasm_error_set(YASM_ERROR_SYNTAX,
                       N_("argument to ORG must be expression"));
        return;
    }
}

struct mem_dir_map_data {
    unsigned long flags;
    /*@only@*/ /*@null@*/ char *filename;
};

static int
dir_map_filename(void *obj, yasm_valparam *vp, unsigned long line, void *data)
{
    struct mem_dir_map_data *mdata = (struct mem_dir_map_data *)data;
    const char *filename;

    if (mdata->filename) {
        yasm_warn_set(YASM_WARN_GENERAL, N_("map file already specified"));
        return 0;
    }

    filename = yasm_vp_string(vp);
    if (!filename) {
        yasm_error_set(YASM_ERROR_SYNTAX,
                       N_("unexpected expression in [map]"));
        return -1;
    }
    mdata->filename = yasm__xstrdup(filename);

    return 1;
}

static void
mem_section_data_destroy(void *data)
{
    mem_section_data *bsd = (mem_section_data *)data;
    if (bsd->start)
        yasm_expr_destroy(bsd->start);
    if (bsd->vstart)
        yasm_expr_destroy(bsd->vstart);
    if (bsd->follows)
        yasm_xfree(bsd->follows);
    if (bsd->vfollows)
        yasm_xfree(bsd->vfollows);
    if (bsd->istart)
        yasm_intnum_destroy(bsd->istart);
    if (bsd->ivstart)
        yasm_intnum_destroy(bsd->ivstart);
    if (bsd->length)
        yasm_intnum_destroy(bsd->length);
    yasm_xfree(data);
}

static void
mem_section_data_print(void *data, FILE *f, int indent_level)
{
    mem_section_data *bsd = (mem_section_data *)data;

    fprintf(f, "%*sbss=%d\n", indent_level, "", bsd->bss);

    fprintf(f, "%*salign=", indent_level, "");
    if (bsd->align)
        yasm_intnum_print(bsd->align, f);
    else
        fprintf(f, "(nil)");
    fprintf(f, "\n%*svalign=", indent_level, "");
    if (bsd->valign)
        yasm_intnum_print(bsd->valign, f);
    else
        fprintf(f, "(nil)");

    fprintf(f, "\n%*sstart=", indent_level, "");
    yasm_expr_print(bsd->start, f);
    fprintf(f, "\n%*svstart=", indent_level, "");
    yasm_expr_print(bsd->vstart, f);

    fprintf(f, "\n%*sfollows=", indent_level, "");
    if (bsd->follows)
        fprintf(f, "\"%s\"", bsd->follows);
    else
        fprintf(f, "(nil)");
    fprintf(f, "\n%*svfollows=", indent_level, "");
    if (bsd->vfollows)
        fprintf(f, "\"%s\"", bsd->vfollows);
    else
        fprintf(f, "(nil)");

    fprintf(f, "\n%*sistart=", indent_level, "");
    if (bsd->istart)
        yasm_intnum_print(bsd->istart, f);
    else
        fprintf(f, "(nil)");
    fprintf(f, "\n%*sivstart=", indent_level, "");
    if (bsd->ivstart)
        yasm_intnum_print(bsd->ivstart, f);
    else
        fprintf(f, "(nil)");

    fprintf(f, "\n%*slength=", indent_level, "");
    if (bsd->length)
        yasm_intnum_print(bsd->length, f);
    else
        fprintf(f, "(nil)");
    fprintf(f, "\n");
}

static void
mem_symrec_data_destroy(void *data)
{
    yasm_xfree(data);
}

static void
mem_symrec_data_print(void *data, FILE *f, int indent_level)
{
    mem_symrec_data *bsymd = (mem_symrec_data *)data;

    fprintf(f, "%*ssection=\"%s\"\n", indent_level, "",
            yasm_section_get_name(bsymd->section));
    fprintf(f, "%*swhich=", indent_level, "");
    switch (bsymd->which) {
        case SSYM_START: fprintf(f, "START"); break;
        case SSYM_VSTART: fprintf(f, "VSTART"); break;
        case SSYM_LENGTH: fprintf(f, "LENGTH"); break;
    }
    fprintf(f, "\n");
}


/* Define valid debug formats to use with this object format */
static const char *mem_objfmt_dbgfmt_keywords[] = {
    "null",
    NULL
};

static const yasm_directive mem_objfmt_directives[] = {
    { "org",    "nasm", mem_objfmt_dir_org,     YASM_DIR_ARG_REQUIRED },
    { NULL, NULL, NULL, 0 }
};

static const char *mem_nasm_stdmac[] = {
    "%imacro org 1+.nolist",
    "[org %1]",
    "%endmacro",
    NULL
};

static const yasm_stdmac mem_objfmt_stdmacs[] = {
    { "nasm", "nasm", mem_nasm_stdmac },
    { "tasm", "tasm", mem_nasm_stdmac },
    { NULL, NULL, NULL }
};

/* Define objfmt structure -- see objfmt.h for details */
yasm_objfmt_module yasm_mem_LTX_objfmt = {
    "Flat format memory",
    "mem",
    NULL,
    16,
    0,
    mem_objfmt_dbgfmt_keywords,
    "null",
    mem_objfmt_directives,
    mem_objfmt_stdmacs,
    mem_objfmt_create,
    mem_objfmt_output,
    mem_objfmt_destroy,
    mem_objfmt_add_default_section,
    mem_objfmt_init_new_section,
    mem_objfmt_section_switch,
    mem_objfmt_get_special_sym
};
