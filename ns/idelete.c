#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <queue>
#include "Cgetopt.h"
#include "Cns_api.h"
#include "serrno.h"
#include "u64subr.h"

#define VERSION 0.10
int cflag = 0;
int rflag = 0;
int Vflag = 0;
int helpflg = 0;

int openpath(char *sourcepath, char *actual_path, size_t *filesize)
{
	int res;
	res = xrd_open(sourcepath, 16, 0, actual_path, filesize);
	return res;
}

int procpath(char *actual_path, char *fullpath, size_t filesize)
{
	int res = 0;
	char host[CA_MAXPATHLEN+1];
	char *tmppath;
	char *basename;
	gethostname(host, sizeof(host));
	/*only del cache*/
	if(cflag){
		res = xrd_refresh(actual_path, fullpath, filesize);
		if(res){
			fprintf(stderr, "%s: cache file delete fail\n", fullpath);
			return res;
		}
	}else{
		/* del meta and cache by default*/
		tmppath = (char *)malloc(strlen(fullpath)+1);
		basename = (char *)malloc(strlen(fullpath)+1);
		strcpy(tmppath, fullpath);
		splitname(tmppath, basename);
		res = xrd_unlink(tmppath, basename);
		if(res){
			fprintf(stderr, "%s: cache drop fail\n", fullpath);
			return res;
		}
		free(basename);
		free(tmppath);
	}
	return 0;
}

int procdir(char *fullpath)
{
	int res = 0;
	queue <string> dir;
	vector <string> filename;
	vector <struct stat> st;
	string path;
	size_t filesize;
	char tmppath[CA_MAXPATHLEN+1];
	char actual_path[CA_MAXPATHLEN+1];
	char basename[CA_MAXPATHLEN+1];
	int len = strlen(fullpath);
	if(fullpath[len-1] == '/'){
		fullpath[len-1] = '\0';
	}
	/*only del cache*/
	if(cflag){
		dir.push(fullpath);
		while(!dir.empty()){
			path = dir.front();
			dir.pop();
			if((res = xrd_readdir(path.c_str(), filename, st)) == 0){
				for(int i = 0; i < filename.size(); i++){
					sprintf(tmppath, "%s/%s", path.c_str(), filename[i].c_str());
					if(S_ISDIR(st[i].st_mode)){
						dir.push(tmppath);
					}else{
						res = openpath(tmppath, actual_path, &filesize);
						if(res){
							fprintf(stderr, "%s: openpath failure %s\n", tmppath, strerror(errno));
							continue;
						}
						res = procpath(actual_path, tmppath, filesize);
						if(res){
							fprintf(stderr,"%s: procdir subpath failure %s\n", tmppath, strerror(errno));
							continue;
						}		
					}
				}
				filename.clear();
				st.clear();
			}else{
				fprintf(stderr, "%s: procdir readdir failure %s\n", path.c_str());
				continue;
			}
		}
	}else{/*del meta and cache*/
		strcpy(tmppath, fullpath);
		splitname(tmppath, basename);
		res = xrd_unlink(tmppath, basename);
		if(res){
			fprintf(stderr, "%s: cache drop fail\n", fullpath);
			return 1;
		}
	}
	return 0;
}

int main(int argc, char* argv[])
{
	int c;
	int i;
	char *path;
	char fullpath[CA_MAXPATHLEN+1];
	char actual_path[CA_MAXPATHLEN+1];
	size_t filesize;
	int res;
	int errflg = 0;
	Coptind = 1;
	Copterr = 1;
	static struct Coptions longopts[] = {
		{"help", NO_ARGUMENT, &helpflg, 1},
		{"version", NO_ARGUMENT, &Vflag, 1},
		{0, 0, 0, 0}
	};
	/* for args */
	while ((c = Cgetopt_long(argc, argv, "chrV", longopts, NULL)) != EOF){
		switch(c){
			case 'c':
				cflag++;
				break;
			case 'h':
				helpflg++;
				break;
			case 'r':
				rflag++;
				break;
			case 'V':
				Vflag++;
				break;
			case '?':
				errflg++;
				break;
			default:
				break;
		}
	}
	if (Vflag){
		fprintf(stdout, "Version: %.2f\n", VERSION);
		exit(0);
	}
	/*no file configure*/
	if (Coptind >= argc && !errflg){
		errflg++;
	}
	if (errflg){
		fprintf(stderr, "usage: %s [-chrV] [--help version] path...\ninfo: delete cache files\n", argv[0]);
		exit (USERR);
	}
	/* each file check*/
	for (i = Coptind; i < argc; i++){
		path = argv[i];
		if(*path != '/'){
			fprintf(stderr, "%s: invalid path", path);
			errflg++;
			continue;
		}else{
			if(strlen(path) > CA_MAXPATHLEN){
				fprintf(stderr, "%s : %s\n", path, sstrerror(SENAMETOOLONG));
				errflg++;
				continue;	
			}else
				strcpy(fullpath, path);
		}
		res = openpath(fullpath, actual_path, &filesize);
		if(res == 0){
			if(procpath(actual_path, fullpath, filesize)){
				fprintf(stderr, "%s: procget failure %s\n", fullpath, strerror(errno));
				errflg++;
			}
		}else if(res == EISDIR){
			if(!rflag){
				fprintf(stderr, "%s: derictories must be with -r\n", fullpath);
				errflg++;
			}else{
				if(procdir(fullpath)){
					errflg++;
				}
			}
		}else{
			fprintf(stderr, "%s: no such file/directory\n", fullpath);
			errflg++;
		}
	}
	if(errflg){
		exit(USERR);
	}
	exit(0);
}
