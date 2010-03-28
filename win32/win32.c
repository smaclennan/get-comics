/*
 * win32.c - start and cleanup windows sockets
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

#include "../get-comics.h"

static void win32_cleanup(void)
{
	WSACleanup();
}

void win32_init(void)
{
	WSADATA data;

	int rc = WSAStartup(2, &data);
	if (rc) {
		printf("WSAStartup failed %d\n", rc);
		exit(1);
	}

	atexit(win32_cleanup);
}
