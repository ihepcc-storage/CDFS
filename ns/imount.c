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

#include "Cgetopt.h"
#include "Cns_api.h"
#include "serrno.h"
#include "u64subr.h"
#include "Cns.h"

#define VERSION 0.10
#define MAXURLLEN 256
#define DEPTH 0

int Vflag=0;

int helpflg=0;

int proc(void){
        int res;
	char *ddir=(char *)malloc(128);
	char *basename=(char *)malloc(CA_MAXPATHLEN+1);
	char *url_path=(char *)malloc(MAXURLLEN+1);
	vector <string> dv;
        string str_tmp;
	queue <string> dir;
	char path_tmp[CA_MAXPATHLEN+1];
	int mountdirnum=0;

	if(get_conf_value(CONF_FILE, "DATA_DIR", ddir)){
                printf("Configure wrong\n");
                return 1;
	}else{
		str_tmp=ddir;
        	SplitString(str_tmp, dv);
	}
	mountdirnum=dv.size();
	for(int i = 0; i < mountdirnum; i++){
		if(dv[i][dv[i].length()-1]=='/'){
			dv[i][dv[i].length()-1]='\0';
		}
		dir.push(dv[i]);
	}

	while(!dir.empty()){
		memset(path_tmp, '0', sizeof(path_tmp));
		strcpy(path_tmp, dir.front().c_str());
		dir.pop();
		res = xrd_loadmetadata(path_tmp, DEPTH);
		if(res){
			fprintf(stderr, "%s load metadata failed\n", url_path);
			continue;
		}
	}	
	
	free(ddir);
	free(basename);
	free(url_path);
	return res;
}

int main(int argc, char **argv)
{
	int c;
	int errflg=0;
        static struct Coptions longopts[] = {
                {"help", NO_ARGUMENT, &helpflg, 1},
                {"version", NO_ARGUMENT, &Vflag, 1},
                {0, 0, 0, 0}
	};
        Copterr = 1;
        Coptind = 1;
        while ((c = Cgetopt_long (argc, argv, "hV", longopts, NULL)) != EOF) {
                switch (c) {
                case 'V':
                        Vflag++;
                        break;
                case 'h':
                        helpflg++;
                        break;
                case '?':
                        errflg++;
                        break;
                default:
                        break;
                }
        }

        if (Coptind != argc && ! errflg) {      /* no file specified */
                errflg++;
        }
        if (errflg) {
                fprintf(stderr, "usage: %s [-hV] [--help version] ...\ninfo: load metadata from the remote station\n", argv[0]);
                exit (USERR);
        }
        if(Vflag){
                fprintf(stdout, "Version: %.2f\n", VERSION);
                exit(0);
        }
	if(helpflg){
		fprintf(stderr, "usage: %s [-hV] [--help version] ...\ninfo: load from the remote station\n", argv[0]);
		exit(0);
	}
	if(proc()){
		fprintf(stderr, "load metadata failed\n");
		errflg++;
	}
	
	if(errflg)
		exit(USERR);
	exit(0);	

}
