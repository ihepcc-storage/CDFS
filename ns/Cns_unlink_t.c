#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>
#include "marshall.h"
#include "Cns_api.h"
#include "Cns.h"
#include "serrno.h"

int Cns_unlink_t(const char *path, const char *name)
{
	int res;
	char *actual_path;
	int c, n;
	char func[16];
	uid_t uid;
	gid_t gid;
	int msglen;
        char *q;
	char *rbp;
        char *sbp;
        char sendbuf[REQBUFSZ];
	char repbuf[CA_MAXCOMMENTLEN+1];
        char server[CA_MAXHOSTNAMELEN+1];
        struct Cns_api_thread_info *thip;

	strcpy(func, "Cns_unlink_t");
        if (Cns_apiinit (&thip))
                return (-1);
        uid = geteuid();
        gid = getegid();
        if (! path || !name) {
                serrno = EFAULT;
                return (-1);
        }
        if (Cns_selectsrvr (path, thip->server, server, &actual_path))
                return (-1);
        /* Build request header */

        sbp = sendbuf;
        marshall_LONG (sbp, CNS_MAGIC);
        marshall_LONG (sbp, CNS_UNLINK_T);
        q = sbp;        /* save pointer. The next field will be updated */
        msglen = 3 * LONGSIZE;
        marshall_LONG (sbp, msglen);

        /* Build request body */

        marshall_LONG (sbp, uid);
        marshall_LONG (sbp, gid);
        marshall_HYPER (sbp, thip->cwd);
        marshall_STRING (sbp, path);
	marshall_STRING (sbp, name);

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
