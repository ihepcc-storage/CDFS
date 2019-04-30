#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pwd.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#include <netinet/in.h>
#endif

#include "marshall.h"
#include "Cns_api.h"
#include "Cns.h"
#include "serrno.h"

#define MEMSIZE (1024*1024+10)

int Cns_download_seg(const char *path, off_t offset, size_t size, char *location, size_t filesize, char *buff, char *host)
{
	char *actual_path;
	int msglen;
	char *sbp;
	char *rbp;
	char *q;
	int buff_tag=0;
	char func[16];
	char sendbuf[REQBUFSZ];
	int c;	
	int res=-1;
	char server[CA_MAXHOSTNAMELEN+1];
	struct Cns_api_thread_info *thip;
	char *repbuf=(char *)malloc(MEMSIZE);
	char *buff_tmp=(char *)malloc(size*2);
	strcpy(func, "Cns_download_seg");
	if(Cns_apiinit(&thip))
		return -1;
	uid_t uid=getuid();
	gid_t gid=getgid();
        struct passwd* pwd;
        pwd = getpwuid(uid);

	if(!path||!location||offset<0||size<=0||filesize<0){
		serrno=EFAULT;
		return -1;
	}
	if(strlen(path)>CA_MAXPATHLEN){
		serrno=ENAMETOOLONG;
		return -1;
	}
	
	if (Cns_selectsrvr (path, thip->server, server, &actual_path))
       		return (-1);

        /* Build request header */

        sbp = sendbuf;
        marshall_LONG (sbp, CNS_MAGIC);
        marshall_LONG (sbp, CNS_DOWNLOAD_SEG);
        q = sbp;        /* save pointer. The next field will be updated */
        msglen = 3 * LONGSIZE;
        marshall_LONG (sbp, msglen);
	
        /* Build request body */

        marshall_LONG (sbp, uid);
        marshall_LONG (sbp, gid);
        marshall_HYPER (sbp, thip->cwd);
      	
        marshall_STRING (sbp, actual_path);
	marshall_HYPER (sbp, offset);
	marshall_HYPER (sbp, size);
	marshall_STRING (sbp, location);
	marshall_HYPER (sbp, filesize);
	marshall_STRING (sbp, host);
	marshall_STRING (sbp, pwd->pw_name);
	if(buff!=NULL){
		buff_tag=1;
	}
	marshall_LONG(sbp, buff_tag);
        msglen = sbp - sendbuf;
        marshall_LONG (q, msglen);      /* update length field */
/*
        while ((c = send2nsd (NULL, server, sendbuf, msglen, NULL, 0)) &&
            serrno == ENSNACT)
                sleep (RETRYI);
        if (c && serrno == SENAMETOOLONG) serrno = ENAMETOOLONG;
*/
	while ((c = send2nsd (NULL, server, sendbuf, msglen, repbuf, MEMSIZE)) &&
            serrno == ENSNACT)
                sleep (RETRYI);
        if (c == 0 && buff_tag==1) {
                rbp = repbuf;
                unmarshall_LONG(rbp, res);
                unmarshall_STRING(rbp, buff_tmp);
		strncpy(buff, buff_tmp, size);
        }else if(c == 0 && buff_tag==0){
	        rbp = repbuf;
                unmarshall_LONG(rbp, res);	
	}
        if (c && serrno == SENAMETOOLONG) serrno = ENAMETOOLONG;
        free(repbuf);
	free(buff_tmp);
	if(c!=0)
		return c;
        return res;
}
