/*
compiler.c

Copyright © 2011 William Astle

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

/*
This is the actual compiler bit; it drives the parser and code generation
*/

#include <stdio.h>

#include <lw_alloc.h>
#include <lw_string.h>

#include "lwbasic.h"
#include "symtab.h"


/* size of a type */
static int sizeof_type(int type)
{
	/* everything is an "int" right now; 2 bytes */
	return 2;
}

/* parse a type; the next token will be acquired as a result */
/* the token advancement is to provide consistency */
static int parse_type(cstate *state)
{
	int pt = -1;
	
	switch (state -> lexer_token)
	{
	case token_kw_integer:
		pt = 1;
		break;

	default:
		lwb_error("Invalid type specification");
	}
	lexer(state);
	/* look for "unsigned" modifier for integer types */
	return pt;
}

static void parse_decls(cstate *state)
{
	/* declarations */
	/* the first thing that doesn't look like a declaration is assumed */
	/* to be a statement and will trigger a bailout */
	int vt;
	char *vn;
	symtab_entry_t *se;
	
	for (;;)
	{
		switch (state -> lexer_token)
		{
		/* DIM keyword */
		case token_kw_dim:
			lexer(state);
			if (state -> lexer_token != token_identifier)
			{
				lwb_error("Expecting identifier, got %s\n", lexer_return_token(state));
			}
			vn = lw_strdup(state -> lexer_token_string);
			lexer(state);
			if (state -> lexer_token != token_kw_as)
			{
				lwb_error("Expecting AS, got %s\n", lexer_return_token(state));
			}
			lexer(state);
			vt = parse_type(state);
			
			se = symtab_find(state -> local_syms, vn);
			if (se)
			{
				lwb_error("Multiply defined local variable %s", vn);
			}
			state -> framesize += sizeof_type(vt);
			symtab_register(state -> local_syms, vn, -(state -> framesize), symtype_var, NULL);
			
			lw_free(vn);
			break;
		default:
			return;
		}
		if (state -> lexer_token != token_eol)
			lwb_error("Expecting end of line; got %s", lexer_return_token(state));
		lexer(state);
	}
}


/* issub means RETURNS is not allowed; !issub means RETURNS is required */

static void parse_subfunc(cstate *state, int issub)
{
	int pt, rt;
	char *subname, *pn;
	int vis = 0;
	symtab_entry_t *se;
	int paramsize = 0;
	
	state -> local_syms = symtab_init();
	state -> framesize = 0;
	
	lexer(state);
	if (state -> lexer_token != token_identifier)
	{
		lwb_error("Invalid sub name '%s'", state -> lexer_token_string);
	}
	
	subname = lw_strdup(state -> lexer_token_string);
	
	lexer(state);
	if (state -> lexer_token == token_kw_public || state -> lexer_token == token_kw_private)
	{
		if (state -> lexer_token == token_kw_public)
			vis = 1;
		lexer(state);
	}

	/* ignore the "PARAMS" keyword if present */
	if (state -> lexer_token == token_kw_params)
		lexer(state);
	
	if (state -> lexer_token == token_eol || state -> lexer_token == token_kw_returns)
		goto noparms;

paramagain:
	if (state -> lexer_token != token_identifier)
	{
		lwb_error("Parameter name expected, got %s\n", lexer_return_token(state));
	}
	pn = lw_strdup(state -> lexer_token_string);
	lexer(state);
	
	if (state -> lexer_token != token_kw_as)
		lwb_error("Expecting AS\n");
	lexer(state);
	
	pt = parse_type(state);

	se = symtab_find(state -> local_syms, pn);
	if (se)
	{
		lwb_error("Duplicate parameter name %s\n", pn);
	}
	symtab_register(state -> local_syms, pn, paramsize, symtype_param, NULL);
	paramsize += sizeof_type(pt);
	lw_free(pn);
	
	if (state -> lexer_token == token_char && state -> lexer_token_string[0] == ',')
	{
		lexer(state);
		goto paramagain;
	}

noparms:
	rt = -1;
	if (!issub)
	{
		if (state -> lexer_token != token_kw_returns)
		{
			lwb_error("FUNCTION must have RETURNS\n");
		}
		lexer(state);
/*		if (state -> lexer_token == token_identifier)
		{
			printf("Return value named: %s\n", state -> lexer_token_string);
			
			lexer(state);
			if (state -> lexer_token != token_kw_as)
				lwb_error("Execting AS after RETURNS");
			lexer(state);
		}
*/
		rt = parse_type(state);
	}
	else
	{
		if (state -> lexer_token == token_kw_returns)
		{
			lwb_error("SUB cannot specify RETURNS\n");
		}
	}

	
	if (state -> lexer_token != token_eol)
	{
		lwb_error("EOL expected; found %s\n", lexer_return_token(state));
	}

	
	se = symtab_find(state -> global_syms, subname);
	if (se)
	{
		lwb_error("Multiply defined symbol %s\n", subname);
	}

	symtab_register(state -> global_syms, subname, -1, issub ? symtype_sub : symtype_func, NULL);

	state -> currentsub = subname;
	state -> returntype = rt;
	/* consume EOL */
	lexer(state);
	
	/* variable declarations */
	parse_decls(state);
	
	/* output function/sub prolog */
	emit_prolog(state, vis);
	
	/* parse statement block  */
	/* parse_statements(state); */
	
	if (issub)
	{
		if (state -> lexer_token != token_kw_endsub)
		{
			lwb_error("Expecting ENDSUB, got %s\n", lexer_return_token(state));
		}
	}
	else
	{
		if (state -> lexer_token != token_kw_endfunction)
		{
			lwb_error("Expecting ENDFUNCTION, got %s\n", lexer_return_token(state));
		}
	}
	/* output function/sub epilog */
	emit_epilog(state);
	
	lw_free(state -> currentsub);
	state -> currentsub = NULL;
	symtab_destroy(state -> local_syms);
	state -> local_syms = NULL;
}

void parser(cstate *state)
{
	state -> lexer_curchar = -1;
	state -> global_syms = symtab_init();
		
	/* now look for a global declaration */
	for (;;)
	{
		state -> parser_state = parser_state_global;
		lexer(state);
		switch (state -> lexer_token)
		{
		case token_kw_function:
			printf("Function\n");
			parse_subfunc(state, 0);
			break;
			
		case token_kw_sub:
			printf("Sub\n");
			parse_subfunc(state, 1);
			break;

		/* blank lines are allowed */
		case token_eol:
			continue;
		
		/* EOF is allowed - end of parsing */
		case token_eof:
			return;

		default:
			lwb_error("Invalid token '%s' in global state\n", lexer_return_token(state));
		}
	}	
}
