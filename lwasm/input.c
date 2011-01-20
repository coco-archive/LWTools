/*
input.c

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

/*
This file is used to handle reading input files. It serves to encapsulate
the entire input system to make porting to different media and systems
less difficult.
*/

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <lw_alloc.h>
#include <lw_stringlist.h>
#include <lw_string.h>
#include "lwasm.h"

/*
Data type for storing input buffers
*/

enum input_types_e
{
	input_type_file,			// regular file, no search path
	input_type_include,			// include path, start from "local"
	input_type_string,			// input from a string

	input_type_error			// invalid input type
};

struct input_stack
{
	struct input_stack *next;
	int type;
	void *data;
	int data2;
	char *filespec;
};

#define IS	((struct input_stack *)(as -> input_data))

void input_init(asmstate_t *as)
{
	struct input_stack *t;

	if (as -> file_dir)
		lw_stack_destroy(as -> file_dir);
	as -> file_dir = lw_stack_create(lw_free);
	as -> includelist = lw_stack_create(lw_free);
	lw_stringlist_reset(as -> input_files);
	while (IS)
	{
		t = IS;
		as -> input_data = IS -> next;
		lw_free(t);
	}
}

void input_pushpath(asmstate_t *as, char *fn)
{
	/* take apart fn into path and filename then push the path */
	/* onto the current file path stack */
	
	/* also add it to the list of files included */
	char *dn, *dp;
	int o;

	dn = lw_strdup(fn);
	lw_stack_push(as -> includelist, dn);

	dn = lw_strdup(fn);
	dp = dn + strlen(dn);
	
	while (--dp != dn)
	{
		if (*dp == '/')
			break;
	}
	if (*dp == '/')
		*dp = '\0';
	
	if (dp == dn)
	{
		lw_free(dn);
		dn = lw_strdup(".");
		lw_stack_push(as -> file_dir, dn);
		return;
	}
	dp = lw_strdup(dn);
	lw_free(dn);
	lw_stack_push(as -> file_dir, dp);
}

void input_openstring(asmstate_t *as, char *s, char *str)
{
	struct input_stack *t;
	
	t = lw_alloc(sizeof(struct input_stack));
	t -> filespec = lw_strdup(s);

	t -> type = input_type_string;
	t -> data = lw_strdup(str);
	t -> data2 = 0;
	t -> next = IS;
	as -> input_data = t;
	t -> filespec = lw_strdup(s);
}

void input_open(asmstate_t *as, char *s)
{
	struct input_stack *t;
	char *s2;
	char *p, *p2;

	t = lw_alloc(sizeof(struct input_stack));
	t -> filespec = lw_strdup(s);

	for (s2 = s; *s2 && (*s2 != ':'); s2++)
		/* do nothing */ ;
	if (!*s2)
	{
		t -> type = input_type_file;
	}
	else
	{
		char *ts;
		
		ts = lw_strndup(s, s2 - s);
		s = s2 + 1;
		if (!strcmp(ts, "include"))
			t -> type = input_type_include;
		else if (!strcmp(ts, "file"))
			t -> type = input_type_file;
		else
			t -> type = input_type_error;
		
		lw_free(ts);
	}
	t -> next = as -> input_data;
	as -> input_data = t;
	
	switch (IS -> type)
	{
	case input_type_include:
		/* first check for absolute path and if so, skip path */
		if (*s == '/')
		{
			/* absolute path */
			IS -> data = fopen(s, "rb");
			debug_message(as, 1, "Opening (abs) %s", s);
			if (!IS -> data)
			{
				lw_error("Cannot open file '%s': %s", s, strerror(errno));
			}
			input_pushpath(as, s);
			return;
		}
		
		/* relative path, check relative to "current file" directory */
		p = lw_stack_top(as -> file_dir);
		0 == asprintf(&p2, "%s/%s", p, s);
		debug_message(as, 1, "Open: (cd) %s\n", p2);
		IS -> data = fopen(p2, "rb");
		if (IS -> data)
		{
			input_pushpath(as, p2);
			lw_free(p2);
			return;
		}
		debug_message(as, 2, "Failed to open: (cd) %s (%s)\n", p2, strerror(errno));
		lw_free(p2);

		/* now check relative to entries in the search path */
		lw_stringlist_reset(as -> include_list);
		while (p = lw_stringlist_current(as -> include_list))
		{
			0 == asprintf(&p2, "%s/%s", p, s);
		debug_message(as, 1, "Open (sp): %s\n", p2);
			IS -> data = fopen(p2, "rb");
			if (IS -> data)
			{
				input_pushpath(as, p2);
				lw_free(p2);
				return;
			}
		debug_message(as, 2, "Failed to open: (sp) %s (%s)\n", p2, strerror(errno));
			lw_free(p2);
			lw_stringlist_next(as -> include_list);
		}
		lw_error("Cannot open include file '%s': %s", s, strerror(errno));
		break;
		
	case input_type_file:
		debug_message(as, 1, "Opening (reg): %s\n", s);
		IS -> data = fopen(s, "rb");

		if (!IS -> data)
		{
			lw_error("Cannot open file '%s': %s", s, strerror(errno));
		}
		input_pushpath(as, s);
		return;
	}

	lw_error("Cannot figure out how to open '%s'.", t -> filespec);
}

FILE *input_open_standalone(asmstate_t *as, char *s)
{
	char *s2;
	FILE *fp;
	char *p, *p2;

	/* first check for absolute path and if so, skip path */
	if (*s == '/')
	{
		/* absolute path */
		debug_message(as, 2, "Open file (st abs) %s", s);
		fp = fopen(s, "rb");
		if (!fp)
		{
			return NULL;
		}
		return fp;
	}

	/* relative path, check relative to "current file" directory */
	p = lw_stack_top(as -> file_dir);
	0 == asprintf(&p2, "%s/%s", p, s);
	debug_message(as, 2, "Open file (st cd) %s", p2);
	fp = fopen(p2, "rb");
	if (fp)
	{
		lw_free(p2);
		return fp;
	}
	lw_free(p2);

	/* now check relative to entries in the search path */
	lw_stringlist_reset(as -> include_list);
	while (p = lw_stringlist_current(as -> include_list))
	{
		0 == asprintf(&p2, "%s/%s", p, s);
		debug_message(as, 2, "Open file (st ip) %s", p2);
		fp = fopen(p2, "rb");
		if (fp)
		{
			lw_free(p2);
			return fp;
		}
		lw_free(p2);
		lw_stringlist_next(as -> include_list);
	}
	
	return NULL;
}

char *input_readline(asmstate_t *as)
{
	char *s;
	char linebuff[2049];
	int lbloc;
	int eol = 0;
	
	/* if no file is open, open one */
nextfile:
	if (!IS) {
		s = lw_stringlist_current(as -> input_files);
		if (!s)
			return NULL;
		lw_stringlist_next(as -> input_files);
		input_open(as, s);
	}
	
	switch (IS -> type)
	{
	case input_type_file:
	case input_type_include:
		/* read from a file */
		lbloc = 0;
		for (;;)
		{
			int c, c2;
			c = fgetc(IS -> data);
			if (c == EOF)
			{
				if (lbloc == 0)
				{
					struct input_stack *t;
					fclose(IS -> data);
					lw_free(lw_stack_pop(as -> file_dir));
					lw_free(IS -> filespec);
					t = IS -> next;
					lw_free(IS);
					as -> input_data = t;
					goto nextfile;
				}
				linebuff[lbloc] = '\0';
				eol = 1;
			}
			else if (c == '\r')
			{
				linebuff[lbloc] = '\0';
				eol = 1;
				c2 = fgetc(IS -> data);
				if (c2 == EOF)
					c = EOF;  
				else if (c2 != '\n')
					ungetc(c2, IS -> data);  
			}
			else if (c == '\n')
			{
				linebuff[lbloc] = '\0';
				eol = 1;
				c2 = fgetc(IS -> data);
				if (c2 == EOF)
					c = EOF;  
				else if (c2 != '\r')
					ungetc(c2, IS -> data);  
			}
			else
			{
				if (lbloc < 2048)
					linebuff[lbloc++] = c;
			}
			if (eol)
			{
				s = lw_strdup(linebuff);
				return s;
			}
		}

	case input_type_string:
		/* read from a string */
		if (((char *)(IS -> data))[IS -> data2] == '\0')
		{
			struct input_stack *t;
			lw_free(IS -> data);
			lw_free(IS -> filespec);
			t = IS -> next;
			lw_free(IS);
			as -> input_data = t;
			goto nextfile;
		}
		s = (char *)(IS -> data);
		lbloc = 0;
		for (;;)
		{
			int c;
			c = s[IS -> data2];
			if (c)
				IS -> data2++;
			if (c == '\0')
			{
				linebuff[lbloc] = '\0';
				eol = 1;
			}
			else if (c == '\r')
			{
				linebuff[lbloc] = '\0';
				eol = 1;
				if (s[IS -> data2] == '\n')
					IS -> data2++;
			}
			else if (c == '\n')
			{
				linebuff[lbloc] = '\0';
				eol = 1;
				if (s[IS -> data2] == '\r')
					IS -> data2++;
			}
			else
			{
				if (lbloc < 2048)
					linebuff[lbloc++] = c;
			}
			if (eol)
			{
				s = lw_strdup(linebuff);
				return s;
			}
		}
	
	default:
		lw_error("Problem reading from unknown input type");
	}
}

char *input_curspec(asmstate_t *as)
{
	if (IS)
		return IS -> filespec;
	return NULL;
}
