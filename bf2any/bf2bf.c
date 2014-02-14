#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf2any.h"

/*
 * BF translation to BF. This isn't an identity translation because even
 * without most of the peephole optimisation the loader will still remove
 * sequences that are cancelling or that can never be run because they
 * begin with the '][' comment loop introducer.
 *
 * Then there are all the variants.
 *
 * Some of these also generate a set of C #define lines so the output
 * is compilable as C.
 */

static const char bf[] = "><+-.,[]";

/* Language "C" */
static const char * cbyte[] = { "m+=1;", "m-=1;", "++*m;", "--*m;",
		   "write(1,m,1);", "read(0,m,1);", "while(*m){", "}", 0 };

static const char * cbyte_rle[] = { ";m+=1", ";m-=1", ";*m+=1", ";*m-=1",
		   ";write(1,m,1)", ";read(0,m,1)", ";while(*m){", ";}", "+1"};

static const char * cint[] = { "m+=1;", "m-=1;", "++*m;", "--*m;",
	"putchar(*m);", "{int _c=getchar();if(_c!=EOF)*m=_c;}",
	"while(*m){", "}", 0 };

static const char * cint_rle[] = { ";m+=1", ";m-=1", ";*m+=1", ";*m-=1",
	";putchar(*m)", ";{int _c=getchar();if(_c!=EOF)*m=_c;}",
	";while(*m){", ";}", "+1" };

/* Language "ook" */
static const char * ook[] =
		{"Ook. Ook?", "Ook? Ook.", "Ook. Ook.", "Ook! Ook!",
		"Ook! Ook.", "Ook. Ook!", "Ook! Ook?", "Ook? Ook!"};

/* Language "blub" */
static const char *blub[] =
		{"blub. blub?", "blub? blub.", "blub. blub.", "blub! blub!",
		"blub! blub.", "blub. blub!", "blub! blub?", "blub? blub!"};

/* Language "fuck fuck" */
static const char *f__k[] =
    {"folk", "sing", "barb", "teas", "cask", "kerb", "able", "bait"};

/* Language "pogaack" */
static const char * pogaack[] =
		{"pogack!", "pogaack!", "pogaaack!", "poock!",
		"pogaaack?", "poock?", "pogack?", "pogaack?"};

/* Language "triplet" */
static const char * trip[] =
    { "OOI", "IOO", "III", "OOO", "OIO", "IOI", "IIO", "OII" };

/* Language "Descriptive BF" */
static const char * nice[] =
    { "right", "left", "up", "down", "out", "in", "begin", "end" };

static const char * bc[] = { "r", "l", "u", "d", "o", "i", "b", "e", "x" };

/* Order should be "there", "once", "was", "a", "fish", "named", "Fred" */
static const char * fish[] =
    { "once", "there", "was", "a", "fish", "dead", "named", "Fred" };

/* Silly (er) ones. */
static const char * dotty[] =
    { "..", "::", ".:.", ".::", ":.::", ":...", ":.:.", ":..:" };

static const char * lisp2[] =
    { "((", "))", "()(", "())", ")())", ")(((", ")()(", ")(()" };

/* Language COW: Not quite as simple as some commands aren't direct replacements. */
static const char * moo[] = {"moO", "mOo", "MoO", "MOo",
		"MMMMOOMooOOOmooMMM", "OOOMoo", "MOOmoOmOo", "MoOMOomoo"};

/* BF Doubler doubles the cell size. */
static const char * doubler[] =
    {">>>>", "<<<<", ">+<+[>-]>[->>+<]<<", ">+<[>-]>[->>-<]<<-",
    ".", ">>>[-]<<<[-],",
    ">+<[>-]>[->+>[<-]<[<]>[-<+>]]<-" "[+<",
    ">+<[>-]>[->+>[<-]<[<]>[-<+>]]<-" "]<",
    ">[-]>[-]<<"};

/* Some random Chinese words */
static const char *chinese[] =
    { "右", "左", "上", "下", "出", "出", "始", "末" };

/* Ρ″ */
static const char *rhoprime[] =
    { "r′λ", "R", "λR", "r′", "Ρ″", "Ιⁿ", "(", ")" };

/* https://github.com/mescam/zerolang */
static const char *zero[] =
    { "0+", "0-", "0++", "0--", "0.", "0?", "0/", "/0" };

static const char ** lang = 0;
static const char ** c = 0;
static int linefix = EOF;
static char langver = -1;
static int col = 0;
static int maxcol = 72;
static int state = 0;
static int c_style = 0;

static int headsecksconv[] = {3, 2, 0, 1, 4, 5, 6, 7 };

static void risbf(int ch);
static void headsecks(int ch, int count);

int
check_arg(const char * arg)
{
    if (strcmp(arg, "-c") == 0) {
	lang = cbyte; langver = 0; c_style = 2; return 1;
    } else
    if (strcmp(arg, "-db") == 0 || strcmp(arg, "-double") == 0) {
	lang = doubler; langver = 3; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-n") == 0 || strcmp(arg, "-nice") == 0) {
	lang = nice; langver = 0; c_style = 1; return 1;
    } else
    if (strcmp(arg, "-mini") == 0) {
	lang = bc; langver = 0; c_style = 1; return 1;
    } else
    if (strcmp(arg, "-f") == 0 || strcmp(arg, "-fish") == 0) {
	lang = fish; langver = 0; c_style = 1; return 1;
    } else
    if (strcmp(arg, "-trip") == 0 || strcmp(arg, "-triplet") == 0) {
	lang = trip; langver = 0; c_style = 1; return 1;
    } else
    if (strcmp(arg, "-ook") == 0) {
	lang = ook; langver = 0; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-blub") == 0) {
	lang = blub; langver = 0; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-moo") == 0) {
	lang = moo; langver = 1; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-fk") == 0) {
	lang = f__k; langver = 0; c_style = 1; return 1;
    } else
    if (strcmp(arg, "-pog") == 0 || strcmp(arg, "-pogaack") == 0) {
	lang = pogaack; langver = 0; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-:") == 0) {
	lang = dotty; langver = 3; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-lisp") == 0) {
	lang = lisp2; langver = 3; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-chi") == 0 || strcmp(arg, "-chinese") == 0) {
	lang = chinese; langver = 1; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-head") == 0) {
	lang = 0; langver = 11; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-rho") == 0 || strcmp(arg, "-rhoprime") == 0) {
	lang = rhoprime; langver = 1; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-zero") == 0) {
	lang = zero; langver = 0; c_style = 0; return 1;
    } else

    if (strcmp(arg, "-risbf") == 0) {
	lang = 0; langver = 10; c_style = 0; return 1;
    } else
    if (strcmp(arg, "-rle") == 0) {
	lang = bc; langver = 2; c_style = 1; return 1;
    } else
    if (strcmp(arg, "-dump") == 0) {
	lang = 0; langver = 4; c_style = 0; return 1;
    } else

    if (strcmp(arg, "-O") == 0 && langver == 4) {
	return 1;
    } else
    if (strncmp(arg, "-w", 2) == 0 && arg[2] >= '0' && arg[2] <= '9') {
	maxcol = atol(arg+2);
	return 1;
    } else
    if (strcmp("-h", arg) ==0) {
	fprintf(stderr, "%s\n",
	"\t"    "-w99    Width to line wrap after, default 72"
	"\n\t"  "-rho    The original 1964 Ρ″ by Corrado Böhm (Rho double prime)"
	"\n\t"  "-double BF to BF translation, cell size doubler."
	"\n\t"  "-c      Plain C"
	"\n\t"  "-rle    Odd RLE C translation"
	"\n\t"  "-nice   Nice memorable C translation."
	"\n\t"  "-mini   Compact C translation."
	"\n\t"  "-double BF to BF translation, cell size doubler."
	"\n\t"  "-fish   There once was a (dead) fish named Fred"
	"\n\t"  "-trip   Triplet like translation"
	"\n\t"  "-ook    Ook!"
	"\n\t"  "-blub   Blub!"
	"\n\t"  "-moo    Cow -- http://www.frank-buss.de/cow.html"
	"\n\t"  "-fk     fuck fuck"
	"\n\t"  "-head   Headsecks."
	"\n\t"  "-:      Dotty"
	"\n\t"  "-lisp   Lisp Zero"
	"\n\t"  "-risbf  RISBF"
	"\n\t"  "-dump   Token dump"
	"\n\t"  "-pog    Pogaack."
	"\n\t"  "-chi    In chinese."
	"\n\t"  "-zero   'zerolang' from mescam on github"
	);
	return 1;
    } else
	return 0;
}

static void
pc(int ch)
{
    if (col>=maxcol && maxcol) {
	if (linefix != EOF) putchar(linefix);
	putchar('\n');
	col = 0;
	if (ch == ' ') ch = 0;
    }
    if (ch) {
	putchar(ch);
	if ((ch&0xC0) != 0x80) /* Count UTF-8 */
	    col++;
    }
}

static void
ps(const char * s)
{
    if (langver != 1) pc(' '); else pc(0);

    while (*s) {
	putchar(*s);
	if ((*s&0xC0) != 0x80) /* Count UTF-8 */
	    col++;
	s++;
    }
}

void
outcmd(int ch, int count)
{
    if (ch == '!') {
	if (langver != 2) {
	    if (bytecell) c = cbyte; else c = cint;
	} else {
	    if (bytecell) c = cbyte_rle; else c = cint_rle;
	}
	if (lang == cbyte) lang = c;
    }

    switch (langver) {
    case 0: case 1: case 2:
	while(count-->0){
	    char * p = strchr(bf,ch);
	    if (!p) continue;
	    ps(lang[p-bf]);
	    if (langver == 2)
		while(count-->0) ps(lang[8]);
	}
	break;

    case 3:
	while(count-->0){
	    char * p = strchr(bf,ch);
	    const char * l;
	    if (!p) continue;
	    l = lang[p-bf];
	    while (*l)
		pc(*l++);
	}
	break;

    case 4: printf("%c %d\n", ch, count); col = 0; break;

    case 10: while (count-->0) risbf(ch); break;
    case 11: headsecks(ch, count); break;
    default: while (count-->0) pc(ch); break;
    }

    if (ch == '~') {
	pc(0);
	if (c_style == 1)
	    col += printf("%s%s", col?" ":"", "_");
	if (c_style == 2)
	    col += printf("%s%s", col?" ":"", "return 0;}");
	if(col) {
	    if (linefix != EOF) putchar(linefix);
	    putchar('\n');
	}
    }

    if (ch == '!' && c_style) {
	int i;
	if (bytecell)
	    printf("#include<unistd.h>\n");
	else
	    printf("#include<stdio.h>\n");
	if (c_style == 1) {
	    for (i=0; i<8 + (langver == 2); i++)
		printf("#define %s %s\n", lang[i], c[i]);
	    printf("#define _ %sreturn 0;}\n", langver==2?";":"");
	}
	if (bytecell)
	    printf("char mem[30000];int main(){register char*m=mem;\n");
	else
	    printf("int mem[30000];int main(){register int*m=mem;\n");
    }
}

static void
risbf(int ch)
{
    switch(ch) {
    case '>':
	if (state!=1) pc('*'); state=1;
	pc('+');
	break;
    case '<':
	if (state!=1) pc('*'); state=1;
	pc('-');
	break;
    case '+':
	if (state!=0) pc('*'); state=0;
	pc('+');
	break;
    case '-':
	if (state!=0) pc('*'); state=0;
	pc('-');
	break;
    case '.': pc('/'); pc('/'); break;
    case ',': pc('/'); pc('*'); break;
    case '[': pc('/'); pc('+'); break;
    case ']': pc('/'); pc('-'); break;
    }
}

static void
headsecks(int ch, int count)
{
    char * p;
    int rset;
    static int rnd = 1;

    if (ch == '!') linefix=';';
    if (! (p = strchr(bf,ch))) return;

    while(count-->0) {
	rnd = rnd * 75 + ch + count;
	rnd = rnd + rnd / 65537;
	rnd &= 0x7FFFFFFF;

	ch = headsecksconv[p-bf];
	rset = rnd % 6;
	if (rset & 1) rset += 8;
	rset /= 2;
	rset *= 8;
	if (ch == 0) rset += 8;
	pc('A' -1 + ch + rset);
    }
}