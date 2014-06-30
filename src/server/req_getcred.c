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
/*
 * req_getcred.c
 *
 * This file contains function relating to the PBS credential system,
 * it includes the major functions:
 *   req_authenuser - Authenticate a user connection based on pbs_iff  (new)
 *   req_connect    - validate the credential in a Connection Request (old)
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pthread.h>

#include "libpbs.h"
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "credential.h"
#include "net_connect.h"
#include "batch_request.h"
#include "log.h"
#include "../lib/Liblog/pbs_log.h"
#include "../lib/Libnet/lib_net.h" /* global_sock_add */
#include "../lib/Libifl/lib_ifl.h"
#include "req_getcred.h" /* req_altauthenuser */
#include "net_cache.h"

#define SPACE 32 /* ASCII space character */

/* External Global Data Items Referenced */


extern struct connection svr_conn[];
extern char *path_credentials;
extern int    LOGLEVEL;

/* Global Data Home in this file */

struct credential conn_credent[PBS_NET_MAX_CONNECTIONS];
/* there is one per possible connectinn */

/*
 * req_connect - process a Connection Request
 *  Almost does nothing.
 */

int req_connect(

  struct batch_request *preq)

  {
  int            sock = preq->rq_conn;
  unsigned short conn_authen;

  /* Called from one location inside a lock */
  pthread_mutex_lock(svr_conn[sock].cn_mutex);
  conn_authen = svr_conn[sock].cn_authen;
  pthread_mutex_unlock(svr_conn[sock].cn_mutex);
  if (conn_authen == 0)
    {
    reply_ack(preq);
    }
  else
    {
    req_reject(PBSE_BADCRED, 0, preq, NULL, "Connection not authorized");
    return(PBSE_BADCRED);
    }

  return(PBSE_NONE);
  }  /* END req_connect() */




int get_encode_host(
  
  int s, 
  char *munge_buf, 
  struct batch_request *preq)
  
  {
  char *ptr;
  char host_name[PBS_MAXHOSTNAME];
  int i;

  /* ENCODE_HOST: is a keyword in the unmunge data that holds the host name */
  ptr = strstr(munge_buf, "ENCODE_HOST:");
  if (!ptr)
    {
    log_err(PBSE_SYSTEM, __func__, "could not read unmunge data host");
    return(PBSE_SYSTEM);
    }

  /* Move us to the end of the ENCODE_HOST keyword. There are spaces
   	 between the : and the first letter of the host name */
  ptr = strchr(ptr, ':');
  if(ptr == NULL)
    {
    log_err(PBSE_SYSTEM, __func__, "ENCODE_HOST malformed");
    return(PBSE_SYSTEM);
    }
  ptr++;
  while (*ptr == SPACE)
    {
    ptr++;
    }

  memset(host_name, 0, PBS_MAXHOSTNAME);
  i = 0;
  while (*ptr != SPACE && !isspace(*ptr))
    {
    host_name[i++] = *ptr;
    ptr++;
    }

  strcpy(conn_credent[s].hostname, host_name);

  return(PBSE_NONE);
  } /* END get_encode_host() */


int get_UID(
    
  int                   s, 
  char                 *munge_buf, 
  struct batch_request *preq)
  
  {
  char *ptr;
  char  user_name[PBS_MAXUSER];
  int   i = 0;

  ptr = strstr(munge_buf, "UID:");
	if (!ptr)
	  {
	  log_err(PBSE_SYSTEM, __func__, "could not read UID from munge buffer");
	  return(PBSE_SYSTEM);
	  }

	memset(user_name, 0, sizeof(user_name));

	ptr = strchr(ptr, ':');

  if (ptr != NULL)
    {
    ptr++;
    while (*ptr == SPACE)
      {
      ptr++;
      }

    while ((*ptr != SPACE) && 
           (!isspace(*ptr)) &&
           (i < (int)sizeof(user_name)))
      {
      user_name[i++] = *ptr;
      ptr++;
      }
    }

	strncpy(conn_credent[s].username, user_name, sizeof(conn_credent[s].username) - 1);
        conn_credent[s].username[sizeof(conn_credent[s].username) - 1] = 0;
	
  return(PBSE_NONE);
  } /* END get_UID() */




int write_munge_temp_file(

  struct batch_request *preq,          /* I */
  char                 *mungeFileName) /* I */

  {
  int  fd;
  int  cred_size;
  int  bytes_written;
  int  rc;
  char log_buf[LOCAL_LOG_BUF_SIZE];

  if ((cred_size = strlen(preq->rq_ind.rq_authen.rq_cred)) == 0)
    {
    log_err(errno, __func__, "munge credential invalid");
    return(PBSE_SYSTEM);
    }

  if ((fd = open(mungeFileName, O_WRONLY|O_NONBLOCK)) < 0)
    {
    snprintf(log_buf, sizeof(log_buf), "could not open temporary munge file %s", mungeFileName);
    log_err(errno, __func__, log_buf);
    return(PBSE_SYSTEM);
    }

  bytes_written = write_ac_socket(fd, preq->rq_ind.rq_authen.rq_cred, cred_size);

  if ((bytes_written == -1) || 
      (bytes_written != cred_size))
    {
    snprintf(log_buf, sizeof(log_buf), "could not write credential to temporary munge file %s, bytes_written = %d", 
	     mungeFileName, bytes_written);
    /* errno set by write_ac_socket */
    log_err(errno, __func__, log_buf);
    close(fd);
    return(PBSE_SOCKET_WRITE);
    }

  if ((rc = fsync(fd)) < 0)
    {
      snprintf(log_buf, sizeof(log_buf), "could not fsync temporary munge file %s, rc=%d", mungeFileName, rc);
    log_err(errno, __func__, log_buf);
    close(fd);
    return(PBSE_SYSTEM);
    }

  close(fd);

  return(PBSE_NONE);
  } /* END write_munge_temp_file() */




int pipe_and_read_unmunge(

  char                 *mungeFileName, /* I */
  struct batch_request *preq,          /* I */
  int                   sock)          /* I */

  {
  char         munge_buf[MUNGE_SIZE << 4];
  char         log_buf[LOCAL_LOG_BUF_SIZE];

  FILE *munge_pipe;
  char *ptr; /* pointer to the current place to copy data into munge_buf */
  char  munge_command[MAXPATHLEN<<1];
  int   bytes_read;
  int   total_bytes_read = 0;
  int   fd;
  int   rc;
  int   errno_save = 0;
  int   flags = 0;

  snprintf(munge_command, sizeof(munge_command), "unmunge --input=%s",
	   mungeFileName);
  
  if ((munge_pipe = popen(munge_command,"r")) == NULL)
    {
    /* FAILURE */
    snprintf(log_buf, sizeof(log_buf), "Unable to popen command '%s' for read",
	     munge_command);
    log_err(errno, __func__, log_buf);
    
    unlink(mungeFileName);
    return(PBSE_CAN_NOT_OPEN_FILE);
    }
  
  memset(munge_buf, 0, MUNGE_SIZE);
  ptr = munge_buf;
  
  fd = fileno(munge_pipe);
  if (fd == -1)
    {
    log_err(errno, __func__, "invalid stream for unmunge pipe");
    return(PBSE_SYSTEM);
    }

  if ((flags = fcntl(fd, F_GETFL)) == -1)
    {
    snprintf(log_buf, sizeof(log_buf), "cannot get file status flags for unmunge of %s", 
	     mungeFileName);
    log_err(errno, __func__, log_buf);
    pclose(munge_pipe);
    return(PBSE_SYSTEM);
    }

#if defined(FNDELAY) && !defined(__hpux)
  if (flags & FNDELAY)
#else
  if (flags & O_NONBLOCK)
#endif
    {
      /* flags already set */
      /* NO-OP */
    }
  else
    {
#if defined(FNDELAY) && !defined(__hpux)
    flags |= FNDELAY;
#else
    flags |= O_NONBLOCK;
#endif
    if (fcntl(fd,F_SETFL,flags) == -1)
      {
      snprintf(log_buf, sizeof(log_buf), "cannot set file status flags to %d for unmunge of %s", 
	       flags, mungeFileName);
      log_err(errno, __func__, log_buf);
      pclose(munge_pipe);
      return(PBSE_SYSTEM);
      }
    }

  while ((bytes_read = read_ac_socket(fd, ptr, MUNGE_SIZE)) > 0)
    {
    total_bytes_read += bytes_read;
    ptr += bytes_read;
    }

  errno_save = errno;

  pclose(munge_pipe);
  
  if (bytes_read == -1)
    {
    /* read failed, errno is set by read_ac_socket */
    snprintf(log_buf, sizeof(log_buf), "error reading munge data from %s", mungeFileName);
    log_err(errno_save, __func__, log_buf);
    rc = PBSE_SOCKET_READ;
    }
  else if (total_bytes_read == 0)
    {
    /* 
     * unmunge failed. Probably a bad credential. But we do not know for sure.
     * Bad credential gives us ECHILD error which gets added to log message
     * and confuses users, so reset it to zero show it does not show up in log
     */
    if (errno == ECHILD)
      errno = 0;
    snprintf(log_buf, sizeof(log_buf), "could not unmunge credentials, munge_command=|%s|, last bytes_read=%d", 
	     munge_command, bytes_read);
    log_err(errno_save, __func__, log_buf);
    rc = PBSE_BADCRED;
    }
  else if ((rc = get_encode_host(sock, munge_buf, preq)) == PBSE_NONE)
    {
    rc = get_UID(sock, munge_buf, preq);
    if (rc != PBSE_NONE)
      {
      snprintf(log_buf, sizeof(log_buf), "get_UID returned %d", rc);
      log_err(-1, __func__, log_buf);
      }
    }
  else
    {
    snprintf(log_buf, sizeof(log_buf), "get_encode_host returned %d", rc);
    log_err(-1, __func__, log_buf);
    }

  return(rc);
  } /* END pipe_and_read_unmunge() */





int unmunge_request(
    
  int                   sock, /* I */
  struct batch_request *preq) /* M */
 
  {
  char            mungeFileName[MAXPATHLEN + MAXNAMLEN+1];
  int             rc = PBSE_NONE;
  char            log_buf[LOCAL_LOG_BUF_SIZE];
  int             fd;

  /* create a unique temporary file for the credential data */
  snprintf(mungeFileName, sizeof(mungeFileName), "%smunge-XXXXXX", path_credentials);

  // set the mask for the temporary file
  umask(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  fd = mkstemp(mungeFileName);

  if (fd == -1)
    {
    snprintf(log_buf, sizeof(log_buf), "could not create temporary munge file %s", 
	     mungeFileName);
    log_err(errno, __func__, log_buf);
    return(PBSE_SYSTEM);
    }

  rc = close(fd);
  if (rc == -1)
    {
    snprintf(log_buf, sizeof(log_buf), "could not close temporary munge file %s", 
	     mungeFileName);
    log_err(errno, __func__, log_buf);
    return(PBSE_SYSTEM);
    }

  /* Write the munge credential to the newly created file */
  if ((rc = write_munge_temp_file(preq,mungeFileName)) == PBSE_NONE)
    {
    /* open the munge command as a pipe and read the result */
    rc = pipe_and_read_unmunge(mungeFileName,preq,sock);
    if (rc != PBSE_NONE)
      {
      snprintf(log_buf, sizeof(log_buf), "Error reading munge temp file %s, rc=%d", 
	       mungeFileName, rc);
      log_err(PBSE_SYSTEM, __func__, log_buf);
      }
    }
  else
    {
      snprintf(log_buf, sizeof(log_buf), "Error writing munge temp file %s, rc=%d", 
	       mungeFileName, rc);
      log_err(PBSE_SYSTEM, __func__, log_buf);
    }

  /* delete the old file unless we had a problem */
  if (rc == PBSE_NONE)
    unlink(mungeFileName);

  return(rc);
  } /* END unmunge_request */




/*
 * req_authenuser - Authenticate a user connection based on the (new)
 * pbs_iff information.  pbs_iff will contact the server on a privileged
 * port and identify the user who has made an existing, but yet unused,
 * non-privileged connection.  This connection is marked as authenticated.
 */

int req_authenuser(
   
  batch_request *preq)

  {
  int             s;
  int             debug = 0;
  int             delay_cntr = 0;
  char            log_buf[LOCAL_LOG_BUF_SIZE];
  unsigned short  conn_port;
  unsigned long   conn_addr;
#ifndef NOPRIVPORTS
  unsigned short  conn_authen;
#endif
  unsigned long   incoming_conn_addr = get_connectaddr(preq->rq_conn, TRUE);
  std::string     incoming_fullhostname;

  get_cached_fullhostname(incoming_conn_addr, incoming_fullhostname);

  /*
   * find the socket whose client side is bound to the port named
   * in the request
   */

  if (getenv("PBSDEBUG"))
    {
    debug = 1;
    }

  if (LOGLEVEL >= 6)
    {
    sprintf(log_buf, "addr: %ld  port: %d", incoming_conn_addr, preq->rq_ind.rq_authen.rq_port);
    log_record(PBSEVENT_CLIENTAUTH, PBS_EVENTCLASS_TRQAUTHD, __func__, log_buf);
    }

  for (delay_cntr = 0; delay_cntr < 5;delay_cntr++)
    {
    for (s = 0; s < PBS_NET_MAX_CONNECTIONS; s++)
      {
      // we know it won't be the connection from trqauthd, which this connection is
      if (s == preq->rq_conn)
        continue;

      pthread_mutex_lock(svr_conn[s].cn_mutex);
      conn_port = svr_conn[s].cn_port;
      conn_addr = get_connectaddr(s, FALSE);
#ifndef NOPRIVPORTS
      conn_authen = svr_conn[s].cn_authen;
#endif
      pthread_mutex_unlock(svr_conn[s].cn_mutex);
  
      if (preq->rq_ind.rq_authen.rq_port != conn_port)
        continue;

      if (conn_addr != incoming_conn_addr)
        {
        // to handle multi-homed hosts, make sure that the full hostnames are different.
        std::string conn_fullhostname;
        get_cached_fullhostname(conn_addr, conn_fullhostname);

        if (conn_fullhostname != incoming_fullhostname)
          continue;
        }

#ifndef NOPRIVPORTS
      if (conn_authen == 0)
#endif
        {
        strcpy(conn_credent[s].username, preq->rq_user);
        strcpy(conn_credent[s].hostname, preq->rq_host);

        /* time stamp just for the record */

        conn_credent[s].timestamp = time(NULL);

        pthread_mutex_lock(svr_conn[s].cn_mutex);
        svr_conn[s].cn_authen = PBS_NET_CONN_AUTHENTICATED;
        pthread_mutex_unlock(svr_conn[s].cn_mutex);
        }

      reply_ack(preq);

      /* SUCCESS */
      if (debug)
        printf("(FOUND_PROCESSED) unlock %d (port %d)\n", s,conn_port);

      return(PBSE_NONE);
      }  /* END for (s) */

    if (debug) 
      fprintf(stderr, "sock not found, sleeping (%d)\n", delay_cntr);

    usleep(250000); /* sleep a quarter of a second and lets see if the connection comes in */
    }

  sprintf(log_buf, "trqauthd fail %d", preq->rq_ind.rq_authen.rq_port);
  log_err(PBSE_CLIENT_CONN_NOT_FOUND, __func__, log_buf);
  if (debug) 
    printf("%s\n", log_buf);
  req_reject(PBSE_CLIENT_CONN_NOT_FOUND, 0, preq, NULL, "cannot authenticate user. Client connection not found");

  /* FAILURE */

  return(PBSE_CLIENT_CONN_NOT_FOUND);
  }  /* END req_authenuser() */


/*
 * req_altauthenuser - The intent of this function is to have a way to use 
 * multiple types of authorization utilities. But for right now we are using 
 * munge so this function is munge specific until we add support for another 
 * utility 
 * 
*/
int req_altauthenuser(

  struct batch_request *preq)  /* I */

  {
  int s;
  int rc = PBSE_NONE;
  unsigned short        conn_port;
  
  /*
   * find the socket whose client side is bound to the port named
   * in the request
   */

  for (s = 0;s < PBS_NET_MAX_CONNECTIONS;++s)
    {
    pthread_mutex_lock(svr_conn[s].cn_mutex);
    conn_port = svr_conn[s].cn_port;
    pthread_mutex_unlock(svr_conn[s].cn_mutex);

    if (preq->rq_ind.rq_authen.rq_port != conn_port)
      {
      continue;
      }
    break;
    }  /* END for (s) */

  /* If s is less than PBS_NET_MAX_CONNECTIONS we have our port */
  if (s >= PBS_NET_MAX_CONNECTIONS)
    {
    req_reject(PBSE_BADCRED, 0, preq, NULL, "cannot authenticate user. Client connection not found");
    return(PBSE_BADCRED);
    }

  rc = unmunge_request(s, preq);
  if (rc != PBSE_NONE)
    {
    /* FAILED */
    req_reject(PBSE_BADCRED, 0, preq, NULL, "cannot authenticate user. Failed to unmunge request");
    return(PBSE_BADCRED);
    }

  /* SUCCESS */

  /* time stamp just for the record */

  conn_credent[s].timestamp = time(NULL);

  pthread_mutex_lock(svr_conn[s].cn_mutex);
  svr_conn[s].cn_authen = PBS_NET_CONN_AUTHENTICATED;
  pthread_mutex_unlock(svr_conn[s].cn_mutex);

  reply_ack(preq);
  
  return(PBSE_NONE);
  }  /* END req_altauthenuser() */


/* END req_getcred.c */

