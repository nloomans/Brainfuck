/*
 *
 * Robert de Bath (c) 2014,2015 GPL v2 or later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <limits.h>
#if _POSIX_VERSION >= 199506L || defined(LLONG_MAX)
# include <inttypes.h>
#endif

#ifndef INTMAX_MAX
# define intmax_t long
# define PRIdMAX  "ld"
#endif

#define CELL int
#define MINOFF (0)
#define MAXOFF (10)
#define MINALLOC 1024
#define SAFE_CELL_MAX	((1<<30) -1)

CELL * mem = 0;
int memsize = 0;
int memshift = 0;

void run(void);
void optimise(void);
void print_summary(void);
void hex_output(FILE * ofd, int ch);
int hex_bracket = -1;

struct bfi { int cmd; int arg; } *pgm = 0;
int pgmlen = 0, on_eof = 1, debug = 0;

int physical_overflow = 0;
int physical_min = 0;
int physical_max = 255;
int quick_summary = 0;
int quick_with_counts = 0;
int cell_mask = 255;
int all_cells = 0;
int suppress_io = 0;

int nonl = 0;		/* Last thing printed was not an end of line. */
int tape_min = 0;	/* The lowest tape cell moved to. */
int tape_max = 0;	/* The highest tape cell moved to. */
int final_tape_pos = 0;	/* Where the tape pointer finished. */

intmax_t overflows =0;	/* Number of detected overflows. */
intmax_t underflows =0;	/* Number of detected underflows. */
int hard_wrap = 0;	/* Have cell values gone outside 0..255 ? */
int do_optimise = 1;

char bf[] = "+-><[].,";
intmax_t profile[256*4];
int program_len;	/* Number of BF command characters */

int main(int argc, char **argv)
{
    FILE * ifd;
    int ch;
    int p = -1, n = -1, j = -1;
    char * progname = argv[0];
    char * datafile = 0;

    while (argc>1) {
	if (argv[1][0] != '-' || !strcmp(argv[1], "-")) {
	    if (datafile) {
		fprintf(stderr, "Only one file allowed\n");
		exit(1);
	    }
	    datafile = argv[1]; argc--; argv++;
	} else if (!strcmp(argv[1], "-e")) {
	    on_eof = -1; argc--; argv++;
	} else if (!strcmp(argv[1], "-z")) {
	    on_eof = 0; argc--; argv++;
	} else if (!strcmp(argv[1], "-n")) {
	    on_eof = 1; argc--; argv++;
	} else if (!strcmp(argv[1], "-N")) {
	    suppress_io = 1; argc--; argv++;
	} else if (!strcmp(argv[1], "-d")) {
	    debug = 1; argc--; argv++;
	} else if (!strcmp(argv[1], "-p")) {
	    physical_overflow=1; argc--; argv++;
	} else if (!strcmp(argv[1], "-q")) {
	    quick_summary=1; argc--; argv++;
	} else if (!strcmp(argv[1], "-Q")) {
	    quick_summary=1; quick_with_counts=1; argc--; argv++;
	} else if (!strcmp(argv[1], "-a")) {
	    all_cells++; argc--; argv++;
	} else if (!strcmp(argv[1], "-Z")) {
	    do_optimise=0; argc--; argv++;
	} else if (!strcmp(argv[1], "-w")) {
	    cell_mask = (1<<16)-1;
	    physical_min = 0;
	    physical_max = cell_mask;
	    argc--; argv++;
	} else if (!strcmp(argv[1], "-sc")) {
	    cell_mask = (1<<8)-1;
	    physical_max = (cell_mask>>1);
	    physical_min = -1 - physical_max;
	    argc--; argv++;
	} else if (!strcmp(argv[1], "-12")) {
	    cell_mask = (1<<12)-1;
	    physical_min = 0;
	    physical_max = cell_mask;
	    argc--; argv++;
	} else if (!strcmp(argv[1], "-7")) {
	    cell_mask = (1<<7)-1;
	    physical_min = 0;
	    physical_max = cell_mask;
	    argc--; argv++;
	} else if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
	    printf("Usage: %s [options] [file]\n", progname);
	    puts("    Runs the brainfuck program provided.");
	    puts("    Multiple statistics are output once the program completes.");

	    puts(   "    Default operation uses 8 bit cells and no change on EOF."
	    "\n"    "    The stats will display the number of logical overflows,"
	    "\n"    "    these are overflows that alter the flow of control."
	    "\n"
	    "\n"    "Options:"
	    "\n"    "    -e  Return EOF (-1) on end of file."
	    "\n"    "    -z  Return zero on end of file."
	    "\n"    "    -n  Do not change the current cell on end of file (Default)."
	    "\n"    "    -N  Disable ALL I/O from the BF program; just output the stats."
	    "\n"    "    -d  Use the '#' command to output the current tape."
	    "\n"    "    -p  Count physical overflows not logical ones."
	    "\n"    "    -q  Output a quick summary of the run."
	    "\n"    "    -Q  Output a quick summary of the run (variant)."
	    "\n"    "    -a  Output all calls that have been used."
	    "\n"    "    -sc Use 'signed character' (8bit) cells."
	    "\n"    "    -w  Use 'WORD' (16bit) cells instead of 8 bit."
	    "\n"    "    -12 Use 12bit cells instead of 8 bit."
	    "\n"    "    -7  Use 7bit cells instead of 8 bit."
	    );

	    exit(1);
	} else {
	    fprintf(stderr, "Unknown option '%s'\n", argv[1]);
	    exit(1);
	}
    }

    ifd = datafile && strcmp(datafile, "-") ? fopen(datafile, "r") : stdin;
    if(!ifd) {
	perror(datafile);
	return 1;
    }

    /*
     *	For each character, if it's a valid BF command add it onto the
     *	end of the program. If the input is stdin use the '!' character
     *	to mark the end of the program and the start of the data, but
     *	only if we have a complete program.  The 'j' variable points
     *	at the list of currently open '[' commands, one is matched off
     *	by each ']'.  A ']' without a matching '[' is not a legal BF
     *	command and so is ignored.  If there are any '[' commands left
     *	over at the end they are not valid BF commands and so are ignored.
     *
     *  A run of any of the commands "<>+-" is converted into a single
     *  entry in the pgm array. This run is limited to 128 items of the
     *  same type so the counts will be correct.
     *
     *  The sequence "[-]" is detected and converted into a single token.
     *  If the debug flag is set the '#' command is also included.
     */

    while((ch = getc(ifd)) != EOF && (ifd != stdin || ch != '!' || j >= 0 || !pgm)) {
	int r = (ch == '<' || ch == '>' || ch == '+' || ch == '-');
	if (r || (debug && ch == '#') || (ch == ']' && j >= 0) ||
	    ch == '[' || ch == ',' || ch == '.') {
	    if (r && p >= 0 && pgm[p].cmd == ch) {
		if (pgm[p].arg < 128 || ch == '<' || ch == '>')
		    { pgm[p].arg += r; continue; }
	    }
	    n++;
	    if (n >= pgmlen-2) pgm = realloc(pgm, (pgmlen = n+99)*sizeof *pgm);
	    if (!pgm) { perror("realloc"); exit(1); }
	    pgm[n].cmd = ch; pgm[n].arg = r; p = n;
	    if (pgm[n].cmd == '[') { pgm[n].cmd = ' '; pgm[n].arg = j; j = n; }
	    else if (pgm[n].cmd == ']') {
		pgm[n].arg = j; j = pgm[r = j].arg; pgm[r].arg = n; pgm[r].cmd = '[';
		if (pgm[n-1].cmd == '-' && pgm[n-1].arg == 1 &&
		    pgm[n-2].cmd == '[') {
		    n -= 2; pgm[p=n].cmd = '='; pgm[n].arg = 0;
		}
	    }
	}
    }
    if (ifd != stdin) fclose(ifd);
    setbuf(stdout, NULL);
    if (!pgm) {
	fprintf(stderr, "Empty program, everything is zero.\n");
	return 1;
    }

    pgm[n+1].cmd = 0;

    {
	int i;
	for(program_len = i = 0; pgm[i].cmd; i++) {
	    if (pgm[i].cmd == '>' || pgm[i].cmd == '<' ||
		pgm[i].cmd == '+' || pgm[i].cmd == '-' ) {
		program_len += pgm[i].arg;
	    } else if (pgm[i].cmd == '=')
		program_len += 3;
	    else if(pgm[i].cmd != ' ')
		program_len++;
	}
    }

    if (do_optimise) optimise();

    run();

    print_summary();
    return 0;
}

int
alloc_ptr(int memoff)
{
    int amt, i, off;
    if (memoff >= 0 && memoff < memsize) return memoff;

    off = 0;
    if (memoff<0) off = -memoff;
    else if(memoff>=memsize) off = memoff-memsize;
    amt = (off / MINALLOC + 1) * MINALLOC;
    mem = realloc(mem, (memsize+amt)*sizeof(*mem));
    if (mem == 0) {
	fprintf(stderr, "memory allocation failure for %d cells\n", memsize+amt);
	exit(1);
    }
    if (memoff<0) {
        memmove(mem+amt, mem, memsize*sizeof(*mem));
        for(i=0; i<amt; i++)
            mem[i] = 0;
        memoff += amt;
	memshift += amt;
    } else {
        for(i=0; i<amt; i++)
            mem[memsize+i] = 0;
    }
    memsize += amt;
    return memoff;
}

void run(void)
{
    int m = 0;
    int n, v = 1;
    m = alloc_ptr(MAXOFF)-MAXOFF;
    for(n = 0;; n++) {
	switch(pgm[n].cmd)
	{
	case 0:
	    final_tape_pos = m-memshift;
	    return;

	case '>':
	    profile[pgm[n].cmd*4] += pgm[n].arg;
	    m += pgm[n].arg;
	    if (m+MAXOFF >= memsize) m = alloc_ptr(m+MAXOFF)-MAXOFF;
	    if(tape_max < m-memshift) tape_max = m-memshift;
	    break;
	case '<':
	    profile[pgm[n].cmd*4] += pgm[n].arg;
	    m -= pgm[n].arg;
	    if (m+MINOFF <= 0) m = alloc_ptr(m+MINOFF)-MINOFF;
	    if(tape_min > m-memshift) {
		tape_min = m-memshift;
		if(tape_min < -1000) {
		    fprintf(stderr, "Tape underflow at pointer %d\n", tape_min);
		    exit(1);
		}
	    }
	    break;

	case '+':
	    profile[pgm[n].cmd*4] += pgm[n].arg;
	    mem[m] += pgm[n].arg;

	    if(physical_overflow) {
		if (mem[m] > physical_max) {
		    overflows++;
		    mem[m] -= (physical_max-physical_min+1);
		}
	    } else {
		if (mem[m] > physical_max) hard_wrap = 1;
		if (mem[m] > SAFE_CELL_MAX) {
		    /* Even if we're checking on '[' it's possible for our "int" cell to overflow; trap that here. */
		    overflows++;
		    mem[m] -= (SAFE_CELL_MAX+1);
		    profile[pgm[n].cmd*4 + 3]++;
		}
	    }
	    break;
	case '-':
	    profile[pgm[n].cmd*4] += pgm[n].arg;
	    mem[m] -= pgm[n].arg;

	    if(physical_overflow) {
		if (mem[m] < physical_min) {
		    underflows++;
		    mem[m] += (physical_max-physical_min+1);
		}
	    } else {
		if (mem[m] < physical_min) hard_wrap = 1;
		if (mem[m] < -SAFE_CELL_MAX) {
		    /* Even if we're checking on '[' it's possible for our "int" cell to overflow; trap that here. */
		    underflows++;
		    mem[m] += (SAFE_CELL_MAX+1);
		    profile[pgm[n].cmd*4 + 3]++;
		}
	    }
	    break;

	case '=':
	    if (mem[m] < 0) underflows++;

	    if (mem[m] < 0) {
		profile['='*4 + 3]++;
		profile['='*4] ++;
	    } else if (mem[m] > 0)
		profile['='*4 + 2]++;
	    else
		profile['='*4 + 1]++;

	    mem[m] &= cell_mask;

	    if (mem[m]) {
		profile['['*4 + 2] ++;
		profile['-'*4 + 0] += mem[m];
		profile[']'*4 + 1] ++;
		profile[']'*4 + 2] += mem[m]-1;
	    } else
		profile['['*4 + 1]++;

	    mem[m] = 0;
	    break;

	case '[':
	    if (!physical_overflow) {
		if ((mem[m] & cell_mask) == 0 && mem[m]) {
		    /* This condition will be different. */
		    if (mem[m] < 0) underflows++; else overflows++;
		    profile[pgm[n].cmd*4 + 3]++;

		    mem[m] &= cell_mask;
		    if (mem[m] > physical_max)
			mem[m] -= (physical_max-physical_min+1);
		}
	    }

	    profile[pgm[n].cmd*4+1 + !!mem[m]]++;
	    if (mem[m] == 0) n = pgm[n].arg;
	    break;
	case ']':
	    if (!physical_overflow) {
		if ((mem[m] & cell_mask) == 0 && mem[m]) {
		    /* This condition will be different. */
		    if (mem[m] < 0) underflows++; else overflows++;
		    profile[pgm[n].cmd*4 + 3]++;

		    mem[m] &= cell_mask;
		    if (mem[m] > physical_max)
			mem[m] -= (physical_max-physical_min+1);
		}
	    }

	    profile[pgm[n].cmd*4+1 + !!mem[m]]++;
	    if (mem[m] != 0) n = pgm[n].arg;
	    break;

	case '.':
	    profile[pgm[n].cmd*4]++;
	    { int a = (mem[m] & 0xFF & cell_mask);
	      if (!suppress_io) putchar(a);
	      if (a != 13) nonl = (a != '\n'); }
	    break;
	case ',':
	    profile[pgm[n].cmd*4+1 + !!mem[m]]++;

	    { int a = suppress_io?EOF:getchar();
	      if(a != EOF) mem[m] = a;
	      else if (on_eof != 1) mem[m] = on_eof; }
	    break;
	case '#':
	    if (all_cells && cell_mask == 0xFF) {
		int a;
		fprintf(stderr, "Debug dump ->\n");
		hex_bracket = m;
		for(a = 0; a <= tape_max-tape_min; a++) {
		    hex_output(stderr, mem[a] & cell_mask);
		}
		hex_output(stderr, EOF);
		fprintf(stderr, "\n");
	    } else {
		int a;
		for (a=0; a<10; a++)
		    fprintf(stderr, "%c%-5d", a==m?'>':' ', mem[a]);
		fprintf(stderr, "\n");
	    }
	    break;

	case 'B':
	    if (!physical_overflow) {
		if ((mem[m] & cell_mask) == 0 && mem[m]) {
		    /* This condition will be different. */
		    if (mem[m] < 0) underflows++; else overflows++;
		    profile['['*4 + 3]++;

		    mem[m] &= cell_mask;
		    if (mem[m] > physical_max)
			mem[m] -= (physical_max-physical_min+1);
		}
	    }

	    profile['['*4+1 + !!mem[m]]++;
	    if (mem[m] == 0){
		n = pgm[n].arg;
		v = 1;
	    } else
		v = (mem[m] & cell_mask);
	    break;

	case 'E':
	    profile[']'*4 + 1] ++;
	    profile[']'*4 + 2] += v-1;
	    v = 1;
	    break;

	case 'M':
	    profile['+'*4] += pgm[n].arg*v;
	    mem[m] += pgm[n].arg*v;

	    if(physical_overflow) {
		while (mem[m] > physical_max) {
		    overflows++;
		    mem[m] -= (physical_max-physical_min+1);
		}
	    } else {
		if (mem[m] > physical_max) hard_wrap = 1;
		if (mem[m] > SAFE_CELL_MAX) {
		    /* Even if we're checking on '[' it's possible for our "int" cell to overflow; trap that here. */
		    overflows++;
		    mem[m] -= (SAFE_CELL_MAX+1);
		    profile[pgm[n].cmd*4 + 3]++;
		}
	    }
	    break;
	case 'N':
	    profile['-'*4] += pgm[n].arg*v;
	    mem[m] -= pgm[n].arg*v;

	    if(physical_overflow) {
		while (mem[m] < physical_min) {
		    underflows++;
		    mem[m] += (physical_max-physical_min+1);
		}
	    } else {
		if (mem[m] < physical_min) hard_wrap = 1;
		if (mem[m] < -SAFE_CELL_MAX) {
		    /* Even if we're checking on '[' it's possible for our "int" cell to overflow; trap that here. */
		    underflows++;
		    mem[m] += (SAFE_CELL_MAX+1);
		    profile[pgm[n].cmd*4 + 3]++;
		}
	    }
	    break;

	case 'R':
	    profile['>'*4] += pgm[n].arg*v;
	    m += pgm[n].arg;
	    if (m+MAXOFF >= memsize) m = alloc_ptr(m+MAXOFF)-MAXOFF;
	    if(tape_max < m-memshift) tape_max = m-memshift;
	    break;
	case 'L':
	    profile['<'*4] += pgm[n].arg*v;
	    m -= pgm[n].arg;
	    if (m+MINOFF <= 0) m = alloc_ptr(m+MINOFF)-MINOFF;
	    if(tape_min > m-memshift) {
		tape_min = m-memshift;
		if(tape_min < -1000) {
		    fprintf(stderr, "Tape underflow at pointer %d\n", tape_min);
		    exit(1);
		}
	    }
	    break;

	}
    }
}

void optimise(void)
{
    // +++++ [  -  >  +++  >  ++++  <<  ]
    // 5+    [ 1- 1>  3+  1>  4+    2<  ]
    // 5+    B    1>  3M  1>  4M    2<  Z
    // 5i    1    1*B 3*B 1*B 4*B   2*B 2*B

    int n, lastopen = 0, balance = 0, decfound = 0;

    for(n = 0; pgm[n].cmd; n++) {
	// > < + - = [ ] . , #
	if (pgm[n].cmd == '[') {
	    lastopen = n;
	    decfound = balance = 0;
	} else if (pgm[n].cmd == '>') {
	    balance += pgm[n].arg;
	} else if (pgm[n].cmd == '<') {
	    balance -= pgm[n].arg;
	} else if (pgm[n].cmd == '+' || pgm[n].cmd == '-') {
	    if (balance == 0) {
		if (pgm[n].cmd == '-' && pgm[n].arg == 1) {
			if (decfound) lastopen = 0;
			else decfound = 1;
		} else
		    lastopen = 0;
	    }
	} else if (pgm[n].cmd == ']' && lastopen && balance == 0 && decfound) {
	    /* We have a balanced loop with simple contents */
	    int i;
	    for(i=lastopen; i<n; i++) {
		if (pgm[i].cmd == '[') pgm[i].cmd = 'B';
		else if (pgm[i].cmd == '+') pgm[i].cmd = 'M';
		else if (pgm[i].cmd == '-') pgm[i].cmd = 'N';
		else if (pgm[i].cmd == '>') pgm[i].cmd = 'R';
		else if (pgm[i].cmd == '<') pgm[i].cmd = 'L';
	    }
	    pgm[n].cmd = 'E';
	} else
	    lastopen = 0;
    }
}

void
print_pgm()
{
    int n;
    for(n = 0; pgm[n].cmd; n++) {
	if (pgm[n].cmd == '>' || pgm[n].cmd == '<' ||
	    pgm[n].cmd == '+' || pgm[n].cmd == '-' ) {

	    int i;
	    for(i = 0; i < pgm[n].arg; i++)
		fprintf(stderr, "%c", pgm[n].cmd);
	} else if (pgm[n].cmd == '=')
	    fprintf(stderr, "[-]");
	else if (pgm[n].cmd == 'M' || pgm[n].cmd == 'N' ||
	         pgm[n].cmd == 'R' || pgm[n].cmd == 'L') {
	    int i;
	    for(i = 0; i < pgm[n].arg; i++)
		fprintf(stderr, "%c",
			pgm[n].cmd=='M'?'+':
			pgm[n].cmd=='M'?'-':
			pgm[n].cmd=='R'?'>':
			'<');
	}
	else if (pgm[n].cmd == 'B') fprintf(stderr, "[");
	else if (pgm[n].cmd == 'E') fprintf(stderr, "]");
	else if (pgm[n].cmd != ' ') fprintf(stderr, "%c", pgm[n].cmd);
    }
}

void
print_summary()
{
    intmax_t total_count = 0;

    {
	int i;
	for (i = 0; i < (int)sizeof(bf); i++) {
	    total_count += profile[bf[i]*4]
			+  profile[bf[i]*4+1]
			+  profile[bf[i]*4+2];
	}
    }

    if (!quick_summary)
    {
	int ch, n;

	if (nonl && !suppress_io)
	    fprintf(stderr, "\n\\ no newline at end of output.\n");
	else if (nonl)
	    fprintf(stderr, "No newline at end of output.\n");

	if (program_len <= 60) {
	    fprintf(stderr, "Program size %d : ", program_len);
	    print_pgm();
	    fprintf(stderr, "\n");
	} else
	    fprintf(stderr, "Program size %d\n", program_len);
	fprintf(stderr, "Final tape contents:\n");

	{
	    int pw = 3, cc = 0, pc = 0;
	    {
		char buf[64];
		int i;
		i = sprintf(buf, "%d", physical_min);
		if (i > pw && i < (int)sizeof(buf)) pw = i;
		i = sprintf(buf, "%d", physical_max);
		if (i > pw && i < (int)sizeof(buf)) pw = i;
	    }

	    if (all_cells && cell_mask == 0xFF && tape_min == 0) {
		fprintf(stderr, "Pointer at: %d\n", final_tape_pos);
		hex_bracket = final_tape_pos;
		for(ch = 0; ch <= tape_max-tape_min; ch++) {
		    hex_output(stderr, mem[ch+memshift] & cell_mask);
		}
		hex_output(stderr, EOF);
	    } else {
		if (tape_min < 0) cc += fprintf(stderr, " !");

		for(ch = 0; (all_cells || ch < 16) && ch <= tape_max-tape_min; ch++) {
		    if (((ch+tape_min)&15) == 0) {
			if (ch+tape_min != 0) {
			    if (pc) fprintf(stderr, "\n%*s\n", pc, "^");
			    else fprintf(stderr, "\n");
			    cc = pc = 0;
			}
			cc += fprintf(stderr, " :");
		    }
		    cc += fprintf(stderr, " %*d",
				  pw, mem[ch+memshift+tape_min] & cell_mask);
		    if (final_tape_pos == ch+tape_min)
			pc = cc;
		}
		if (!all_cells && tape_max-tape_min >= 16) fprintf(stderr, " ...");
		if (pc) fprintf(stderr, "\n%*s\n", pc, "^");
		else fprintf(stderr, "\nPointer at: %d\n", final_tape_pos);
	    }
	}

	if (tape_min < 0)
	    fprintf(stderr, "WARNING: Tape pointer minimum %d, segfault.\n", tape_min);
	fprintf(stderr, "Tape pointer maximum %d\n", tape_max);

	if (overflows || underflows) {
	    fprintf(stderr, "Range error: ");
	    if (physical_overflow)
		fprintf(stderr, "range %d..%d", physical_min, physical_max);
	    else
		fprintf(stderr, "value check");
	    if (overflows)
		fprintf(stderr, ", overflows: %"PRIdMAX"", overflows);
	    if (underflows)
		fprintf(stderr, ", underflows: %"PRIdMAX"", underflows);
	    fprintf(stderr, "\n");
	} else if (!physical_overflow && hard_wrap) {
	    fprintf(stderr, "Hard wrapping would occur for %s cells.\n",
		physical_min<0?"signed":"unsigned");
	}

	if (!physical_overflow) {
	    if (profile['+'*4+3]+profile['-'*4+3])
		fprintf(stderr, "Physical overflows '+' %"PRIdMAX", '-' %"PRIdMAX"\n",
		    profile['+'*4+3], profile['-'*4+3]);
	}

	if (profile['['*4+3]+profile[']'*4+3])
	    fprintf(stderr, "Program logic effects '['->%"PRIdMAX", ']'->%"PRIdMAX"\n",
		profile['['*4+3], profile[']'*4+3]);

	if (profile['='*4 + 3])
	    fprintf(stderr, "Sequence '[-]' on negative cell: %"PRIdMAX"\n",
		    profile['='*4 + 3]);

	if (profile['['*4+1])
	    fprintf(stderr, "Skipped loops (zero on '['): %"PRIdMAX"\n",
		profile['['*4+1]);

	for(n = 0; n < (int)sizeof(bf)-1; n++) {
	    ch = bf[n];

	    if (n==0 || n==4)
		fprintf(stderr, "Counts:    ");

	    fprintf(stderr, " %c: %-12"PRIdMAX"", ch,
		    profile[ch*4]+ profile[ch*4+1]+ profile[ch*4+2]);

	    if (n == 3)
		fprintf(stderr, "\n");
	}
	fprintf(stderr, "\nTotal:         %-12"PRIdMAX"\n", total_count);
    }
    else
    {
	if (tape_min < -16) {
	    fprintf(stderr, "ERROR ");
	    print_pgm();
	    fprintf(stderr, "\n");
	} else {
	    int ch, nonwrap =
		(overflows == 0 && underflows == 0 &&
		 (mem[final_tape_pos+memshift] & ~cell_mask) == 0);

	    fprintf(stderr, "%d ", (mem[final_tape_pos+memshift] & cell_mask) );
	    fprintf(stderr, "%d ", program_len-tape_min);
	    fprintf(stderr, "%d ", tape_max-tape_min+1);
	    fprintf(stderr, "%"PRIdMAX" ", total_count);
	    for(ch = tape_min; ch < 0; ch++)
		fprintf(stderr, ">");
	    print_pgm();

	    if (quick_with_counts)
		fprintf(stderr, " (%d, %d, %"PRIdMAX") %swrapping%s\n",
			program_len-tape_min, tape_max-tape_min+1,
			total_count-tape_min,
			nonwrap ? "non-" : "",
			nonwrap && hard_wrap ? " (soft)" : "");
	    else
		fprintf(stderr, " (%d, %d) %swrapping%s\n",
			program_len-tape_min, tape_max-tape_min+1,
			nonwrap ? "non-" : "",
			nonwrap && hard_wrap ? " (soft)" : "");
	}
    }
}

void
hex_output(FILE * ofd, int ch)
{
    static char lastbuf[80];
    static char linebuf[80];
    static char buf[20];
    static int pos = 0, addr = 0, lastmode = 0;

    if( ch == EOF ) {
	if(pos)
	    fprintf(ofd, "%06x: %.67s\n", addr, linebuf);
	pos = 0;
	addr = 0;
	*lastbuf = 0;
	lastmode = 0;
	hex_bracket = -1;
    } else {
	if(!pos)
	    memset(linebuf, ' ', sizeof(linebuf));
	sprintf(buf, "%02x", ch&0xFF);
	memcpy(linebuf+pos*3+(pos > 7)+1, buf, 2);
	if (addr+pos == hex_bracket) {
	    linebuf[pos*3+(pos > 7)] = '(';
	    linebuf[pos*3+(pos > 7)+3] = ')';
	    hex_bracket = 0;
	}

	if( ch > ' ' && ch <= '~' )
	    linebuf[51+pos] = ch;
	else linebuf[51+pos] = '.';
	pos = ((pos+1) & 0xF);
	if( pos == 0 ) {
	    linebuf[67] = 0;
	    if (*lastbuf && strcmp(linebuf, lastbuf) == 0) {
		if (!lastmode)
		    fprintf(ofd, "*\n");
		lastmode = 1;
	    } else {
		lastmode = 0;
		fprintf(ofd, "%06x: %.67s\n", addr, linebuf);
		strcpy(lastbuf, linebuf);
	    }
	    addr += 16;
	}
    }
}
