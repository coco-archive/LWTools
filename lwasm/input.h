/*
input.h

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

#ifndef ___input_h_seen___
#define ___input_h_seen___

#include "lwasm.h"

extern void input_init(asmstate_t *as);
extern void input_openstring(asmstate_t *as, char *s, char *str);
extern void input_open(asmstate_t *as, char *s);
extern char *input_readline(asmstate_t *as);
extern char *input_curspec(asmstate_t *as);
extern FILE *input_open_standalone(asmstate_t *as, char *s);

#endif
