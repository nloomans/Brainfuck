/*
 * GNU Lightning JIT runner.
 *
 * This converts the BF tree into JIT assembler using GNU lightning.
 *
 * This works with both v1 and v2.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "bfi.tree.h"
#include "bfi.gnulit.h"

#ifndef DISABLE_GNULIGHTNING
#define LIGHTNING_DETECT
#include <lightning.h>

#ifdef jit_set_ip
#ifndef GNULIGHTv1
#define GNULIGHTv1
#endif
#elif defined(JIT_R0)
#ifdef GNULIGHTv1
#warning GNU Lightning V2 disabled, library will not be linked.
#undef GNULIGHTv1
#else
#define GNULIGHTv2
#endif
#elif defined(GNULIGHTv1)
#undef GNULIGHTv1
#warning GNU Lightning V1 disabled, Header file did not configure.
#endif

#include "bfi.run.h"
#include "clock.h"

#if defined(GNULIGHTv1) || defined(GNULIGHTv2)

/* GNU lightning's macros upset GCC a little ... */
#if (defined(__GNUC__) && ((__GNUC__>4) || (__GNUC__==4 && __GNUC_MINOR__>=3))) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#ifdef __clang__
#pragma GCC diagnostic ignored "-Wused-but-marked-unused"
#endif
#endif

#ifdef GNULIGHTv1
#define jit_addi jit_addi_i
#define jit_movi jit_movi_i
#define jit_negr jit_negr_i
#define jit_muli jit_muli_i
#define jit_addr jit_addr_i
#define jit_andi jit_andi_i
#define jit_extr_uc jit_extr_uc_i

#if !defined(__x86_64__) && !defined(__amd64__) && !defined(_M_AMD64)
#define JITLIBOK 1
#else
#define JITLIBOK 0
#warning "The GNU Lightning header smells bad, I won't be using it by default."
#endif
#endif

#ifdef GNULIGHTv2
static jit_state_t *_jit;
#define jit_stxi_uc jit_stxi_c
#define jit_str_uc jit_str_c
#define JITLIBOK 1
#endif

/*
 * For i386 JIT_V0 == ebx, JIT_V1 == esi, JIT_V2 == edi
 *          JIT_R0 == eax, JIT_R1 == ecx, JIT_R2 == edx
 */

#define REG_P   JIT_V1
#define REG_ACC JIT_R0
#define REG_A1	JIT_R1

int gnulightning_ok = JITLIBOK;

static int tape_step = sizeof(int);

static void (*codeptr)(void);
static int acc_loaded = 0;
static int acc_offset = 0;
static int acc_dirty = 0;
static int acc_hi_dirty = 0;

static void
clean_acc(void)
{
    if (acc_loaded && acc_dirty) {
	if (tape_step > 1) {
	    if (acc_offset)
		jit_stxi_i(acc_offset * tape_step, REG_P, REG_ACC);
	    else
		jit_str_i(REG_P, REG_ACC);
	} else {
	    if (acc_offset)
		jit_stxi_uc(acc_offset, REG_P, REG_ACC);
	    else
		jit_str_uc(REG_P, REG_ACC);
	}

	acc_dirty = 0;
    }
}

static void
set_acc_offset(int offset)
{
    if (acc_loaded && acc_dirty && acc_offset != offset)
	clean_acc();

    acc_offset = offset;
    acc_loaded = 1;
    acc_dirty = 1;
    acc_hi_dirty = 1;
}

static void
load_acc_offset(int offset)
{
    if (acc_loaded) {
	if (acc_offset == offset) return;
	if (acc_dirty)
	    clean_acc();
    }

    acc_offset = offset;
    if (tape_step > 1) {
	if (acc_offset)
	    jit_ldxi_i(REG_ACC, REG_P, acc_offset * tape_step);
	else
	    jit_ldr_i(REG_ACC, REG_P);
    } else {
	if (acc_offset)
	    jit_ldxi_uc(REG_ACC, REG_P, acc_offset);
	else
	    jit_ldr_uc(REG_ACC, REG_P);
    }

    acc_loaded = 1;
    acc_dirty = 0;
    acc_hi_dirty = (tape_step*8 != cell_size);
}

static void puts_without_nl(char * s) { fputs(s, stdout); }

static void failout(void) __attribute__ ((__noreturn__));
static void failout(void) { fprintf(stderr, "STOP Command executed.\n"); exit(1); }

struct freecell { struct freecell * next; void * memp; } * saved_pointers = 0;

static void
save_ptr_for_free(void * memp)
{
    struct freecell * n = malloc(sizeof*n);
    if (!n) {perror("malloc"); return; }
    n->next = saved_pointers;
    n->memp = memp;
    saved_pointers = n;
}

static void
free_saved_memory(void)
{
    while(saved_pointers) {
	struct freecell * n = saved_pointers;
	saved_pointers = saved_pointers->next;
	free(n->memp);
	free(n);
    }
}

void
run_gnulightning(void)
{
    struct bfi * n = bfprog;
    int maxstack = 0, stackptr = 0;
    void * tape_memory = 0;
#ifdef GNULIGHTv1
    jit_insn** loopstack = 0;
    jit_insn *codeBuffer;
    void * startptr;
#endif
#ifdef GNULIGHTv2
    jit_node_t    *start, *end;	    /* For size of code */
    jit_node_t** loopstack = 0;
#endif

    if (cell_size == 8)
	tape_step = 1;

    tape_memory = map_hugeram();

#ifdef GNULIGHTv1
    /* TODO: Use mmap for allocating memory, the x86 execute protection
     * bit is on the segment so Linux has to say thay everything below
     * a specific address is executable. If you ask mmap for executable
     * memory it can put it below the current value. The mprotect()
     * function can't do this.
     */
    if (total_nodes < 4096)
	codeBuffer = malloc(65536);
    else
	codeBuffer = malloc(16 * total_nodes);
    save_ptr_for_free(codeBuffer);

    codeptr = jit_set_ip(codeBuffer).vptr;
    startptr = jit_get_ip().ptr;
    /* Function call prolog */
    jit_prolog(0);
    /* Get the data area pointer */
    jit_movi_p(REG_P, tape_memory);
#endif

#ifdef GNULIGHTv2
    init_jit(NULL); // argv[0]);
    _jit = jit_new_state();
    start = jit_note(__FILE__, __LINE__);
    jit_prolog();

    /* Get the data area pointer */
    jit_movi(REG_P, (jit_word_t) tape_memory);
#endif

    while(n)
    {
	switch(n->type)
	{
	case T_MOV:
	    if (acc_loaded)
		acc_offset -= n->count;

	    jit_addi(REG_P, REG_P, n->count * tape_step);
	    break;

	case T_ADD:
	    load_acc_offset(n->offset);
	    set_acc_offset(n->offset);

	    jit_addi(REG_ACC, REG_ACC, n->count);
	    break;

	case T_SET:
	    set_acc_offset(n->offset);
	    jit_movi(REG_ACC, n->count);
	    break;

	case T_CALC:
	    if (n->offset == n->offset2 && n->count2 == 1) {
		load_acc_offset(n->offset);
		set_acc_offset(n->offset);
		if (n->count)
		    jit_addi(REG_ACC, REG_ACC, n->count);
	    } else if (n->count2 != 0) {
		load_acc_offset(n->offset2);
		set_acc_offset(n->offset);
		if (n->count2 == -1)
		    jit_negr(REG_ACC, REG_ACC);
		else if (n->count2 != 1)
		    jit_muli(REG_ACC, REG_ACC, n->count2);
		if (n->count)
		    jit_addi(REG_ACC, REG_ACC, n->count);
	    } else {
		clean_acc();
		set_acc_offset(n->offset);

		jit_movi(REG_ACC, n->count);
		if (n->count2 != 0) {
		    if (tape_step > 1)
			jit_ldxi_i(REG_A1, REG_P, n->offset2 * tape_step);
		    else
			jit_ldxi_uc(REG_A1, REG_P, n->offset2);
		    if (n->count2 == -1)
			jit_negr(REG_A1, REG_A1);
		    else if (n->count2 != 1)
			jit_muli(REG_A1, REG_A1, n->count2);
		    jit_addr(REG_ACC, REG_ACC, REG_A1);
		}
	    }

	    if (n->count3 != 0) {
		if (tape_step > 1)
		    jit_ldxi_i(REG_A1, REG_P, n->offset3 * tape_step);
		else
		    jit_ldxi_uc(REG_A1, REG_P, n->offset3);
		if (n->count3 == -1)
		    jit_negr(REG_A1, REG_A1);
		else if (n->count3 != 1)
		    jit_muli(REG_A1, REG_A1, n->count3);
		jit_addr(REG_ACC, REG_ACC, REG_A1);
	    }
	    break;

	case T_IF: case T_MULT: case T_CMULT: case T_FOR:
	case T_WHL:
	    load_acc_offset(n->offset);
	    clean_acc();

	    if (stackptr >= maxstack) {
		loopstack = realloc(loopstack,
			    ((maxstack+=32)+2)*sizeof(*loopstack));
		if (loopstack == 0) {
		    perror("loop stack realloc failure");
		    exit(1);
		}
	    }

	    if (cell_mask > 0 && acc_hi_dirty) {
		if (cell_mask == 0xFF)
		    jit_extr_uc(REG_ACC,REG_ACC);
		else
		    jit_andi(REG_ACC, REG_ACC, cell_mask);
	    }

#ifdef GNULIGHTv1
	    loopstack[stackptr] = jit_beqi_i(jit_forward(), REG_ACC, 0);
	    loopstack[stackptr+1] = jit_get_label();
#endif
#ifdef GNULIGHTv2
	    loopstack[stackptr] = jit_beqi(REG_ACC, 0);
	    loopstack[stackptr+1] = jit_label();
#endif
	    stackptr += 2;
	    break;

	case T_END:
	    load_acc_offset(n->offset);
	    clean_acc();

	    stackptr -= 2;
	    if (stackptr < 0) {
		fprintf(stderr, "Code gen failure: Stack pointer negative.\n");
		exit(1);
	    }

	    if (cell_mask > 0 && acc_hi_dirty) {
		if (cell_mask == 0xFF)
		    jit_extr_uc(REG_ACC,REG_ACC);
		else
		    jit_andi(REG_ACC, REG_ACC, cell_mask);
	    }

#ifdef GNULIGHTv1
	    jit_bnei_i(loopstack[stackptr+1], REG_ACC, 0);
	    jit_patch(loopstack[stackptr]);
#endif
#ifdef GNULIGHTv2
	    {
		jit_node_t *ref;
		ref = jit_bnei(REG_ACC, 0);
		jit_patch_at(ref, loopstack[stackptr+1]);
		jit_patch(loopstack[stackptr]);
	    }
#endif
	    break;

	case T_ENDIF:
	    clean_acc();
	    acc_loaded = 0;

	    stackptr -= 2;
	    if (stackptr < 0) {
		fprintf(stderr, "Code gen failure: Stack pointer negative.\n");
		exit(1);
	    }
	    jit_patch(loopstack[stackptr]);
	    break;

	case T_PRT:
	    clean_acc();
	    load_acc_offset(n->offset);
	    acc_loaded = 0;

	    if (cell_mask > 0 && acc_hi_dirty) {
		if (cell_mask == 0xFF)
		    jit_extr_uc(REG_ACC,REG_ACC);
		else
		    jit_andi(REG_ACC, REG_ACC, cell_mask);
	    }

#ifdef GNULIGHTv1
	    jit_prepare_i(1);
	    jit_pusharg_i(REG_ACC);
	    jit_finish(putch);
#endif
#ifdef GNULIGHTv2
	    jit_prepare();
	    jit_pushargr(REG_ACC);
	    jit_finishi(putch);
#endif
	    break;

	case T_CHR:
	    clean_acc();
	    acc_loaded = 0;

	    if (n->count <= 0 || (n->count >= 127 && iostyle == 1) ||
		    !n->next || n->next->type != T_CHR) {
		jit_movi(REG_ACC, n->count);
#ifdef GNULIGHTv1
		jit_prepare_i(1);
		jit_pusharg_i(REG_ACC);
		jit_finish(putch);
#endif
#ifdef GNULIGHTv2
		jit_prepare();
		jit_pushargr(REG_ACC);
		jit_finishi(putch);
#endif
	    } else {
		int i = 0, j;
		struct bfi * v = n;
		char *s, *p;
		while(v->next && v->next->type == T_CHR &&
			v->next->count > 0 &&
			    (v->next->count < 127 || iostyle != 1)) {
		    v = v->next;
		    i++;
		}
		p = s = malloc(i+2);
		save_ptr_for_free(s);

		for(j=0; j<i; j++) {
		    *p++ = (char) /*GCC -Wconversion*/ n->count;
		    n = n->next;
		}
		*p++ = (char) /*GCC -Wconversion*/ n->count;
		*p++ = 0;

#ifdef GNULIGHTv1
		jit_movi_p(REG_ACC, s);
		jit_prepare_i(1);
		jit_pusharg_i(REG_ACC);
		jit_finish(puts_without_nl);
#endif
#ifdef GNULIGHTv2
		jit_prepare();
		jit_pushargi((jit_word_t) s);
		jit_finishi(puts_without_nl);
#endif
	    }
	    break;

	case T_INP:
	    load_acc_offset(n->offset);
	    set_acc_offset(n->offset);

#ifdef GNULIGHTv1
	    jit_prepare_i(1);
	    jit_pusharg_i(REG_ACC);
	    jit_finish(getch);
	    jit_retval_i(REG_ACC);
#endif
#ifdef GNULIGHTv2
	    jit_prepare();
	    jit_pushargr(REG_ACC);
	    jit_finishi(getch);
	    jit_retval(REG_ACC);
#endif
	    break;

	case T_STOP:
#ifdef GNULIGHTv1
	    jit_prepare_i(0);
	    jit_finish(failout);
#endif
#ifdef GNULIGHTv2
	    jit_prepare();
	    jit_finishi(failout);
#endif
	    break;

	case T_NOP:
	case T_DUMP:
	    fprintf(stderr, "Warning on code generation: "
		    "%s node: ptr+%d, cnt=%d, @(%d,%d).\n",
		    tokennames[n->type],
		    n->offset, n->count, n->line, n->col);
	    break;

	default:
	    fprintf(stderr, "Code gen error: "
		    "%s\t"
		    "%d:%d, %d:%d, %d:%d\n",
		    tokennames[n->type],
		    n->offset, n->count,
		    n->offset2, n->count2,
		    n->offset3, n->count3);
	    exit(1);
	}
	n=n->next;

#if 0 /*def GNULIGHTv2 */
	if(n && enable_trace) {
	    char *p, buf[250];
	    clean_acc();
	    acc_loaded = 0;

	    sprintf(buf, "@(%d,%d)\n", n->line, n->col);
	    p = strdup(buf);
	    save_ptr_for_free(p);

	    jit_prepare();
	    jit_pushargi((jit_word_t) p);
	    jit_finishi(puts_without_nl);
	}
#endif

#ifdef GNULIGHTv1
	/* TODO -- Check for codeBuffer overflow (add jmp to new) */
#endif
    }

    jit_ret();

#ifdef GNULIGHTv1
    jit_flush_code(startptr, jit_get_ip().ptr);

    if (verbose)
	fprintf(stderr, "Generated %d bytes of machine code, running\n",
		(int)(jit_get_ip().ptr - (char*)startptr));

    start_runclock();
    codeptr();
    finish_runclock(&run_time, &io_time);
#endif

#ifdef GNULIGHTv2
    jit_epilog();
    end = jit_note(__FILE__, __LINE__);
    codeptr = jit_emit();

    if (verbose)
	fprintf(stderr, "Generated %d bytes of machine code, running\n",
	    (int)((char*)jit_address(end) - (char*)jit_address(start)));

    jit_clear_state();
    // jit_disassemble();

    start_runclock();
    codeptr();
    finish_runclock(&run_time, &io_time);
#endif

#if 0
  /* This code writes the generated instructions to a file
   * so we can disassemble it using:  ndisasm -b 32/64 code.bin  */
    {
#ifdef GNULIGHTv1
	char   *p = startptr;
	int     s = jit_get_ip().ptr - p;
#endif
#ifdef GNULIGHTv2
	char   *p = (char*)jit_address(start);
	int     s = (char*)jit_address(end) - p;
#endif
	FILE   *fp = fopen("code.bin", "w");
	int     i;
	for (i = 0; i < s; ++i) {
	    fputc(p[i], fp);
	}
	fclose(fp);
    }
#endif

#ifdef GNULIGHTv2
    jit_destroy_state();
    finish_jit();
#endif

    codeptr = 0;
    if (loopstack) { free(loopstack); loopstack = 0; }
    free_saved_memory();
}

#else
#warning "GNU Lightning did not configure successfully."
int gnulightning_ok = 0;
void
run_gnulightning(void)
{
    fprintf(stderr, "GNU Lightning isn't configured\n");
    exit(1);
}
#endif
#endif
