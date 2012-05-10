/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/

/* pbs_connect.c
 *
 * Open a connection with the TORQUE server.  At this point several
 * things are stubbed out, and other things are hard-wired.
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <pwd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/param.h>
#if HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif
#if HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>

#include "libpbs.h"
#include "csv.h"
#include "dis.h"
#include "net_connect.h"
#include "log.h" /* log */
#include "../Liblog/log_event.h" /* log_event */
#include "../Libnet/lib_net.h" /* socket_* */
#include "../Libifl/lib_ifl.h" /* AUTH_TYPE_IFF, DIS_* */
#include "pbs_constants.h" /* LOCAL_IP */

#define LOCAL_LOG_BUF 1024
#define CNTRETRYDELAY 5
#define MUNGE_SIZE 256 /* I do not know what the proper size of this should be. My 
                          testing with munge shows it creates a string of 128 bytes */


/* NOTE:  globals, must not impose per connection constraints */

extern time_t pbs_tcp_timeout;              /* source? */

static unsigned int dflt_port = 0;

static char server_list[PBS_MAXSERVERNAME*3 + 1];
static char dflt_server[PBS_MAXSERVERNAME + 1];
static char fb_server[PBS_MAXSERVERNAME + 1];

static int got_dflt = FALSE;
static char server_name[PBS_MAXSERVERNAME + 1];  /* definite conflicts */
static unsigned int server_port;                 /* definite conflicts */
static const char *pbs_destn_file = PBS_DEFAULT_FILE;

char *pbs_server = NULL;


/* empty_alarm_handler -- this routine was added to help fix bug 76.
   blocking reads would not timeout on a SIG_IGN so this routine
   was added to enable a time out on a blocking read */
void empty_alarm_handler(int signo)
  {
  }

/**
 * Attempts to get a list of server names.  Trys first
 * to obtain the list from an envrionment variable PBS_DEFAULT.
 * If this is not set, it then trys to read the first line
 * from the file <b>server_name</b> in the <b>/var/spool/torque</b>
 * directory.
 * <p>
 * NOTE:  PBS_DEFAULT format:    <SERVER>[,<FBSERVER>]
 *        pbs_destn_file format: <SERVER>[,<FBSERVER>]
 * <p>
 * @return A pointer to the server list.
 * The side effect is
 * that the global variable <b>server_list</b> is set.  The one-shot
 * flag <b>got_dflt</b> is used to limit re-reading of the list.
 * @see pbs_default()
 * @see pbs_fbserver()
 */

char *pbs_get_server_list(void)

  {
  FILE *fd;
  char *pn;
  char *server;
  char tmp[1024];
  int len;

  if (got_dflt != TRUE)
    {
    memset(server_list, 0, sizeof(server_list));
    server = getenv("PBS_DEFAULT");

    if ((server == NULL) || (*server == '\0'))
      {
      server = getenv("PBS_SERVER");
      }

    if ((server == NULL) || (*server == '\0'))
      {
      fd = fopen(pbs_destn_file, "r");

      if (fd == NULL)
        {
        return(server_list);
        }

      if (fgets(tmp, sizeof(tmp), fd) == NULL)
        {
        fclose(fd);
        fprintf(stderr, "\n%s: server_name file\n(%s)\nis EMPTY!!!\n\n", __func__, pbs_destn_file);
        return(server_list);
        }

      strcpy(server_list, tmp);
      if ((pn = strchr(server_list, (int)'\n')))
        * pn = '\0';

      while(fgets(tmp, sizeof(tmp), fd))
        {
        strcat(server_list, ",");
        strcat(server_list, tmp);
        len = strlen(server_list);
        if (server_list[len-1] == '\n')
          {
          server_list[len-1] = '\0';
          }
        }

      fclose(fd);
      }
    else
      {
      snprintf(server_list, sizeof(server_list), "%s", server);
      }

    got_dflt = TRUE;
    }  /* END if (got_dflt != TRUE) */

  return(server_list);
  } /* END pbs_get_server_list() */



void get_port_from_server_name_file(unsigned int *server_name_file_port)
  {
  char *the_server = pbs_default();

  if (the_server != NULL)
    {
    PBS_get_server(the_server, server_name_file_port);
    }
  }


/**
 * The routine is called to get the name of the primary
 * server.  It can possibly trigger reading of the server name
 * list from the envrionment or the disk.
 * As a side effect, it set file local strings <b>dflt_server</b>
 * and <b>server_name</b>.  I am not sure if this is needed but
 * it seems in the spirit of the original routine.
 * @return A pointer to the default server name.
 * @see pbs_fbserver()
 */

char *pbs_default(void)

  {
  char *cp;

  pbs_get_server_list();
  server_name[0] = '\0';
  cp = csv_nth(server_list, 0); /* get the first item from list */

  if (cp)
    {
    strcpy(dflt_server, cp);
    strcpy(server_name, cp);
    }

  return(server_name);
  } /* END pbs_default() */





/**
 * The routine is called to get the name of the fall-back
 * server.  It can possibly trigger reading of the server name
 * list from the envrionment or the disk.
 * As a side effect, it set file local strings <b>fb_server</b>
 * and <b>server_name</b>.  I am not sure if this is needed but
 * it seems in the spirit of the original routine.
 * @return A pointer to the fall-back server name.
 * @see pbs_default()
 */

char *pbs_fbserver(void)

  {
  char *cp;

  pbs_get_server_list();
  server_name[0] = 0;
  cp = csv_nth(server_list, 1); /* get the second item from list */

  if (cp)
    {
    strcpy(fb_server, cp);
    strcpy(server_name, cp);
    }

  return(server_name);
  } /* END pbs_fbserver() */



char *PBS_get_server(

  char         *server,  /* I (NULL|'\0' for not set,modified) */
  unsigned int *port)    /* O */

  {
  int   i;
  char *pc;

  for (i = 0;i < PBS_MAXSERVERNAME + 1;i++)
    {
    /* clear global server_name */

    server_name[i] = '\0';
    }

  if (dflt_port == 0)
    {
    dflt_port = get_svrport(
                  PBS_BATCH_SERVICE_NAME,
                  "tcp",
                  PBS_BATCH_SERVICE_PORT);
    }

  /* first, get the "net.address[:port]" into 'server_name' */

  if ((server == (char *)NULL) || (*server == '\0'))
    {
    if (pbs_default() == NULL)
      {
      return(NULL);
      }
    }
  else
    {
    snprintf(server_name, sizeof(server_name), "%s", server);
    }

  /* now parse out the parts from 'server_name' */

  if ((pc = strchr(server_name, (int)':')))
    {
    /* got a port number */

    *pc++ = '\0';

    *port = atoi(pc);
    }
  else
    {
    *port = dflt_port;
    }

  return(server_name);
  }  /* END PBS_get_server() */


/*
 * PBSD_munge_authenticate - This function will use munge to authenticate 
 * a user connection with the server. 
 */
#ifdef MUNGE_AUTH
int PBSD_munge_authenticate(

  int psock,  /* I */
  int handle) /* I */

  {
  int                 rc = PBSE_NONE;

  int                 fd;
  FILE               *munge_pipe;
  char                munge_buf[MUNGE_SIZE];
  char                munge_command[MUNGE_SIZE];
  char               *ptr; /* pointer to the current place to copy data into munge_buf */
  int                 bytes_read;
  int                 total_bytes_read = 0;
  int                 local_errno = 0;
  
  /* user id and name stuff */
  struct passwd      *pwent;
  uid_t               myrealuid;
  struct batch_reply *reply;
  unsigned short      user_port = 0;
  struct sockaddr_in  sockname;
  socklen_t           socknamelen = sizeof(sockname);
  struct tcp_chan *chan = NULL;

  snprintf(munge_command,sizeof(munge_command),
    "munge -n 2>/dev/null");

  memset(munge_buf, 0, MUNGE_SIZE);
  ptr = munge_buf; 

  if ((munge_pipe = popen(munge_command,"r")) == NULL)
    {
    /* FAILURE */
    return(-1);
    }

  fd = fileno(munge_pipe);

  while ((bytes_read = read(fd, ptr, MUNGE_SIZE - total_bytes_read)) > 0)
    {
    total_bytes_read += bytes_read;
    ptr += bytes_read;
    }

  pclose(munge_pipe);

  if (bytes_read == -1)
    {
    /* read failed */
    local_errno = errno;
    log_err(local_errno, __func__, "error reading pipe in PBSD_munge_authenticate");
    return -1;
    }
  
  /* if we got no bytes back then Munge may not be installed etc. */
  if (total_bytes_read == 0)
    {
    return(PBSE_MUNGE_NOT_FOUND);
    }

  /* We got the certificate. Now make the PBS_BATCH_AltAuthenUser request */
  myrealuid = getuid();  
  pwent = getpwuid(myrealuid);
  
  rc = getsockname(psock, (struct sockaddr *)&sockname, &socknamelen);
  
  if (rc == -1)
    {
    fprintf(stderr, "getsockname failed: %d\n", errno);
    return rc;
    }
  
  user_port = ntohs(sockname.sin_port);
  
  if ((chan = DIS_tcp_setup(psock)) == NULL)
    {
    rc = PBSE_MEM_MALLOC;
    }
  else if ((rc = encode_DIS_ReqHdr(chan,PBS_BATCH_AltAuthenUser,pwent->pw_name))
      || (rc = diswui(chan, user_port))
      || (rc = diswst(chan, munge_buf))
      || (rc = encode_DIS_ReqExtend(chan, NULL))
      || (rc = DIS_tcp_wflush(chan)))
    {
    /* ERROR */
    }
  else
    {
    /* read the reply */
    if ((reply = PBSD_rdrpy(&local_errno, handle)) != NULL)
      free(reply);
    rc = PBSE_NONE;
    }
  if (chan != NULL)
    DIS_tcp_cleanup(chan);
  return rc;
  } /* END PBSD_munge_authenticate() */
#endif /* ifdef MUNGE_AUTH */


/*
 * PBS_authenticate - call pbs_iff(1) to authenticate use to the PBS server.
 */

#ifndef MUNGE_AUTH

int parse_daemon_response(long long code, long long len, char *buf)
  {
  int rc = PBSE_NONE;
  if (code == 0)
    {
    /* Success */
    }
  else
    {
    fprintf(stderr, "Error code - %lld : message [%s]\n", code, buf);
    rc = code;
    }
  return rc;
  }



int get_parent_client_socket(int psock, int *pcsock)
  {
  int rc = PBSE_NONE;
  struct sockaddr_in sockname;
  socklen_t socknamelen = sizeof(sockname);
  if (getsockname(psock, (struct sockaddr *)&sockname, &socknamelen) < 0)
    {
    rc = PBSE_SOCKET_FAULT;
    fprintf(stderr, "iff: cannot get sockname for socket %d, errno=%d (%s)\n", psock, errno, strerror(errno));
    }
  else
    {
    *pcsock = ntohs(sockname.sin_port);
    }
  return rc;
  }

int validate_socket(

  int psock)

  {
  int            rc = PBSE_NONE;
  static char    id[] = "validate_socket";
  char           tmp_buf[LOCAL_LOG_BUF];
  char           write_buf[1024];
  char          *read_buf = NULL;
  long long      read_buf_len = 0;
  uid_t          myrealuid;
  int            local_socket = 0;
  int            parent_client_socket = 0;
  struct passwd *pwent;
  char          *err_msg = NULL;
  char          *l_server = NULL;
  int            l_server_len = 0;
  unsigned short af_family;
  long long      code = -1;
  int            write_buf_len = 0;
  int            local_errno;

  myrealuid = getuid();

  if ((pwent = getpwuid(myrealuid)) == NULL)
    {
    snprintf(tmp_buf, LOCAL_LOG_BUF, "cannot get account info: uid %d, errno %d (%s)\n", (int)myrealuid, errno, strerror(errno));
    log_event(PBSEVENT_ADMIN,PBS_EVENTCLASS_SERVER,id,tmp_buf);
    }
  else if ((rc = get_hostaddr_hostent_af(&local_errno, AUTH_IP, &af_family, &l_server, &l_server_len)) != PBSE_NONE)
    {
    }
  else if ((rc = get_parent_client_socket(psock, &parent_client_socket)) != PBSE_NONE)
    {
    }
  else
    {
    /* format is:
     * trq_system|trq_port|Validation_type|user|psock|
     */
    sprintf(write_buf, "%d|%s|%d|%d|%d|%s|%d|", (int)strlen(server_name), server_name, server_port, AUTH_TYPE_IFF, (int)strlen(pwent->pw_name), pwent->pw_name, parent_client_socket);
    /*
     * total_length|val
     */
    write_buf_len = strlen(write_buf);
    if ((local_socket = socket_get_tcp()) <= 0)
      {
      fprintf(stderr, "socket_get_tcp error\n");
      rc = PBSE_SOCKET_FAULT;
      }
    else if ((rc = socket_connect(&local_socket, l_server, l_server_len, AUTH_PORT, AF_INET, 0, &err_msg)) != PBSE_NONE)
      {
      fprintf(stderr, "socket_connect error (VERIFY THAT trqauthd IS RUNNING)\n");
      }
    else if ((rc = socket_write(local_socket, write_buf, write_buf_len)) != write_buf_len)
      {
      rc = PBSE_SOCKET_WRITE;
      fprintf(stderr, "socket_write error\n");
      }
    else if ((rc = socket_read_num(local_socket, &code)) != PBSE_NONE)
      {
      fprintf(stderr, "socket_read_num error\n");
      }
    else if ((rc = socket_read_str(local_socket, &read_buf, &read_buf_len)) != PBSE_NONE)
      {
      fprintf(stderr, "socket_read_str error\n");
      }
    else if ((rc = parse_daemon_response(code, read_buf_len, read_buf)) != PBSE_NONE)
      {
      fprintf(stderr, "parse_daemon_response error\n");
      }
    else
      {
      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "%s : Connection authorized (server socket %d)\n", id, parent_client_socket);
        }
      socket_close(local_socket);
      }
    }
  if (rc != PBSE_NONE)
    {
    if (err_msg != NULL)
      {
      fprintf(stderr, "Error in connection to trqauthd (%d)-[%s]\n", rc, err_msg);
      }
    }
  if (err_msg != NULL)
    free(err_msg);
  if (read_buf != NULL)
    free(read_buf);
  return rc;
  }

#endif /* ifndef MUNGE_AUTH */


#ifdef ENABLE_UNIX_SOCKETS
ssize_t    send_unix_creds(int sd)
  {

  struct iovec    vec;

  struct msghdr   msg;

  struct cmsghdr  *cmsg;
  char dummy = 'm';
  char buf[CMSG_SPACE(sizeof(struct ucred))];

  struct ucred *uptr;


  memset(&msg, 0, sizeof(msg));
  vec.iov_base = &dummy;
  vec.iov_len = 1;
  msg.msg_iov = &vec;
  msg.msg_iovlen = 1;
  msg.msg_control = buf;
  msg.msg_controllen = sizeof(buf);
  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_CREDS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(struct ucred));
  uptr = (struct ucred *)CMSG_DATA(cmsg);
  SPC_PEER_UID(uptr) = getuid();
  SPC_PEER_GID(uptr) = getgid();
#ifdef linux
  uptr->pid = getpid();
#endif
  msg.msg_controllen = cmsg->cmsg_len;

  return (sendmsg(sd, &msg, 0) != -1);

  }

#endif /* END ENABLE_UNIX_SOCKETS */

/*
 * Set a preferred outbound interface for communication
 * 
 * The current solution uses a Linux ioctl call to
 * find the named interfaces address. We will need
 * to conditionally compile this for other platforms
 *
 * Return 0 on success. Non-zero on failure
 */
#define MAX_IFS 64

int trq_set_preferred_network_interface(
    
  char            *if_name,
  struct sockaddr *preferred_addr)

  {
	struct ifreq *ifr;
  struct ifreq *ifend;
	struct ifreq  ifreqx;
	struct ifconf ifc;
	struct ifreq  ifs[MAX_IFS];
	int           sockfd;

	/* make sure we have a valid name for the interface */
  if ((if_name == NULL) ||
      (preferred_addr == NULL))
		return(PBSE_IVALREQ);	

  memset(preferred_addr, 0, sizeof(struct sockaddr));

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)
    {
    /* We can't do this without a socket */
    return(PBSE_SYSTEM);
    }

	ifc.ifc_len = sizeof(ifs);
	ifc.ifc_req = ifs;
	if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0)
    {
    close(sockfd);
    return(PBSE_SYSTEM);
    }

	/* We will walk through all the interfaces until we find the one we are looking for */
  ifend = ifs + (ifc.ifc_len / sizeof(struct ifreq));
  for (ifr = ifc.ifc_req; ifr < ifend; ifr++)
    {
    if (ifr->ifr_addr.sa_family == AF_INET)
      {
      if (!strncmp(if_name, ifr->ifr_name, IF_NAMESIZE))
        {
        snprintf(ifreqx.ifr_name, sizeof(ifreqx.ifr_name), "%s", ifr->ifr_name);

        if (ioctl(sockfd, SIOCGIFADDR, &ifreqx) < 0)
        	{
        	close(sockfd);
        	return(PBSE_SYSTEM);
          }

        /* get the address */
        memcpy(preferred_addr, &ifreqx.ifr_ifru.ifru_addr, sizeof(struct sockaddr));
        }
      }
    }

  close(sockfd);

  return(PBSE_NONE);
  } /* END trq_set_preferred_network_interface() */




/* returns socket descriptor or negative value (-1) on failure */

/* NOTE:  cannot use globals or static information as API
   may be used to connect a single server to multiple TORQUE
   interfaces */

/* NOTE:  0 is not a valid return value */

int pbs_original_connect(

  char *server)  /* I (FORMAT:  NULL | '\0' | HOSTNAME | HOSTNAME:PORT )*/

  {
  struct sockaddr_in server_addr;
  char *if_name;
  struct addrinfo *addr_info;
  int out;
  int i;
  int rc;
  int local_errno;

  struct sockaddr preferred_addr; /* set if TRQ_IFNAME set in torque.cfg */
  struct passwd *pw;
  int use_unixsock = 0;
  uid_t pbs_current_uid;

#ifdef ENABLE_UNIX_SOCKETS
  struct sockaddr_un unserver_addr;
  char hnamebuf[256];
#endif

  char  *ptr;

  /* reserve a connection state record */

  out = -1;

  for (i = 1;i < NCONNECTS;i++)
    {
    if (connection[i].ch_mutex == NULL)
      {
      connection[i].ch_mutex = calloc(1, sizeof(pthread_mutex_t));
      pthread_mutex_init(connection[i].ch_mutex,NULL);
      }

    pthread_mutex_lock(connection[i].ch_mutex);

    if (connection[i].ch_inuse == FALSE)
      {
      out = i;
      
      connection[out].ch_inuse  = TRUE;
      connection[out].ch_errno  = 0;
      connection[out].ch_socket = -1;
      connection[out].ch_errtxt = NULL;

      break;
      }

    pthread_mutex_unlock(connection[i].ch_mutex);
    }

  if (out < 0)
    {
    if (getenv("PBSDEBUG"))
      fprintf(stderr, "ALERT:  cannot locate free channel\n");

    /* FAILURE */
    /* no need to unlock mutex here - in this case no connection was found */

    return(PBSE_NOCONNECTS * -1);
    }

  /* get server host and port */

  server = PBS_get_server(server, &server_port);

  if (server == NULL)
    {
    connection[out].ch_inuse = FALSE;

    pthread_mutex_unlock(connection[out].ch_mutex);

    if (getenv("PBSDEBUG"))
      fprintf(stderr, "ALERT:  PBS_get_server() failed\n");

    return(PBSE_NOSERVER * -1);
    }

  /* determine who we are */

  pbs_current_uid = getuid();

  if ((pw = getpwuid(pbs_current_uid)) == NULL)
    {
    if (getenv("PBSDEBUG"))
      {
      fprintf(stderr, "ALERT:  cannot get password info for uid %ld\n",
              (long)pbs_current_uid);
      }

    pthread_mutex_unlock(connection[out].ch_mutex);

    return(PBSE_SYSTEM * -1);
    }

  strcpy(pbs_current_user, pw->pw_name);

  pbs_server = server;    /* set for error messages from commands */


#ifdef ENABLE_UNIX_SOCKETS
  /* determine if we want to use unix domain socket */

  if (!strcmp(server, "localhost"))
    use_unixsock = 1;
  else if ((gethostname(hnamebuf, sizeof(hnamebuf) - 1) == 0) && !strcmp(hnamebuf, server))
    use_unixsock = 1;

  /* NOTE: if any part of using unix domain sockets fails,
   * we just cleanup and try again with inet sockets */

  /* get socket */

  if (use_unixsock)
    {
    connection[out].ch_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    DIS_tcp_init(connection[out].ch_socket);

    if (connection[out].ch_socket < 0)
      {
      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot create socket:  errno=%d (%s)\n",
                errno,
                strerror(errno));
        }

      connection[out].ch_inuse = FALSE;

      local_errno = PBSE_PROTOCOL;

      use_unixsock = 0;
      }
    }

  /* and connect... */

  if (use_unixsock)
    {
    unserver_addr.sun_family = AF_UNIX;
    strcpy(unserver_addr.sun_path, TSOCK_PATH);

    if (connect(
          connection[out].ch_socket,
          (struct sockaddr *)&unserver_addr,
          (strlen(unserver_addr.sun_path) + sizeof(unserver_addr.sun_family))) < 0)
      {
      close(connection[out].ch_socket);

      connection[out].ch_inuse = FALSE;
      local_errno = errno;

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot connect to server, errno=%d (%s)\n",
                errno,
                strerror(errno));
        }

      use_unixsock = 0;  /* will try again with inet socket */
      }
    }

  if (use_unixsock)
    {
    if (!send_unix_creds(connection[out].ch_socket))
      {
      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot send unix creds to pbs_server:  errno=%d (%s)\n",
                errno,
                strerror(errno));
        }

      close(connection[out].ch_socket);

      connection[out].ch_inuse = FALSE;
      local_errno = PBSE_PROTOCOL;

      use_unixsock = 0;  /* will try again with inet socket */
      }
    }

#endif /* END ENABLE_UNIX_SOCKETS */

  if (!use_unixsock)
    {

    /* at this point, either using unix sockets failed, or we determined not to
     * try */

    connection[out].ch_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (connection[out].ch_socket < 0)
      {
      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot connect to server \"%s\", errno=%d (%s)\n",
                server,
                errno,
                strerror(errno));
        }

      connection[out].ch_inuse = FALSE;

      pthread_mutex_unlock(connection[out].ch_mutex);

      return(PBSE_PROTOCOL * -1);
      }

    /* This is probably an IPv4 solution for the if_name and preferred_addr
       We need to see what ioctl call we need for IPv6 */
    if_name = trq_get_if_name();
    if (if_name)
      {
      rc = trq_set_preferred_network_interface(if_name, &preferred_addr);
      if (rc != PBSE_NONE)
        {
        fprintf(stderr, "could not set preferred network interface (%s): %d\n",
                if_name, rc);
        if(if_name)
          free(if_name);
        return(rc);
        }

      rc = bind(connection[out].ch_socket, &preferred_addr, sizeof(struct sockaddr));
      if (rc < 0)
        {
        fprintf(stderr, "ERROR: could not bind preferred network interface (%s): errno: %d",
                if_name, errno);
        if (if_name)
          free(if_name);
        return(PBSE_SYSTEM * -1);
        }
      }

    /* we are done with if_name at this point. trq_get_if_name allocated
       space for it. We need to free it */
    if (if_name)
      free(if_name);

    server_addr.sin_family = AF_INET;

    if (getaddrinfo(server, NULL, NULL, &addr_info) != 0)
      {
      close(connection[out].ch_socket);
      connection[out].ch_inuse = FALSE;

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot get servername (%s) errno=%d (%s)\n",
                (server != NULL) ? server : "NULL",
                errno,
                strerror(errno));
        }

      pthread_mutex_unlock(connection[out].ch_mutex);

      return(PBSE_BADHOST * -1);
      }

    server_addr.sin_addr = ((struct sockaddr_in *)addr_info->ai_addr)->sin_addr;
    freeaddrinfo(addr_info);

    server_addr.sin_port = htons(server_port);

    if (connect(
          connection[out].ch_socket,
          (struct sockaddr *)&server_addr,
          sizeof(server_addr)) < 0)
      {
      close(connection[out].ch_socket);

      connection[out].ch_inuse = FALSE;

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot connect to server, errno=%d (%s)\n",
                errno,
                strerror(errno));
        }

      pthread_mutex_unlock(connection[out].ch_mutex);

      return(errno * -1);
      }

    /* FIXME: is this necessary?  Contributed by one user that fixes a problem,
       but doesn't fix the same problem for another user! */
#if 0
#if defined(__hpux)
    /*HP-UX : avoiding socket caching */
    send(connection[out].ch_socket, '?', 1, MSG_OOB);

#endif
#endif

#ifdef MUNGE_AUTH
    rc = PBSD_munge_authenticate(connection[out].ch_socket, out);

    if (rc != 0)
      {
      close(connection[out].ch_socket);

      connection[out].ch_inuse = FALSE;

      if (rc == PBSE_MUNGE_NOT_FOUND)
        {
        local_errno = PBSE_MUNGE_NOT_FOUND;
        
        if (getenv("PBSDEBUG"))
          {
          fprintf(stderr, "ERROR:  cannot find munge executable\n");
          }
        }
      else
        {
        local_errno = PBSE_PERM;
        
        if (getenv("PBSDEBUG"))
          {
          fprintf(stderr, "ERROR:  cannot authenticate connection to server \"%s\", errno=%d (%s)\n",
            server,
            errno,
            strerror(errno));
          }
        }

      pthread_mutex_unlock(connection[out].ch_mutex);

      return(-1 * local_errno);
      }
    
    
#else  
    /* new version of iff using daemon */
    if ((ENABLE_TRUSTED_AUTH == FALSE) && ((rc = validate_socket(connection[out].ch_socket)) != PBSE_NONE))
      {
      close(connection[out].ch_socket);

      connection[out].ch_inuse = FALSE;

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot authenticate connection to server \"%s\", errno=%d (%s)\n",
                server, rc, pbs_strerror(rc));
        }
      
      local_errno = PBSE_SOCKET_FAULT;

      pthread_mutex_unlock(connection[out].ch_mutex);

      return(-1 * local_errno);
      }
#endif /* ifdef MUNGE_AUTH */

    } /* END if !use_unixsock */

  /* setup DIS support routines for following pbs_* calls */

  if ((ptr = getenv("PBSAPITIMEOUT")) != NULL)
    {
    pbs_tcp_timeout = strtol(ptr, NULL, 0);

    if (pbs_tcp_timeout <= 0)
      {
      pbs_tcp_timeout = 10800;      /* set for 3 hour time out */
      }
    }
  else
    {
    pbs_tcp_timeout = 10800;      /* set for 3 hour time out */
    }

  pthread_mutex_unlock(connection[out].ch_mutex);

  return(out);
  }  /* END pbs_original_connect() */



int pbs_disconnect_socket(

  int sock)  /* I (socket descriptor) */

  {
  char tmp_buf[THE_BUF_SIZE / 4];
  struct tcp_chan *chan = NULL;

  if ((chan = DIS_tcp_setup(sock)) == NULL)
    {
    }
  else if ((encode_DIS_ReqHdr(chan,PBS_BATCH_Disconnect, pbs_current_user) == 0)
      && (DIS_tcp_wflush(chan) == 0))
    {
    int atime;
    struct sigaction act;
    struct sigaction oldact;

    /* set alarm to break out of potentially infinite read */
/*    act.sa_handler = SIG_IGN; */
    act.sa_handler = empty_alarm_handler;  /* need SOME handler or blocking read never gets interrupted */

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, &oldact);
    atime = alarm(5);

    while (1)
      {
      /* wait for server to close connection */
      /* NOTE:  if read of 'sock' is blocking, request below may hang forever
         -- hence the signal handler above */
      if (read(sock, &tmp_buf, sizeof(tmp_buf)) < 1)
        break;
      }

    alarm(atime);
    sigaction(SIGALRM, &oldact, NULL);
    }

  if (chan != NULL)
    DIS_tcp_cleanup(chan);
  close(sock);
  return(0);
  }  /* END pbs_disconnect() */


int pbs_disconnect(

  int connect)  /* I (location in connection array) */

  {
  int  sock;

  pthread_mutex_lock(connection[connect].ch_mutex);

  /* send close-connection message */
  sock = connection[connect].ch_socket;

  pbs_disconnect_socket(sock);

  if (connection[connect].ch_errtxt != (char *)NULL)
    free(connection[connect].ch_errtxt);

  connection[connect].ch_errno = 0;
  connection[connect].ch_inuse = FALSE;
  pthread_mutex_unlock(connection[connect].ch_mutex);

  return(0);
  }  /* END pbs_disconnect() */



void print_server_port_to_stderr(
    char *s_name)
  {
  int rc = PBSE_NONE;
  char *s_addr = NULL;
  unsigned short af_family;
  struct in_addr  hostaddr;
  char *ip_addr = NULL;
  int s_len = 0;
  if ((rc = get_hostaddr_hostent_af(&rc, s_name, &af_family, &s_addr, &s_len)) == PBSE_NONE)
    {   
    memcpy((void *)&hostaddr, (void *)s_addr, s_len);
    ip_addr = inet_ntoa(hostaddr);
    fprintf(stderr, "Error communicating with %s(%s)\n", s_name, ip_addr);
    }
  else  
    {
    fprintf(stderr, "Can not resolve name for server %s. (rc = %d - %s)\n",
        s_name,
        rc,  
        pbs_strerror(rc));
    }        
  }

/**
 * This is a new version of this function that allows
 * connecting to a list of servers.  It is backwards
 * compatible with the previous version in that it
 * will accept a single server name.
 *
 * @param server_name_ptr A pointer to a server name or server name list.
 * @returns A file descriptor number.
 */

int pbs_connect(
    
  char *server_name_ptr)    /* I (optional) */

  {
  int connect = -1;
  int i, list_len;
  char server_name_list[PBS_MAXSERVERNAME*3+1];
  char current_name[PBS_MAXSERVERNAME+1];
  char *tp;

  memset(server_name_list, 0, sizeof(server_name_list));

  /* Use the list from the server_name file.
   * If a server name is passed in, append it at the beginning. */

  if (server_name_ptr && server_name_ptr[0])
    {
    snprintf(server_name_list, sizeof(server_name_list), "%s", server_name_ptr);
    strcat(server_name_list, ",");
    }

  strncat(server_name_list, pbs_get_server_list(),
      sizeof(server_name_list) -1 - strlen(server_name_ptr) - 1);

  if (getenv("PBSDEBUG"))
    fprintf(stderr, "pbs_connect using following server list \"%s\"\n",
        server_name_list);

  list_len = csv_length(server_name_list);

  for (i = 0; i < list_len; i++)  /* Try all server names in the list. */
    {
    tp = csv_nth(server_name_list, i);

    if (tp && tp[0])
      {
      /* Trim any leading space */
      while(isspace(*tp)) tp++;

      memset(current_name, 0, sizeof(current_name));
      snprintf(current_name, sizeof(current_name), "%s", tp);

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "pbs_connect attempting connection to server \"%s\"\n",
                current_name);
        }

      if ((connect = pbs_original_connect(current_name)) >= 0)
        {
        if (getenv("PBSDEBUG"))
          {
          fprintf(stderr, "pbs_connect: Successful connection to server \"%s\", fd = %d\n", current_name, connect);
          }

        return(connect);  /* Success, we have a connection, return it. */
        }
      else
        print_server_port_to_stderr(current_name);
      }
    }

  return(connect);
  } /* END pbs_connect() */


/**
 * This routine is not used but was implemented to
 * support qsub.
 *
 * @param server_name_ptr A pointer to a server name or server name list.
 * @param retry_seconds The period of time for which retrys should be attempted.
 * @returns A file descriptor number.
 */
int pbs_connect_with_retry(
    
  char *server_name_ptr, /* I */
  int   retry_seconds)   /* I */

  {
  int n_times_to_try = retry_seconds / CNTRETRYDELAY;
  int connect = -1;
  int n;

  for (n = 0; n < n_times_to_try; n++)  /* This is the retry loop */
    {
    if ((connect = pbs_connect(server_name_ptr)) >= 0)
      return(connect);  /* Success, we have a connection, return it. */

    sleep(CNTRETRYDELAY);
    }

  return(connect);
  }




int pbs_query_max_connections(void)

  {
  return(NCONNECTS - 1);
  }




void initialize_connections_table()

  {
  int i;

  for (i = 1;i < PBS_NET_MAX_CONNECTIONS;i++)
    {
    connection[i].ch_mutex = calloc(1, sizeof(pthread_mutex_t));
    pthread_mutex_init(connection[i].ch_mutex, NULL);
    }
  } /* END initialize_connections_table() */



/* END pbsD_connect.c */

