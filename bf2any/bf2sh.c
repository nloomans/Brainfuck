#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf2any.h"

/*
 * Bourne Shell (dash) translation from BF, runs at about 170,000 instructions per second.
 *
 * The pseudo RLE is required so that dash doesn't run out of memory on the
 * Lost Kingdom program. (Yes it does work ... but slowly! )
 *
 * NB: Dash needs an MAXPRLE of 2, bash needs 22.
 *
 * It seems that shells older than dash don't implement ${var#?} this is a
 * significant problem for '>' and '<' and probably an insoluble problem
 * for ',' without using external programs.
 *
 * The 'printf' command in the ',' command could be replaced by a "case".
 *
 * If external programs are allowed it becomes a lot simpler.
 */

#define MAXPRLE	2

int do_input = 0;
int do_output = 0;
int ind = 0;

int
check_arg(char * arg)
{
    if (strcmp(arg, "-b") == 0) return 1;
    return 0;
}

void
outcmd(int ch, int count)
{
    int i;
    while (count>MAXPRLE) { outcmd(ch, MAXPRLE); count -= MAXPRLE; }

    if (count > 1) {
	switch(ch) {
	case '+': printf("u%d\n", count); break;
	case '-': printf("d%d\n", count); break;
	case '>': printf("r%d\n", count); break;
	case '<': printf("l%d\n", count); break;
	}
	return;
    }

    switch(ch) {
    case '=': printf("z\n"); break;
    case '+': printf("u\n"); break;
    case '-': printf("d\n"); break;
    case '>': printf("r\n"); break;
    case '<': printf("l\n"); break;
    case '[':
	printf("f\n");
	printf("while [ \"$A\" != 0 ] ; do\n");
	ind++;
	break;
    case ']':
	printf("f\n");
	ind--; printf("done\n");
	break;
    case '.': printf("o\n"); do_output = 1; break;
    case ',': printf("i\n"); do_input = 1; break;
    }

    switch(ch) {
    case '!': printf("#!/bin/sh\nbf(){\n"); break;
    case '~':
	printf("}\n");

	if (MAXPRLE>1) {
	    for (i=3; i<=MAXPRLE; i++) {
		printf("u%d() { u ; u%d; }\n", i, i-1);
		printf("d%d() { d ; d%d; }\n", i, i-1);
		printf("l%d() { l ; l%d; }\n", i, i-1);
		printf("r%d() { r ; r%d; }\n", i, i-1);
	    }

	    printf("u2() { u ; u; }\n");
	    printf("d2() { d ; d; }\n");
	    printf("l2() { l ; l; }\n");
	    printf("r2() { r ; r; }\n");
	}

	printf("u() { eval \"A=\\$M$P\"; inc; eval \"M$P=$A\" ; }\n");
	printf("d() { eval \"A=\\$M$P\"; dec; eval \"M$P=$A\" ; }\n");
	printf("f() { eval \"A=\\$M$P\"; [ .$A = . ] && A=0; }\n");
	printf("z() { eval \"M$P=0\" ; }\n");

	printf("inc() {\n");
	printf("case \"$A\" in\n");
	for(i=0; i<256; i++)
	    printf("%d ) A=%d ;;\n", i, ((i+1) & 0xFF));
	printf("* ) A=1 ;;\n");
	printf("esac\n");
	printf("}\n");

	printf("dec() {\n");
	printf("case \"$A\" in\n");
	for(i=0; i<256; i++)
	    printf("%d ) A=%d ;;\n", i, ((i-1) & 0xFF));
	printf("* ) A=255 ;;\n");
	printf("esac\n");
	printf("}\n");

	printf("%s\n",

"\n"	    "r(){"
"\n"	    "B="
"\n"	    "while [ ${#P} -gt 0 ]"
"\n"	    "do"
"\n"	    "    C=$P"
"\n"	    "    P=${P#?}"
"\n"	    "    case \"$C\" in"
"\n"	    "    9* ) B=${B}0 ;;"
"\n"	    "    1* ) B=${B}2 ; break ;;"
"\n"	    "    2* ) B=${B}3 ; break ;;"
"\n"	    "    3* ) B=${B}4 ; break ;;"
"\n"	    "    4* ) B=${B}5 ; break ;;"
"\n"	    "    5* ) B=${B}6 ; break ;;"
"\n"	    "    6* ) B=${B}7 ; break ;;"
"\n"	    "    7* ) B=${B}8 ; break ;;"
"\n"	    "    8* ) B=${B}9 ; break ;;"
"\n"	    "    * ) B=${B}1 ; break ;;"
"\n"	    "    esac"
"\n"	    "done"
"\n"	    "P=$B$P"
"\n"	    "}"
"\n"	    ""
"\n"	    "l(){"
"\n"	    "B="
"\n"	    "while [ ${#P} -gt 0 ]"
"\n"	    "do"
"\n"	    "    C=$P"
"\n"	    "    P=${P#?}"
"\n"	    "    case \"$C\" in"
"\n"	    "    0* ) B=${B}9 ;;"
"\n"	    "    1* ) B=${B}0 ; break ;;"
"\n"	    "    2* ) B=${B}1 ; break ;;"
"\n"	    "    3* ) B=${B}2 ; break ;;"
"\n"	    "    4* ) B=${B}3 ; break ;;"
"\n"	    "    5* ) B=${B}4 ; break ;;"
"\n"	    "    6* ) B=${B}5 ; break ;;"
"\n"	    "    7* ) B=${B}6 ; break ;;"
"\n"	    "    8* ) B=${B}7 ; break ;;"
"\n"	    "    9* ) B=${B}8 ; break ;;"
"\n"	    "    esac"
"\n"	    "done"
"\n"	    "P=$B$P"
"\n"	    "}"
	    );

	if(do_output) {
	    printf("\n");
	    printf("o() {\n");
	    printf("eval \"A=\\$M$P\"\n");
	    printf("case \"$A\" in\n");
	    for(i=0; i<256; i++) {
		if (i >= ' ' && i <= '~' && i != '\'' && i != '\\')
		    printf("%d ) echo -n '%c' ;;\n", i, i);
		else if (i == 10 )
		    printf("%d ) echo ;;\n", i);
		else
		    printf("%d ) echoe '\\%03o' ;;\n", i, i);
	    }
	    printf("esac\n}\n");
	    printf("%s\n",
"\n"		"if [ .`echo -n` = .-n ]"
"\n"		"then"
"\n"		"    echon() { echo \"$1\\c\"; }"
"\n"		"    echoe() { echo \"$1\\c\"; }"
"\n"		"else"
"\n"		"    echon() { echo -n \"$1\"; }"
"\n"		"    if [ .`echo -e` = .-e ]"
"\n"		"    then echoe() { echo -n \"$1\"; }"
"\n"		"    else echoe() { echo -n -e \"$1\"; }"
"\n"		"    fi"
"\n"		"fi"
		);
	}

	if (do_input) {
	    printf("%s\n",
		"\n"	"i() {"
		"\n"	"    [ \"$goteof\" = \"y\" ] && return;"
		"\n"	"    [ \"$gotline\" != \"y\" ] && {"
		"\n"	"        if read -r line"
		"\n"	"        then"
		"\n"	"            gotline=y"
		"\n"	"        else"
		"\n"	"            goteof=y"
		"\n"	"            return"
		"\n"	"        fi"
		"\n"	"    }"
		"\n"	"    [ \"$line\" = \"\" ] && {"
		"\n"	"        gotline=n"
		"\n"	"        eval \"M$P=10\""
		"\n"	"        return"
		"\n"	"    }"
		"\n"	"    A=\"$line\""
		"\n"	"    while [ ${#A} -gt 1 ] ; do A=\"${A%?}\"; done"
		"\n"	"    line=\"${line#?}\""
		"\n"	"# This printf command is probably not portable"
		"\n"	"    A=`printf %d \\'\"$A\"`"
		"\n"	"    eval \"M$P=$A\""
		"\n"	"}"
	    );
	}

	printf("\nP=00000\nbf\n");
	break;
    }
}

#if 0
/* Simple filter to find [-] sequences */
void
outcmd(int ch, int count)
{
static int zstate = 0;

    switch(zstate)
    {
    case 1:
	if (count == 1 && ch == '-') { zstate++; return; }
	outcmd2('[', 1);
	break;
    case 2:
	if (count == 1 && ch == ']') { zstate=0; outcmd2('=', 1); return; }
	outcmd2('[', 1);
	outcmd2('-', 1);
	break;
    }
    zstate=0;
    if (count == 1 && ch == '[') { zstate++; return; }
    outcmd2(ch, count);
}
#endif

#if 0

 RLE on the +/- commands is possible (below) but it takes bash 15 seconds to
 create the addition table. Running 65536 direct assignments is quicker,
 but still very slow.

p=255
for i in \
0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 \
27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 \
51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 \
75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 96 97 98 \
99 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 \
117 118 119 120 121 122 123 124 125 126 127 128 129 130 131 132 133 134 \
135 136 137 138 139 140 141 142 143 144 145 146 147 148 149 150 151 152 \
153 154 155 156 157 158 159 160 161 162 163 164 165 166 167 168 169 170 \
171 172 173 174 175 176 177 178 179 180 181 182 183 184 185 186 187 188 \
189 190 191 192 193 194 195 196 197 198 199 200 201 202 203 204 205 206 \
207 208 209 210 211 212 213 214 215 216 217 218 219 220 221 222 223 224 \
225 226 227 228 229 230 231 232 233 234 235 236 237 238 239 240 241 242 \
243 244 245 246 247 248 249 250 251 252 253 254 255
do eval "inc$p=$i" ; p=$i
done

sum=0
a=0
b=0
while :
do  while :
    do  eval "sum${a}_${b}=$sum"
	eval "sum=\$inc$sum"
	eval "a=\$inc$a"
	[ "$a" = 0 ] && break
    done
    eval "sum=\$inc$sum"
    eval "b=\$inc$b"
    [ "$b" = 0 ] && break
done

echo $sum15_250
#endif