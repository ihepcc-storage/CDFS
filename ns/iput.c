#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <queue>
#include <uuid/uuid.h>
#include <dirent.h>
#include <pwd.h>
#include "Cgetopt.h"
#include "Cns_api.h"
#include "u64subr.h"
#define VERSION 0.10
#define PATH "/cdfs_data/"
#define BUFLEN 10485760

int cmflag=0;
int rflag=0;
int Vflag=0;
int Rflag=0;
int vflag=0;
int helpflg=0;

u_signed64 total_bytes = 0;

struct timeval first, last, finish;
XrdPosixXrootd xrdpx;

int openpath(char* sourcepath, char* actual_path, size_t *filesize){
        int res;
        res=xrd_open(sourcepath, 16, 0, actual_path, filesize);
        return res;
}

void computesize(u_signed64 size)
{

        total_bytes += size;
}


float computelapse(struct timeval ftime,struct timeval etime)
{
        struct timeval lapsed;
        long start, end;
        float tsend;
        start =  ftime.tv_sec*1000000 + ftime.tv_usec;
        end =  etime.tv_sec*1000000 + etime.tv_usec;
        tsend = (float) (end - start)/1000000;
        //fprintf(stdout,"tsend is \t%.3fs\n",tsend);
        return tsend;
}

int copyfile(int fd_in, int fd_out, size_t size, off_t offset)
{
	size_t nbread_tot = 0;
	size_t nbread = 0;
	size_t nbwrite_tot = 0;
	size_t nbwrite = 0;
	size_t nbtoread;
	off_t off_in = offset;
	off_t off_out = offset;
	char *buf =(char *)malloc(BUFLEN);
	if(!buf){
		fprintf(stderr, "allocate memory failed\n");
		return -1;
	}
	do{
		bzero(buf, BUFLEN);
		nbtoread = size - nbread_tot;
		if(nbtoread <= 0){
			break;
		}
		if(nbtoread > BUFLEN){
			nbtoread = BUFLEN;
		}
		nbread = pread(fd_in, buf, nbtoread, off_in);
		if(nbread < 0){
			fprintf(stderr, "Failed to read source file offset: %lu\n", off_in);
			return -1;
		}
		if(nbread == 0){
			off_t curoff = lseek(fd_in, 0, SEEK_CUR);
			break;
		}
		off_in = off_in + nbread;
		computesize(nbread);
		nbread_tot = nbread_tot + nbread;
		nbwrite_tot = 0;
		do{
			nbwrite = XrdPosixXrootd::Pwrite(fd_out, buf, nbread - nbwrite_tot, off_out);
			if(nbwrite < 0){
				fprintf(stderr, "write to cache file failed offset:%lu\n", off_out);
				return -1;
			}
			off_out = off_out + nbwrite;
			nbwrite_tot = nbwrite + nbwrite;
		}while(nbwrite_tot < nbread);
	}while(nbread_tot < size);
	free(buf);
	return 0;
}

int procput(char *sourcepath, char *targetpath)
{
	int res;
	struct timezone tzp;
	float tsend;
	char path_buff[1024];
	char host[64];
	char targetfile[CA_MAXPATHLEN+1];
	char cachefile[CA_MAXPATHLEN+1];
	char dir[CA_MAXPATHLEN+1];
	char str[32];
	char *p;
	size_t filesize;
	char actual_path[CA_MAXPATHLEN+1];
	XrdOucString xpath;
	XrdOucString xdirpath;
	XrdOucString xhost;
	XrdOucString xuser;
	XrdOucString xdir;
	XrdOucString xcachepath;
	uid_t uid;
	uuid_t uuid;
	struct passwd *pwd;
	struct stat statbuf;
	int fd_in;
	int fd_out;
	/*pre-deal the pathes*/
	gethostname(host, sizeof(host));
	strcpy(path_buff, sourcepath);
        stat(path_buff, &statbuf);
        total_bytes = statbuf.st_size;
        char *path_tmp=(char *)malloc(strlen(path_buff)+1);
        char *basename_tmp=(char *)malloc(strlen(path_buff)+1);
        splitname(path_buff, basename_tmp);
        sprintf(basename_tmp, "%s%s", basename_tmp, "_ul");
	sprintf(targetfile, "%s/%s", targetpath, basename_tmp);
	res = openpath(targetfile, actual_path, &filesize);
	if(res == 0 || res == EISDIR){
		fprintf(stdout, "%s: file already exit\n", targetfile);
		return 1;
	}
	int hashcode=RSHash(targetfile);
	int dirnum = hashcode % 6;
	sprintf(dir, "%s%d%s", PATH, dirnum, "/");
        uuid_generate(uuid);
        uuid_unparse(uuid, str);
        sprintf(cachefile, "%s%s", dir, str);
	p = getenv("CNS_HOST");
	xhost = p;
	uid = getuid();
	pwd = getpwuid(uid);
	xuser = pwd->pw_name;
	xdir = dir;
	xcachepath = cachefile;
	xdirpath = xuser + "://" + xhost + "/" + xdir;
	xpath = xuser + "://" + xhost + "/" + xcachepath;

	/*path_buff xdirpath xpath targetfile for use*/
	XrdPosixXrootd::setEnv("ConnectionWindow", 1);
	XrdPosixXrootd::setEnv("ConnectionRetry", 2);
	res = XrdPosixXrootd::Access(xdirpath.c_str(), F_OK | W_OK );
	if(res == -1){
		if((res = XrdPosixXrootd::Mkdir(xdirpath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) == -1){
			fprintf(stderr, "%s: xrd_mkdir failed\n", xdirpath.c_str());
			return -1;
		}
	}
	if(Rflag){
		res = xrd_unlink(targetpath, basename_tmp);
		if(res){
			fprintf(stderr,"xrd_unlink  %s/%s failed\n",targetpath, basename_tmp);
			return res;
		}
	}
	gettimeofday(&first, &tzp);
	fd_in = open(sourcepath, O_RDONLY);
	if(fd_in < 0){
		fprintf(stderr, "%s: open source file failed\n", sourcepath);
		return -1;
	}
	fd_out = XrdPosixXrootd::Open(xpath.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0644);
	if(fd_out < 0){
		fprintf(stderr, "%s: open cache file failed\n", xpath.c_str());
		return -1;
	}
	res = XrdPosixXrootd::Ftruncate(fd_out, total_bytes);
	if(res){
		fprintf(stderr, "%s: allocate space for cache file failed\n", cachefile);
		close(fd_in);
		XrdPosixXrootd::Close(fd_out);
		return -1;	
	}
	res = copyfile(fd_in, fd_out, total_bytes, 0);
	if(res){
		fprintf(stderr, "copy file to cache failed\n");
		return -1;
	}
	res = xrd_rfsync(cachefile, targetfile);
	gettimeofday(&finish, &tzp);
	tsend=computelapse(first, finish);
	if(vflag){
		if(res==0){
			char tmpbuf[21];
			bzero(tmpbuf, 21);
			u64tostr(total_bytes, tmpbuf, 0);
			fprintf(stdout, "Upload %s success:\n", path_buff);
			fprintf(stdout,"%s bytes transferred in %.2f seconds (%.2f MB/s)\n", tmpbuf,
                        tsend, (float)total_bytes/(tsend*1024*1024));
		}
	}
	if(res==0)
		return res;		
        else if(res==ENOENT){
        	fprintf(stderr, "%s: no such file/directory\n", sourcepath);
		res=-1;
        }else if(res==EISDIR){
                res=1;
        }else if(res==-ENOTDIR){
                fprintf(stderr, "%s: must be a directory\n", targetpath);
                exit(USERR);
        }else if(res==-ENOENT){
                fprintf(stderr, "%s: no such direcoty\n", targetpath);
                exit(USERR);
        }else if(res==-EEXIST){
                fprintf(stderr, "%s: file already upload\n", sourcepath);
                res=-1;
        }else if(res==EEXIST){
		fprintf(stderr, "%s: file already created\n", sourcepath);
		res=-1;
	}else if(res==EACCES){
		fprintf(stderr, "%s: permission denied\n", targetpath);
		res=-1;
	}else if(res==-1){
                fprintf(stderr, "Error: iput to %s failure\n", targetpath);
                exit(USERR);
        }else{
                fprintf(stderr, "%s: input failure\n", sourcepath);
                res=-1;
        }
        free(path_tmp);
        free(basename_tmp);
	close(fd_in);
	XrdPosixXrootd::Close(fd_out);
	return res;
}

int procdir(char *sourcepath, char *targetpath)
{
	int res;
	queue <string> dir;
	string path;
	char fullpath[CA_MAXPATHLEN+1];
	DIR *directory;
	struct dirent *ptr;
	struct stat st;
	int len=strlen(sourcepath);
	if(sourcepath[len-1]=='/')
		sourcepath[len-1]='\0';
	dir.push(sourcepath);
	while(!dir.empty()){
		path=dir.front();
		dir.pop();
		if((directory=opendir(path.c_str()))==NULL){
			fprintf(stderr, "%s: open dir error\n", path.c_str());
			res=-1;
			continue;	
		}else{
			while((ptr=readdir(directory))!=NULL){
				if(strcmp(ptr->d_name, ".")==0 || strcmp(ptr->d_name, "..")==0)
					continue;
				else{
					sprintf(fullpath, "%s/%s", path.c_str(), ptr->d_name);
					stat(fullpath, &st);
					if(S_ISDIR(st.st_mode))
						dir.push(fullpath);
					else
						res=procput(fullpath, targetpath);
				}
			}
		}
		closedir(directory);
	}
	return res;
}

int main(int argc, char * argv[])
{
	int c;
	int i;
	int errflg=0;
	Coptind=1;
	Copterr=1;
	int res;
	char targetpath[CA_MAXPATHLEN+1];
	char sourcepath[CA_MAXPATHLEN+1];
	char actual_path[CA_MAXPATHLEN+1];
	char path_buff[CA_MAXPATHLEN+1];
	size_t filesize;
	struct stat s_buf;
	static struct Coptions longopts[]={
		{"comment", NO_ARGUMENT, &cmflag, 1},
		{"version", NO_ARGUMENT, &Vflag, 1},
		{"help",    NO_ARGUMENT, &helpflg, 1},
		{"verbose", NO_ARGUMENT, &vflag, 1},
		{"refresh", NO_ARGUMENT, &Rflag, 1},
		{0, 0, 0, 0}
	};
	/*for args*/
	while((c=Cgetopt_long (argc, argv, "rvVRH", longopts, NULL)) != EOF){
		switch(c){
			case 'r':
				rflag++;
				break;
			case 'V':
				Vflag++;
				break;
			case 'H':
				helpflg++;
				break;
			case 'v':
				vflag++;
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
		fprintf(stdout,"Version: %.2f\n", VERSION);
		exit(0);
	}
	if(Coptind >= argc-1 && !errflg){
		errflg++;	
	}
	if(errflg){
		fprintf (stderr,
                    "usage: %s [-r] [--comment] files/directories r_directory...\ninfo: upload files to the remote station\n",
                        argv[0]);
		exit(USERR);
	}
	/*for targetpath*/
	strcpy(targetpath, argv[argc-1]);
	if(*targetpath != '/' || strlen(targetpath)+1 > CA_MAXPATHLEN){
		fprintf(stderr, "%s: invalid path\n", targetpath);
		exit(USERR);
	}else{
		res = openpath(targetpath, actual_path, &filesize);
		if(res != EISDIR){
			fprintf(stderr, "%s: targetpath must be a directory\n", targetpath);
			exit(USERR);
		}
	}
	/*for sourcepath*/
	for(i=Coptind; i<argc-1; i++){
		strcpy(sourcepath, argv[i]);
		if(realpath(sourcepath, path_buff) == NULL){//no such file
			fprintf(stderr, "%s: no such file/directory\n", path_buff);
			exit(USERR);
		}else{
			stat(path_buff, &s_buf);
			if(!S_ISDIR(s_buf.st_mode)){//files
				res = procput(path_buff, targetpath);
				if(res == 0 || res == 1)
					continue;
				else if(res == -1)
					errflg++;
			}else{//directory
				if(!rflag){
					fprintf(stderr, "%s: derictories must be with -r\n", path_buff);
					errflg++;
				}else{
					if(procdir(path_buff, targetpath)){
						errflg++;
					}
				}
			}
		}
	}

	if(errflg)
		exit(USERR);
	exit(0);
}
