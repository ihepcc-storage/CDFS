/*	iget - download files from remote station  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <queue>
#include <pwd.h>
#include "Cgetopt.h"
#include "Cns_api.h"
#include "serrno.h"
#include "u64subr.h"
#include "XrdPosix/XrdPosixXrootd.hh"
#include "XrdOuc/XrdOucString.hh"
#include "XrdCl/XrdClFile.hh"

#define BUFLEN 10485760 //1MB
#define SMALLSIZE 10485760 //10MB 
#define THREAD_NUM 10 
#define VERSION 0.10

int rflag = 0;
int cmflag = 0;
int Rflag = 0;
int Sflag = 0;

u_signed64 total_bytes = 0;
u_signed64 last_bytes = 0;
struct timeval last_time;
struct timeval  first, finish;
pthread_mutex_t  size_lock;

int verbose = 0;
int helpflg = 0;
int stopflag = 0;
int force = 0;
int mthread = 0;
int being_shutdown = 0;

XrdPosixXrootd xrdpx;

void cleanup(int sig)
{
        fprintf(stderr, "\nuser canceled!!\n");
        being_shutdown = 1;
        errno = ECANCELED;
}

struct mProcArg
{
        unsigned long long fd_in;
        unsigned long long fd_out;
        size_t size;
        off_t offset;
};

void computesize(u_signed64 size)
{

	pthread_mutex_lock (&size_lock);
	total_bytes += size;
	pthread_mutex_unlock (&size_lock);
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

void *perf_mon(void *) {
        struct timezone tzp;
        struct timeval curtime;
        int ret = 0;
        float  tsend;
        float  atsend;
        u_signed64 transferred;
        while (stopflag == 0) {
                char tmpbuf[21];
                float avg;
                float inst;
                if (total_bytes == 0)
                        continue;
                gettimeofday (&curtime, &tzp);
                atsend = computelapse(first, curtime);
                tsend = computelapse(last_time, curtime);
                transferred = total_bytes - last_bytes;
                avg = (float)total_bytes/(atsend*1024*1024);
                inst = (float)transferred/(tsend*1024*1024);
                fprintf(stdout,"\r %s bytes", u64tostr(total_bytes, tmpbuf, 0));
                fprintf(stdout," %9.2lf MB/sec avg", avg);
                fprintf(stdout," %9.2lf MB/sec inst", inst);
                fflush(stdout);
                last_bytes = total_bytes;
                last_time = curtime;
                for (int i=0;i<100;i++) {
			if (!stopflag) usleep(10000);
		}
		//sleep(1);
        }
        pthread_exit((void *)&ret);
}



int mktargetdir(char *filepath, char *targetpath)
{
	char basename[CA_MAXPATHLEN+1];
	char path_s[CA_MAXPATHLEN+1];
	strcpy(path_s, filepath);
	splitname(path_s, basename);
	sprintf(targetpath, "%s/%s", targetpath, basename);
	if(mkdirs(targetpath, 0755)!=0){
		return 1;
	}	
	return 0;
	
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
/*
                nbread = pread(fd_in, buf, nbtoread, off_in);
*/
		nbread = XrdPosixXrootd::Pread(fd_in, buf, nbtoread, off_in);
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
	printf("DEBUG: read %lu bytes of size %lu\n", nbread_tot, size);
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

int f_cp_fd(char *actual_path, char *targetpath, char *basename, size_t filesize)
{
        char fullpath[CA_MAXPATHLEN+1];
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
	char *p;
	uid_t uid;
	struct passwd* pwd;
	int nbytes;
	sprintf(fullpath,"%s/%s",targetpath, basename);
	if (!stat(fullpath, &stbuf)) {
		if ((stbuf.st_size == filesize) && (force == 0)) {
			fprintf(stderr, "WARN: Target file %s exists, please specify '--force' or '-f' to overwrite it\n", fullpath);
			return 0;
		}
		if (unlink(fullpath) < 0){
			fprintf(stderr, "Failed to overwrite existing file %s %s\n", fullpath, strerror(errno));
			return -1;
		}
	}

        XrdOucString path;
        XrdOucString xhost;
        XrdOucString xuser;
        XrdOucString xpath;
	p = getenv("CNS_HOST");
	xhost = p;
	uid = getuid();
	pwd = getpwuid(uid);
	xuser = pwd->pw_name;
	xpath = actual_path;
	path = xuser + "://" + xhost + "/" + xpath;
        XrdPosixXrootd::setEnv("ConnectionWindow", 1);
        XrdPosixXrootd::setEnv("ConnectionRetry", 2);
	fd_in = XrdPosixXrootd::Open(path.c_str(), O_RDONLY, 0);
	if(fd_in < 0){
		fprintf(stderr, "xrd_open cache file failed\n");
		return -1;
	}
/*
	if((fd_in = open(actual_path, O_RDONLY))==NULL){
                fprintf(stderr,"open data_dir %s failure %s\n", actual_path, strerror(errno));
                return -1;
        }
*/
	if (verbose) {
		printf("DEBUG: Cache file: %s filesize %lu\n", actual_path, filesize);
	}
        if((fd_out = open(fullpath, O_WRONLY|O_TRUNC|O_CREAT, 0644))==NULL){
                fprintf(stderr,"open target_file %s failure %s\n", fullpath, strerror(errno));
                return -1;
        }
	if (!mthread || (filesize < SMALLSIZE)) {
                res =  copyfile(fd_in, fd_out, filesize, 0);
                goto _COPY_DONE;
        }
	if (ftruncate(fd_out, filesize)  < 0){
                fprintf(stderr, "Failed to allocate space for file '%s' %s\n", fullpath, strerror(errno));
                XrdPosixXrootd::Close(fd_in);
                close(fd_out);
                return(-1);
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
	}
	for (i = 0; i < THREAD_NUM; i++) {
		if (verbose) {
			int *p = (int *)status[i];
                	if (*p != 0) printf("DEBUG: threadid %ld return %d\n", threadid[i], *p);
		}
		free(status[i]);
        }
	pthread_mutex_destroy (&size_lock);
_COPY_DONE:
	stopflag = 1;
   	XrdPosixXrootd::Close(fd_in);
        close(fd_out);
	return res;
}

int procget(char *actual_path, size_t size, off_t offset, char *sourcepath, char *targetpath)
{
        int res;
	char basename[CA_MAXPATHLEN+1];
	char fullpath[CA_MAXPATHLEN+1];
	pthread_t pm_thread;
	char *src, *tgt;
        struct timezone tzp;
        float tsend;
	char host[CA_MAXPATHLEN+1];

	gethostname(host, sizeof(host));	
	gettimeofday (&first, &tzp);
	if(Rflag){
		res = xrd_refresh(actual_path, sourcepath, size);
		if(res){
			fprintf(stderr, "xrd_refresh cache %s failed", sourcepath);
			return res;
		}
	}
        res = xrd_read(actual_path, size, 0, NULL, sourcepath, size, host);
	if (res) {
		fprintf(stderr, "xrd_read %s failed\n", actual_path);
		return res;
	}
	/*1 only cache no download*/
	if (Sflag){
		return res;
	}
	/*2 sync to client*/
	strcpy(fullpath, sourcepath);
	splitname(fullpath, basename);
	if (verbose) {
               	if (pthread_create(&pm_thread, NULL, perf_mon, NULL) < 0)
                       	return -1;
      	}
//       	gettimeofday (&first, &tzp);
       	last_time = first;
	res = f_cp_fd(actual_path, targetpath, basename, size);			
        stopflag = 1;
        gettimeofday (&finish, &tzp);
        tsend = computelapse(first, finish);
	if (verbose) {
                printf("\n");
                pthread_join(pm_thread, NULL);
                if (res == 0) {
                        char tmpbuf[21];
                        bzero(tmpbuf, 21);
                        u64tostr(total_bytes, tmpbuf, 0);
                        fprintf(stdout,"%s bytes transferred in %.2f seconds (%.2f MB/s)\n", tmpbuf,
                        tsend, (float)total_bytes/(tsend*1024*1024));
        	}
	}

	return res;
}

int openpath(char* sourcepath, char* actual_path, size_t *filesize){
        int res;
        res=xrd_open(sourcepath, 16, 0, actual_path, filesize);
        return res;
}

int procdir(char *sourcepath, char *targetpath){
	queue <string> dir;
	vector <string> filename;
	vector <struct stat> st;
	string path;
	int res;
	char fullpath[CA_MAXPATHLEN+1];
	char actual_path[CA_MAXPATHLEN+1];
	char path_t[CA_MAXPATHLEN+1];
	size_t filesize;
	int len;
	len=strlen(sourcepath);
	if(sourcepath[len-1]=='/'){
		sourcepath[len-1]='\0';
	}
	dir.push(sourcepath);
	strcpy(path_t, targetpath);
	mktargetdir(sourcepath, path_t);
	while(!dir.empty()){
		path=dir.front();
		dir.pop();
		if((res=xrd_readdir(path.c_str(), filename, st))==0){
			for(int i=0; i<filename.size();i++){
				sprintf(fullpath, "%s/%s", path.c_str(), filename[i].c_str());
				if(S_ISDIR(st[i].st_mode)){
					dir.push(fullpath);
					if(mktargetdir(fullpath, path_t)!=0){
						printf("targetpath: %s\n", path_t);
						fprintf(stderr,"%s: mkdir failure %s\n", path_t, strerror(errno));
						res=1;
					}
				}else{
					if(int t=(openpath(fullpath, actual_path, &filesize))){
						fprintf(stderr,"%s: openpath failure %s\n", fullpath, strerror(errno));
						res=1;	
					}
					if(int tt=(procget(actual_path, filesize, 0, fullpath, path_t))){
						fprintf(stderr,"%s: procget failure %s\n", fullpath, strerror(errno));
						res=1;
					}
				}
			}
			filename.clear();
			st.clear();	
		}else{
			res=1;
		}
		
	}
	return res;
}

int main(int argc, char **argv)
{
        int c;
	int i;
        int errflg = 0;
	Coptind = 1;
	Copterr = 1;
	char *path;
	char targetpath[CA_MAXPATHLEN+1];
	char sourcepath[CA_MAXPATHLEN+1];
	struct stat s_buf;
	char actual_path[CA_MAXPATHLEN+1];
	size_t filesize;
	int res;
	char path_buff[CA_MAXPATHLEN+1];
        static struct Coptions longopts[] = {
		{"comment", NO_ARGUMENT, &cmflag, 1},
		{"help", NO_ARGUMENT, &helpflg, 1},
                {"verbose", NO_ARGUMENT, &verbose, 1},	
                {"force", NO_ARGUMENT, &force, 1},	
                {"mthread", NO_ARGUMENT, &mthread, 1},
		{"refresh", NO_ARGUMENT, &Rflag, 1},
		{"subscription", NO_ARGUMENT, &Sflag, 1},	
	        {0, 0, 0, 0}
        };
	/*for args*/
        while ((c = Cgetopt_long (argc, argv, "rhvVfmRS", longopts, NULL)) != EOF) {
                switch (c) {
                case 'r':
                        rflag++;
                        break;
		case 'h':
                        helpflg = 1;
                        break;
                case 'v':
                        verbose = 1;
                        break;
		case 'f':
                        force = 1;
                        break;
		case 'm':
                        mthread = 1;
                        break;
                case 'V':
                        fprintf(stdout, "Version: %.2f\n", VERSION);
                        exit (0);
		case 'R':
			Rflag = 1;
			break;
		case 'S':
			Sflag = 1;
			break;
                case '?':
                        errflg++;
                        break;
                default:
                        break;
                }
        }
	if(Coptind >= argc-1 && !errflg){
		errflg++;
	}
        if (errflg) {
                fprintf (stderr,
                    "usage: %s [-hvfmRrV] [--help --verbose --force --mthread --refreash] files/direcories l_directory...\ninfo: get files from the remote station\n",
                        argv[0]);
                exit (USERR);
        }
	/*for targetpath*/
	strcpy(targetpath,argv[argc-1]);
	if(realpath(targetpath, path_buff)==NULL){
		fprintf(stderr,"%s: no such directory\n",targetpath);
		exit (USERR);
	}else{
		stat(path_buff, &s_buf);
		if(!S_ISDIR(s_buf.st_mode)){
			fprintf(stderr,"%s:targetpath must be a directory\n",path_buff);
			exit (USERR);	
		}	
	}
	/*for sourcepath*/
	for(i=Coptind; i<argc-1; i++){	
		path = argv[i];
		if(*path != '/'||strlen(path)+1 > CA_MAXPATHLEN){
			fprintf(stderr,"%s: invalid path\n",path);
			errflg++;
			continue;
		}else{
			strcpy(sourcepath, path);
		}
		res = openpath(sourcepath, actual_path, &filesize);
		if(res == 0){//files
			if(procget(actual_path, filesize, 0, sourcepath, path_buff)){
				fprintf(stderr,"%s: procget failure %s\n", sourcepath, strerror(errno));
				errflg++;
			}
		}else if(res == EISDIR){//directories
			if(!rflag){
				fprintf(stderr,"%s: derictories must be with -r\n", sourcepath);
				errflg++;
			}else{
				if(procdir(sourcepath, path_buff)){
					errflg++;
				}
			}		
		}else{//fail
			fprintf(stderr,"%s: invalid path\n",sourcepath);
			errflg++;
		}
	}	

        if (errflg)
                exit (USERR);
        exit (0);
}

