/*
 * Copyright (C) 2016 by XuQi
 * All rights reserved
 */

#ifndef lint
static char sccsid[] = "@(#)nsdelactualpath.c, 2016/08/29 10:29 XuQi";
#endif /* not lint */

/*	nsdelactualpath - delete a actualpath associated with a file/directory */
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <winsock2.h>
#endif
#include "Cns.h"
#include "Cns_api.h"
#include "serrno.h"
extern	char	*getenv();
int main(int argc, char **argv)
{
	int c;
	int errflg = 0;
	char fullpath[CA_MAXPATHLEN+1];
	char *p;
	char path[CA_MAXPATHLEN+1];
#if defined(_WIN32)
	WSADATA wsadata;
#endif

	if (argc != 2) {
		fprintf (stderr,
		    "usage: %s file\n", argv[0]);
		exit (USERR);
	}
#if defined(_WIN32)
	if (WSAStartup (MAKEWORD (2, 0), &wsadata)) {
		fprintf (stderr, NS052);
		exit (SYERR);
	}
#endif
	strcpy(path ,argv[1]);
	if (*path != '/' && strstr (path, ":/") == NULL) {
		if ((p = getenv ("CASTOR_HOME")) == NULL ||
		    strlen (p) + strlen (path) + 1 > CA_MAXPATHLEN) {
			fprintf (stderr, "%s: invalid path\n", path);
			errflg++;
		} else
			sprintf (fullpath, "%s/%s", p, path);
	} else {
		if (strlen (path) > CA_MAXPATHLEN) {
			fprintf (stderr, "%s: %s\n", path,
			    sstrerror(SENAMETOOLONG));
			errflg++;
		} else
			strcpy (fullpath, path);
	}
	if (errflg == 0 && Cns_delactualpath (fullpath)) {
		fprintf (stderr, "%s: %s\n", path, sstrerror(serrno));
		errflg++;
	}
#if defined(_WIN32)
	WSACleanup();
#endif
	if (errflg)
		exit (USERR);
	exit (0);
}
