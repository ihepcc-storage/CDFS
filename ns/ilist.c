/*	ilist - list name server directory/file entries */
#include <errno.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include "Cgetopt.h"
#include "Cns.h"
#include "Cns_api.h"
#include "serrno.h"
#include "u64subr.h"
#define SIXMONTHS (6*30*24*60*60)
#define VERSION 0.10
extern	char	*getenv();

int depth=1;
int lflag=0;
int iflag=0;
int cflag=0;
int uflag=0;
int Rflag=0;
int Vflag=0;

int cmflag=0;
int helpflg=0;

time_t current_time;
char host[64];

int listdir_t(char *dir);
int listentry(char *path,struct Cns_filestat *statbuf,char *comment, ino_t fileid);
int procpath(char *fullpath);

int main(int argc, char **argv)
{
        int c;
        char *dp;
        char *endp;
        int errflg = 0;
        char fullpath[CA_MAXPATHLEN+1];
        int i;
        static struct Coptions longopts[] = {
		{"comment", NO_ARGUMENT, &cmflag, 1},
		{"help", NO_ARGUMENT, &helpflg, 1},
		{"version", NO_ARGUMENT, &Vflag, 1},
		{"refresh", NO_ARGUMENT, &Rflag, 1},
        	{0, 0, 0, 0}
        };
        char *p;
        char *path;
        Copterr = 1;
        Coptind = 1;
        while ((c = Cgetopt_long (argc, argv, "ciluhVR", longopts, NULL)) != EOF) {
                switch (c) {
                case 'l':
                        lflag++;
                        break;
                case 'i':
                        iflag++;
                        break;
                case 'c':
                        cflag++;
                        break;
                case 'u':
                        uflag++;
                        break;
		case 'V':
			Vflag++;
			break;
		case 'h':
			helpflg++;
			break;
		case 'R':
			Rflag++;
			break;

                case '?':
                        errflg++;
                        break;
                default:
                        break;
                }
        }
        if(Vflag){
                fprintf(stdout, "Version: %.2f\n", VERSION);
                exit(0);
        }
        (void) time (&current_time);
        if (Coptind >= argc && ! errflg) {	/* no file specified */
        	errflg++;
        }
        if (errflg) {
                fprintf (stderr,
                    "usage: %s [-ciluVhR] [--comment] path...\ninfo: list files from the remote station\n",
                        argv[0]);
                exit (USERR);
        }
        for (i = Coptind; i < argc; i++) {
                path = argv[i];
                if (*path != '/') {
                	fprintf (stderr, "%s: invalid path\n", path);
                        errflg++;
                        continue;
                } else {
                        if (strlen (path) > CA_MAXPATHLEN) {
                                fprintf (stderr, "%s: %s\n", path, sstrerror(SENAMETOOLONG));
                                errflg++;
                                continue;
                        } else{
                                strcpy (fullpath, path);
			}
                }
                if (procpath (fullpath)) {
                        fprintf (stderr, "%s: %s\n", path, sstrerror(serrno));
                        errflg++;
                }
        }
        if (errflg)
                exit (USERR);
        exit (0);
}

int procpath(char *fullpath)
{
	int res;
        char comment[CA_MAXCOMMENTLEN+1];
        struct Cns_filestat statbuf;
	char tmp_path[CA_MAXPATHLEN+1];
	strcpy(tmp_path, fullpath);
	if(Rflag){
		if(strcmp(tmp_path, "/") == 0){
			fprintf(stdout, "WARNNING: root directory cannot refresh\n");
		}else{
			if(tmp_path[strlen(tmp_path)-1] == '/'){
				tmp_path[strlen(tmp_path)-1] = '\0';
			}
			res = xrd_loadmetadata(tmp_path, depth);
			if(res){
				fprintf(stderr, "load metadata failed\n");
				exit(USERR);
			}
		}
	}	
        if(Cns_get_Data_daemon(fullpath, &statbuf) < 0){
		 return (-1);
	}
	if(statbuf.filemode& S_IFDIR ){
		return(listdir_t(fullpath));
	}else{
	        if (statbuf.status == 'D')
 	               return (0);
        	return(listentry(fullpath, &statbuf, comment, 0));
	}
}

int listdir_t(char *dir)
{
	char curdir[CA_MAXPATHLEN+1];
	struct Cns_direncomm *dcp;
	struct dirlist {
		char *d_name;
		struct dirlist *next;
	};
	Cns_DIR *dirp;
	struct dirlist *dlc;		/* pointer to current directory in the list */
	struct dirlist *dlf = NULL;	/* pointer to first directory in the list */
	struct dirlist *dll;		/* pointer to last directory in the list */
	struct dirent *dp;
	struct Cns_direnstatc *dxcp;
	struct Cns_direnstat *dxp;
	struct Cns_direntape *dxtp;
	if ((dirp = Cns_opendir_t (dir)) == NULL)
		return (-1);

	if (! iflag && ! lflag) {
		/* dirent entries contain enough information */
		while ((dp = Cns_readdir_t (dirp)) != NULL) {
			printf ("%s\n", dp->d_name);
		}
	} else {
		while ((dxp = Cns_readdirx_t (dirp)) != NULL) {
//			printf("--------------%d  %d  %ld  %s---------- \n", dxp->fileid, dxp->filemode,  dxp->filesize, dxp->d_name);
			listentry (dxp->d_name, (Cns_filestat*)dxp, NULL, dirp->fileid);
		}
	}
	(void) Cns_closedir_t (dirp);
	while (dlf) {
		if (strcmp (dir, "/"))
			sprintf (curdir, "%s/%s", dir, dlf->d_name);
		else
			sprintf (curdir, "/%s", dlf->d_name);
		if (listdir_t (curdir) < 0)
			fprintf (stderr, "%s: %s\n", curdir, sstrerror(serrno));
		free (dlf->d_name);
		dlc = dlf;
		dlf = dlf->next;
		free (dlc);
	}
	return (0);
}


int listentry(char *path, struct Cns_filestat *statbuf, char *comment, ino_t fileid)
{
        struct passwd *pw;
        struct group *gr;
        char modestr[11];
        static gid_t sav_gid = -1;
        static char sav_gidstr[7];
        time_t ltime;
        static uid_t sav_uid = -1;
        static char sav_uidstr[CA_MAXUSRNAMELEN+1];
        char timestr[13];
        struct tm *tm;
        char tmpbuf[21];

        if (statbuf->status == 'D')
                return (0);
        if (iflag)
//		printf("%d  ",statbuf->fileid);
                printf ("%s ", u64tostr (statbuf->fileid, tmpbuf, 20));
        if (lflag) {
                if (statbuf->filemode & S_IFDIR)
                        modestr[0] = 'd';
                else
                        modestr[0] = statbuf->status;
                modestr[1] = (statbuf->filemode & S_IRUSR) ? 'r' : '-';
                modestr[2] = (statbuf->filemode & S_IWUSR) ? 'w' : '-';
                if (statbuf->filemode & S_IXUSR)
                        if (statbuf->filemode & S_ISUID)
                                modestr[3] = 's';
                        else
                                modestr[3] = 'x';
                else
                        modestr[3] = '-';
                modestr[4] = (statbuf->filemode & S_IRGRP) ? 'r' : '-';
                modestr[5] = (statbuf->filemode & S_IWGRP) ? 'w' : '-';
                if (statbuf->filemode & S_IXGRP)
                        if (statbuf->filemode & S_ISGID)
                                modestr[6] = 's';
                        else
                                modestr[6] = 'x';
                else
                        modestr[6] = '-';
                modestr[7] = (statbuf->filemode & S_IROTH) ? 'r' : '-';
                modestr[8] = (statbuf->filemode & S_IWOTH) ? 'w' : '-';
                if (statbuf->filemode & S_IXOTH)
                        if (statbuf->filemode & S_ISVTX)
                                modestr[9] = 't';
                        else
                                modestr[9] = 'x';
                else
                        modestr[9] = '-';
                modestr[10] = '\0';
                if (statbuf->uid != sav_uid) {
                        sav_uid = statbuf->uid;
                        if (pw = getpwuid (sav_uid))
                                strcpy (sav_uidstr, pw->pw_name);
                        else
                                sprintf (sav_uidstr, "%-8u", sav_uid);
                }
                if (statbuf->gid != sav_gid) {
                        sav_gid = statbuf->gid;
                        if (gr = getgrgid (sav_gid)) {
                                strncpy (sav_gidstr, gr->gr_name, sizeof(sav_gidstr) - 1);
                                sav_gidstr[sizeof(sav_gidstr) - 1] = '\0';
                        } else
                                sprintf (sav_gidstr, "%-6u", sav_gid);
                }

                if (cflag)
                        ltime = statbuf->ctime;
                else if (uflag)
                        ltime = statbuf->atime;
                else
                        ltime = statbuf->mtime;
                tm = localtime (&ltime);
                if (ltime < current_time - SIXMONTHS ||
                    ltime > current_time + 60)
                        strftime (timestr, 13, "%b %d  %Y", tm);
                else
                        strftime (timestr, 13, "%b %d %H:%M", tm);

                printf ("%s %3d %-8.8s %-6.6s %s %s ",
                    modestr, statbuf->nlink, sav_uidstr, sav_gidstr,
                    u64tostr (statbuf->filesize, tmpbuf, 20), timestr);
  
      }
	if(fileid==1){
		printf("%s\n", path);
		return 0;
	}
	char basename[214];
       	char *p;	
	p = path + strlen (path) - 1;
	while (*p == '/' && p != path)
		*p = '\0';
	if ((p = strrchr (path, '/')) == NULL)
		p = path - 1;
	strcpy (basename, (*(p + 1)) ? p + 1 : "/");
	if (p <= path)	
		p++;
	*p = '\0';
	printf ("%s", basename);
        printf ("\n");
        return (0);
}

