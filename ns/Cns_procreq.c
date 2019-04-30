
	/*
 * Copyright (C) 1999-2004 by CERN/IT/PDP/DM
 * All rights reserved
 */
 
#ifndef lint
static char sccsid[] = "@(#)Cns_procreq.c,v 1.61 2004/03/03 08:51:31 CERN IT-PDP/DM Jean-Philippe Baud";
#endif /* not lint */
 
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/types.h>
#include <uuid/uuid.h>
#include <python3.5m/Python.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <queue>

#if defined(_WIN32)
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#define S_ISGID 0002000
#define S_ISVTX 0001000
#include <winsock2.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#endif

#define MAXPATHLEN 1023 //the largest size of the file path 
#define UNIT_SIZE (1024*1024)
#define PATH "/cdfs_data/"
#define VIRPATH "/data/xrootdfs/file/"
#define THREAD_NUM 10
#define SMALLSIZE 10485760
#define BUFLEN 10485760 //1MB
#define SMALLSIZE 10485760 //10MB 

#include "marshall.h"
#include "Cgrp.h"
#include "Cns.h"
#include "Cns_server.h"
#include "Cpwd.h"
#include "Cupv_api.h"
#include "rfcntl.h"
#include "serrno.h"
#include "u64subr.h"
#include "Cns_api.h"

XrdPosixXrootd xrdpx;
extern int being_shutdown;
extern char localhost[CA_MAXHOSTNAMELEN+1];

pthread_mutex_t  size_lock;
extern int stopflag = 0;
u_signed64 total_bytes = 0;

struct mProcArg
{
        unsigned long long fd_in;
        unsigned long long fd_out;
        size_t size;
        off_t offset;
};

/*get key from confile*/
int get_conf_value(char *file_path, char *key_name, char *key_value)
{
        FILE *fp = NULL;
        char *line = NULL, *substr = NULL;
        char value[100];
        size_t len = 0, tlen = 0;
        ssize_t read = 0;

    if(file_path == NULL || key_name == NULL || key_value == NULL)
    {
        printf( "config_key parameter is wrong\n");
        return -1;
    }
        fp = fopen(file_path, "r");
        if (fp == NULL)
    {
        printf("open config_file failed\n");
        return -1;
    }
 while ((read = getline(&line, &len, fp)) != -1)
    {
        substr = strstr(line, key_name);
        if(substr == NULL)
        {
            continue;
        }
        else
        {
            tlen = strlen(key_name);
            if(line[tlen] == '=')
            {
                strncpy(value, &line[tlen+1], len-tlen+1);
                tlen = strlen(value);
                *(value+tlen-1) = '\0';
                break;
            }
            else
            {
                printf("config file format is invaild\n");
                fclose(fp);
                return -2;
            }
        }
    }
    if(substr == NULL)
    {
        printf("key: %s is not in config file!\n", key_name);
        fclose(fp);
        return -1;
    }
    strcpy(key_value, value);
    free(line);
    fclose(fp);
    return 0;

}

 
/*	Cns_logreq - log a request */

/*	Split the message into lines so they don't exceed LOGBUFSZ-1 characters
 *	A backslash is appended to a line to be continued
 *	A continuation line is prefixed by '+ '
 */
void Cns_logreq(char *func,char *logbuf)
{
	int n1, n2;
	char *p;
	char savechrs1[2];
	char savechrs2[2];

	n1 = LOGBUFSZ - strlen (func) - 36;
	n2 = strlen (logbuf);
	p = logbuf;
	while (n2 > n1) {
		savechrs1[0] = *(p + n1);
		savechrs1[1] = *(p + n1 + 1);
		*(p + n1) = '\\';
		*(p + n1 + 1) = '\0';
		nslogit (func, NS098, p);
		if (p != logbuf) {
			*p = savechrs2[0];
			*(p + 1) = savechrs2[1];
		}
		p += n1 - 2;
		savechrs2[0] = *p;
		savechrs2[1] = *(p + 1);
		*p = '+';
		*(p + 1) = ' ';
		*(p + 2) = savechrs1[0];
		*(p + 3) = savechrs1[1];
		n2 -= n1;
	}
	nslogit (func, NS098, p);
	if (p != logbuf) {
		*p = savechrs2[0];
		*(p + 1) = savechrs2[1];
	}
}

int marshall_DIRX (char **sbpp,struct Cns_file_metadata *fmd_entry)
{
	char *sbp = *sbpp;

	marshall_HYPER (sbp, fmd_entry->fileid);
	marshall_WORD (sbp, fmd_entry->filemode);
	marshall_LONG (sbp, fmd_entry->nlink);
	marshall_LONG (sbp, fmd_entry->uid);
	marshall_LONG (sbp, fmd_entry->gid);
	marshall_HYPER (sbp, fmd_entry->filesize);
	marshall_TIME_T (sbp, fmd_entry->atime);
	marshall_TIME_T (sbp, fmd_entry->mtime);
	marshall_TIME_T (sbp, fmd_entry->ctime);
	marshall_WORD (sbp, fmd_entry->fileclass);
	marshall_BYTE (sbp, fmd_entry->status);
	marshall_STRING (sbp, fmd_entry->name);
	*sbpp = sbp;
	return (0);
}

int marshall_DIRXT (char **sbpp,int magic,struct Cns_file_metadata *fmd_entry,struct Cns_seg_metadata *smd_entry)
{
	char *sbp = *sbpp;

	marshall_HYPER (sbp, fmd_entry->parent_fileid);
	if (magic >= CNS_MAGIC3)
		marshall_HYPER (sbp, smd_entry->s_fileid);
		marshall_WORD (sbp, smd_entry->copyno);
		marshall_WORD (sbp, smd_entry->fsec);
		marshall_HYPER (sbp, smd_entry->segsize);
		marshall_LONG (sbp, smd_entry->compression);
		marshall_BYTE (sbp, smd_entry->s_status);
		marshall_STRING (sbp, smd_entry->vid);
		if (magic >= CNS_MAGIC4) {
			marshall_STRING (sbp, smd_entry->checksum_name);
			marshall_LONG (sbp, smd_entry->checksum);
		}
	if (magic >= CNS_MAGIC2)
		marshall_WORD (sbp, smd_entry->side);
        marshall_LONG (sbp, smd_entry->fseq);
        marshall_OPAQUE (sbp, smd_entry->blockid, 4);
        marshall_STRING (sbp, fmd_entry->name);
        *sbpp = sbp;
	return (0);
}

/*	Cns_srv_access - check accessibility of a file/directory */

int Cns_srv_access(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int amode;
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata fmd_entry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+13];
	mode_t mode;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	uid_t uid;

	strcpy (func, "Cns_srv_access");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "access", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, amode);
	sprintf (logbuf, "access %o %s", amode, path);
	Cns_logreq (func, logbuf);

	if (amode & ~(R_OK | W_OK | X_OK | F_OK))
		return (EINVAL);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/') {	/* Cns_access / */
		parent_dir.fileid = 0;
	} else { /* check parent directory components for search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &parent_dir, NULL))
			return (serrno);
	}

	/* get basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &fmd_entry, 0, NULL))
		return (serrno);

	/* check permissions for basename */

	if (amode == F_OK)
		return (0);
	mode = (amode & (R_OK|W_OK|X_OK)) << 6;
	if (Cns_chkentryperm (&fmd_entry, mode, uid, gid, clienthost))
		return (EACCES);
	return (0);
}

/*      Cns_srv_chclass - change class on directory */

int Cns_srv_chclass(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	char class_name[CA_MAXCLASNAMELEN+1];
	int classid;
	u_signed64 cwd;
	struct Cns_file_metadata fmd_entry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+CA_MAXCLASNAMELEN+16];
	struct Cns_class_metadata new_class_entry;
	Cns_dbrec_addr new_rec_addrc;
	struct Cns_class_metadata old_class_entry;
	Cns_dbrec_addr old_rec_addrc;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;
	uid_t uid;

	strcpy (func, "Cns_srv_chclass");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "chclass", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, classid);
	if (unmarshall_STRINGN (rbp, class_name, CA_MAXCLASNAMELEN+1))
		return (EINVAL);
	sprintf (logbuf, "chclass %s %d %s", path, classid, class_name);
	Cns_logreq (func, logbuf);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/') {	/* Cns_chclass / */
		parent_dir.fileid = 0;
	} else { /* check parent directory components for search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &parent_dir, NULL))
			return (serrno);
	}

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* is the class valid? */

	if (classid > 0) {
		if (Cns_get_class_by_id (&thip->dbfd, classid, &new_class_entry,
		    1, &new_rec_addrc))
			if (serrno == ENOENT) {
				sendrep (thip->s, MSG_ERR, "No such class\n");
				return (EINVAL);
			} else
				return (serrno);
		if (*class_name && strcmp (class_name, new_class_entry.name))
			return (EINVAL);
	} else {
		if (Cns_get_class_by_name (&thip->dbfd, class_name, &new_class_entry,
		    1, &new_rec_addrc))
			if (serrno == ENOENT) {
				sendrep (thip->s, MSG_ERR, "No such class\n");
				return (EINVAL);
			} else
				return (serrno);
	}

	/* get/lock basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &fmd_entry, 1, &rec_addr))
		return (serrno);

	/* check if the user is authorized to chclass this entry */

	if (uid != fmd_entry.uid &&
	    Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
		return (EPERM);
	if ((fmd_entry.filemode & S_IFDIR) == 0)
		return (ENOTDIR);

	/* update entries */

	if (fmd_entry.fileclass != new_class_entry.classid) {
		if (fmd_entry.fileclass > 0) {
			if (Cns_get_class_by_id (&thip->dbfd, fmd_entry.fileclass,
			    &old_class_entry, 1, &old_rec_addrc))
				return (serrno);
			old_class_entry.nbdirs_using_class--;
			if (Cns_update_class_entry (&thip->dbfd, &old_rec_addrc,
			    &old_class_entry))
				return (serrno);
		}
		fmd_entry.fileclass = new_class_entry.classid;
		fmd_entry.ctime = time (0);
		if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &fmd_entry))
			return (serrno);
		new_class_entry.nbdirs_using_class++;
		if (Cns_update_class_entry (&thip->dbfd, &new_rec_addrc,
		    &new_class_entry))
			return (serrno);
	}
	return (0);
}

/*      Cns_srv_chdir - change current working directory */

int Cns_srv_chdir(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata direntry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char repbuf[8];
	char *sbp;
	uid_t uid;

	strcpy (func, "Cns_srv_chdir");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "chdir", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "chdir %s", path);
	Cns_logreq (func, logbuf);

	/* check directory components for search permission */

	if (strcmp (path, "..") == 0) {
		if (Cns_get_fmd_by_fileid (&thip->dbfd, cwd, &direntry, 0, NULL))
			return (serrno);
		if (direntry.parent_fileid) {
			if (Cns_get_fmd_by_fileid (&thip->dbfd,
			    direntry.parent_fileid, &direntry, 0, NULL))
				return (serrno);
			if (Cns_chkentryperm (&direntry, S_IEXEC, uid, gid, clienthost))
				return (EACCES);
		}
	} else if (strcmp (path, ".") == 0) {
		if (Cns_get_fmd_by_fileid (&thip->dbfd, cwd, &direntry, 0, NULL))
			return (serrno);
	} else {
		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &direntry, NULL))
			return (serrno);
	}

	/* return directory fileid */

	sbp = repbuf;
	marshall_HYPER (sbp, direntry.fileid);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

/*      Cns_srv_chmod - change file/directory permissions */

int Cns_srv_chmod(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata fmd_entry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	mode_t mode;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;
	uid_t uid;

	strcpy (func, "Cns_srv_chmod");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "chmod", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, mode);
	sprintf (logbuf, "chmod %o %s", mode, path);
	Cns_logreq (func, logbuf);
	if (uid == 0){
                if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
                        return (serrno);
	}

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/') {	/* Cns_chmod / */
		parent_dir.fileid = 0;
	} else { /* check parent directory components for search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &parent_dir, NULL))
			return (serrno);
	}

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* get/lock basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &fmd_entry, 1, &rec_addr))
		return (serrno);

	/* check if the user is authorized to chmod this entry */

	if (uid != fmd_entry.uid &&
	    Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
		return (EPERM);
	if ((fmd_entry.filemode & S_IFDIR) == 0 && uid != 0)
		mode &= ~S_ISVTX;
	if (gid != fmd_entry.gid && uid != 0)
		mode &= ~S_ISGID;

	/* update entry */

	fmd_entry.filemode = (fmd_entry.filemode & S_IFMT) | (mode & ~S_IFMT);
	fmd_entry.ctime = time (0);
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &fmd_entry))
		return (serrno);
	return (0);
}

/*      Cns_srv_chown - change owner and group of a file or a directory */

int Cns_srv_chown(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata fmd_entry;
	int found;
	char func[16];
	gid_t gid;
	struct group *gr;
	char logbuf[CA_MAXPATHLEN+19];
	char **membername;
	int need_p_admin = 0;
	int need_p_expt_admin = 0;
	gid_t new_gid;
	uid_t new_uid;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	struct passwd *pw;
	char *rbp;
	Cns_dbrec_addr rec_addr;
	uid_t uid;

	strcpy (func, "Cns_srv_chown");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "chown", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, new_uid);
	unmarshall_LONG (rbp, new_gid);
	sprintf (logbuf, "chown %d:%d %s", new_uid, new_gid, path);
	Cns_logreq (func, logbuf);

	if (uid == 0){
                if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
                        return (serrno);
	}

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/') {	/* Cns_chown / */
		parent_dir.fileid = 0;
	} else { /* check parent directory components for search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &parent_dir, NULL))
			return (serrno);
	}

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* get/lock basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &fmd_entry, 1, &rec_addr))
		return (serrno);

	/* check if the user is authorized to change ownership this entry */

	if (fmd_entry.uid != new_uid && new_uid != -1) {
		if (gid != fmd_entry.gid)
			need_p_admin = 1;
		else if ((pw = Cgetpwuid (new_uid)) == NULL)
			need_p_admin = 1;
		else if (pw->pw_gid == gid)	/* new owner belongs to same group */
			need_p_expt_admin = 1;
		else
			need_p_admin = 1;
	}
	if (fmd_entry.gid != new_gid && new_gid != -1) {
		if (uid != fmd_entry.uid) {
			need_p_admin = 1;
		} else if ((pw = Cgetpwuid (uid)) == NULL) {
			need_p_admin = 1;
		} else if ((gr = Cgetgrgid (new_gid)) == NULL) {
			need_p_admin = 1;
		} else {
			if (new_gid == pw->pw_gid) {
				found = 1;
			} else {
				found = 0;
				if (membername = gr->gr_mem) {
					while (*membername) {
						if (strcmp (pw->pw_name, *membername) == 0) {
							found = 1;
							break;
						}
						membername++;
					}
				}
			}
			if (!found)
				need_p_admin = 1;
		}
	}
	if (need_p_admin) {
		if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
			return (EPERM);
	} else if (need_p_expt_admin) {
		if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN) &&
		    Cupv_check (uid, gid, clienthost, localhost, P_GRP_ADMIN))
			return (EPERM);
	}

	/* update entry */

	if (new_uid != -1)
		fmd_entry.uid = new_uid;
	if (new_gid != -1)
		fmd_entry.gid = new_gid;
	fmd_entry.ctime = time (0);
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &fmd_entry))
		return (serrno);
	return (0);
}

/*      Cns_srv_creat - create a file entry */
 
int Cns_srv_creat(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	int bof = 1;
	int c;
	u_signed64 cwd;
	DBLISTPTR dblistptr;
	struct Cns_file_metadata filentry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+17];
	mode_t mask;
	mode_t mode;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;	/* file record address */
	Cns_dbrec_addr rec_addrp;	/* parent record address */
	Cns_dbrec_addr rec_addrs;	/* segment record address */
	char repbuf[8];
	char *sbp;
	struct Cns_seg_metadata smd_entry;
	char tmpbuf[21];
	uid_t uid;

	strcpy (func, "Cns_srv_creat");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "creat", uid, gid, clienthost);
	unmarshall_WORD (rbp, mask);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, mode);
	sprintf (logbuf, "creat %s %o %o", path, mode, mask);
	Cns_logreq (func, logbuf);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/')	/* Cns_creat / */
		return (EISDIR);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* check parent directory components for write/search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IWRITE|S_IEXEC, uid, gid,
	    clienthost, &parent_dir, &rec_addrp))
		return (serrno);

	if (strcmp (basename, ".") == 0 || strcmp (basename, "..") == 0)
		return (EISDIR);

	/* check if the file exists already */

	if ((c = Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 1, &rec_addr)) && serrno != ENOENT)
		return (serrno);

	if (c == 0) {	/* file exists */
		if (filentry.filemode & S_IFDIR)
			return (EISDIR);

		/* check write permission in basename entry */

		if (Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
			return (EACCES);

		/* delete file segments if any */

		while ((c = Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
		    &smd_entry, 1, &rec_addrs, 0, &dblistptr)) == 0) {
			if (Cns_delete_smd_entry (&thip->dbfd, &rec_addrs))
				return (serrno);
			bof = 0;
		}
		(void) Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
		    &smd_entry, 1, &rec_addrs, 1, &dblistptr);	/* free res */
		if (c < 0)
			return (serrno);

		/* update basename entry */

		filentry.filesize = 0;
		filentry.mtime = time (0);
		filentry.ctime = filentry.mtime;
		filentry.status = '-';
		if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &filentry))
			return (serrno);
		nslogit (func, "file %s reset\n", u64tostr (filentry.fileid, tmpbuf, 0));
	} else {	/* must create the file */
		if (parent_dir.fileclass <= 0)
			return (EINVAL);
		memset ((char *) &filentry, 0, sizeof(filentry));
		if (Cns_unique_id (&thip->dbfd, &filentry.fileid) < 0)
			return (serrno);
		filentry.parent_fileid = parent_dir.fileid;
		strcpy (filentry.name, basename);
		filentry.filemode = S_IFREG | ((mode & ~S_IFMT) & ~mask);
		filentry.nlink = 1;
		filentry.uid = uid;
		if (parent_dir.filemode & S_ISGID) {
			filentry.gid = parent_dir.gid;
			if (gid == filentry.gid)
				filentry.filemode |= S_ISGID;
		} else
			filentry.gid = gid;
		filentry.atime = time (0);
		filentry.mtime = filentry.atime;
		filentry.ctime = filentry.atime;
		filentry.fileclass = parent_dir.fileclass;
		filentry.status = '-';

		/* write new file entry */

		if (Cns_insert_fmd_entry (&thip->dbfd, &filentry))
			return (serrno);

		/* update parent directory entry */

		parent_dir.nlink++;
		parent_dir.mtime = time (0);
		parent_dir.ctime = parent_dir.mtime;
		if (Cns_update_fmd_entry (&thip->dbfd, &rec_addrp, &parent_dir))
			return (serrno);
		nslogit (func, "file %s created\n", u64tostr (filentry.fileid, tmpbuf, 0));
	}
	sbp = repbuf;
	marshall_HYPER (sbp, filentry.fileid);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

/*	Cns_srv_delcomment - delete a comment associated with a file/directory */

int Cns_srv_delcomment(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata filentry;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addru;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_delcomment");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "delcomment", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "delcomment %s", path);
	Cns_logreq (func, logbuf);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* check parent directory components for search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
	    clienthost, &parent_dir, NULL))
		return (serrno);

	/* get basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 0, NULL))
		return (serrno);

	/* check if the user is authorized to delete the comment on this entry */

	if (uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* delete the comment if it exists */

	if (Cns_get_umd_by_fileid (&thip->dbfd, filentry.fileid, &umd_entry, 1,
	    &rec_addru))
		return (serrno);
	if (Cns_delete_umd_entry (&thip->dbfd, &rec_addru))
		return (serrno);
	return (0);
}

/*      Cns_srv_delete - logically remove a file entry */
 
int Cns_srv_delete(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	int bof = 1;
	int c;
	u_signed64 cwd;
	DBLISTPTR dblistptr;
	struct Cns_file_metadata filentry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+8];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;	/* file record address */
	Cns_dbrec_addr rec_addrp;	/* parent record address */
	Cns_dbrec_addr rec_addrs;	/* segment record address */
	struct Cns_seg_metadata smd_entry;
	uid_t uid;

	strcpy (func, "Cns_srv_delete");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "delete", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "delete %s", path);
	Cns_logreq (func, logbuf);
	if (uid == 0){
                if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
                        return (serrno);
	}

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/')	/* Cns_delete / */
		return (EINVAL);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* check parent directory components for write/search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IWRITE|S_IEXEC, uid, gid,
	    clienthost, &parent_dir, &rec_addrp))
		return (serrno);

	/* get and lock requested file entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 1, &rec_addr))
		return (serrno);

	if (filentry.filemode & S_IFDIR)
		return (EPERM);

	/* if the parent has the sticky bit set,
	   the user must own the file or the parent or
	   the basename entry must have write permission */

	if (parent_dir.filemode & S_ISVTX &&
	    uid != parent_dir.uid && uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* mark file segments if any as logically deleted */

	while ((c = Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
	    &smd_entry, 1, &rec_addrs, 0, &dblistptr)) == 0) {
		smd_entry.s_status = 'D';
		if (Cns_update_smd_entry (&thip->dbfd, &rec_addrs, &smd_entry))
			return (serrno);
		bof = 0;
	}
	(void) Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
	    &smd_entry, 1, &rec_addrs, 1, &dblistptr);	/* free res */
	if (c < 0)
		return (serrno);

	/* mark file entry as logically deleted */

	filentry.status = 'D';
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &filentry))
		return (serrno);

	/* update parent directory entry */

	parent_dir.mtime = time (0);
	parent_dir.ctime = parent_dir.mtime;
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addrp, &parent_dir))
		return (serrno);
	return (0);
}

/*	Cns_srv_deleteclass - delete a file class definition */

int Cns_srv_deleteclass(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int bol = 1;
	struct Cns_class_metadata class_entry;
	char class_name[CA_MAXCLASNAMELEN+1];
	int classid;
	DBLISTPTR dblistptr;
	char func[20];
	gid_t gid;
	char logbuf[CA_MAXCLASNAMELEN+19];
	char *rbp;
	Cns_dbrec_addr rec_addr;
	Cns_dbrec_addr rec_addrt;
	struct Cns_tp_pool tppool_entry;
	uid_t uid;

	strcpy (func, "Cns_srv_deleteclass");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "deleteclass", uid, gid, clienthost);
	unmarshall_LONG (rbp, classid);
	if (unmarshall_STRINGN (rbp, class_name, CA_MAXCLASNAMELEN+1))
		return (EINVAL);
	sprintf (logbuf, "deleteclass %d %s", classid, class_name);
	Cns_logreq (func, logbuf);

	if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
		return (serrno);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (classid > 0) {
		if (Cns_get_class_by_id (&thip->dbfd, classid, &class_entry,
		    1, &rec_addr))
			return (serrno);
		if (*class_name && strcmp (class_name, class_entry.name))
			return (EINVAL);
	} else {
		if (Cns_get_class_by_name (&thip->dbfd, class_name, &class_entry,
		    1, &rec_addr))
			return (serrno);
	}
	if (class_entry.nbdirs_using_class)
		return (EEXIST);
	while (Cns_get_tppool_by_cid (&thip->dbfd, bol, class_entry.classid,
	    &tppool_entry, 1, &rec_addrt, 0, &dblistptr) == 0) {
		if (Cns_delete_tppool_entry (&thip->dbfd, &rec_addrt))
			return (serrno);
		bol = 0;
	}
	(void) Cns_get_tppool_by_cid (&thip->dbfd, bol, class_entry.classid,
	    &tppool_entry, 1, &rec_addrt, 1, &dblistptr);	/* free res */
	if (Cns_delete_class_entry (&thip->dbfd, &rec_addr))
		return (serrno);
	return (0);
}

/*	Cns_srv_enterclass - define a new file class */

int Cns_srv_enterclass(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	struct Cns_class_metadata class_entry;
	char func[19];
	gid_t gid;
	int i;
	char logbuf[CA_MAXCLASNAMELEN+19];
	int nbtppools;
	char *rbp;
	struct Cns_tp_pool tppool_entry;
	uid_t uid;

	strcpy (func, "Cns_srv_enterclass");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "enterclass", uid, gid, clienthost);
	memset ((char *) &class_entry, 0, sizeof(class_entry));
	unmarshall_LONG (rbp, class_entry.classid);
	if (unmarshall_STRINGN (rbp, class_entry.name, CA_MAXCLASNAMELEN+1))
		return (EINVAL);
	unmarshall_LONG (rbp, class_entry.uid);
	unmarshall_LONG (rbp, class_entry.gid);
	unmarshall_LONG (rbp, class_entry.min_filesize);
	unmarshall_LONG (rbp, class_entry.max_filesize);
	unmarshall_LONG (rbp, class_entry.flags);
	unmarshall_LONG (rbp, class_entry.maxdrives);
	unmarshall_LONG (rbp, class_entry.max_segsize);
	unmarshall_LONG (rbp, class_entry.migr_time_interval);
	unmarshall_LONG (rbp, class_entry.mintime_beforemigr);
	unmarshall_LONG (rbp, class_entry.nbcopies);
	unmarshall_LONG (rbp, class_entry.retenp_on_disk);
	unmarshall_LONG (rbp, nbtppools);
	sprintf (logbuf, "enterclass %d %s", class_entry.classid,
	    class_entry.name);
	Cns_logreq (func, logbuf);

	if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
		return (serrno);

	/* start transaction */

	if (class_entry.classid <= 0 || *class_entry.name == '\0')
		return (EINVAL);
	if (class_entry.max_filesize < class_entry.min_filesize)
		return (EINVAL);
	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (Cns_insert_class_entry (&thip->dbfd, &class_entry))
		return (serrno);

	/* receive/store tppool entries */

	tppool_entry.classid = class_entry.classid;
	for (i = 0; i < nbtppools; i++) {
		if (unmarshall_STRINGN (rbp, tppool_entry.tape_pool, CA_MAXPOOLNAMELEN+1))
			return (EINVAL);
		if (Cns_insert_tppool_entry (&thip->dbfd, &tppool_entry))
			return (serrno);
	}
	return (0);
}

/*	Cns_srv_getcomment - get the comment associated with a file/directory */

int Cns_srv_getcomment(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata filentry;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char repbuf[CA_MAXCOMMENTLEN+1];
	char *sbp;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_getcomment");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "getcomment", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "getcomment %s", path);
	Cns_logreq (func, logbuf);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* check parent directory components for search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
	    clienthost, &parent_dir, NULL))
		return (serrno);

	/* get basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 0, NULL))
		return (serrno);

	/* check if the user is authorized to get the comment for this entry */

	if (uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IREAD, uid, gid, clienthost))
		return (EACCES);

	/* get the comment if it exists */

	if (Cns_get_umd_by_fileid (&thip->dbfd, filentry.fileid, &umd_entry, 0,
	    NULL))
		return (serrno);

	sbp = repbuf;
	marshall_STRING (sbp, umd_entry.comments);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

int Cns_srv_getpath(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	u_signed64 cur_fileid;
	struct Cns_file_metadata fmd_entry;
	char func[16];
	gid_t gid;
	int n;
	char *p;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char repbuf[CA_MAXPATHLEN+1];
	char *sbp;
	uid_t uid;

	strcpy (func, "Cns_srv_getpath");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "getpath", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cur_fileid);

	p = path + CA_MAXPATHLEN;
	*p = '\0';
	if (cur_fileid == 2)
		*(--p) = '/';
	else while (cur_fileid != 2) {
		if (Cns_get_fmd_by_fileid (&thip->dbfd, cur_fileid, &fmd_entry,
		    0, NULL))
			return (serrno);
		n = strlen (fmd_entry.name);
		if ((p -= n) < path + 1)
			return (SENAMETOOLONG);
		memcpy (p, fmd_entry.name, n);
		*(--p) = '/';
		cur_fileid = fmd_entry.parent_fileid;
	}
	sbp = repbuf;
	marshall_STRING (sbp, p);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

/*	Cns_srv_getsegattrs - get file segments attributes */

int Cns_srv_getsegattrs(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	int bof = 1;
	int c;
	int copyno;
	u_signed64 cwd;
	DBLISTPTR dblistptr;
	u_signed64 fileid;
	struct Cns_file_metadata filentry;
	int fsec;
	char func[20];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+34];
	int nbseg = 0;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *q;
	char *rbp;
	char repbuf[REPBUFSZ];
	char *sbp;
	struct Cns_seg_metadata smd_entry;
	char tmpbuf[21];
	uid_t uid;

	strcpy (func, "Cns_srv_getsegattrs");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "getsegattrs", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        unmarshall_HYPER (rbp, fileid);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "getsegattrs %s %s",
	    u64tostr (fileid, tmpbuf, 0), path);
	Cns_logreq (func, logbuf);

	if (fileid) {
		/* get basename entry */

		if (Cns_get_fmd_by_fileid (&thip->dbfd, fileid,
		    &filentry, 0, NULL))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkbackperm (&thip->dbfd, filentry.parent_fileid,
		    S_IEXEC, uid, gid, clienthost))
			return (serrno);
	} else {
		if (Cns_splitname (cwd, path, basename))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &parent_dir, NULL))
			return (serrno);

		/* get basename entry */

		if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
		    &filentry, 0, NULL))
			return (serrno);
	}

	/* check if the entry is a regular file */

	if (filentry.filemode & S_IFDIR)
		return (EISDIR);

	/* get/send file segment entries */

	sbp = repbuf;
	marshall_WORD (sbp, nbseg);	/* will be updated */
	while ((c = Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
	    &smd_entry, 0, NULL, 0, &dblistptr)) == 0) {
		marshall_WORD (sbp, smd_entry.copyno);
		marshall_WORD (sbp, smd_entry.fsec);
		marshall_HYPER (sbp, smd_entry.segsize);
		marshall_LONG (sbp, smd_entry.compression);
		marshall_BYTE (sbp, smd_entry.s_status);
		marshall_STRING (sbp, smd_entry.vid);
		if (magic >= CNS_MAGIC2)
			marshall_WORD (sbp, smd_entry.side);
		marshall_LONG (sbp, smd_entry.fseq);
		marshall_OPAQUE (sbp, smd_entry.blockid, 4);
		if (magic >= CNS_MAGIC4) {
 			marshall_STRING (sbp, smd_entry.checksum_name);
            marshall_LONG (sbp, smd_entry.checksum);
        } 
		nbseg++;
		bof = 0;
	}
	(void) Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
	    &smd_entry, 0, NULL, 1, &dblistptr);	/* free res */
	if (c < 0)
		return (serrno);

	q = repbuf;
	marshall_WORD (q, nbseg);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

int Cns_srv_listclass(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip,struct Cns_class_metadata *class_entry,int endlist,DBLISTPTR *dblistptr)
{
	int bol;	/* beginning of class list flag */
	int bot;	/* beginning of tape pools list flag */
	int c;
	int eol = 0;	/* end of list flag */
	char func[18];
	gid_t gid;
	int listentsz;	/* size of client machine Cns_fileclass structure */
	int maxsize;
	int nbentries = 0;
	int nbtppools;
	char outbuf[LISTBUFSZ+4];
	char *p;
	char *q;
	char *rbp;
	char *sav_sbp;
	char *sbp;
	DBLISTPTR tplistptr;
	struct Cns_tp_pool tppool_entry;
	uid_t uid;

	strcpy (func, "Cns_srv_listclass");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "listclass", uid, gid, clienthost);
	unmarshall_WORD (rbp, listentsz);
	unmarshall_WORD (rbp, bol);

	/* return as many entries as possible to the client */

	maxsize = LISTBUFSZ;
	sbp = outbuf;
	marshall_WORD (sbp, nbentries);		/* will be updated */

	if (bol || endlist)
		c = Cns_list_class_entry (&thip->dbfd, bol, class_entry,
		    endlist, dblistptr);
	else
		c = 0;
	while (c == 0) {
		if (listentsz > maxsize) break;
		sav_sbp = sbp;
		marshall_LONG (sbp, class_entry->classid);
		marshall_STRING (sbp, class_entry->name);
		marshall_LONG (sbp, class_entry->uid);
		marshall_LONG (sbp, class_entry->gid);
		marshall_LONG (sbp, class_entry->min_filesize);
		marshall_LONG (sbp, class_entry->max_filesize);
		marshall_LONG (sbp, class_entry->flags);
		marshall_LONG (sbp, class_entry->maxdrives);
		marshall_LONG (sbp, class_entry->max_segsize);
		marshall_LONG (sbp, class_entry->migr_time_interval);
		marshall_LONG (sbp, class_entry->mintime_beforemigr);
		marshall_LONG (sbp, class_entry->nbcopies);
		marshall_LONG (sbp, class_entry->retenp_on_disk);

		/* get/send tppool entries */

		bot = 1;
		nbtppools = 0;
		q = sbp;
		marshall_LONG (sbp, nbtppools);	/* will be updated */
		maxsize -= listentsz;
		while ((c = Cns_get_tppool_by_cid (&thip->dbfd, bot,
		    class_entry->classid, &tppool_entry, 0, NULL, 0, &tplistptr)) == 0) {
			maxsize -= CA_MAXPOOLNAMELEN + 1;
			if (maxsize < 0) {
				sbp = sav_sbp;
				goto reply;
			}
			marshall_STRING (sbp, tppool_entry.tape_pool);
			nbtppools++;
			bot = 0;
		}
		(void) Cns_get_tppool_by_cid (&thip->dbfd, bot, class_entry->classid,
		    &tppool_entry, 0, NULL, 1, &tplistptr);	/* free res */
		if (c < 0)
			return (serrno);

		marshall_LONG (q, nbtppools);
		nbentries++;
		bol = 0;
		c = Cns_list_class_entry (&thip->dbfd, bol, class_entry,
		    endlist, dblistptr);
	}
	if (c < 0)
		return (serrno);
	if (c == 1)
		eol = 1;
reply:
	marshall_WORD (sbp, eol);
	p = outbuf;
	marshall_WORD (p, nbentries);		/* update nbentries in reply */
	sendrep (thip->s, MSG_DATA, sbp - outbuf, outbuf);
	return (0);
}

int Cns_srv_listtape(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip,struct Cns_file_metadata *fmd_entry,struct Cns_seg_metadata *smd_entry,int endlist,DBLISTPTR *dblistptr)
{
	int bov;	/* beginning of volume flag */
	int c;
	char dirbuf[DIRBUFSZ+4];
	int direntsz;	/* size of client machine dirent structure excluding d_name */
	int eov = 0;	/* end of volume flag */
	char func[17];
	gid_t gid;
	char logbuf[CA_MAXVIDLEN+12];
	int maxsize;
	int nbentries = 0;
	char *p;
	char *rbp;
	char *sbp;
	uid_t uid;
	char vid[CA_MAXVIDLEN+1];

	strcpy (func, "Cns_srv_listtape");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "listtape", uid, gid, clienthost);
	unmarshall_WORD (rbp, direntsz);
	if (unmarshall_STRINGN (rbp, vid, CA_MAXVIDLEN+1))
		return (EINVAL);
	unmarshall_WORD (rbp, bov);
	sprintf (logbuf, "listtape %s %d", vid, bov);
	Cns_logreq (func, logbuf);

	/* return as many entries as possible to the client */

	maxsize = DIRBUFSZ - direntsz;
	sbp = dirbuf;
	marshall_WORD (sbp, nbentries);		/* will be updated */

	if (! bov && ! endlist) {
		marshall_DIRXT (&sbp, magic, fmd_entry, smd_entry);
		nbentries++;
		maxsize -= ((direntsz + strlen (fmd_entry->name) + 8) / 8) * 8;
	}
	while ((c = Cns_get_smd_by_vid (&thip->dbfd, bov, vid, smd_entry,
	    endlist, dblistptr)) == 0) {
		if (Cns_get_fmd_by_fileid (&thip->dbfd, smd_entry->s_fileid,
		    fmd_entry, 0, NULL) < 0)
			return (serrno);
		if ((int) strlen (fmd_entry->name) > maxsize) break;
		marshall_DIRXT (&sbp, magic, fmd_entry, smd_entry);
		nbentries++;
		bov = 0;
		maxsize -= ((direntsz + strlen (fmd_entry->name) + 8) / 8) * 8;
	}
	if (c < 0)
		return (serrno);
	if (c == 1)
		eov = 1;

	marshall_WORD (sbp, eov);
	p = dirbuf;
	marshall_WORD (p, nbentries);		/* update nbentries in reply */
	sendrep (thip->s, MSG_DATA, sbp - dirbuf, dirbuf);
	return (0);
}

/*      Cns_srv_mkdir - create a directory entry */
 
int Cns_srv_mkdir(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	int c;
	struct Cns_class_metadata class_entry;
	u_signed64 cwd;
	struct Cns_file_metadata direntry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+17];
	mode_t mask;
	mode_t mode;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addrc;
	Cns_dbrec_addr rec_addrp;
	uid_t uid;

	strcpy (func, "Cns_srv_mkdir");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "mkdir", uid, gid, clienthost);
	unmarshall_WORD (rbp, mask);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, mode);
	sprintf (logbuf, "mkdir %s %o %o", path, mode, mask);
	Cns_logreq (func, logbuf);

	if (uid == 0){
		if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
                	return (serrno);
        }

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/')	/* Cns_mkdir / */
		return (EEXIST);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* check parent directory components for write/search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IWRITE|S_IEXEC, uid, gid,
	    clienthost, &parent_dir, &rec_addrp))
		return (serrno);

	if (strcmp (basename, ".") == 0 || strcmp (basename, "..") == 0)
		return (EEXIST);

	/* check if basename entry exists already */

	if ((c = Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid,
	    basename, &direntry, 0, NULL)) && serrno != ENOENT)
		return (serrno);
	if (c == 0)
		return (EEXIST);

	/* build new directory entry */

	memset ((char *) &direntry, 0, sizeof(direntry));
	if (Cns_unique_id (&thip->dbfd, &direntry.fileid) < 0)
		return (serrno);
	direntry.parent_fileid = parent_dir.fileid;
	strcpy (direntry.name, basename);
	direntry.filemode = S_IFDIR | ((mode & ~S_IFMT) & ~mask);
	direntry.nlink = 0;
	direntry.uid = uid;
	if (parent_dir.filemode & S_ISGID) {
		direntry.gid = parent_dir.gid;
		if (gid == direntry.gid)
			direntry.filemode |= S_ISGID;
	} else
		direntry.gid = gid;
	direntry.atime = time (0);
	direntry.mtime = direntry.atime;
	direntry.ctime = direntry.atime;
	direntry.fileclass = parent_dir.fileclass;
	direntry.status = '-';

	/* write new directory entry */

	if (Cns_insert_fmd_entry (&thip->dbfd, &direntry))
		return (serrno);

	/* update parent directory entry */

	parent_dir.nlink++;
	parent_dir.mtime = time (0);
	parent_dir.ctime = parent_dir.mtime;
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addrp, &parent_dir))
		return (serrno);

	/* update nbdirs_using_class in Cns_class_metadata */

	if (direntry.fileclass > 0) {
		if (Cns_get_class_by_id (&thip->dbfd, direntry.fileclass,
		    &class_entry, 1, &rec_addrc))
			return (serrno);
		class_entry.nbdirs_using_class++;
		if (Cns_update_class_entry (&thip->dbfd, &rec_addrc, &class_entry))
			return (serrno);
	}
	return (0);
}

/*	Cns_srv_modifyclass - modify an existing fileclass definition */

int Cns_srv_modifyclass(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int bol = 1;
	struct Cns_class_metadata class_entry;
	gid_t class_group;
	char class_name[CA_MAXCLASNAMELEN+1];
	uid_t class_user;
	int classid;
	DBLISTPTR dblistptr;
	int flags;
	char func[20];
	gid_t gid;
	int i;
	char logbuf[CA_MAXCLASNAMELEN+19];
	int maxdrives;
	int max_filesize;
	int max_segsize;
	int migr_time_interval;
	int mintime_beforemigr;
	int min_filesize;
	int nbcopies;
	int nbtppools;
	char new_class_name[CA_MAXCLASNAMELEN+1];
	char *p;
	char *rbp;
	Cns_dbrec_addr rec_addr;
	Cns_dbrec_addr rec_addrt;
	int retenp_on_disk;
	struct Cns_tp_pool tppool_entry;
	char *tppools;
	uid_t uid;

	strcpy (func, "Cns_srv_modifyclass");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "modifyclass", uid, gid, clienthost);
	unmarshall_LONG (rbp, classid);
	if (unmarshall_STRINGN (rbp, class_name, CA_MAXCLASNAMELEN+1))
		return (EINVAL);
	if (unmarshall_STRINGN (rbp, new_class_name, CA_MAXCLASNAMELEN+1))
		return (EINVAL);
	unmarshall_LONG (rbp, class_user);
	unmarshall_LONG (rbp, class_group);
	unmarshall_LONG (rbp, min_filesize);
	unmarshall_LONG (rbp, max_filesize);
	unmarshall_LONG (rbp, flags);
	unmarshall_LONG (rbp, maxdrives);
	unmarshall_LONG (rbp, max_segsize);
	unmarshall_LONG (rbp, migr_time_interval);
	unmarshall_LONG (rbp, mintime_beforemigr);
	unmarshall_LONG (rbp, nbcopies);
	unmarshall_LONG (rbp, retenp_on_disk);
	unmarshall_LONG (rbp, nbtppools);
	sprintf (logbuf, "modifyclass %d %s", classid, class_name);
	Cns_logreq (func, logbuf);

	if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
		return (serrno);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* get and lock entry */

	memset((void *) &class_entry, 0, sizeof(struct Cns_class_metadata));
	if (classid > 0) {
		if (Cns_get_class_by_id (&thip->dbfd, classid, &class_entry,
		    1, &rec_addr))
			return (serrno);
		if (*class_name && strcmp (class_name, class_entry.name))
			return (EINVAL);
	} else {
		if (Cns_get_class_by_name (&thip->dbfd, class_name, &class_entry,
		    1, &rec_addr))
			return (serrno);
	}

	/* update entry */

	if (*new_class_name)
		strcpy (class_entry.name, new_class_name);
	if (class_user != -1)
		class_entry.uid = class_user;
	if (class_group != -1)
		class_entry.gid = class_group;
	if (min_filesize >= 0)
		class_entry.min_filesize = min_filesize;
	if (max_filesize >= 0)
		class_entry.max_filesize = max_filesize;
	if (flags >= 0)
		class_entry.flags = flags;
	if (maxdrives >= 0)
		class_entry.maxdrives = maxdrives;
	if (max_segsize >= 0)
		class_entry.max_segsize = max_segsize;
	if (migr_time_interval >= 0)
		class_entry.migr_time_interval = migr_time_interval;
	if (mintime_beforemigr >= 0)
		class_entry.mintime_beforemigr = mintime_beforemigr;
	if (nbcopies >= 0)
		class_entry.nbcopies = nbcopies;
	if (retenp_on_disk >= 0)
		class_entry.retenp_on_disk = retenp_on_disk;

	if (Cns_update_class_entry (&thip->dbfd, &rec_addr, &class_entry))
		return (serrno);

	if (nbtppools > 0) {
		if ((tppools = (char *)calloc (nbtppools, CA_MAXPOOLNAMELEN+1)) == NULL)
			return (ENOMEM);
		p = tppools;
		for (i = 0; i < nbtppools; i++) {
			if (unmarshall_STRINGN (rbp, p, CA_MAXPOOLNAMELEN+1)) {
				free (tppools);
				return (EINVAL);
			}
			p += (CA_MAXPOOLNAMELEN+1);
		}

		/* delete the entries which are not needed anymore */

		while (Cns_get_tppool_by_cid (&thip->dbfd, bol, class_entry.classid,
		    &tppool_entry, 1, &rec_addrt, 0, &dblistptr) == 0) {
			p = tppools;
			for (i = 0; i < nbtppools; i++) {
				if (strcmp (tppool_entry.tape_pool, p) == 0) break;
				p += (CA_MAXPOOLNAMELEN+1);
			}
			if (i >= nbtppools) {
				if (Cns_delete_tppool_entry (&thip->dbfd, &rec_addrt)) {
					free (tppools);
					return (serrno);
				}
			} else
				*p = '\0';
			bol = 0;
		}
		(void) Cns_get_tppool_by_cid (&thip->dbfd, bol, class_entry.classid,
		    &tppool_entry, 1, &rec_addrt, 1, &dblistptr);	/* free res */

		/* add the new entries if any */

		tppool_entry.classid = class_entry.classid;
		p = tppools;
		for (i = 0; i < nbtppools; i++) {
			if (*p) {
				strcpy (tppool_entry.tape_pool, p);
				if (Cns_insert_tppool_entry (&thip->dbfd, &tppool_entry)) {
					free (tppools);
					return (serrno);
				}
			}
			p += (CA_MAXPOOLNAMELEN+1);
		}
		free (tppools);
	}
	return (0);
}

/*      Cns_srv_open - open a file */
 
int Cns_srv_open(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	int bof = 1;
	int c;
	u_signed64 cwd;
	DBLISTPTR dblistptr;
	struct Cns_file_metadata filentry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+21];
	mode_t mask;
	mode_t mode;
	int oflag;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;	/* file record address */
	Cns_dbrec_addr rec_addrp;	/* parent record address */
	Cns_dbrec_addr rec_addrs;	/* segment record address */
	char repbuf[8];
	char *sbp;
	struct Cns_seg_metadata smd_entry;
	uid_t uid;

	strcpy (func, "Cns_srv_open");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "open", uid, gid, clienthost);
	unmarshall_WORD (rbp, mask);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, oflag);
	oflag = ntohopnflg (oflag);
	unmarshall_LONG (rbp, mode);
	sprintf (logbuf, "open %s %o %o %o", path, oflag, mode, mask);
	Cns_logreq (func, logbuf);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (*basename == '/') {	/* Cns_open / */
		parent_dir.fileid = 0;
	} else { /* check parent directory components for (write)/search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path,
		    (oflag & O_CREAT) ? S_IWRITE|S_IEXEC : S_IEXEC, uid, gid,
		    clienthost, &parent_dir, &rec_addrp))
			return (serrno);
	}

	/* check if the file exists already */

	if ((c = Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, (oflag & O_TRUNC) ? 1 : 0, &rec_addr)) && serrno != ENOENT)
		return (serrno);

	if (c && (oflag & O_CREAT) == 0)
		return (ENOENT);

	if (c == 0) {	/* file exists */
		if (oflag & O_CREAT && oflag & O_EXCL)
			return (EEXIST);
		if (filentry.filemode & S_IFDIR &&
		    (oflag & O_WRONLY || oflag & O_RDWR || oflag & O_TRUNC))
			return (EISDIR);

		/* check permissions in basename entry */

		if (Cns_chkentryperm (&filentry,
		    (oflag & O_WRONLY || oflag & O_RDWR || oflag & O_TRUNC) ? S_IWRITE : S_IREAD,
		    uid, gid, clienthost))
			return (EACCES);

		if (oflag & O_TRUNC) {

			/* delete file segments if any */

			while ((c = Cns_get_smd_by_pfid (&thip->dbfd, bof,
			    filentry.fileid, &smd_entry, 1, &rec_addrs,
			    0, &dblistptr)) == 0) {
				if (Cns_delete_smd_entry (&thip->dbfd, &rec_addrs))
					return (serrno);
				bof = 0;
			}
			(void) Cns_get_smd_by_pfid (&thip->dbfd, bof,
			    filentry.fileid, &smd_entry, 1, &rec_addrs,
			    1, &dblistptr);	/* free res */
			if (c < 0)
				return (serrno);

			/* update basename entry */

			filentry.filesize = 0;
			filentry.mtime = time (0);
			filentry.ctime = filentry.mtime;
			filentry.status = '-';
			if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &filentry))
				return (serrno);
		}
	} else {	/* must create the file */
		memset ((char *) &filentry, 0, sizeof(filentry));
		if (Cns_unique_id (&thip->dbfd, &filentry.fileid) < 0)
			return (serrno);
		filentry.parent_fileid = parent_dir.fileid;
		strcpy (filentry.name, basename);
		filentry.filemode = S_IFREG | ((mode & ~S_IFMT) & ~mask);
		filentry.filemode &= ~S_ISVTX;
		filentry.nlink = 1;
		filentry.uid = uid;
		if (parent_dir.filemode & S_ISGID) {
			filentry.gid = parent_dir.gid;
			if (gid == filentry.gid)
				filentry.filemode |= S_ISGID;
		} else
			filentry.gid = gid;
		filentry.atime = time (0);
		filentry.mtime = filentry.atime;
		filentry.ctime = filentry.atime;
		filentry.fileclass = parent_dir.fileclass;
		filentry.status = '-';

		/* write new file entry */

		if (Cns_insert_fmd_entry (&thip->dbfd, &filentry))
			return (serrno);

		/* update parent directory entry */

		parent_dir.nlink++;
		parent_dir.mtime = time (0);
		parent_dir.ctime = parent_dir.mtime;
		if (Cns_update_fmd_entry (&thip->dbfd, &rec_addrp, &parent_dir))
			return (serrno);
	}

	/* return fileid */

	sbp = repbuf;
	marshall_HYPER (sbp, filentry.fileid);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

/*      Cns_srv_opendir - open a directory entry */

int Cns_srv_opendir(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	u_signed64 cwd;
	struct Cns_file_metadata direntry;
	char func[16];
	char logbuf[CA_MAXPATHLEN+9];
	gid_t gid;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char repbuf[8];
	char *sbp;
	uid_t uid;

	strcpy (func, "Cns_srv_opendir");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "opendir", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "opendir %s", path);
	Cns_logreq (func, logbuf);

	if (! cwd && *path == 0)
		return (ENOENT);
	if (! cwd && *path != '/')
		return (EINVAL);

	if (strcmp (path, ".") == 0) {
		if (Cns_get_fmd_by_fileid (&thip->dbfd, cwd, &direntry, 0, NULL))
			return (serrno);
	} else {
		/* check parent directory components for search permission and
		   check directory basename for read permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IREAD|S_IEXEC,
		    uid, gid, clienthost, &direntry, NULL))
			return (serrno);
	}

	/* return directory fileid */

	sbp = repbuf;
	marshall_HYPER (sbp, direntry.fileid);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

/*	Cns_srv_queryclass - query about a file class */

int Cns_srv_queryclass(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int bol = 1;
	int c;
	struct Cns_class_metadata class_entry;
	char class_name[CA_MAXCLASNAMELEN+1];
	int classid;
	DBLISTPTR dblistptr;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXCLASNAMELEN+18];
	int nbtppools = 0;
	char *q;
	char *rbp;
	char repbuf[LISTBUFSZ];
	char *sbp;
	struct Cns_tp_pool tppool_entry;
	uid_t uid;

	strcpy (func, "Cns_srv_queryclass");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "queryclass", uid, gid, clienthost);
	unmarshall_LONG (rbp, classid);
	if (unmarshall_STRINGN (rbp, class_name, CA_MAXCLASNAMELEN+1))
		return (EINVAL);
	sprintf (logbuf, "queryclass %d %s", classid, class_name);
	Cns_logreq (func, logbuf);

	memset((void *) &class_entry, 0, sizeof(struct Cns_class_metadata));
	if (classid > 0) {
		if (Cns_get_class_by_id (&thip->dbfd, classid, &class_entry,
		    0, NULL))
			return (serrno);
		if (*class_name && strcmp (class_name, class_entry.name))
			return (EINVAL);
	} else {
		if (Cns_get_class_by_name (&thip->dbfd, class_name, &class_entry,
		    0, NULL))
			return (serrno);
	}

	sbp = repbuf;
	marshall_LONG (sbp, class_entry.classid);
	marshall_STRING (sbp, class_entry.name);
	marshall_LONG (sbp, class_entry.uid);
	marshall_LONG (sbp, class_entry.gid);
	marshall_LONG (sbp, class_entry.min_filesize);
	marshall_LONG (sbp, class_entry.max_filesize);
	marshall_LONG (sbp, class_entry.flags);
	marshall_LONG (sbp, class_entry.maxdrives);
	marshall_LONG (sbp, class_entry.max_segsize);
	marshall_LONG (sbp, class_entry.migr_time_interval);
	marshall_LONG (sbp, class_entry.mintime_beforemigr);
	marshall_LONG (sbp, class_entry.nbcopies);
	marshall_LONG (sbp, class_entry.retenp_on_disk);

	/* get/send tppool entries */

	q = sbp;
	marshall_LONG (sbp, nbtppools);	/* will be updated */
	while ((c = Cns_get_tppool_by_cid (&thip->dbfd, bol, class_entry.classid,
	    &tppool_entry, 0, NULL, 0, &dblistptr)) == 0) {
		marshall_STRING (sbp, tppool_entry.tape_pool);
		nbtppools++;
		bol = 0;
	}
	(void) Cns_get_tppool_by_cid (&thip->dbfd, bol, class_entry.classid,
	    &tppool_entry, 0, NULL, 1, &dblistptr);	/* free res */
	if (c < 0)
		return (serrno);

	marshall_LONG (q, nbtppools);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

/*      Cns_srv_readdir - read directory entries */

int Cns_srv_readdir(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip,struct Cns_file_metadata *fmd_entry,struct Cns_seg_metadata *smd_entry,struct Cns_user_metadata *umd_entry,int endlist,DBLISTPTR *dblistptr,DBLISTPTR *smdlistptr)
{
	int bod;	/* beginning of directory flag */
	int bof;	/* beginning of file flag */
	int c;
	int cml;	/* comment length */
	char dirbuf[DIRBUFSZ+4];
	struct Cns_file_metadata direntry;
	int direntsz;	/* size of client machine dirent structure excluding d_name */
	u_signed64 dir_fileid;
	int eod = 0;	/* end of directory flag */
	int fnl;	/* filename length */
	char func[16];
	int getattr;
	gid_t gid;
	int maxsize;
	int nbentries = 0;
	char *p;
	char *rbp;
	Cns_dbrec_addr rec_addr;
	char *sbp;
	uid_t uid;

	strcpy (func, "Cns_srv_readdir");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "readdir", uid, gid, clienthost);
	unmarshall_WORD (rbp, getattr);
	unmarshall_WORD (rbp, direntsz);
	unmarshall_HYPER (rbp, dir_fileid);
	unmarshall_WORD (rbp, bod);

	/* return as many entries as possible to the client */

	if (getattr == 1 || getattr == 4)
		if (DIRXSIZE > direntsz)
			direntsz = DIRXSIZE;
	maxsize = DIRBUFSZ - direntsz;
	sbp = dirbuf;
	marshall_WORD (sbp, nbentries);		/* will be updated */

	if (endlist && getattr == 2)
		(void) Cns_get_smd_by_pfid (&thip->dbfd, 0, fmd_entry->fileid,
		    smd_entry, 0, NULL, 1, smdlistptr);
	if (! bod && ! endlist) {
		fnl = strlen (fmd_entry->name);
		if (getattr == 0) {		/* readdir */
			marshall_STRING (sbp, fmd_entry->name);
			nbentries++;
			maxsize -= ((direntsz + fnl + 8) / 8) * 8;
		} else if (getattr == 1) {	/* readdirx */
			marshall_DIRX (&sbp, fmd_entry);
			nbentries++;
			maxsize -= ((direntsz + fnl + 8) / 8) * 8;
		} else if (getattr == 2) {	/* readdirxt */
			bof = 0;
			while (1) {	/* loop on segments */
				marshall_DIRXT (&sbp, magic, fmd_entry, smd_entry);
				nbentries++;
				maxsize -= ((direntsz + fnl + 8) / 8) * 8;
				if (c = Cns_get_smd_by_pfid (&thip->dbfd, bof, 
				    fmd_entry->fileid, smd_entry, 0, NULL,
				    0, smdlistptr)) break;
				if (fnl > maxsize)
					goto reply;
			}
			(void) Cns_get_smd_by_pfid (&thip->dbfd, bof,
			    fmd_entry->fileid, smd_entry, 0, NULL, 1, smdlistptr);
			if (c < 0)
				return (serrno);
		} else if (getattr == 3) {	/* readdirc */
			cml = strlen (umd_entry->comments);
			marshall_STRING (sbp, fmd_entry->name);
			marshall_STRING (sbp, umd_entry->comments);
			nbentries++;
			maxsize -= ((direntsz + fnl + cml + 9) / 8) * 8;
		} else {			/* readdirxc */
			cml = strlen (umd_entry->comments);
			marshall_DIRX (&sbp, fmd_entry);
			marshall_STRING (sbp, umd_entry->comments);
			nbentries++;
			maxsize -= ((direntsz + fnl + cml + 9) / 8) * 8;
		}
	}
	while ((c = Cns_get_fmd_by_pfid (&thip->dbfd, bod, dir_fileid,
	    fmd_entry, getattr, endlist, dblistptr)) == 0) {	/* loop on directory entries */
		fnl = strlen (fmd_entry->name);
		if (getattr == 0) {		/* readdir */
			if (fnl > maxsize) break;
			marshall_STRING (sbp, fmd_entry->name);
			nbentries++;
			maxsize -= ((direntsz + fnl + 8) / 8) * 8;
		} else if (getattr == 1) {	/* readdirx */
			if (fnl > maxsize) break;
			marshall_DIRX (&sbp, fmd_entry);
			nbentries++;
			maxsize -= ((direntsz + fnl + 8) / 8) * 8;
		} else if (getattr == 2) {	/* readdirxt */
			bof = 1;
			while (1) {	/* loop on segments */
				if (c = Cns_get_smd_by_pfid (&thip->dbfd, bof,
				    fmd_entry->fileid, smd_entry, 0, NULL,
				    0, smdlistptr)) break;
				if (fnl > maxsize)
					goto reply;
				marshall_DIRXT (&sbp, magic, fmd_entry, smd_entry);
				nbentries++;
				bof = 0;
				maxsize -= ((direntsz + fnl + 8) / 8) * 8;
			}
			(void) Cns_get_smd_by_pfid (&thip->dbfd, bof,
			    fmd_entry->fileid, smd_entry, 0, NULL, 1, smdlistptr);
			if (c < 0)
				return (serrno);
		} else if (getattr == 3) {	/* readdirc */
			*umd_entry->comments = '\0';
			if (Cns_get_umd_by_fileid (&thip->dbfd, fmd_entry->fileid,
			    umd_entry, 0, NULL) && serrno != ENOENT)
				return (serrno);
			cml = strlen (umd_entry->comments);
			if (fnl + cml > maxsize) break;
			marshall_STRING (sbp, fmd_entry->name);
			marshall_STRING (sbp, umd_entry->comments);
			nbentries++;
			maxsize -= ((direntsz + fnl + cml + 9) / 8) * 8;
		} else {			/* readdirxc */
			*umd_entry->comments = '\0';
			if (Cns_get_umd_by_fileid (&thip->dbfd, fmd_entry->fileid,
			    umd_entry, 0, NULL) && serrno != ENOENT)
				return (serrno);
			cml = strlen (umd_entry->comments);
			if (fnl + cml > maxsize) break;
			marshall_DIRX (&sbp, fmd_entry);
			marshall_STRING (sbp, umd_entry->comments);
			nbentries++;
			maxsize -= ((direntsz + fnl + cml + 9) / 8) * 8;
		}
		bod = 0;
	}
	if (c < 0)
		return (serrno);
	if (c == 1) {
		eod = 1;

		/* start transaction */

		(void) Cns_start_tr (thip->s, &thip->dbfd);

		/* update directory access time */

		if (Cns_get_fmd_by_fileid (&thip->dbfd, dir_fileid, &direntry,
		    1, &rec_addr))
			return (serrno);
		direntry.atime = time (0);
		if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &direntry))
			return (serrno);
	}
reply:
	marshall_WORD (sbp, eod);
	p = dirbuf;
	marshall_WORD (p, nbentries);		/* update nbentries in reply */
	sendrep (thip->s, MSG_DATA, sbp - dirbuf, dirbuf);
	return (0);
}

/*      Cns_srv_rename - rename a file or a directory */
 
int Cns_srv_rename(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int bof = 1;
	int c;
	u_signed64 cwd;
	DBLISTPTR dblistptr;
	char func[16];
	gid_t gid;
	char logbuf[2*CA_MAXPATHLEN+9];
	int n;
	char new_basename[CA_MAXNAMELEN+1];
	int new_exists = 0;
	struct Cns_file_metadata new_fmd_entry;
	struct Cns_file_metadata new_parent_dir;
	Cns_dbrec_addr new_rec_addr;
	Cns_dbrec_addr new_rec_addrp;
	char newpath[CA_MAXPATHLEN+1];
	char old_basename[CA_MAXNAMELEN+1];
	struct Cns_file_metadata old_fmd_entry;
	struct Cns_file_metadata old_parent_dir;
	Cns_dbrec_addr old_rec_addr;
	Cns_dbrec_addr old_rec_addrp;
	char oldpath[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addrs;	/* segment record address */
	Cns_dbrec_addr rec_addru;	/* comment record address */
	struct Cns_seg_metadata smd_entry;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_rename");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "rename", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, oldpath, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	if (unmarshall_STRINGN (rbp, newpath, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "rename %s %s", oldpath, newpath);
	Cns_logreq (func, logbuf);

	if (uid == 0){
                if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
                        return (serrno);
	}

	if (strcmp (oldpath, newpath) == 0)
		return (0);

	if (Cns_splitname (cwd, oldpath, old_basename))
		return (serrno);
	if (Cns_splitname (cwd, newpath, new_basename))
		return (serrno);

	if (*old_basename == '/' || *new_basename == '/')	/* nsrename / */
		return (EINVAL);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* check parents directory components for write/search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, oldpath, S_IWRITE|S_IEXEC, uid, gid,
	    clienthost, &old_parent_dir, &old_rec_addrp))
		return (serrno);
	if (Cns_chkdirperm (&thip->dbfd, cwd, newpath, S_IWRITE|S_IEXEC, uid, gid,
	    clienthost, &new_parent_dir, &new_rec_addrp))
		return (serrno);

	/* get and lock 'old' basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, old_parent_dir.fileid,
	    old_basename, &old_fmd_entry, 1, &old_rec_addr))
		return (serrno);

	/* if renaming a directory, 'new' must not be a descendant of 'old' */

	if (old_fmd_entry.filemode & S_IFDIR) {
		oldpath[strlen (oldpath)] = '/';
		newpath[strlen (newpath)] = '/';
		if (strlen (newpath) > (n = strlen (oldpath)) &&
		    strncmp (oldpath, newpath, n) == 0 &&
		    newpath[n] == '/')
			return (EINVAL);
	}

	/* check if 'new' basename entry exists already */

	if ((c = Cns_get_fmd_by_fullid (&thip->dbfd, new_parent_dir.fileid,
	    new_basename, &new_fmd_entry, 1, &new_rec_addr)) && serrno != ENOENT)
		return (serrno);

	if (c == 0) {	/* 'new' basename entry exists already */
		new_exists++;

		/* 'old' and 'new' must be of the same type */

		if ((old_fmd_entry.filemode & S_IFDIR) == 0 &&
		    new_fmd_entry.filemode & S_IFDIR)
			return (EISDIR);
		if (old_fmd_entry.filemode & S_IFDIR &&
		    (new_fmd_entry.filemode & S_IFDIR) == 0)
			return (ENOTDIR);

		/* if the existing 'new' entry is a directory, the directory
		   must be empty */

		if (new_fmd_entry.filemode & S_IFDIR && new_fmd_entry.nlink)
			return (EEXIST);

		/* if parent of 'new' has the sticky bit set,
		   the user must own 'new' or the parent of 'new' or
		   the basename entry must have write permission */

		if (new_parent_dir.filemode & S_ISVTX &&
		    uid != new_parent_dir.uid && uid != new_fmd_entry.uid &&
		    Cns_chkentryperm (&new_fmd_entry, S_IWRITE, uid, gid, clienthost))
			return (EACCES);
	}

	/* if 'old' is a directory, its basename entry must have write permission */

	if (old_fmd_entry.filemode & S_IFDIR)
		if (Cns_chkentryperm (&old_fmd_entry, S_IWRITE, uid, gid, clienthost))
			return (EACCES);

	/* if parent of 'old' has the sticky bit set,
	   the user must own 'old' or the parent of 'old' or
	   the basename entry must have write permission */

	if (old_parent_dir.filemode & S_ISVTX &&
	    uid != old_parent_dir.uid && uid != old_fmd_entry.uid &&
	    Cns_chkentryperm (&old_fmd_entry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	if (new_exists) {	/* must remove it */
		/* delete file segments if any */

		while ((c = Cns_get_smd_by_pfid (&thip->dbfd, bof,
		    new_fmd_entry.fileid, &smd_entry, 1, &rec_addrs, 0, &dblistptr)) == 0) {
			if (Cns_delete_smd_entry (&thip->dbfd, &rec_addrs))
				return (serrno);
			bof = 0;
		}
		(void) Cns_get_smd_by_pfid (&thip->dbfd, bof, new_fmd_entry.fileid,
		    &smd_entry, 1, &rec_addrs, 1, &dblistptr);	/* free res */
		if (c < 0)
			return (serrno);

		/* delete the comment if it exists */

		if (Cns_get_umd_by_fileid (&thip->dbfd, new_fmd_entry.fileid,
		    &umd_entry, 1, &rec_addru) == 0) {
			if (Cns_delete_umd_entry (&thip->dbfd, &rec_addru))
				return (serrno);
		} else if (serrno != ENOENT)
			return (serrno);

		if (Cns_delete_fmd_entry (&thip->dbfd, &new_rec_addr))
			return (serrno);
	}
	if (old_parent_dir.fileid != new_parent_dir.fileid) {

		/* update 'old' parent directory entry */

		old_parent_dir.nlink--;

		/* update 'new' parent directory entry */

		new_parent_dir.nlink++;
		new_parent_dir.mtime = time (0);
		new_parent_dir.ctime = new_parent_dir.mtime;
		if (Cns_update_fmd_entry (&thip->dbfd, &new_rec_addrp, &new_parent_dir))
			return (serrno);
	}

	/* update 'old' basename entry */

	old_fmd_entry.parent_fileid = new_parent_dir.fileid;
	strcpy (old_fmd_entry.name, new_basename);
	old_fmd_entry.ctime = time (0);
	if (Cns_update_fmd_entry (&thip->dbfd, &old_rec_addr, &old_fmd_entry))
		return (serrno);

	/* update parent directory entry */

	old_parent_dir.mtime = time (0);
	old_parent_dir.ctime = old_parent_dir.mtime;
	if (Cns_update_fmd_entry (&thip->dbfd, &old_rec_addrp, &old_parent_dir))
		return (serrno);
	return (0);
}

/*	Cns_srv_updateseg_checksum - Updates file segment checksum
    when previous value is NULL*/

int Cns_srv_updateseg_checksum(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int copyno;
	u_signed64 fileid;
	struct Cns_file_metadata filentry;
	int fsec;
	int fseq;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+34];
	struct Cns_seg_metadata old_smd_entry;
	struct Cns_file_metadata parent_dir;
	char *rbp;
	Cns_dbrec_addr rec_addr;
	Cns_dbrec_addr rec_addrs;
	u_signed64 segsize;
	int side;
	struct Cns_seg_metadata smd_entry;
	char tmpbuf[21];
	char tmpbuf2[21];
	uid_t uid;
	char vid[CA_MAXVIDLEN+1];
    int checksum_ok;
    
	strcpy (func, "Cns_srv_updateseg_checksum");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "updateseg_checksum", uid, gid, clienthost);
    unmarshall_HYPER (rbp, fileid);
    unmarshall_WORD (rbp, copyno);
    unmarshall_WORD (rbp, fsec);
	sprintf (logbuf, "updateseg_checksum %s %d %d",
             u64tostr (fileid, tmpbuf, 0), copyno, fsec);
	Cns_logreq (func, logbuf);

	/* start transaction */
	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* get/lock basename entry */
	if (Cns_get_fmd_by_fileid (&thip->dbfd, fileid, &filentry, 1, &rec_addr))
		return (serrno);

	/* check if the entry is a regular file */
	if (filentry.filemode & S_IFDIR)
		return (EISDIR);

	if (unmarshall_STRINGN (rbp, vid, CA_MAXVIDLEN+1))
		return (EINVAL);
	unmarshall_WORD (rbp, side);
	unmarshall_LONG (rbp, fseq);
    
	/* get/lock segment metadata entry to be updated */

	if (Cns_get_smd_by_fullid (&thip->dbfd, fileid, copyno, fsec,
                               &old_smd_entry, 1, &rec_addrs))
		return (serrno);
    
	if (strcmp (old_smd_entry.vid, vid) || old_smd_entry.side != side ||
	    old_smd_entry.fseq != fseq)
		return (SEENTRYNFND);

	sprintf (logbuf, "old segment: %s %d %d %s %d %c %s %d %d %02x%02x%02x%02x \"%s\" %x",
             u64tostr (old_smd_entry.s_fileid, tmpbuf, 0), old_smd_entry.copyno,
             old_smd_entry.fsec, u64tostr (old_smd_entry.segsize, tmpbuf2, 0),
             old_smd_entry.compression, old_smd_entry.s_status, old_smd_entry.vid,
             old_smd_entry.side, old_smd_entry.fseq, old_smd_entry.blockid[0],
             old_smd_entry.blockid[1], old_smd_entry.blockid[2], old_smd_entry.blockid[3],
             old_smd_entry.checksum_name, old_smd_entry.checksum);
	Cns_logreq (func, logbuf);

    /* Checking that the segment has not checksum */
    if (!(old_smd_entry.checksum_name == NULL
          || old_smd_entry.checksum_name[0] == '\0')) {
        sprintf (logbuf, "old checksum \"%s\" %d non NULL, Cannot overwrite",
                 old_smd_entry.checksum_name,
                 old_smd_entry.checksum);
        Cns_logreq (func, logbuf);
        return(EPERM);
    }
        
	memset ((char *) &smd_entry, 0, sizeof(smd_entry));
	smd_entry.s_fileid = fileid;
	smd_entry.copyno = copyno;
	smd_entry.fsec = fsec;
	smd_entry.segsize = old_smd_entry.segsize;
    smd_entry.compression = old_smd_entry.compression;
    smd_entry.s_status = old_smd_entry.s_status;
    strcpy(smd_entry.vid, old_smd_entry.vid);
    smd_entry.side = old_smd_entry.side;
    smd_entry.fseq = old_smd_entry.fseq;
    memcpy(smd_entry.blockid, old_smd_entry.blockid, 4);
    unmarshall_STRINGN (rbp, smd_entry.checksum_name, CA_MAXCKSUMNAMELEN);
    smd_entry.checksum_name[CA_MAXCKSUMNAMELEN] = '\0';
    unmarshall_LONG (rbp, smd_entry.checksum);    

    if (smd_entry.checksum_name == NULL
        || strlen(smd_entry.checksum_name) == 0) {
        checksum_ok = 0;
    } else {
        checksum_ok = 1;
    }

    if (magic >= CNS_MAGIC4) {
        
        /* Checking that we can't have a NULL checksum name when a
           checksum is specified */
        if (!checksum_ok
            && smd_entry.checksum != 0) {
            sprintf (logbuf, "setsegattrs: NULL checksum name with non zero value, overriding");
            Cns_logreq (func, logbuf);
            smd_entry.checksum = 0;
        } 
    }
    
	sprintf (logbuf, "new segment: %s %d %d %s %d %c %s %d %d %02x%02x%02x%02x \"%s\" %x",
	    u64tostr (smd_entry.s_fileid, tmpbuf, 0), smd_entry.copyno,
	    smd_entry.fsec, u64tostr (smd_entry.segsize, tmpbuf2, 0),
	    smd_entry.compression, smd_entry.s_status, smd_entry.vid,
	    smd_entry.side, smd_entry.fseq, smd_entry.blockid[0],
	    smd_entry.blockid[1], smd_entry.blockid[2], smd_entry.blockid[3],
        smd_entry.checksum_name, smd_entry.checksum);
	Cns_logreq (func, logbuf);


        
	/* update file segment entry */

	if (Cns_update_smd_entry (&thip->dbfd, &rec_addrs, &smd_entry))
		return (serrno);

	return (0);
}




/*	Cns_srv_replaceseg - replace file segment */

int Cns_srv_replaceseg(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int copyno;
	u_signed64 fileid;
	struct Cns_file_metadata filentry;
	int fsec;
	int fseq;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+34];
	struct Cns_seg_metadata old_smd_entry;
	struct Cns_file_metadata parent_dir;
	char *rbp;
	Cns_dbrec_addr rec_addr;
	Cns_dbrec_addr rec_addrs;
	u_signed64 segsize;
	int side;
	struct Cns_seg_metadata smd_entry;
	char tmpbuf[21];
	char tmpbuf2[21];
	uid_t uid;
	char vid[CA_MAXVIDLEN+1];
    int checksum_ok;
    
	strcpy (func, "Cns_srv_replaceseg");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "replaceseg", uid, gid, clienthost);
        unmarshall_HYPER (rbp, fileid);
        unmarshall_WORD (rbp, copyno);
        unmarshall_WORD (rbp, fsec);
	sprintf (logbuf, "replaceseg %s %d %d",
	    u64tostr (fileid, tmpbuf, 0), copyno, fsec);
	Cns_logreq (func, logbuf);

	/* check if the user is authorized to replace segment attributes */

	if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
		return (serrno);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* get/lock basename entry */

	if (Cns_get_fmd_by_fileid (&thip->dbfd, fileid, &filentry, 1, &rec_addr))
		return (serrno);

	/* check if the entry is a regular file */

	if (filentry.filemode & S_IFDIR)
		return (EISDIR);

	if (unmarshall_STRINGN (rbp, vid, CA_MAXVIDLEN+1))
		return (EINVAL);
	unmarshall_WORD (rbp, side);
	unmarshall_LONG (rbp, fseq);

	/* get/lock segment metadata entry to be updated */

	if (Cns_get_smd_by_fullid (&thip->dbfd, fileid, copyno, fsec,
	    &old_smd_entry, 1, &rec_addrs))
		return (serrno);

	if (strcmp (old_smd_entry.vid, vid) || old_smd_entry.side != side ||
	    old_smd_entry.fseq != fseq)
		return (SEENTRYNFND);

	sprintf (logbuf, "old segment: %s %d %d %s %d %c %s %d %d %02x%02x%02x%02x \"%s\" %x",
	    u64tostr (old_smd_entry.s_fileid, tmpbuf, 0), old_smd_entry.copyno,
	    old_smd_entry.fsec, u64tostr (old_smd_entry.segsize, tmpbuf2, 0),
	    old_smd_entry.compression, old_smd_entry.s_status, old_smd_entry.vid,
	    old_smd_entry.side, old_smd_entry.fseq, old_smd_entry.blockid[0],
	    old_smd_entry.blockid[1], old_smd_entry.blockid[2], old_smd_entry.blockid[3],
        old_smd_entry.checksum_name, old_smd_entry.checksum);
	Cns_logreq (func, logbuf);

	memset ((char *) &smd_entry, 0, sizeof(smd_entry));
	smd_entry.s_fileid = fileid;
	smd_entry.copyno = copyno;
	smd_entry.fsec = fsec;
	smd_entry.segsize = old_smd_entry.segsize;
	unmarshall_LONG (rbp, smd_entry.compression);
	smd_entry.s_status = old_smd_entry.s_status;
	if (unmarshall_STRINGN (rbp, smd_entry.vid, CA_MAXVIDLEN+1))
		return (EINVAL);
	unmarshall_WORD (rbp, smd_entry.side);
	unmarshall_LONG (rbp, smd_entry.fseq);
	unmarshall_OPAQUE (rbp, smd_entry.blockid, 4);
    if (magic >= CNS_MAGIC3) {
        unmarshall_STRINGN (rbp, smd_entry.checksum_name, CA_MAXCKSUMNAMELEN);
        smd_entry.checksum_name[CA_MAXCKSUMNAMELEN] = '\0';
        unmarshall_LONG (rbp, smd_entry.checksum);    
    } else {
        smd_entry.checksum_name[0] = '\0';
        smd_entry.checksum = 0;
    }

    if (smd_entry.checksum_name == NULL
        || strlen(smd_entry.checksum_name) == 0) {
        checksum_ok = 0;
    } else {
        checksum_ok = 1;
    }

   if (magic >= CNS_MAGIC4) {
        
        /* Checking that we can't have a NULL checksum name when a
           checksum is specified */
        if (!checksum_ok
            && smd_entry.checksum != 0) {
            sprintf (logbuf, "setsegattrs: NULL checksum name with non zero value, overriding");
            Cns_logreq (func, logbuf);
            smd_entry.checksum = 0;
        } 
    }
    
	sprintf (logbuf, "new segment: %s %d %d %s %d %c %s %d %d %02x%02x%02x%02x \"%s\" %x",
	    u64tostr (smd_entry.s_fileid, tmpbuf, 0), smd_entry.copyno,
	    smd_entry.fsec, u64tostr (smd_entry.segsize, tmpbuf2, 0),
	    smd_entry.compression, smd_entry.s_status, smd_entry.vid,
	    smd_entry.side, smd_entry.fseq, smd_entry.blockid[0],
	    smd_entry.blockid[1], smd_entry.blockid[2], smd_entry.blockid[3],
        smd_entry.checksum_name, smd_entry.checksum);
	Cns_logreq (func, logbuf);

	/* update file segment entry */

	if (Cns_update_smd_entry (&thip->dbfd, &rec_addrs, &smd_entry))
		return (serrno);

	return (0);
}

/*      Cns_srv_rmdir - remove a directory entry */
 
int Cns_srv_rmdir(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	struct Cns_class_metadata class_entry;
	u_signed64 cwd;
	struct Cns_file_metadata direntry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+7];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;
	Cns_dbrec_addr rec_addrc;
	Cns_dbrec_addr rec_addrp;
	Cns_dbrec_addr rec_addru;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_rmdir");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "rmdir", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "rmdir %s", path);
	Cns_logreq (func, logbuf);

	if (uid == 0){
                if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
                        return (serrno);
	}

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/')	/* Cns_rmdir / */
		return (EINVAL);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* check parent directory components for write/search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IWRITE|S_IEXEC, uid, gid,
	    clienthost, &parent_dir, &rec_addrp))
		return (serrno);

	/* get and lock requested directory entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &direntry, 1, &rec_addr))
		return (serrno);

	if ((direntry.filemode & S_IFDIR) == 0)
		return (ENOTDIR);
	if (direntry.fileid == cwd)
		return (EINVAL);	/* cannot remove current working directory */
	if (direntry.nlink)
		return (EEXIST);

	/* if the parent has the sticky bit set,
	   the user must own the directory or the parent or
	   the basename entry must have write permission */

	if (parent_dir.filemode & S_ISVTX &&
	    uid != parent_dir.uid && uid != direntry.uid &&
	    Cns_chkentryperm (&direntry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* delete the comment if it exists */

	if (Cns_get_umd_by_fileid (&thip->dbfd, direntry.fileid, &umd_entry, 1,
	    &rec_addru) == 0) {
		if (Cns_delete_umd_entry (&thip->dbfd, &rec_addru))
			return (serrno);
	} else if (serrno != ENOENT)
		return (serrno);

	/* delete directory entry */

	if (Cns_delete_fmd_entry (&thip->dbfd, &rec_addr))
		return (serrno);

	/* update parent directory entry */

	parent_dir.nlink--;
	parent_dir.mtime = time (0);
	parent_dir.ctime = parent_dir.mtime;
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addrp, &parent_dir))
		return (serrno);

	/* update nbdirs_using_class in Cns_class_metadata */

	if (direntry.fileclass > 0) {
		if (Cns_get_class_by_id (&thip->dbfd, direntry.fileclass,
		    &class_entry, 1, &rec_addrc))
			return (serrno);
		class_entry.nbdirs_using_class--;
		if (Cns_update_class_entry (&thip->dbfd, &rec_addrc, &class_entry))
			return (serrno);
	}
	return (0);
}

/*	Cns_srv_setatime - set last access time */

int Cns_srv_setatime(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	u_signed64 fileid;
	struct Cns_file_metadata filentry;
	char func[17];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+31];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;
	char tmpbuf[21];
	uid_t uid;

	strcpy (func, "Cns_srv_setatime");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "setatime", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
        unmarshall_HYPER (rbp, fileid);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "setatime %s %s", u64tostr (fileid, tmpbuf, 0), path);
	Cns_logreq (func, logbuf);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);
 
	if (fileid) {
		/* get/lock basename entry */

		if (Cns_get_fmd_by_fileid (&thip->dbfd, fileid,
		    &filentry, 1, &rec_addr))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkbackperm (&thip->dbfd, filentry.parent_fileid,
		    S_IEXEC, uid, gid, clienthost))
			return (serrno);
	} else {
		if (Cns_splitname (cwd, path, basename))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &parent_dir, NULL))
			return (serrno);

		/* get/lock basename entry */

		if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
		    &filentry, 1, &rec_addr))
			return (serrno);
	}
 
	/* check if the entry is a regular file and
	   if the user is authorized to set access time for this entry */

	if (filentry.filemode & S_IFDIR)
		return (EISDIR);
	if (uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IREAD, uid, gid, clienthost))
		return (EACCES);

	/* update entry */

	filentry.atime = time (0);

	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &filentry))
		return (serrno);
	return (0);
}

/*	Cns_srv_setcomment - add/replace a comment associated with a file/directory */

int Cns_srv_setcomment(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	char comment[CA_MAXCOMMENTLEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata filentry;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	struct Cns_user_metadata old_umd_entry;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addru;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_setcomment");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "setcomment", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	if (unmarshall_STRINGN (rbp, comment, CA_MAXCOMMENTLEN+1))
		return (EINVAL);
	sprintf (logbuf, "setcomment %s", path);
	Cns_logreq (func, logbuf);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* check parent directory components for search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
	    clienthost, &parent_dir, NULL))
		return (serrno);

	/* get basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 0, NULL))
		return (serrno);

	/* check if the user is authorized to add/replace the comment on this entry */

	if (uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* add the comment or replace the comment if it exists */

	memset ((char *) &umd_entry, 0, sizeof(umd_entry));
	umd_entry.u_fileid = filentry.fileid;
	strcpy (umd_entry.comments, comment);
	if (Cns_insert_umd_entry (&thip->dbfd, &umd_entry)) {
		if (serrno != EEXIST ||
		    Cns_get_umd_by_fileid (&thip->dbfd, filentry.fileid,
			&old_umd_entry, 1, &rec_addru) ||
		    Cns_update_umd_entry (&thip->dbfd, &rec_addru, &umd_entry))
			return (serrno);
	}
	return (0);
}

/*	Cns_srv_setfsize - set file size and last modification time */

int Cns_srv_setfsize(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	u_signed64 fileid;
	struct Cns_file_metadata filentry;
	u_signed64 filesize;
	char func[17];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+52];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;
	char tmpbuf[21];
	char tmpbuf2[21];
	uid_t uid;

	strcpy (func, "Cns_srv_setfsize");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "setfsize", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
        unmarshall_HYPER (rbp, fileid);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_HYPER (rbp, filesize);
	sprintf (logbuf, "setfsize %s %s %s", u64tostr (fileid, tmpbuf, 0),
	    path, u64tostr (filesize, tmpbuf2, 0));
	Cns_logreq (func, logbuf);
 
	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (fileid) {
		/* get/lock basename entry */

		if (Cns_get_fmd_by_fileid (&thip->dbfd, fileid,
		    &filentry, 1, &rec_addr))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkbackperm (&thip->dbfd, filentry.parent_fileid,
		    S_IEXEC, uid, gid, clienthost))
			return (serrno);
	} else {
		if (Cns_splitname (cwd, path, basename))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &parent_dir, NULL))
			return (serrno);

		/* get/lock basename entry */

		if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
		    &filentry, 1, &rec_addr))
			return (serrno);
	}

	/* check if the entry is a regular file and
	   if the user is authorized to set modification time for this entry */

	if (filentry.filemode & S_IFDIR)
		return (EISDIR);
	if (uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* update entry */

	filentry.filesize = filesize;
	filentry.mtime = time (0);
	filentry.ctime = filentry.mtime;

	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &filentry))
		return (serrno);
	return (0);
}

/*	Cns_srv_setsegattrs - set file segment attributes */

int Cns_srv_setsegattrs(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	int copyno = 0;
	u_signed64 cwd;
	u_signed64 fileid;
	struct Cns_file_metadata filentry;
	int fsec;
	char func[20];
	gid_t gid;
	int i;
	char logbuf[CA_MAXPATHLEN+34];
	int nbseg;
	struct Cns_seg_metadata old_smd_entry;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;
	Cns_dbrec_addr rec_addrs;
	struct Cns_seg_metadata smd_entry;
	char tmpbuf[21];
	char tmpbuf2[21];
	uid_t uid;

	strcpy (func, "Cns_srv_setsegattrs");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "setsegattrs", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        unmarshall_HYPER (rbp, fileid);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_WORD (rbp, nbseg);
	sprintf (logbuf, "setsegattrs %s %s",
	    u64tostr (fileid, tmpbuf, 0), path);
	Cns_logreq (func, logbuf);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (fileid) {
		/* get/lock basename entry */

		if (Cns_get_fmd_by_fileid (&thip->dbfd, fileid,
		    &filentry, 1, &rec_addr))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkbackperm (&thip->dbfd, filentry.parent_fileid,
		    S_IEXEC, uid, gid, clienthost))
			return (serrno);
	} else {
		if (Cns_splitname (cwd, path, basename))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
		    clienthost, &parent_dir, NULL))
			return (serrno);

		/* get/lock basename entry */

		if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
		    &filentry, 1, &rec_addr))
			return (serrno);
	}

	/* check if the entry is a regular file */

	if (filentry.filemode & S_IFDIR)
		return (EISDIR);

	for (i = 0; i < nbseg; i++) {
		memset ((char *) &smd_entry, 0, sizeof(smd_entry));
		smd_entry.s_fileid = filentry.fileid;
		unmarshall_WORD (rbp, smd_entry.copyno);
		unmarshall_WORD (rbp, smd_entry.fsec);
		unmarshall_HYPER (rbp, smd_entry.segsize);
		unmarshall_LONG (rbp, smd_entry.compression);
		unmarshall_BYTE (rbp, smd_entry.s_status);
		if (unmarshall_STRINGN (rbp, smd_entry.vid, CA_MAXVIDLEN+1))
			return (EINVAL);
		if (magic >= CNS_MAGIC2)
			unmarshall_WORD (rbp, smd_entry.side);
		unmarshall_LONG (rbp, smd_entry.fseq);
		unmarshall_OPAQUE (rbp, smd_entry.blockid, 4);
		if (magic >= CNS_MAGIC4) {
			unmarshall_STRINGN (rbp, smd_entry.checksum_name, CA_MAXCKSUMNAMELEN);
            smd_entry.checksum_name[CA_MAXCKSUMNAMELEN] = '\0';
            unmarshall_LONG (rbp, smd_entry.checksum);
        } else {
            smd_entry.checksum_name[0] = '\0';
            smd_entry.checksum = 0;
        }
        
		/* Automatically set the copy number if not provided */

		if (smd_entry.copyno == 0) {
			if (copyno == 0) {
				if (Cns_get_max_copyno (&thip->dbfd,
				    smd_entry.s_fileid, &copyno) &&
				    serrno != ENOENT)
					return (serrno);
				copyno++;
			}
			smd_entry.copyno = copyno;
		}
		sprintf (logbuf, "setsegattrs %s %d %d %s %d %c %s %d %02x%02x%02x%02x %s:%x",
		    u64tostr (smd_entry.s_fileid, tmpbuf, 0), smd_entry.copyno,
		    smd_entry.fsec, u64tostr (smd_entry.segsize, tmpbuf2, 0),
		    smd_entry.compression, smd_entry.s_status, smd_entry.vid,
		    smd_entry.fseq, smd_entry.blockid[0], smd_entry.blockid[1],
            smd_entry.blockid[2], smd_entry.blockid[3],
            smd_entry.checksum_name, smd_entry.checksum);
		Cns_logreq (func, logbuf);

        if (magic >= CNS_MAGIC4) {

            /* Checking that we can't have a NULL checksum name when a
               checksum is specified */
            if ((smd_entry.checksum_name == NULL
                 || strlen(smd_entry.checksum_name) == 0)
                && smd_entry.checksum != 0) {
                sprintf (logbuf, "setsegattrs: invalid checksum name with non zero value");
                return(EINVAL);
            } 
        }
        
		/* insert/update file segment entry */

		if (Cns_insert_smd_entry (&thip->dbfd, &smd_entry)) {
			if (serrno != EEXIST ||
			    Cns_get_smd_by_fullid (&thip->dbfd,
				smd_entry.s_fileid, smd_entry.copyno,
				smd_entry.fsec, &old_smd_entry, 1, &rec_addrs) ||
			    Cns_update_smd_entry (&thip->dbfd, &rec_addrs,
				&smd_entry))
				return (serrno);
		}
	}

	/* delete old segments if they were more numerous */

	fsec = nbseg + 1;
	while (Cns_get_smd_by_fullid (&thip->dbfd, smd_entry.s_fileid, copyno,
	    fsec, &old_smd_entry, 1, &rec_addrs) == 0) {
		if (Cns_delete_smd_entry (&thip->dbfd, &rec_addrs))
			return (serrno);
		fsec++;
	}

	if (filentry.status != 'm') {
		filentry.status = 'm';
		if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &filentry))
			return (serrno);
	}
	return (0);
}

/*	Cns_srv_shutdown - shutdown the name server */

int Cns_srv_shutdown(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int force = 0;
	char func[17];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+6];
	char *rbp;
	uid_t uid;

	strcpy (func, "Cns_srv_shutdown");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "shutdown", uid, gid, clienthost);
	unmarshall_WORD (rbp, force);

	if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
		return (serrno);

	being_shutdown = force + 1;
	return (0);
}

/*	Cns_srv_stat - get information about a file or a directory */

int Cns_srv_stat(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	u_signed64 fileid;
	struct Cns_file_metadata fmd_entry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+6];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char repbuf[57];
	char *sbp;
	char tmpbuf[21];
	uid_t uid;

	strcpy (func, "Cns_srv_stat");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "stat", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	unmarshall_HYPER (rbp, fileid);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "stat %s %s", u64tostr(fileid, tmpbuf, 0), path);
	Cns_logreq (func, logbuf);

	if (fileid) {
		/* get basename entry */

		if (Cns_get_fmd_by_fileid (&thip->dbfd, fileid,
		    &fmd_entry, 0, NULL))
			return (serrno);

		/* check parent directory components for search permission */

		if (Cns_chkbackperm (&thip->dbfd, fmd_entry.parent_fileid,
		    S_IEXEC, uid, gid, clienthost))
			return (serrno);
	} else {
		if (strcmp (path, ".") == 0) {
			if (Cns_get_fmd_by_fileid (&thip->dbfd, cwd, &fmd_entry, 0, NULL))
				return (serrno);
		} else {
			if (Cns_splitname (cwd, path, basename))
				return (serrno);

			if (*basename == '/') {	/* Cns_stat / */
				parent_dir.fileid = 0;
			} else { /* check parent directory components for search permission */

				if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC,
				    uid, gid, clienthost, &parent_dir, NULL))
					return (serrno);
			}

			/* get requested entry */

			if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid,
			    basename, &fmd_entry, 0, NULL))
					return (serrno);
		}
	}
	sbp = repbuf;
	marshall_HYPER (sbp, fmd_entry.fileid);
	marshall_WORD (sbp, fmd_entry.filemode);
	marshall_LONG (sbp, fmd_entry.nlink);
	marshall_LONG (sbp, fmd_entry.uid);
	marshall_LONG (sbp, fmd_entry.gid);
	marshall_HYPER (sbp, fmd_entry.filesize);
	marshall_TIME_T (sbp, fmd_entry.atime);
	marshall_TIME_T (sbp, fmd_entry.mtime);
	marshall_TIME_T (sbp, fmd_entry.ctime);
	marshall_WORD (sbp, fmd_entry.fileclass);
	marshall_BYTE (sbp, fmd_entry.status);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

/*      Cns_srv_undelete - logically restore a file entry */
 
int Cns_srv_undelete(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	int bof = 1;
	int c;
	u_signed64 cwd;
	DBLISTPTR dblistptr;
	struct Cns_file_metadata filentry;
	char func[17];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+10];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;	/* file record address */
	Cns_dbrec_addr rec_addrp;	/* parent record address */
	Cns_dbrec_addr rec_addrs;	/* segment record address */
	struct Cns_seg_metadata smd_entry;
	uid_t uid;

	strcpy (func, "Cns_srv_undelete");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "undelete", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "undelete %s", path);
	Cns_logreq (func, logbuf);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/')	/* Cns_undelete / */
		return (EINVAL);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* check parent directory components for write/search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IWRITE|S_IEXEC, uid, gid,
	    clienthost, &parent_dir, &rec_addrp))
		return (serrno);

	/* get and lock requested file entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 1, &rec_addr))
		return (serrno);

	if (filentry.filemode & S_IFDIR)
		return (EPERM);

	/* if the parent has the sticky bit set,
	   the user must own the file or the parent or
	   the basename entry must have write permission */

	if (parent_dir.filemode & S_ISVTX &&
	    uid != parent_dir.uid && uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* remove the mark "logically deleted" on the file segments if any */

	while ((c = Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
	    &smd_entry, 1, &rec_addrs, 0, &dblistptr)) == 0) {
		smd_entry.s_status = '-';
		if (Cns_update_smd_entry (&thip->dbfd, &rec_addrs, &smd_entry))
			return (serrno);
		bof = 0;
	}
	(void) Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
	    &smd_entry, 1, &rec_addrs, 1, &dblistptr);	/* free res */
	if (c < 0)
		return (serrno);

	/* remove the mark "logically deleted" */

	if (bof)
		filentry.status = '-';
	else
		filentry.status = 'm';
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &filentry))
		return (serrno);

	/* update parent directory entry */

	parent_dir.mtime = time (0);
	parent_dir.ctime = parent_dir.mtime;
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addrp, &parent_dir))
		return (serrno);
	return (0);
}

/*      Cns_srv_unlink - remove a file entry */
 
int Cns_srv_unlink(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	int bof = 1;
	int c;
	u_signed64 cwd;
	DBLISTPTR dblistptr;
	struct Cns_file_metadata filentry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+8];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;	/* file record address */
	Cns_dbrec_addr rec_addrp;	/* parent record address */
	Cns_dbrec_addr rec_addrs;	/* segment record address */
	Cns_dbrec_addr rec_addru;	/* comment record address */
	struct Cns_seg_metadata smd_entry;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_unlink");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "unlink", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "unlink %s", path);
	Cns_logreq (func, logbuf);
	if (uid == 0){
                if (Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
                        return (serrno);
	}
	

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	if (*basename == '/')	/* Cns_unlink / */
		return (EINVAL);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	/* check parent directory components for write/search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IWRITE|S_IEXEC, uid, gid,
	    clienthost, &parent_dir, &rec_addrp))
		return (serrno);

	/* get and lock requested file entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 1, &rec_addr))
		return (serrno);

	if (filentry.filemode & S_IFDIR)
		return (EPERM);

	/* if the parent has the sticky bit set,
	   the user must own the file or the parent or
	   the basename entry must have write permission */

	if (parent_dir.filemode & S_ISVTX &&
	    uid != parent_dir.uid && uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* delete file segments if any */

	while ((c = Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
	    &smd_entry, 1, &rec_addrs, 0, &dblistptr)) == 0) {
		if (Cns_delete_smd_entry (&thip->dbfd, &rec_addrs))
			return (serrno);
		bof = 0;
	}
	(void) Cns_get_smd_by_pfid (&thip->dbfd, bof, filentry.fileid,
	    &smd_entry, 1, &rec_addrs, 1, &dblistptr);	/* free res */
	if (c < 0)
		return (serrno);

	/* delete the comment if it exists */

	if (Cns_get_umd_by_fileid (&thip->dbfd, filentry.fileid, &umd_entry, 1,
	    &rec_addru) == 0) {
		if (Cns_delete_umd_entry (&thip->dbfd, &rec_addru))
			return (serrno);
	} else if (serrno != ENOENT)
		return (serrno);

	/* delete file entry */

	if (Cns_delete_fmd_entry (&thip->dbfd, &rec_addr))
		return (serrno);

	/* update parent directory entry */

	parent_dir.nlink--;
	parent_dir.mtime = time (0);
	parent_dir.ctime = parent_dir.mtime;
	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addrp, &parent_dir))
		return (serrno);
	return (0);
}

/*	Cns_srv_utime - set last access and modification times */

int Cns_srv_utime(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	time_t actime;
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata filentry;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+19];
	time_t modtime;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addr;
	uid_t uid;
	int user_specified_time;

	strcpy (func, "Cns_srv_utime");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "utime", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, user_specified_time);
	if (user_specified_time) {
		unmarshall_TIME_T (rbp, actime);
		unmarshall_TIME_T (rbp, modtime);
	}
	sprintf (logbuf, "utime %s %d", path, user_specified_time);
	Cns_logreq (func, logbuf);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);
 
	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* check parent directory components for search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
	    clienthost, &parent_dir, NULL))
		return (serrno);

	/* get/lock basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 1, &rec_addr))
		return (serrno);
 
	/* check if the user is authorized to set access/modification time
	   for this entry */

	if (user_specified_time) {
		if (uid != filentry.uid &&
		    Cupv_check (uid, gid, clienthost, localhost, P_ADMIN))
			return (EPERM);
	} else {
		if (uid != filentry.uid &&
		    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
			return (EACCES);
	}

	/* update entry */

	filentry.ctime = time (0);
	if (user_specified_time) {
		filentry.atime = actime;
		filentry.mtime = modtime;
	} else {
		filentry.atime = filentry.ctime;
		filentry.mtime = filentry.ctime;
	}

	if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &filentry))
		return (serrno);
	return (0);
}

/*	Cns_srv_setactualpath - add/replace a comment associated with a file/directory */

int Cns_srv_setactualpath(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	char comment[CA_MAXCOMMENTLEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata filentry;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	struct Cns_user_metadata old_umd_entry;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addru;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_setactualpath");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "setactualpath", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	if (unmarshall_STRINGN (rbp, comment, CA_MAXCOMMENTLEN+1))
		return (EINVAL);
	sprintf (logbuf, "setactualpath %s", path);
	Cns_logreq (func, logbuf);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* check parent directory components for search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
	    clienthost, &parent_dir, NULL))
		return (serrno);

	/* get basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 0, NULL))
		return (serrno);

	/* check if the user is authorized to add/replace the comment on this entry */

	if (uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* add the comment or replace the comment if it exists */

	memset ((char *) &umd_entry, 0, sizeof(umd_entry));
	umd_entry.u_fileid = filentry.fileid;
	strcpy (umd_entry.comments, comment);
	if (Cns_insert_fap_entry (&thip->dbfd, &umd_entry)) {
		if (serrno != EEXIST ||
		    Cns_get_fap_by_fileid (&thip->dbfd, filentry.fileid,
			&old_umd_entry, 1, &rec_addru) ||
		    Cns_update_fap_entry (&thip->dbfd, &rec_addru, &umd_entry))
			return (serrno);
	}
	return (0);
}


/*	Cns_srv_delactualpath - delete a comment associated with a file/directory */

int Cns_srv_delactualpath(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata filentry;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addru;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_delactualpath");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "delactualpath", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "delactualpath %s", path);
	Cns_logreq (func, logbuf);

	/* start transaction */

	(void) Cns_start_tr (thip->s, &thip->dbfd);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* check parent directory components for search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
	    clienthost, &parent_dir, NULL))
		return (serrno);

	/* get basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 0, NULL))
		return (serrno);

	/* check if the user is authorized to delete the comment on this entry */

	if (uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IWRITE, uid, gid, clienthost))
		return (EACCES);

	/* delete the comment if it exists */

	if (Cns_get_fap_by_fileid (&thip->dbfd, filentry.fileid, &umd_entry, 1,
	    &rec_addru))
		return (serrno);
	if (Cns_delete_fap_entry (&thip->dbfd, &rec_addru))
		return (serrno);
	return (0);
}


int Cns_srv_getactualpath(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata filentry;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char repbuf[CA_MAXCOMMENTLEN+1];
	char *sbp;
	uid_t uid;
	struct Cns_user_metadata umd_entry;

	strcpy (func, "Cns_srv_getactualpath");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "getactualpath", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "getactualpath %s", path);
	Cns_logreq (func, logbuf);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* check parent directory components for search permission */

	if (Cns_chkdirperm (&thip->dbfd, cwd, path, S_IEXEC, uid, gid,
	    clienthost, &parent_dir, NULL))
		return (serrno);

	/* get basename entry */

	if (Cns_get_fmd_by_fullid (&thip->dbfd, parent_dir.fileid, basename,
	    &filentry, 0, NULL))
		return (serrno);

	/* check if the user is authorized to get the comment for this entry */

	if (uid != filentry.uid &&
	    Cns_chkentryperm (&filentry, S_IREAD, uid, gid, clienthost))
		return (EACCES);

	/* get the comment if it exists */

	if (Cns_get_fap_by_fileid (&thip->dbfd, filentry.fileid, &umd_entry, 0,
	    NULL))
		return (serrno);

	sbp = repbuf;
	marshall_STRING (sbp, umd_entry.comments);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

int Cns_srv_get_Data_daemon (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        char basename[CA_MAXNAMELEN+1];
	char comment[CA_MAXCOMMENTLEN+1];
	u_signed64 cwd;
	struct Cns_file_metadata filentry;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	struct Cns_user_metadata old_umd_entry;
	struct Cns_file_metadata parent_dir;
	char path[CA_MAXPATHLEN+1];
	char filename[CA_MAXNAMELEN+1];
	char *rbp;
	char *sbp;
	Cns_dbrec_addr rec_addru;
	uid_t uid;
	char repbuf[5700];
	struct Cns_user_metadata umd_entry;
	strcpy (func, "Cns_srv_get_Data_daemon");
	rbp = req_data;
	unmarshall_HYPER(rbp, cwd);
	unmarshall_STRING(rbp, path);
//	unmarshall_STRING(rbp, filename);
	Cns_splitname(cwd,path,filename);
	nslogit(func, NS092, "Cns_srv_get_Data_daemon", uid, gid, clienthost);
	sprintf(logbuf, "getDataDaemon %s", path);
	Cns_logreq(func, logbuf);
	if(Cns_get_ftmd_by_fullpath(&thip->dbfd, path, filename, &filentry, 0, NULL))
		return(serrno);
	sbp = repbuf;
	marshall_HYPER(sbp, filentry.fileid);
	marshall_LONG (sbp, filentry.uid);
        marshall_LONG (sbp, filentry.gid);
        marshall_HYPER (sbp, filentry.ino);
        marshall_TIME_T (sbp, filentry.mtime);
        marshall_TIME_T (sbp, filentry.ctime);
        marshall_TIME_T (sbp, filentry.atime);
        marshall_LONG (sbp, filentry.nlink);
        marshall_LONG (sbp, filentry.dev);
        marshall_HYPER (sbp, filentry.filesize);
        marshall_WORD (sbp, filentry.filemode);
	marshall_WORD (sbp, filentry.fileclass);
	marshall_BYTE (sbp, filentry.status);
        marshall_STRING (sbp, filentry.path);
        marshall_STRING (sbp, filentry.name);
	sendrep(thip->s, MSG_DATA, sbp-repbuf, repbuf);
	return (0);
}

int Cns_srv_opendir_t(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	u_signed64 cwd;
	struct Cns_file_metadata direntry;
	char func[16];
	char logbuf[CA_MAXPATHLEN+9];
	gid_t gid;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char repbuf[8];
	char *sbp;
	uid_t uid;
        char filename[CA_MAXNAMELEN+1];

	strcpy (func, "Cns_srv_opendir_r");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "opendir_r", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "opendir_r %s", path);
	Cns_logreq (func, logbuf);

	if (! cwd && *path == 0)
		return (ENOENT);
	if (! cwd && *path != '/')
		return (EINVAL);

	if (strcmp (path, ".") == 0) {
		if (Cns_get_fmd_by_fileid (&thip->dbfd, cwd, &direntry, 0, NULL))
			return (serrno);
	} else {
		Cns_splitname(cwd,path,filename);
		if(Cns_get_ftmd_by_fullpath(&thip->dbfd, path, filename, &direntry, 0, NULL))
               		 return(serrno);

	}

	/* return directory fileid */

	sbp = repbuf;
	marshall_HYPER (sbp, direntry.fileid);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return (0);
}

/*      Cns_srv_readdir - read directory entries */

int Cns_srv_readdir_t(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip,struct Cns_file_metadata *fmd_entry,struct Cns_seg_metadata *smd_entry,struct Cns_user_metadata *umd_entry,int endlist,DBLISTPTR *dblistptr,DBLISTPTR *smdlistptr)
{
	int bod;	/* beginning of directory flag */
	int bof;	/* beginning of file flag */
	int c;
	int cml;	/* comment length */
	char dirbuf[DIRBUFSZ+4];
	struct Cns_file_metadata direntry;
	int direntsz;	/* size of client machine dirent structure excluding d_name */
	u_signed64 dir_fileid;
	int eod = 0;	/* end of directory flag */
	int fnl;	/* filename length */
	char func[16];
	int getattr;
	gid_t gid;
	int maxsize;
	int nbentries = 0;
	char *p;
	char *rbp;
	Cns_dbrec_addr rec_addr;
	char *sbp;
	uid_t uid;

	strcpy (func, "Cns_srv_readdir_t");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "readdir_t", uid, gid, clienthost);
	unmarshall_WORD (rbp, getattr);
	unmarshall_WORD (rbp, direntsz);
	unmarshall_HYPER (rbp, dir_fileid);
	unmarshall_WORD (rbp, bod);

	/* return as many entries as possible to the client */

	if (getattr == 1 || getattr == 4)
		if (DIRXSIZE > direntsz)
			direntsz = DIRXSIZE;
	maxsize = DIRBUFSZ - direntsz;
	sbp = dirbuf;
	marshall_WORD (sbp, nbentries);		/* will be updated */

	if (endlist && getattr == 2)
		(void) Cns_get_smd_by_pfid (&thip->dbfd, 0, fmd_entry->fileid,
		    smd_entry, 0, NULL, 1, smdlistptr);
	if (! bod && ! endlist) {
		fnl = strlen (fmd_entry->name);
		if (getattr == 0) {		/* readdir */
			marshall_STRING (sbp, fmd_entry->name);
			nbentries++;
			maxsize -= ((direntsz + fnl + 8) / 8) * 8;
		} else if (getattr == 1) {	/* readdirx */
			marshall_DIRX (&sbp, fmd_entry);
			nbentries++;
			maxsize -= ((direntsz + fnl + 8) / 8) * 8;
		} else if (getattr == 2) {	/* readdirxt */
			bof = 0;
			while (1) {	/* loop on segments */
				marshall_DIRXT (&sbp, magic, fmd_entry, smd_entry);
				nbentries++;
				maxsize -= ((direntsz + fnl + 8) / 8) * 8;
				if (c = Cns_get_smd_by_pfid (&thip->dbfd, bof, 
				    fmd_entry->fileid, smd_entry, 0, NULL,
				    0, smdlistptr)) break;
				if (fnl > maxsize)
					goto reply;
			}
			(void) Cns_get_smd_by_pfid (&thip->dbfd, bof,
			    fmd_entry->fileid, smd_entry, 0, NULL, 1, smdlistptr);
			if (c < 0)
				return (serrno);
		} else if (getattr == 3) {	/* readdirc */
			cml = strlen (umd_entry->comments);
			marshall_STRING (sbp, fmd_entry->name);
			marshall_STRING (sbp, umd_entry->comments);
			nbentries++;
			maxsize -= ((direntsz + fnl + cml + 9) / 8) * 8;
		} else {			/* readdirxc */
			cml = strlen (umd_entry->comments);
			marshall_DIRX (&sbp, fmd_entry);
			marshall_STRING (sbp, umd_entry->comments);
			nbentries++;
			maxsize -= ((direntsz + fnl + cml + 9) / 8) * 8;
		}
	}
	/* loop on directory entries */
	while ((c = Cns_get_ftmd_by_pfid (&thip->dbfd, bod, dir_fileid,
	    fmd_entry, getattr, endlist, dblistptr)) == 0) {	
		fnl = strlen (fmd_entry->name);
		if (getattr == 0) {		/* readdir */
			if (fnl > maxsize) break;
			if(dir_fileid==1){
				char *tmp=(char *)malloc(strlen(fmd_entry->name)+strlen(fmd_entry->path)+1);
				sprintf(tmp, "%s/%s", fmd_entry->path, fmd_entry->name);
				marshall_STRING (sbp, tmp);
				free(tmp);
			}else
				marshall_STRING (sbp, fmd_entry->name);
			nbentries++;
			maxsize -= ((direntsz + fnl + 8) / 8) * 8;
		} else if (getattr == 1) {	/* readdirx */
			if (fnl > maxsize) break;
			if(dir_fileid==1){
                                char *tmp=(char *)malloc(strlen(fmd_entry->name)+strlen(fmd_entry->path)+1);
                                sprintf(tmp, "%s/%s", fmd_entry->path, fmd_entry->name);
                                strcpy(fmd_entry->name, tmp);
                                free(tmp);
			}
			marshall_DIRX (&sbp, fmd_entry);
			nbentries++;
			maxsize -= ((direntsz + fnl + 8) / 8) * 8;
		} else if (getattr == 2) {	/* readdirxt */
			bof = 1;
			while (1) {	/* loop on segments */
				if (c = Cns_get_smd_by_pfid (&thip->dbfd, bof,
				    fmd_entry->fileid, smd_entry, 0, NULL,
				    0, smdlistptr)) break;
				if (fnl > maxsize)
					goto reply;
				marshall_DIRXT (&sbp, magic, fmd_entry, smd_entry);
				nbentries++;
				bof = 0;
				maxsize -= ((direntsz + fnl + 8) / 8) * 8;
			}
			(void) Cns_get_smd_by_pfid (&thip->dbfd, bof,
			    fmd_entry->fileid, smd_entry, 0, NULL, 1, smdlistptr);
			if (c < 0)
				return (serrno);
		} else if (getattr == 3) {	/* readdirc */
			*umd_entry->comments = '\0';
			if (Cns_get_umd_by_fileid (&thip->dbfd, fmd_entry->fileid,
			    umd_entry, 0, NULL) && serrno != ENOENT)
				return (serrno);
			cml = strlen (umd_entry->comments);
			if (fnl + cml > maxsize) break;
			marshall_STRING (sbp, fmd_entry->name);
			marshall_STRING (sbp, umd_entry->comments);
			nbentries++;
			maxsize -= ((direntsz + fnl + cml + 9) / 8) * 8;
		} else {			/* readdirxc */
			*umd_entry->comments = '\0';
			if (Cns_get_umd_by_fileid (&thip->dbfd, fmd_entry->fileid,
			    umd_entry, 0, NULL) && serrno != ENOENT)
				return (serrno);
			cml = strlen (umd_entry->comments);
			if (fnl + cml > maxsize) break;
			marshall_DIRX (&sbp, fmd_entry);
			marshall_STRING (sbp, umd_entry->comments);
			nbentries++;
			maxsize -= ((direntsz + fnl + cml + 9) / 8) * 8;
		}
		bod = 0;
	}
	if (c < 0)
		return (serrno);
	if (c == 1) {
		eod = 1;

		/* start transaction */

		(void) Cns_start_tr (thip->s, &thip->dbfd);

		/* update directory access time */
/*
		if (Cns_get_fmd_by_fileid (&thip->dbfd, dir_fileid, &direntry,
		    1, &rec_addr))
			return (serrno);
		direntry.atime = time (0);
		if (Cns_update_fmd_entry (&thip->dbfd, &rec_addr, &direntry))
			return (serrno);
*/
	}
reply:
	marshall_WORD (sbp, eod);
	p = dirbuf;
	marshall_WORD (p, nbentries);		/* update nbentries in reply */
	sendrep (thip->s, MSG_DATA, sbp - dirbuf, dirbuf);
	return (0);
}


int Cns_srv_cat (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char basename[CA_MAXNAMELEN+1];
	u_signed64 cwd;
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char repbuf[CA_MAXCOMMENTLEN+1];
	char *sbp;
	uid_t uid;
	char *actual_path=(char *)malloc(CA_MAXPATHLEN+1);
	int fd;
	size_t segsize;
	int mode;
	int res;
	strcpy (func, "Cns_srv_cat");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "cat", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	sprintf (logbuf, "cat  %s", path);
	Cns_logreq (func, logbuf);

	if (Cns_splitname (cwd, path, basename))
		return (serrno);

	/* get the actual_path if it exists */
	/*first time the file may not be load, try one more ?*/
	if (Cns_get_t_filemeta (&thip->dbfd, path, basename, &fd, &segsize, &mode)){
 
		return (serrno);
	}
	if(Cns_get_t_filepath(&thip->dbfd, fd, actual_path))
		return (serrno);
	sbp = repbuf;
	marshall_STRING (sbp, actual_path);
	marshall_LONG(sbp, fd);
	marshall_HYPER(sbp, segsize);
	marshall_LONG(sbp, mode);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	free(actual_path);
	return (0);	
}

int Cns_srv_setseg (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	u_signed64 cwd;
        char basename[CA_MAXNAMELEN+1];
	char func[19];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+12];
	char path[CA_MAXPATHLEN+1];
	char physic_path[CA_MAXPATHLEN+1];
	char *rbp;
	Cns_dbrec_addr rec_addru;
	uid_t uid;
	int fd;
	size_t size;
	int bitmap_num;
	strcpy (func, "Cns_srv_setseg");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "setseg", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
        if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
                return (SENAMETOOLONG);
	unmarshall_LONG (rbp, fd);
	unmarshall_HYPER (rbp, size);
	if (unmarshall_STRINGN (rbp, physic_path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, bitmap_num);
	sprintf (logbuf, "setseg %s", path);
        if (Cns_splitname (cwd, path, basename))
                return (serrno);
	char *bitmap=(char *)malloc(bitmap_num+1);
	memset(bitmap, '0', bitmap_num);
	bitmap[bitmap_num]='\0';
	Cns_logreq (func, logbuf);
	Cns_set_t_segmeta(&thip->dbfd, path, basename, fd, size, physic_path);
	Cns_set_t_filebitmap(&thip->dbfd, path, basename,bitmap);
	return (0);
}

int Cns_srv_access_t(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int amode;
	u_signed64 cwd;
	char func[16];
	gid_t gid;
	char logbuf[CA_MAXPATHLEN+13];
	mode_t mode;
	char path[CA_MAXPATHLEN+1];
	char *rbp;
	char *sbp;
	uid_t uid;
	int res;
        char repbuf[CA_MAXCOMMENTLEN+1];

	strcpy (func, "Cns_srv_access_t");
	rbp = req_data;
	unmarshall_LONG (rbp, uid);
	unmarshall_LONG (rbp, gid);
	nslogit (func, NS092, "access_t", uid, gid, clienthost);
	unmarshall_HYPER (rbp, cwd);
	if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
		return (SENAMETOOLONG);
	unmarshall_LONG (rbp, amode);
	sprintf (logbuf, "access %o %s", amode, path);
	Cns_logreq (func, logbuf);

	if (amode & ~(R_OK | W_OK | X_OK | F_OK))
		return (EINVAL);
	res=access(path, amode);
        sbp=repbuf;
        marshall_LONG(sbp, res);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return  0;
}

int Cns_srv_open_t(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        int flags;
        u_signed64 cwd;
        char func[16];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+13];
        char path[CA_MAXPATHLEN+1];
        char *rbp;
        char *sbp;
        uid_t uid;
        int res;
	int fd;
        char repbuf[CA_MAXCOMMENTLEN+1];

        strcpy (func, "Cns_srv_open_t");
        rbp = req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "open_t", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
                return (SENAMETOOLONG);
        unmarshall_LONG (rbp, flags);
        sprintf (logbuf, "open_t %o %s", flags, path);
        Cns_logreq (func, logbuf);
        if (flags & ~(O_RDONLY |O_WRONLY |O_RDWR))
                return (EINVAL);
        fd=open(path, flags);
	if(fd==-1)
		res=-1;
	else
		res=0;
        sbp=repbuf;
        marshall_LONG(sbp, res);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
        return  0;
}

int Cns_srv_read_t(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        size_t size;
//	char buf[1024*1024+1];
	char *buf=(char *)malloc(1024*1024+1);
	memset(buf, 0, 1024*1024);
	off_t offset;
	u_signed64 cwd;
        char func[16];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+13];
        char path[CA_MAXPATHLEN+1];
	char remote_path[CA_MAXPATHLEN+1];
	char basename[CA_MAXPATHLEN];
        char *rbp;
        char *sbp;
        uid_t uid;
        int res;
	int fd;
//        char repbuf[1024*1024+10];
	char *repbuf=(char *)malloc(1024*1024+10);
	int bitmap_size=1024;

        strcpy (func, "Cns_srv_read_t");
        rbp = req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "read_t", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1)){
		free(buf);
		free(repbuf);
                return (SENAMETOOLONG);
	}
        unmarshall_HYPER (rbp, size);
	unmarshall_HYPER (rbp, offset);
	unmarshall_STRING (rbp, remote_path);
        sprintf (logbuf, "read_t %o %s", offset, path);
        Cns_logreq (func, logbuf);
      
	/*check bitmap*/
	char *file_tmp=(char *)malloc(strlen(remote_path)+1);
        strcpy(file_tmp, remote_path);
        if (Cns_splitname (cwd, file_tmp, basename)){
		free(buf);
		free(repbuf);
                return (serrno);
	}
        char *bitmap=(char *)malloc(bitmap_size*sizeof(char));
        /* start transaction */
        if(Cns_get_bitmap(&thip->dbfd, file_tmp, basename, bitmap)){
		free(buf);
		free(repbuf);
                return(serrno);
	}
        if(bitmap==NULL){
		free(buf);
		free(repbuf); 
               return -1;
        }
        int sblock_num;
        int eblock_num;
        sblock_num=offset/UNIT_SIZE;//the num of the first data blcok 
/*
        eblock_num=(offset+size)/UNIT_SIZE;
        if((offset+size)%UNIT_SIZE!=0)
                eblock_num+=1;
*/
	if(bitmap[sblock_num]=='1'){
                if(strcmp(localfilepath, path)!=0){//wether the file is read yet
                        close(localfileid);
                        fd=open(path, O_RDONLY);
                        if(fd==-1)
                                res=-1;
                        else{
                                res=0;
                                localfileid=fd;
                                strcpy(localfilepath, path);
                                res=pread(localfileid, buf, size, offset);
                        }
                }else{
                        res=pread(localfileid, buf, size, offset);
                }

				 
	}else if(bitmap[sblock_num]=='2'){
/*
                fd=open(path, O_RDONLY);
               	if(fd==-1)
                        res=-1;
                else
                        res=0;
                res=pread(fd, buf, size, offset);
                close(fd);
*/

                if(strcmp(localfilepath, path)!=0){//wether the file is read yet
                        close(localfileid);
                        fd=open(path, O_RDONLY);
                        if(fd==-1)
                                res=-1;
                        else{
                                res=0;
                                localfileid=fd;
                                strcpy(localfilepath, path);
                		res=pread(localfileid, buf, size, offset);
		        }
                }else{
                        res=pread(localfileid, buf, size, offset);
                }

	}else{
		res=-1;
	}
        sbp=repbuf;
	if(res!=-1){
	        marshall_LONG(sbp, 0);
		marshall_STRING(sbp, buf);
	}
	else{
		marshall_LONG(sbp, 1);
	}
	free(file_tmp);
	free(bitmap);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	free(buf);
	free(repbuf);
        return  0;
}

int splitname(char *path, char *basename){
        char *p;
        if (*path == 0 || *path != '/')  {
                return (-1);
        }
        /* silently remove trailing slashes */
        p = path + strlen (path) - 1;
        while (*p == '/' && p != path)
                *p = '\0';
        if ((p = strrchr (path, '/')) == NULL)
                p = path - 1;
        strcpy (basename, (*(p + 1)) ? p + 1 : "/");
        if (p <= path)  /* path in the form abc or /abc */
                p++;
        *p = '\0';
        return (0);
}
unsigned int RSHash(char *str)
{
    unsigned int b = 378551;
    unsigned int a = 63689;
    unsigned int hash = 0;

    while (*str)
    {
        hash = hash * a + (*str++);
        a *= b;
    }

    return (hash & 0x7FFFFFFF);
}
int virfile(const char *path, int filesize, int flag){
        int fd;
        off_t offset;
        char file [100];
        char basename[100];
        char path_tmp[100];
        strcpy(path_tmp, path);
        splitname(path_tmp, basename);

        sprintf(file,"%s%s",VIRPATH,basename);
        fd=open(file,O_RDWR|O_CREAT,S_IRUSR|S_IRGRP|S_IROTH);   //创建一个权限为可读的文件
        if(-1 == fd)   //如果出错返回-1  
        {
            perror("creat");
            return -errno;
        }
	if(flag==0){
        	offset = lseek(fd, filesize-1, SEEK_END);  //设置偏移的大小为1024ll*1024ll*1024ll,并偏移到文件尾 
        	write(fd, "", 1);  //写空，写1个字节到文件描述符里  
        }	
	close(fd);   //关闭文件描述符  
	return 0;
}
int dohash(char *file, char *actual_path, size_t filesize, int flag){
        char dir[100];
        int fd;
	struct passwd *pw;
	uid_t uid;
	gid_t gid;
        int hashcode=RSHash(file);
        int dirnum=hashcode%6;
        sprintf(dir,"%s%d%s",PATH,dirnum,"/");
	pw = getpwnam("xrootd");
	uid = pw->pw_uid;
	gid = pw->pw_gid;
        if(access(dir,0) == -1){
                if(mkdir(dir,S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)==-1){
			nslogit("dohash" ,"make cache dir failed\n");
                        return -1;
                }
		if(chown(dir,uid, gid) == -1){
			nslogit("dohash", "chown cache dir failed\n");
			return -1;
		}
        }
        uuid_t uuid;
        char str[36];
        uuid_generate(uuid);
        uuid_unparse(uuid,str);
        sprintf(actual_path,"%s%s",dir,str);
        fd=open(actual_path, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
        if(fd==-1){
		nslogit("dohash", "open cache file failed\n");
                close(fd);
                return -errno;
        }
	if(flag==0){
        	fallocate(fd, 0, 0, filesize);
//		fallocate(fd, 0, 0, 0);
	}       
	if(fchown(fd, uid, gid) == -1){
		nslogit("dohash", "chown cache file failed\n");
		return -1;
	}
	close(fd);
        return 0;
}
int Cns_srv_createfile_t (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        size_t size;
        u_signed64 cwd;
        char func[16];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+13];
        char path[CA_MAXPATHLEN+1];
	char actual_path[CA_MAXPATHLEN+1];
        char *rbp;
        char *sbp;
        uid_t uid;
        int res;
        char repbuf[CA_MAXCOMMENTLEN+1];

        strcpy (func, "Cns_srv_createfile_t");
        rbp = req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "createfile_t", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
                return (SENAMETOOLONG);
	unmarshall_HYPER (rbp, size);
        sprintf (logbuf, "createfile_t %o %s", size, path);
        Cns_logreq (func, logbuf);

        res=dohash(path,actual_path,size,0);
	sbp=repbuf;
        marshall_LONG (sbp, res);
	marshall_STRING (sbp, actual_path);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
        return  0;
}

int Cns_srv_get_virpath (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        u_signed64 cwd;
        char func[19];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+12];
        char actual_path[CA_MAXPATHLEN+1];
        char *rbp;
        char repbuf[CA_MAXCOMMENTLEN+1];
        char *sbp;
        uid_t uid;
	int fd;
	char *name=(char *)malloc(100);
	char *path=(char *)malloc(CA_MAXPATHLEN+1);

        strcpy (func, "Cns_srv_get_virpath");
        rbp = req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "get_virpath", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        if (unmarshall_STRINGN (rbp, actual_path, CA_MAXPATHLEN+1))
                return (SENAMETOOLONG);
        sprintf (logbuf, "get_virpath %s", path);
        Cns_logreq (func, logbuf);
        
	Cns_get_fd_by_actualpath(&thip->dbfd, actual_path, &fd);
	Cns_get_path_by_fd(&thip->dbfd, fd, path, name);
	strcat(path, "/");
	strcat(path, name);

	sbp = repbuf;
        marshall_STRING (sbp, path);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	
	free(name);
	free(path);
        return (0);

}

int Cns_srv_touch_t (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	u_signed64 cwd;
        char func[19];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+12];
        char actual_path[CA_MAXPATHLEN+1];
	char path[CA_MAXPATHLEN];
	char *rbp;
        char repbuf[CA_MAXCOMMENTLEN+1];
        char *sbp;
        uid_t uid;
	int res;
	char basename[CA_MAXPATHLEN];
        char *path_t=(char *)malloc(CA_MAXPATHLEN+1);
        int fd;
        size_t segsize;
        int mode;

	strcpy(func, "Cns_srv_touch_t");
	rbp=req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "touch_t", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
                return (SENAMETOOLONG);
	sprintf(logbuf, "touch_t %s", path);
	Cns_logreq(func, logbuf);
	strcpy(path_t,path);
        if (Cns_splitname (cwd, path_t, basename))
                return (serrno);
	Cns_get_t_filemeta(&thip->dbfd, path_t, basename, &fd, &segsize, &mode );
	if(fd!=-1){
		strcpy(actual_path, "EXIST");
	}else{
		if((res=virfile(path, 0, 1))==0){
			if((res=dohash(path,actual_path,0,1))==0){
/*
				time_t otime;
				time(&otime);
				Cns_insert_otime(&thip->dbfd, fileid, actual_path, otime);					
*/
			}else
				return -1;
		}else
			return -1;
	}
        sbp=repbuf;
        marshall_STRING (sbp, actual_path);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	free(path_t);
	return 0;					
}
int Cns_srv_stat_t (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        u_signed64 cwd;
        char func[19];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+12];
        char path[CA_MAXPATHLEN];
        char *rbp;
        char repbuf[CA_MAXCOMMENTLEN+1];
        char *sbp;
        uid_t uid;

        char basename[CA_MAXPATHLEN];
        char *path_t=(char *)malloc(CA_MAXPATHLEN+1);
	struct Cns_file_metadata direntry;

        strcpy(func, "Cns_srv_stat_t");
        rbp=req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "stat_t", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
                return (SENAMETOOLONG);
        sprintf(logbuf, "stat_t %s", path);
        Cns_logreq(func, logbuf);
        strcpy(path_t,path);
        if (Cns_splitname (cwd, path_t, basename))
                return (serrno);
        Cns_get_ftmd_by_fullpath(&thip->dbfd, path_t, basename, &direntry,0,NULL);
        sbp=repbuf;
        marshall_LONG (sbp,direntry.filemode);
	marshall_LONG (sbp,direntry.ino);
	marshall_LONG (sbp,direntry.dev);
	marshall_LONG (sbp,direntry.nlink);
	marshall_LONG (sbp,direntry.uid);
	marshall_LONG (sbp,direntry.gid);
	marshall_LONG (sbp,direntry.filesize);
	marshall_LONG (sbp,direntry.atime);
	marshall_LONG (sbp,direntry.mtime);
	marshall_LONG (sbp,direntry.ctime);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
        free(path_t);
        return 0;
}
int Cns_srv_opendir_t_xrd (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        u_signed64 cwd;
        char func[19];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+12];
        char path[CA_MAXPATHLEN];
        char *rbp;
        char repbuf[CA_MAXCOMMENTLEN+1];
        char *sbp;
        uid_t uid;

        char basename[CA_MAXPATHLEN];
        char *path_t=(char *)malloc(CA_MAXPATHLEN+1);
        struct Cns_file_metadata direntry;
	int fileid;
	char *dirlist_tmp=(char *)malloc(1000);
	char *dirlist;

        strcpy(func, "Cns_srv_opendir_t_xrd");
        rbp=req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "opendir_t_xrd", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        if (unmarshall_STRINGN (rbp, path, CA_MAXPATHLEN+1))
                return (SENAMETOOLONG);
        sprintf(logbuf, "opendir_t_xrd %s", path);
        Cns_logreq(func, logbuf);
        strcpy(path_t,path);
        if (Cns_splitname (cwd, path_t, basename))
                return (serrno);
	if(Cns_get_fileid_by_fullpath(&thip->dbfd, path_t, basename, &fileid))
		return serrno;
	if(Cns_get_dirlist_by_parent_fileid(&thip->dbfd, fileid, dirlist_tmp))
		return serrno;
	dirlist=(char *)malloc(strlen(dirlist_tmp)*sizeof(char));
	strcpy(dirlist,dirlist_tmp);
	sbp=repbuf;
        marshall_STRING (sbp,dirlist);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
        free(path_t);
	free(dirlist_tmp);
	free(dirlist);
        return 0;
}
int Cns_srv_getattr_id(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        u_signed64 cwd;
        char func[19];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+12];
        char *rbp;
        char repbuf[CA_MAXCOMMENTLEN+1];
        char *sbp;
        uid_t uid;
	int fileid;

        struct Cns_file_metadata direntry;

        strcpy(func, "Cns_srv_getattr_id");
        rbp=req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "getattr_id", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        unmarshall_LONG (rbp, fileid);
	sprintf(logbuf, "getattr_id  fileid %d", fileid);
        Cns_logreq(func, logbuf);
        
	Cns_get_ftmd_by_fileid(&thip->dbfd, fileid,  &direntry);

        sbp=repbuf;
	marshall_STRING (sbp, direntry.name);
        marshall_LONG (sbp,direntry.filemode);
        marshall_LONG (sbp,direntry.ino);
        marshall_LONG (sbp,direntry.dev);
        marshall_LONG (sbp,direntry.nlink);
        marshall_LONG (sbp,direntry.uid);
        marshall_LONG (sbp,direntry.gid);
        marshall_LONG (sbp,direntry.filesize);
        marshall_LONG (sbp,direntry.atime);
        marshall_LONG (sbp,direntry.mtime);
        marshall_LONG (sbp,direntry.ctime);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
        return 0;
}

void computesize(u_signed64 size)
{

        pthread_mutex_lock (&size_lock);
        total_bytes += size;
        pthread_mutex_unlock (&size_lock);
}


int copyfile(int fd_in, int fd_out, size_t size, off_t offset)
{
        size_t nbread_tot = 0;
        size_t nbread;
        size_t nbwrite_tot = 0;
        size_t nbwrite;
        size_t nbtoread;
        off_t off_in = offset;
        off_t off_out = offset;
        char *buf = (char *)malloc(BUFLEN);
        if (!buf) {
                fprintf(stderr, "Failed to allocate memory %s\n", strerror(errno));
                return -1;
        }
        do {
                bzero(buf, BUFLEN);
                nbtoread = size - nbread_tot;
                if (nbtoread <= 0) break;
                if (nbtoread > BUFLEN)
                        nbtoread = BUFLEN;
                nbread = pread(fd_in, buf, nbtoread, off_in);
                if (nbread < 0) {
                        fprintf(stderr, "Failed to read srcfile offset:%lu %s\n", offset, strerror(errno));
                        return -1;
                }
                if (nbread == 0) {
                        off_t curoff = lseek(fd_in, 0, SEEK_CUR);
                        printf("DEBUG1: read %lu bytes of size %lu offset(%lu:%lu) fd(%d:%d)\n", nbread_tot, size, offset, curoff, fd_in, fd_out);
                        break;
                }
                off_in += nbread;
                computesize(nbread);
                nbread_tot += nbread;
                nbwrite_tot = 0;
                do {
                        nbwrite = pwrite(fd_out, buf, nbread - nbwrite_tot, off_out);
                        if (nbwrite < 0) {
                                fprintf(stderr, "Failed to write dstfile offset:%lu, %s\n", offset, strerror(errno));
                                return -1;
                        }
                        off_out += nbwrite;
                        nbwrite_tot += nbwrite;
                } while (nbwrite_tot < nbread);
        } while (nbread_tot < size);
        free(buf);
        return 0;
}


void *ProcCopy (void *arg)
{
        struct mProcArg *thisarg = (struct mProcArg *) arg;
        int fd_in = thisarg->fd_in;
        int fd_out = thisarg->fd_out;
        size_t size = thisarg->size;
        off_t offset = thisarg->offset;
        int ret;
        int *rret = (int *)malloc(sizeof(int));
        ret = copyfile(fd_in, fd_out, size, offset);
        memcpy(rret,  &ret, sizeof(int));
        pthread_exit((void *)rret);
        return NULL;
}

int f_cp_fd(char *actual_path, char *fullpath, size_t filesize)
{
        size_t len, nbwrite;
        int res = 0;
        int nbtoread = 0;
        struct stat stbuf;
        struct mProcArg myarg[THREAD_NUM];
        pthread_t threadid[THREAD_NUM];
        void *status[THREAD_NUM];
        int fd_in;
        int fd_out;
        size_t bucksize;
        int i;
        char *buff = (char *)malloc(BUFLEN);
        if (!buff) {
                fprintf(stderr, "Failed to allocate buffer %s\n", strerror(errno));
                return -1;
        }

        if((fd_in = open(actual_path, O_RDONLY))==NULL){
                fprintf(stderr,"open data_dir %s failure %s\n", actual_path, strerror(errno));
                return -1;
        }
        if((fd_out = open(fullpath, O_WRONLY|O_TRUNC))==NULL){
                fprintf(stderr,"open target_file %s failure %s\n", fullpath, strerror(errno));
                return -1;
        }
        if ((filesize < SMALLSIZE)) {
                res =  copyfile(fd_in, fd_out, filesize, 0);
                goto _COPY_DONE;
        }
        pthread_mutex_init (&size_lock, NULL);
        bucksize = (size_t)ceil(stbuf.st_size/THREAD_NUM);
        for (i = 0; i < THREAD_NUM; i++) {
                myarg[i].fd_in = fd_in;
                myarg[i].fd_out = fd_out;
                myarg[i].offset = bucksize*i;
                if (i == (THREAD_NUM - 1))
                        myarg[i].size = filesize - bucksize*i;
                else
                        myarg[i].size = bucksize;
                pthread_create (&threadid[i], NULL, ProcCopy, &myarg[i]);
        }
        for (i = 0; i < THREAD_NUM; i++) {
                pthread_join (threadid[i], (void **)&status[i]);
		free(status[i]);
        }
        pthread_mutex_destroy (&size_lock);
_COPY_DONE:
        stopflag = 1;
        close(fd_in);
        close(fd_out);
        free(buff);
        return res;
}


int Cns_srv_rfsync (int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int res=0;
	int c;
	Cns_dbrec_addr rec_addr;
        u_signed64 cwd;
        char func[16];
	char tmpbuf[21];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+12];
        char *rbp;
        char repbuf[CA_MAXCOMMENTLEN+1];
        char *sbp;
        uid_t uid;
        char from[CA_MAXPATHLEN+1];
	char to[CA_MAXPATHLEN+1];
	char tmp[CA_MAXCOMMENTLEN+1];
	char basename[CA_MAXPATHLEN+1];
	char datahost[64];
	char user[32];
	struct stat st_buf;
	struct Cns_file_metadata filentry;
	struct Cns_file_metadata filentry_old;
	struct Cns_file_metadata parent_dir;
	int bitmap_num;
	
        strcpy(func, "Cns_srv_rfsync");
        rbp = req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        unmarshall_HYPER (rbp, cwd);
        unmarshall_STRING (rbp, from);
	unmarshall_STRING (rbp, to);
	unmarshall_STRING (rbp, user);
        sprintf(logbuf, "from %s to %s fileid",from, to);

        if(get_conf_value(CONF_FILE, "DATA_SERVER_IP", datahost)){
                fprintf(stderr, "Configyre file has no server configer\n");
                return 1;
        }
        Cns_logreq(func, logbuf);
        res = lstat(from, &st_buf);
        if(res != 0){
                nslogit(func, "stat %s  failed\n", from);
                return 1;
        }
	/*update metadata*/
	strcpy(tmp, to);
	if (Cns_splitname (cwd, tmp, basename))
		return (serrno);		
	filentry.uid=st_buf.st_uid;
	filentry.gid=st_buf.st_gid;
	filentry.ino=st_buf.st_ino;
	filentry.mtime=st_buf.st_mtime;
	filentry.ctime=st_buf.st_ctime;
	filentry.atime=st_buf.st_atime;
	filentry.nlink=st_buf.st_nlink;
	filentry.dev=st_buf.st_dev;
	strcpy(filentry.path, tmp);
	filentry.filesize=st_buf.st_size;
	filentry.filemode=st_buf.st_mode;
	strcpy(filentry.name,basename);
	//      get parent_Dir
	strcpy(parent_dir.path, tmp);
	if (Cns_splitname (cwd, parent_dir.path, parent_dir.name))
        	return (serrno);
	c = Cns_get_ftmd_by_fullpath (&thip->dbfd, parent_dir.path, parent_dir.name, &parent_dir, 0, &rec_addr);
        if (c  && serrno != ENOENT)
                return (serrno);
        if(c==0){
        	nslogit (func,"parent_dir: %s/%s exits\n",parent_dir.path,parent_dir.name);
        }else{
		nslogit (func,"parent_dir: %s/%s not exits\n",parent_dir.path,parent_dir.name);
		return 1;
        }
			//check if the file exists already
	c = Cns_get_ftmd_by_fullpath (&thip->dbfd, filentry.path, filentry.name, &filentry_old, 0, &rec_addr);
        if (c && serrno != ENOENT)
                return (serrno);
	if(c==0){/*exist*/
 		nslogit (func, "uploadfile %s exist\n",  filentry.name);
		return 1;
	}else{ /*not exist*/
		if(Cns_unique_transform_id(&thip->dbfd, &filentry.fileid)<0)
                	return (serrno);
		filentry.parent_fileid=parent_dir.fileid;
                filentry.fileclass=parent_dir.fileclass;
                filentry.status='u';
                /*write new file entry*/
                if(Cns_insert_ftmd_entry(&thip->dbfd, &filentry))
                        return (serrno);
		bitmap_num=filentry.filesize/UNIT_SIZE;
		if(filentry.filesize%UNIT_SIZE!=0)
			bitmap_num+=1;
		char *bitmap=(char *)malloc(bitmap_num+1);
		memset(bitmap, '2', bitmap_num);
		bitmap[bitmap_num]='\0';
		if(Cns_set_t_filebitmap(&thip->dbfd, filentry.path, filentry.name, bitmap)){
			nslogit (func, "setbitmap %s/%s failed\n",  filentry.path, filentry.name);
			return 1;
		}
		if(Cns_set_t_segmeta(&thip->dbfd, filentry.path, filentry.name, filentry.fileid, filentry.filesize, from)){
			nslogit (func, "setsegmeta %s/%s failed\n",  filentry.path, filentry.name);
			return 1;
		}	
//                		memset ((char *) &filentry, 0, sizeof(filentry));
                nslogit (func, "file %s upload sucess\n", u64tostr (filentry.fileid, tmpbuf, 0));
      	}			
	sbp=repbuf;
	marshall_LONG(sbp,res);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);

	/*up to remote station*/        
        struct timeval start,end;
        float et;
        char msg[1024];
        gettimeofday(&start,0);
        sprintf(msg, "start time: %d.%d\n", start.tv_sec, start.tv_usec);
        nslogit(func, "begin upload %s\n", msg);
        XrdOucString path;
        XrdOucString xhost;
        XrdOucString xuser;
	XrdOucString xto;
	xhost = datahost;
	xuser = user;
	xto = to;
        char buffer[UNIT_SIZE+1];
        int ifd = 0;
        int ofd = 0;
        uint32_t nbytes = 0;
        uint32_t offset = 0;
	XrdPosixXrootd::setEnv("ConnectionWindow", 1);
	XrdPosixXrootd::setEnv("ConnectionRetry", 2);
	ofd = open(from, O_RDONLY);
	if(ofd <= 0){
		nslogit(func, "%s open cachefile failed", from);
		return 1;
	}
	path = xuser + "://" + xhost + "/" + xto;
	ifd = XrdPosixXrootd::Open(path.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0644);
	if (ifd <= 0){
		nslogit(func, "%s xrdopen failed", path.c_str());
		return 1;
	}
	while((nbytes = pread(ofd, buffer, UNIT_SIZE, offset)) > 0){
		nbytes = XrdPosixXrootd::Pwrite(ifd, buffer, strlen(buffer), offset);
		if(nbytes <= 0){
			nslogit(func, "%s xrdwrite failed", path.c_str());
			break;
		}
		offset += nbytes;
		memset(buffer, 0, strlen(buffer));	
	}		
	XrdPosixXrootd::Close(ifd);
	close(ofd);
        gettimeofday(&end,0);
        et = end.tv_sec*1000+end.tv_usec/1000-start.tv_sec*1000-start.tv_usec/1000;
        sprintf(msg, "end time: %d.%d used time %.2f(ms)\n", end.tv_sec, end.tv_usec, et);
        nslogit(func, "end upload %s\n", msg);
	return 0;
}

int Cns_srv_refreshcache(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	int res=0;
	int fd;
	int flag=0;
	size_t filesize;
	int bitmap_num;
        u_signed64 cwd;
        char func[19];
        gid_t gid;
        char logbuf[CA_MAXPATHLEN+12];
        char *rbp;
        char repbuf[CA_MAXCOMMENTLEN+1];
        char *sbp;
        uid_t uid;
        char cachefile[CA_MAXPATHLEN+1];
        char sourcefile[CA_MAXPATHLEN+1];
	char basename[CA_MAXPATHLEN+1];
        strcpy(func, "Cns_srv_refeshcache");

        rbp=req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "Cns_srv_refreshcache", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
        unmarshall_STRING (rbp, cachefile);
        unmarshall_STRING (rbp, sourcefile);
	unmarshall_HYPER (rbp, filesize);
        sprintf(logbuf, "rereshcache to file %s",sourcefile);
        Cns_logreq(func, logbuf);
	
	if(Cns_splitname (cwd, sourcefile, basename))
		return (serrno);
        bitmap_num=filesize/UNIT_SIZE;
        if(filesize%UNIT_SIZE!=0)
                bitmap_num+=1;
        char *bitmap=(char *)malloc(bitmap_num+1);
        memset(bitmap, '\0', bitmap_num+1);
	if(Cns_get_bitmap(&thip->dbfd, sourcefile, basename, bitmap)){
		sprintf(logbuf, "get bitmap failed %s",sourcefile);
		Cns_logreq(func, logbuf);
		return 1;
	}
	for(int i=0; i<bitmap_num; i++){
		if(bitmap[i]=='2'){
			memset(bitmap, '0', bitmap_num);
			bitmap[bitmap_num]='\0';
			flag=1;
			if(Cns_set_t_filebitmap(&thip->dbfd, sourcefile, basename, bitmap)){
				sprintf(logbuf, "set bitmap failed %s",sourcefile);
				Cns_logreq(func, logbuf);
				return 1;
			}
			break;
		}
	}	
	if(flag){
		fd=open(cachefile, O_WRONLY|O_TRUNC);
		if(fd < 0){
			sprintf(logbuf, "ERROR:  cachefile lost-- %s",cachefile);
			Cns_logreq(func, logbuf);
			res=-1;
		}
		if(ftruncate(fd,filesize)<0){
			sprintf(logbuf, "ERROR:  Nospace to get cacchefile-- %s",cachefile);
			Cns_logreq(func, logbuf);
			close(fd);
			res=-1;
		}
	}
        sbp=repbuf;
        marshall_LONG(sbp,res);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return res;
}

int Cns_srv_set_metadata(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
        int res=0;
	int c=0;
	int mounttag=0;
        u_signed64 cwd;
        char func[19];
        char logbuf[CA_MAXPATHLEN+12];
        char *rbp;
        char repbuf[CA_MAXCOMMENTLEN+1];
        char *sbp;
	Cns_dbrec_addr rec_addr;
	struct Cns_file_metadata filentry;
	struct Cns_file_metadata parent_dir;
	struct Cns_file_metadata filentry_old;
	char basename[CA_MAXPATHLEN+1];


        strcpy(func, "Cns_srv_set_metadata");
        rbp=req_data;
	unmarshall_HYPER (rbp, cwd);
        unmarshall_LONG (rbp, filentry.uid);
        unmarshall_LONG (rbp, filentry.gid);
        unmarshall_HYPER (rbp, filentry.ino);
        unmarshall_TIME_T (rbp, filentry.mtime);
        unmarshall_TIME_T (rbp, filentry.ctime);
        unmarshall_TIME_T (rbp, filentry.atime);
        unmarshall_LONG (rbp, filentry.nlink);
        unmarshall_LONG (rbp, filentry.dev);
        unmarshall_STRING (rbp, filentry.path);
        unmarshall_HYPER (rbp, filentry.filesize);
        unmarshall_WORD (rbp, filentry.filemode);
        unmarshall_STRING (rbp, filentry.name);
        nslogit (func, NS092, "Cns_srv_set_metadata", filentry.uid, filentry.gid, clienthost);

        sprintf(logbuf, "set metadata to %s", filentry.path);
        Cns_logreq(func, logbuf);
        if(Cns_splitname (cwd, filentry.path, basename))
                return (serrno);
	strcpy(parent_dir.path, filentry.path);
	if (Cns_splitname (cwd, parent_dir.path, parent_dir.name))
                return (serrno);
        c = Cns_get_ftmd_by_fullpath (&thip->dbfd, parent_dir.path, parent_dir.name, &parent_dir, 0, &rec_addr);
        if (c  && serrno != ENOENT)
                               return (serrno);
        if(c==0){
                sprintf (logbuf, "%s parent_dir exits", filentry.path);
        }else{
		mounttag=1;
                parent_dir.fileclass=0;
/*
                if(Cns_unique_transform_id(&thip->dbfd, &parent_dir.fileid)<0)
                        return (serrno);
*/
		parent_dir.fileid = 1;
        }
        c = Cns_get_ftmd_by_fullpath (&thip->dbfd, filentry.path, filentry.name, &filentry_old, 0, &rec_addr);
        if (c && serrno != ENOENT)
                return (serrno);
	if(c==0){
                filentry.fileid=filentry_old.fileid;
                filentry.parent_fileid=filentry_old.parent_fileid;
                filentry.fileclass=filentry_old.fileclass;
                filentry.status=filentry_old.status;
                res=Cns_update_ftmd_entry(&thip->dbfd, &rec_addr,&filentry);
		nslogit (func, "file %s exist\n",  filentry.name);
	}else{
                if(Cns_unique_transform_id(&thip->dbfd, &filentry.fileid)<0)
                        return (serrno);
                filentry.parent_fileid=parent_dir.fileid;
                filentry.fileclass=parent_dir.fileclass;
                filentry.status='-';
                /*write new file entry*/
                res=Cns_insert_ftmd_entry(&thip->dbfd, &filentry);
                nslogit (func, "file %s/%s insert\n", filentry.path, filentry.name);
	}

        sbp=repbuf;
        marshall_LONG(sbp,res);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
        return 0;
}

int Cns_srv_unlink_t(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip)
{
	char func[16];
	uid_t uid;
	gid_t gid;
	char *rbp;
	char *sbp;
	u_signed64 cwd;
	char path[CA_MAXPATHLEN+1];
	char name[CA_MAXPATHLEN+1];
	char logbuf[CA_MAXPATHLEN+8];
	char repbuf[CA_MAXCOMMENTLEN+1];
	int fileid;
	size_t filesize;
	int mode;
	int res;
	queue <int> dirlist;
	int fileid_tmp;
	char *dirlist_tmp=(char *)malloc(1000);

	strcpy(func, "Cns_srv_unlink_t");
        rbp = req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "unlink_t", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
	unmarshall_STRING(rbp, path);
	unmarshall_STRING(rbp, name);
	sprintf (logbuf, "unlink_t %s/%s", path, name);
	Cns_logreq (func, logbuf);
	if(*name=='/')
		return EINVAL;
	(void) Cns_start_tr (thip->s, &thip->dbfd);
	res = Cns_get_t_filemeta(&thip->dbfd, path, name, &fileid, &filesize, &mode);
	if(!res){
		dirlist.push(fileid);
		while(!dirlist.empty()){
			struct Cns_file_metadata st;
			char actual_path[CA_MAXPATHLEN+1];
			int id=dirlist.front();
			char *pos;
			char *delims={","};
			dirlist.pop();
			res=Cns_get_ftmd_by_fileid( &thip->dbfd, id, &st);
			if(res)
				return 1;
			if(st.filemode & S_IFDIR){
				res=Cns_delete_ftmd_entry_byfid(&thip->dbfd, id);
				if(!res){
					res=Cns_get_dirlist_by_parent_fileid(&thip->dbfd, id, dirlist_tmp);
					if(!res){
						pos=strtok(dirlist_tmp, delims);
						while(pos!=NULL){
							int i=atoi(pos);
							dirlist.push(i);
							pos=strtok(NULL, delims);
						}		
					}else
						return 1;
				}else
					return 1;
				
			}else{
				res=Cns_delete_ftmd_entry_byfid(&thip->dbfd, id);
				if(!res){
					if(res=Cns_get_t_filepath(&thip->dbfd, id, actual_path)){
						nslogit(func, "no data block %s\n", actual_path);
					}else{
						remove(actual_path);
						res=Cns_delete_stmd_entry_byfid(&thip->dbfd, id);
					}
					
				}else
					return 1;
			}
		}
		
	}
        sbp=repbuf;
        marshall_LONG(sbp,res);
        sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	free(dirlist_tmp);
	return res;
}
int Cns_srv_loadmetadata(int magic,char *req_data,char *clienthost,struct Cns_srv_thread_info *thip){
	char func[16];
	uid_t uid;
	gid_t gid;
	int depth;
	int realdepth;
	char *rbp;
	char *sbp;
	u_signed64 cwd;
	int c = 0;
	int i = 0;
	char logbuf[CA_MAXCOMMENTLEN+1];
	char repbuf[CA_MAXCOMMENTLEN+1];
	int res = 0;
	DIR *dir = NULL;
	struct dirent *entry = NULL;
	queue <XrdOucString> dirpath;
	struct stat buf;
	Cns_dbrec_addr rec_addr;
	memset(&buf, '0', sizeof(struct stat));
	vector <struct stat> st;
	vector <XrdOucString> pt;
        char host[CA_MAXPATHLEN+1];
        char path[CA_MAXPATHLEN+1];
	char user[32];
	char dpath[CA_MAXPATHLEN+1];
	
	XrdOucString tmppath;
	XrdOucString tmpurl;
	XrdOucString tmp;
	XrdOucString url;
	XrdOucString xhost;
	XrdOucString xuser;
        struct Cns_file_metadata filentry;
        struct Cns_file_metadata parent_dir;
        struct Cns_file_metadata filentry_old;
        char basename[CA_MAXPATHLEN+1];

	char datahost[128];
	if(get_conf_value(CONF_FILE, "DATA_SERVER_IP", datahost)){
       		fprintf(stderr, "Configyre file has no server configer\n");
        	return 1;
	}
	char depth_str[32];
	
	strcpy(func, "Cns_srv_loadmetadata");
	rbp = req_data;
        unmarshall_LONG (rbp, uid);
        unmarshall_LONG (rbp, gid);
        nslogit (func, NS092, "Cns_srv_loadmetadata", uid, gid, clienthost);
        unmarshall_HYPER (rbp, cwd);
	unmarshall_STRING (rbp, user);
	unmarshall_STRING (rbp, host);
        unmarshall_STRING(rbp, path);
	unmarshall_LONG (rbp, depth);

	if(depth == 1){
		if(get_conf_value(CONF_FILE, "DIR_DEPTH", depth_str)){
        		fprintf(stderr, "Configyre file has no dir_depth\n");
        		return 1;
       		}
		realdepth = atoi(depth_str);

	}else{
		realdepth = 1;
	}
	XrdPosixXrootd::setEnv("ConnectionWindow", 1);
	XrdPosixXrootd::setEnv("ConnectionRetry", 2);
	xhost = datahost;
	xuser = user;
	dirpath.push(path);
	while ((!dirpath.empty()) && (i < realdepth)){
		tmppath = dirpath.front();
		tmpurl = xuser + "://" + xhost + "/" + tmppath;
		dirpath.pop();
		if (XrdPosixXrootd::Stat(tmpurl.c_str(), &buf) < 0){
			res = 1;
			nslogit(func, "%s xrdstat failed", tmpurl.c_str());
			break;
		}
		if(i == 0){
			pt.push_back(tmppath);
			st.push_back(buf);
		}
		if(S_ISDIR(buf.st_mode)){/*is dir*/
			if((dir = XrdPosixXrootd::Opendir(tmpurl.c_str())) == NULL){
				nslogit(func, "%s xrdopendir failed\n", tmpurl.c_str());
				res = 1;
				break;
			}else{
				while((entry = XrdPosixXrootd::Readdir(dir))){
					tmp = tmppath + '/' + entry->d_name;
					url = xuser + "://" + datahost + "/" + tmp;
					memset(&buf, 0, sizeof(struct stat));
					if(XrdPosixXrootd::Stat(url.c_str(), &buf) < 0){
						nslogit(func, "%s xrdstat failed\n", url.c_str());
						break;
					}
					if(S_ISDIR(buf.st_mode)){/*sub is dir*/
						dirpath.push(tmp);	
					}
					pt.push_back(tmp);
					st.push_back(buf);
				}
			}
			if(XrdPosixXrootd::Closedir(dir)){
				nslogit(func, "%s xrdclosedir failed", tmpurl.c_str());
				res = 1;
				break;
			}
		}
		i++;
	}
	if(!res){
		for(int i =0; i < pt.size(); i++){
			strcpy(dpath, pt[i].c_str());
			filentry.uid = st[i].st_uid;
			filentry.gid = st[i].st_gid;
			filentry.ino = st[i].st_ino;
			filentry.mtime = st[i].st_mtime;
			filentry.ctime = st[i].st_ctime;
			filentry.atime = st[i].st_atime;
			filentry.nlink = st[i].st_nlink;
			filentry.dev = st[i].st_dev;
			filentry.filesize = st[i].st_size;
			filentry.filemode = st[i].st_mode;
			if(Cns_splitname (cwd, dpath, basename))
				return (serrno);
			strcpy(filentry.path, dpath);
			strcpy(filentry.name, basename);
		        strcpy(parent_dir.path, dpath);
			if (Cns_splitname (cwd, parent_dir.path, parent_dir.name))
				return serrno;
			c = Cns_get_ftmd_by_fullpath (&thip->dbfd, parent_dir.path, parent_dir.name, &parent_dir, 0, &rec_addr);
			if (c  && serrno != ENOENT)
				return serrno;
			if(c==0){
				sprintf(logbuf, "%s parent_dir exits", filentry.path);
			}else{
				parent_dir.fileclass = 0;
				parent_dir.fileid = 1;
			}
			c = Cns_get_ftmd_by_fullpath (&thip->dbfd, filentry.path, filentry.name, &filentry_old, 0, &rec_addr);
			if (c && serrno != ENOENT)
				return serrno;
			if(c == 0){
				filentry.fileid=filentry_old.fileid;
				filentry.parent_fileid=filentry_old.parent_fileid;
				filentry.fileclass=filentry_old.fileclass;
				filentry.status=filentry_old.status;
				res=Cns_update_ftmd_entry(&thip->dbfd, &rec_addr,&filentry);
				nslogit (func, "file %s exist\n",  filentry.name);
			}else{
				if(Cns_unique_transform_id(&thip->dbfd, &filentry.fileid) < 0)
					return serrno;
				filentry.parent_fileid=parent_dir.fileid;
				filentry.fileclass=parent_dir.fileclass;
				filentry.status='-';
				/*write new file entry*/
				res=Cns_insert_ftmd_entry(&thip->dbfd, &filentry);
				if( res ){
					nslogit (func, "file %s/%s insert failed\n", filentry.path, filentry.name);
					break;
				}else
					nslogit (func, "file %s/%s insert\n", filentry.path, filentry.name);
			}

		}				
	}
	sbp = repbuf;
	marshall_LONG (sbp, res);
	sendrep (thip->s, MSG_DATA, sbp - repbuf, repbuf);
	return res;
}
pthread_mutex_t bitmap_lock;
struct thread_data{
	off_t sblock;
	off_t eblock;
	char *bitmap;
	int ofd;
	int ifd;		
};
void *Procread(void *argv)
{
	int ret = 0;
	int *rret = (int *)malloc(sizeof(int));	

	struct thread_data *thisargv = (struct thread_data *)argv;
	off_t sblock = thisargv->sblock;
	off_t eblock = thisargv->eblock;
	char *bitmap = thisargv->bitmap;
	int ofd = thisargv->ofd;
	int ifd = thisargv->ifd;
	int nbytes;
	char buffer[UNIT_SIZE+1];
	char func[]={"Prcoread"};

	for(off_t i = sblock; i < eblock; i++){
		if(bitmap[i] == '0'){
			nbytes = XrdPosixXrootd::Pread(ofd, buffer, UNIT_SIZE, UNIT_SIZE*i);
			if(nbytes < 0){
				nslogit(func, "xrdread %d failed\n", i);
				ret = -2;
				break;
			}
			nbytes = pwrite(ifd, buffer, strlen(buffer), UNIT_SIZE*i);
			if(nbytes < 0){
				nslogit(func, "wrirte %d to cache failed", i);
				ret = -2;
				break;
			}
			memset(buffer, 0, sizeof(buffer));
			bitmap[i] = '2';
			ret = 0;
		}else if(bitmap[i] == '1' || bitmap[i] == '2'){
			continue;
		}else{
			nslogit(func, "bitmap is wrong");
			ret = -2;
			break;
		}
	}	

	memcpy(rret, &ret, sizeof(int));
	pthread_exit((void *)rret);
	return NULL;
}
int Cns_srv_download_seg(int magic, char *req_data, char *clienthost, struct Cns_srv_thread_info *thip)
{
	u_signed64 cwd;
	char func[16];
	gid_t gid;
	uid_t uid;
	char *rbp;
	int res = 0;
	int bit_res = 0;
	char *sbp;
	char logbuf[CA_MAXPATHLEN+12];
	char filepath[CA_MAXPATHLEN+1];
	char basename[CA_MAXPATHLEN+1];
	char location[CA_MAXPATHLEN+1];
        char host[CA_MAXPATHLEN+1];
        char user[32];
	off_t offset;
	size_t size;
	size_t filesize;
	int buff_tag;
	int bitmap_num = 0;
	int p_res = -1;
	XrdOucString path;
	XrdOucString xhost;
	XrdOucString xuser;
	XrdOucString xfilepath;
	char buffer[UNIT_SIZE+1];
	int ofd = 0;
	int ifd = 0;
	uint32_t nbytes = 0;
	char *repbuf=(char *)malloc(1024*1024+1);
	char *file_tmp=(char *)malloc(strlen(filepath)+1);
	off_t sblock_num=0;
        off_t eblock_num=0;

	strcpy(func, "Cns_srv_download_seg");
	rbp = req_data;
	unmarshall_LONG(rbp, uid);
	unmarshall_LONG(rbp, gid);
	unmarshall_HYPER(rbp, cwd);
	nslogit(func, NS092, "Cns_srv_download_seg", uid, gid, clienthost);
        if (unmarshall_STRINGN (rbp, filepath, CA_MAXPATHLEN+1))
                return (SENAMETOOLONG);
        unmarshall_HYPER(rbp, offset);
        unmarshall_HYPER(rbp, size);
        unmarshall_STRING(rbp, location);
        unmarshall_HYPER(rbp, filesize);
	unmarshall_STRING(rbp, host);
	unmarshall_STRING(rbp, user);
        unmarshall_LONG(rbp, buff_tag);

	char datahost[128];
	if(get_conf_value(CONF_FILE, "DATA_SERVER_IP", datahost)){
		fprintf(stderr, "Configyre file has no server configer\n");
		return 1;
	}

	xhost = datahost;
	xuser =	user;
	xfilepath = filepath;
        sprintf (logbuf, "Cns_srv_download_seg %s", filepath);
        Cns_logreq (func, logbuf);
	char *buff=(char *)malloc(size+1);
	strcpy(file_tmp, filepath);
        if (Cns_splitname (cwd, file_tmp, basename))
                return (serrno);
	bitmap_num = filesize/UNIT_SIZE;
	if(filesize%UNIT_SIZE != 0)
		bitmap_num += 1;	
	char *bitmap = (char *)malloc(bitmap_num+1);
	memset(bitmap, '\0', bitmap_num+1);
        if((bit_res=Cns_get_bitmap(&thip->dbfd, file_tmp, basename, bitmap)))
                return(serrno);
	if(bitmap == NULL)
		return 1;
	sblock_num = offset/UNIT_SIZE;
	if(sblock_num >= bitmap_num){
		nslogit(func,"file offset is more than filesize\n");
		return 1;
	}
	eblock_num = (offset +size)/UNIT_SIZE;
	if((offset + size) % UNIT_SIZE != 0)
		eblock_num += 1;
	if(eblock_num == sblock_num){
		nslogit(func, "No data is request\n");
		return 1;
	}
	if(eblock_num > bitmap_num){
		eblock_num = bitmap_num;
	}
	XrdPosixXrootd::setEnv("ConnectionWindow", 1);
	XrdPosixXrootd::setEnv("ConnectionRetry", 2);
	path = xuser + "://" + xhost + "/" + xfilepath;
        ifd = open(location, O_WRONLY);
        if(ifd < 0){
                nslogit(func, "open cachefile %s failed", location);
                return 1;
        }
	ofd = XrdPosixXrootd::Open(path.c_str(), O_RDONLY, 0);
	if(ofd < 0){
		nslogit(func, "xrdopen %s failed", path.c_str());
		return 1;
	}

/* single-thread mode
	for(off_t i = sblock_num; i < eblock_num; i++){
		if(bitmap[i] == '0'){
			nbytes = XrdPosixXrootd::Pread(ofd, buffer, UNIT_SIZE, UNIT_SIZE*i);
			if(nbytes < 0){
				nslogit(func, "xrdread %s failed", path.c_str());
				p_res = -2;
				break;
			}
			nbytes = pwrite(ifd, buffer, strlen(buffer), UNIT_SIZE*i);
			if(nbytes < 0){
				nslogit(func, "write cachefile %s failed", location);
				p_res = -2;
				break;
			}
			memset(buffer, 0, sizeof(buffer));
			bitmap[i] = '2';
			p_res = 0;
		}else if(bitmap[i] ==  '1' || bitmap[i] == '2'){
			continue;
		}else{
			nslogit(func, "%s bitmnap is wrong", path.c_str());
			p_res = -2;
			break;
		}		
	}
*/
	/* multi-thread mode*/
	off_t chunck_num = eblock_num - sblock_num;
	int thread_num = 0;
	if(chunck_num <= 100){
		for(off_t i = sblock_num; i < eblock_num; i++){
			if(bitmap[i] == '0'){
				nbytes = XrdPosixXrootd::Pread(ofd, buffer, UNIT_SIZE, UNIT_SIZE*i);
				if(nbytes < 0){
                                	nslogit(func, "xrdread %s failed", path.c_str());
                                	p_res = -2;
                                	break;
                        	}
                        	nbytes = pwrite(ifd, buffer, strlen(buffer), UNIT_SIZE*i);
                        	if(nbytes < 0){
                                	nslogit(func, "write cachefile %s failed", location);
                                	p_res = -2;
                                	break;
                        	}
                        	memset(buffer, 0, sizeof(buffer));
                        	bitmap[i] = '2';
                        	p_res = 0;
                	}else if(bitmap[i] ==  '1' || bitmap[i] == '2'){
                        	continue;
                	}else{
                        	nslogit(func, "%s bitmnap is wrong", path.c_str());
                        	p_res = -2;
                        	break;
                	}
        	}	
	}else{
		thread_num = chunck_num/100;
		if(thread_num % 100){
			thread_num++;
		}
		pthread_t threads[thread_num];
		void *status[thread_num];
		struct thread_data td[thread_num];
		for(int i = 0; i < thread_num; i++){
			td[i].ifd = ifd;
			td[i].ofd = ofd;	
			td[i].sblock = i*100;
			td[i].bitmap = bitmap;
			if(i == (thread_num - 1)){
				td[i].eblock = eblock_num - 100*(i-1);
			}else{
				td[i].eblock = 100*(i+1);
			}
			if(pthread_create(&threads[i], NULL, Procread, (void *)&td[i])){
				nslogit(func, "%d: thread create failed", i);
				break;
			}
		}
		for(int i = 0; i < thread_num; i++){
			pthread_join(threads[i], (void **)&status[i]);
		}
		for(int i = 0; i < thread_num; i++){
			int *p =(int *)status[i];
			if(*p != 0){
				nslogit(func, "%d: thread get failed %d", i, *p);
			}
			p_res = *p;
			free(status[i]);
		}
	}

	XrdPosixXrootd::Close(ofd);
	close(ifd);
	if(p_res == 0){
		res = Cns_set_t_filebitmap(&thip->dbfd, file_tmp, basename, bitmap);
	}else if(p_res == -2){
		res = 1;
	}else{
		res = 0;
	}
	sbp =repbuf;
	if(res == 0 && buff_tag ==1 && size <= UNIT_SIZE){
		if(*buff != '\0'){
			marshall_LONG(sbp ,res);
			marshall_STRING(sbp, buff);
		}else{
			int fd = open(location, O_RDONLY);
			if(fd == -1){
				marshall_LONG(sbp, 1);
				marshall_LONG(sbp, "Fail");
				res = 1;
			}else{
				pread(fd, buff, size, offset);
				close(fd);
				marshall_LONG(sbp, res);
				marshall_STRING(sbp, buff);
			}
		}
	}else if(res == 1 && buff_tag == 1){
		marshall_LONG(sbp ,res);
		marshall_STRING(sbp, "Fail");
	}else{
		marshall_LONG(sbp, res);
	}
	sendrep(thip->s, MSG_DATA, sbp - repbuf, repbuf);
	free(bitmap);
	free(file_tmp);
	free(buff);
	free(repbuf);
	return res;
}
