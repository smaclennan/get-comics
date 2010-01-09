/*
 * getopt.h - implementation of getopt for incomplete systems
 * Copyright (C) 2003 Sean MacLennan <seanm@seanm.ca>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this project; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _GETOPT_H
#define _GETOPT_H

#include <string.h>

char *optarg = NULL;
int   optind = 0;
int   opterr = 0;

/*
 * I don't like defining functions in include files, but this is
 * usually only used in one file and it makes the porting easier.
 */
static int getopt(int argc, char * const argv[], const char *optstring)
{
	static int index;
	char f, *p;

	if(optind == 0) { optind = 1; index = 1; }
	optarg = NULL;

	if(optind >= argc) return -1;

	if(*argv[optind] != '-') return -1;

	f = argv[optind][index];
	if(f == '-' && index == 1) {
		++optind;
		return -1;
	}

	if((p = strchr(optstring, f)) == NULL) return '?';

	if(*(p + 1) == ':') {
		if(argv[optind][index + 1]) {
			optarg = argv[optind] + index + 1;
			++optind;
			index = 1;
		} else {
			++optind;
			index = 1;
			if(optind >= argc) return '?';
			optarg = argv[optind];
			++optind;
		}
	} else {
		++index;
		if(!argv[optind][index]) {
			++optind;
			index = 1;
		}
	}

	return f;
}

#endif
