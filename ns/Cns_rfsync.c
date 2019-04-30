#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include "marshall.h"
#include "Cns_api.h"
#include "Cns.h"
#include "serrno.h"

int Cns_rfsync(const char *from, const char *to){
        char *actual_path;
        int c, n;
        char func[16];
        gid_t gid;
        int msglen;
        char *q;
        char *sbp;
        char *rbp;
        char sendbuf[REQBUFSZ];
        char server[CA_MAXHOSTNAMELEN+1];
        struct Cns_api_thread_info *thip;
        uid_t uid;
        char repbuf[CA_MAXCOMMENTLEN+1];
        int res;

        strcpy (func, "Cns_rfsync");
        if (Cns_apiinit (&thip))
                return (-1);
        uid = getuid();
        gid = getgid();
	struct passwd *pwd;
	pwd = getpwuid(uid);
        if (! to || !from) {
                serrno = EFAULT;
                return (-1);
        }

        if (Cns_selectsrvr (to, thip->server, server, &actual_path))
                return (-1);

        /* Build request header */

        sbp = sendbuf;
        marshall_LONG (sbp, CNS_MAGIC);
        marshall_LONG (sbp, CNS_RFSYNC);
        q = sbp;        /* save pointer. The next field will be updated */
        msglen = 3 * LONGSIZE;
        marshall_LONG (sbp, msglen);

        /* Build request body */

        marshall_LONG (sbp, uid);
        marshall_LONG (sbp, gid);
        marshall_HYPER (sbp, thip->cwd);
        marshall_STRING (sbp, from);
        marshall_STRING (sbp, to);
	marshall_STRING (sbp, pwd->pw_name);

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
