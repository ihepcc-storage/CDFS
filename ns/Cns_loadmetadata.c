#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <netinet/in.h>
#include "marshall.h"
#include "Cns_api.h"
#include "Cns.h"
#include "serrno.h"

int Cns_loadmetadata(const char *host ,const char *path, const int depth)
{
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
	int res;

        strcpy (func, "Cns_loadmetadata");
        if (Cns_apiinit (&thip))
                return (-1);
        uid = geteuid();
        gid = getegid();
	struct passwd* pwd;
	pwd = getpwuid(uid);	

        if (! path ) {
                serrno = EFAULT;
                return (-1);
        }
                                                                                   
        if (strlen (path) > CA_MAXPATHLEN) {
                serrno = ENAMETOOLONG;
                return (-1);
        }

        if (Cns_selectsrvr (path, thip->server, server, &actual_path))
                return (-1);

        /* Build request header */

        sbp = sendbuf;
        marshall_LONG (sbp, CNS_MAGIC);
        marshall_LONG (sbp, CNS_LOADMETADATA);
        q = sbp;        /* save pointer. The next field will be updated */
        msglen = 3 * LONGSIZE;
        marshall_LONG (sbp, msglen);

        /* Build request body */

        marshall_LONG (sbp, uid);
        marshall_LONG (sbp, gid);
        marshall_HYPER (sbp, thip->cwd);
	marshall_STRING (sbp, pwd->pw_name);
	marshall_STRING (sbp, host);
        marshall_STRING (sbp, actual_path);
	marshall_LONG (sbp, depth);

        msglen = sbp - sendbuf;
        marshall_LONG (q, msglen);      /* update length field */

        while ((c = send2nsd (NULL, server, sendbuf, msglen, repbuf, sizeof(repbuf))) &&
            serrno == ENSNACT)
                sleep (RETRYI);
        if (c == 0) {
                rbp = repbuf;
                unmarshall_LONG (rbp, res);
        }
        if (c && serrno == SENAMETOOLONG) serrno = ENAMETOOLONG;
        return res;

}
