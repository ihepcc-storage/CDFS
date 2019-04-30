#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include "marshall.h"
#include "Cns_api.h"
#include "Cns.h"
#include "serrno.h"

int Cns_setmetadata(const char *filename, struct Cns_filestat fst)
{
	int res;
	char *actual_path;
        int c;
        char func[16];
        gid_t gid;
        int msglen;
        char *q;
        char *sbp;
	char *rbp;
        char sendbuf[REQBUFSZ];
	char repbuf[CA_MAXCOMMENTLEN+1];
        char server[CA_MAXHOSTNAMELEN+1];
        struct Cns_api_thread_info *thip;
        uid_t uid;

        strcpy (func, "Cns_setmetadata");
        if (Cns_apiinit (&thip))
                return (-1);
        /*get the user info*/
        uid = geteuid();
        gid = getegid();

	if (! filename || ! (&fst)) {
                serrno = EFAULT;
                return (-1);
        }

        if (strlen (fst.path) > CA_MAXPATHLEN) {
                serrno = ENAMETOOLONG;
                return (-1);
        }


        if (Cns_selectsrvr (fst.path, thip->server, server, &actual_path))
                return (-1);

        /* Build request header */

        sbp = sendbuf;
        marshall_LONG (sbp, CNS_MAGIC);
        marshall_LONG (sbp, CNS_SETFILETRANSFORMMETADATA);
        q = sbp;        /* save pointer. The next field will be updated */
        msglen = 3 * LONGSIZE;
        marshall_LONG (sbp, msglen);
        /* Build request body */
        marshall_HYPER (sbp, thip->cwd);
        marshall_LONG (sbp, fst.uid);
        marshall_LONG (sbp, fst.gid);
        marshall_HYPER (sbp, fst.ino);
        marshall_TIME_T (sbp, fst.mtime);
        marshall_TIME_T (sbp, fst.ctime);
        marshall_TIME_T (sbp, fst.atime);
        marshall_LONG (sbp, fst.nlink);
        marshall_LONG (sbp, fst.dev);
        marshall_STRING (sbp, fst.path);
        marshall_HYPER (sbp, fst.filesize);
        marshall_WORD (sbp, fst.filemode);
        marshall_STRING (sbp, filename);

        msglen = sbp - sendbuf;
        marshall_LONG (q, msglen);      /* update length field */

        while ((c = send2nsd (NULL, server, sendbuf, msglen, repbuf, sizeof(repbuf))) &&
            serrno == ENSNACT)
                sleep (RETRYI);
        if (c == 0) {
                rbp = repbuf;
                unmarshall_LONG(rbp, res);
        }
        if (c && serrno == SENAMETOOLONG)
                serrno = ENAMETOOLONG;
        return res;

}
