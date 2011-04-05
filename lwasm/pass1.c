/*
pass1.c

Copyright © 2010 William Astle

This file is part of LWTOOLS.

LWTOOLS is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <lw_alloc.h>
#include <lw_string.h>

#include "lwasm.h"
#include "instab.h"
#include "input.h"

extern int expand_macro(asmstate_t *as, line_t *l, char **p, char *opc);
extern int expand_struct(asmstate_t *as, line_t *l, char **p, char *opc);
extern int add_macro_line(asmstate_t *as, char *optr);

/*
pass 1: parse the lines

line format:

[<symbol>] <opcode> <operand>[ <comment>]

If <symbol> is followed by a :, whitespace may precede the symbol

A line may optionally start with a number which must not be preceded by
white space and must be followed by a single whitespace character. After
that whitespace character, the line is parsed as if it had no line number.

*/
void do_pass1(asmstate_t *as)
{
	char *line;
	line_t *cl;
	char *p1;
	int stspace;
	char *tok, *sym;
	int opnum;
	int lc = 1;
	for (;;)
	{
		sym = NULL;
		line = input_readline(as);
		if (!line)
			break;
		if (line[0] == 1 && line[1] == 1)
		{
			// special internal directive
			// these DO NOT appear in the output anywhere
			// they are generated by the parser to pass information
			// forward
			for (p1 = line + 2; *p1 && !isspace(*p1); p1++)
				/* do nothing */ ;
			*p1++ = '\0';
			if (!strcmp(line + 2, "SETCONTEXT"))
			{
				as -> context = strtol(p1, NULL, 10);
			}
			else if (!strcmp(line + 2, "SETLINENO"))
			{
				lc = strtol(p1, NULL, 10);
			}
			else if (!strcmp(line + 2, "SETNOEXPANDSTART"))
			{
				as -> line_tail -> noexpand_start = 1;
			}
			else if (!strcmp(line + 2, "SETNOEXPANDEND"))
			{
				as -> line_tail -> noexpand_end = 1;
			}
			lw_free(line);
			if (lc == 0)
				lc = 1;
			continue;
		}
		debug_message(as, 75, "Read line: %s", line);
		
		cl = lw_alloc(sizeof(line_t));
		memset(cl, 0, sizeof(line_t));
		cl -> outputl = -1;
		cl -> linespec = lw_strdup(input_curspec(as));
		cl -> prev = as -> line_tail;
		cl -> insn = -1;
		cl -> as = as;
		cl -> inmod = as -> inmod;
		cl -> csect = as -> csect;
		cl -> pragmas = as -> pragmas;
		cl -> context = as -> context;
		cl -> ltext = lw_strdup(line);
		cl -> soff = -1;
		cl -> dshow = -1;
		cl -> dsize = 0;
		cl -> dptr = NULL;
		cl -> isbrpt = 0;
		as -> cl = cl;
		if (!as -> line_tail)
		{
			as -> line_head = cl;
			cl -> addr = lw_expr_build(lw_expr_type_int, 0);
		}
		else
		{
			lw_expr_t te;

			cl -> lineno = as -> line_tail -> lineno + 1;
			as -> line_tail -> next = cl;

			// set the line address
			te = lw_expr_build(lw_expr_type_special, lwasm_expr_linelen, cl -> prev);
			cl -> addr = lw_expr_build(lw_expr_type_oper, lw_expr_oper_plus, cl -> prev -> addr, te);
			lw_expr_destroy(te);
			lwasm_reduce_expr(as, cl -> addr);
//			lw_expr_simplify(cl -> addr, as);

			// carry DP value forward
			cl -> dpval = cl -> prev -> dpval;
			
		}
		if (!lc && strcmp(cl -> linespec, cl -> prev -> linespec))
			lc = 1;
		if (lc)
		{
			cl -> lineno = lc;
			lc = 0;
		}
		as -> line_tail = cl;
		// blank lines don't count for anything
		// except a local symbol context break
		if (!*line)
		{
			as -> context = lwasm_next_context(as);
			goto nextline;
		}
	
		// skip comments
		// commends do not create a context break
		if (*line == '*' || *line == ';' || *line == '#')
			goto nextline;

		p1 = line;
		if (isdigit(*p1))
		{
			// skip line number
			while (*p1 && isdigit(*p1))
				p1++;
			if (!*p1 && !isspace(*p1))
				p1 = line;
			else if (*p1 && !isspace(*p1))
				p1 = line;
			else if (*p1 && isspace(*p1))
				p1++;
		}

		// blank line - context break
		if (!*p1)
		{
			as -> context = lwasm_next_context(as);
			goto nextline;
		}

		// comment - no context break
		if (*p1 == '*' || *p1 == ';' || *p1 == '#')
			goto nextline;

		if (isspace(*p1))
		{
			for (; *p1 && isspace(*p1); p1++)
				/* do nothing */ ;
			stspace = 1;
		}
		else
			stspace = 0;

		if (*p1 == '*' || *p1 == ';' || *p1 == '#')
			goto nextline;
		if (!*p1)
		{
			// nothing but whitespace - context break
			as -> context = lwasm_next_context(as);
			goto nextline;
		}

		// find the end of the first token
		for (tok = p1; *p1 && !isspace(*p1) && *p1 != ':' && *p1 != '='; p1++)
			/* do nothing */ ;
		
		if (*p1 == ':' || *p1 == '=' || stspace == 0)
		{
			// have a symbol here
			sym = lw_strndup(tok, p1 - tok);
			if (*p1 == ':')
				p1++;
			for (; *p1 && isspace(*p1); p1++)
				/* do nothing */ ;
			
			if (*p1 == '=')
			{
				tok = p1++;
			}
			else
			{
				for (tok = p1; *p1 && !isspace(*p1); p1++)
					/* do nothing */ ;
			}
		}
		if (sym && strcmp(sym, "!") == 0)
			cl -> isbrpt = 1;
		else if (sym)
			cl -> sym = lw_strdup(sym);
		cl -> symset = 0;
		
		// tok points to the opcode for the line or NUL if none
		if (*tok)
		{
			// look up operation code
			sym = lw_strndup(tok, p1 - tok);
			for (; *p1 && isspace(*p1); p1++)
				/* do nothing */ ;

			for (opnum = 0; instab[opnum].opcode; opnum++)
			{
				if (!strcasecmp(instab[opnum].opcode, sym))
					break;
			}
			
			// p1 points to the start of the operand
			
			// if we're inside a macro definition and not at ENDM,
			// add the line to the macro definition and continue
			if (as -> inmacro && !(instab[opnum].flags & lwasm_insn_endm))
			{
				add_macro_line(as, line);
				goto linedone;
			}
			
			// if skipping a condition and the operation code doesn't
			// operate within a condition (not a conditional)
			// do nothing
			if (as -> skipcond && !(instab[opnum].flags & lwasm_insn_cond))
				goto linedone;
        	
			if (instab[opnum].opcode == NULL)
			{
				cl -> insn = -1;
				if (*tok != ';' && *tok != '*')
				{
					// bad opcode; check for macro here
					if (expand_macro(as, cl, &p1, sym) != 0)
					{
						// macro expansion failed
						if (expand_struct(as, cl, &p1, sym) != 0)
						{
							// structure expansion failed
							lwasm_register_error(as, cl, "Bad opcode");
						}
					}
				}
			}
			else
			{
				cl -> insn = opnum;
				// no parse func means operand doesn't matter
				if (instab[opnum].parse)
				{
					if (as -> instruct == 0 || instab[opnum].flags & lwasm_insn_struct)
					{
						struct line_expr_s *le;

						cl -> len = -1;
						// call parse function
						(instab[opnum].parse)(as, cl, &p1);
					
						if (*p1 && !isspace(*p1))
						{
							// flag bad operand error
							lwasm_register_error(as, cl, "Bad operand (%s)", p1);
						}
						
						/* do a reduction on the line expressions to avoid carrying excessive expression baggage if not needed */
						as -> cl = cl;
		
						// simplify address
						lwasm_reduce_expr(as, cl -> addr);
		
						// simplify each expression
						for (le = cl -> exprs; le; le = le -> next)
							lwasm_reduce_expr(as, le -> expr);
						
						/* try resolving the instruction as well */
						if (cl -> insn >= 0 && instab[cl -> insn].resolve)
						{
							(instab[cl -> insn].resolve)(as, cl, 0);
						}

					}
					else if (as -> instruct == 1)
					{
						lwasm_register_error(as, cl, "Bad operand (%s)", p1);
					}
				}
			}
		}
	
	linedone:
		lw_free(sym);
		
		if (!as -> skipcond && !as -> inmacro)
		{
			if (cl -> sym && cl -> symset == 0)
			{
				debug_message(as, 50, "Register symbol %s: %s", cl -> sym, lw_expr_print(cl -> addr));
	
				// register symbol at line address
				if (!register_symbol(as, cl, cl -> sym, cl -> addr, symbol_flag_none))
				{
					// symbol error
					// lwasm_register_error(as, cl, "Bad symbol '%s'", cl -> sym);
				}
			}
			debug_message(as, 40, "Line address: %s", lw_expr_print(cl -> addr));
		}
		
	nextline:
		lw_free(line);
		
		// if we've hit the "end" bit, finish out
		if (as -> endseen)
			return;
	}
}
