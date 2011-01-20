/*
pass4.c

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

#include <lw_alloc.h>
#include <lw_string.h>

#include "lwasm.h"
#include "instab.h"

/*
Resolve2 Pass

Force resolution of instruction sizes.

*/
void do_pass4_aux(asmstate_t *as, int force)
{
	int rc;
	int cnt;
	line_t *cl, *sl;
	struct line_expr_s *le;

	// first, count the number of unresolved instructions
	for (cnt = 0, cl = as -> line_head; cl; cl = cl -> next)
	{
		if (cl -> len == -1)
			cnt++;
	}

	sl = as -> line_head;
	while (cnt > 0)
	{
		// find an unresolved instruction
		for ( ; sl && sl -> len != -1; sl = sl -> next)
		{
			as -> cl = sl;
			lwasm_reduce_expr(as, sl -> addr);
		
			// simplify each expression
			for (le = sl -> exprs; le; le = le -> next)
				lwasm_reduce_expr(as, le -> expr);
		}
				
		// simplify address
		as -> cl = sl;
		lwasm_reduce_expr(as, sl -> addr);
		
		// simplify each expression
		for (le = sl -> exprs; le; le = le -> next)
			lwasm_reduce_expr(as, le -> expr);


		if (sl -> len == -1 && sl -> insn >= 0 && instab[sl -> insn].resolve)
		{
			(instab[sl -> insn].resolve)(as, sl, 1);
			if (force && sl -> len == -1)
			{
				lwasm_register_error(as, sl, "Instruction failed to resolve.");
				return;
			}
		}
		cnt--;
		if (cnt == 0)
			return;

		do
		{
			rc = 0;
			for (cl = sl; cl; cl = cl -> next)
			{
				as -> cl = cl;
			
				// simplify address
				lwasm_reduce_expr(as, cl -> addr);
		
				// simplify each expression
				for (le = cl -> exprs; le; le = le -> next)
					lwasm_reduce_expr(as, le -> expr);
			
				if (cl -> len == -1)
				{
					// try resolving the instruction length
					// but don't force resolution
					if (cl -> insn >= 0 && instab[cl -> insn].resolve)
					{
						(instab[cl -> insn].resolve)(as, cl, 0);
						if (cl -> len != -1)
						{
							rc++;
							cnt--;
							if (cnt == 0)
								return;
						}
					}
				}
			}
			if (as -> errorcount > 0)
				return;
		} while (rc > 0);
	}
}

void do_pass4(asmstate_t *as)
{
	do_pass4_aux(as, 1);
}
