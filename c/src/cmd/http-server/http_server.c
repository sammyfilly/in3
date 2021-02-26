/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/blockchainsllc/in3
 * 
 * Copyright (C) 2018-2020 slock.it GmbH, Blockchains LLC
 * 
 * 
 * COMMERCIAL LICENSE USAGE
 * 
 * Licensees holding a valid commercial license may use this file in accordance 
 * with the commercial license agreement provided with the Software or, alternatively, 
 * in accordance with the terms contained in a written agreement between you and 
 * slock.it GmbH/Blockchains LLC. For licensing terms and conditions or further 
 * information please contact slock.it at in3@slock.it.
 * 	
 * Alternatively, this file may be used under the AGPL license as follows:
 *    
 * AGPL LICENSE USAGE
 * 
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 * [Permissions of this strong copyleft license are conditioned on making available 
 * complete source code of licensed works and modifications, which include larger 
 * works using a licensed work, under the same license. Copyright and license notices 
 * must be preserved. Contributors provide an express grant of patent rights.]
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *******************************************************************************/

#include "http_server.h"
#include "../../core/client/keys.h"
#include "../../core/client/request.h"
#include "../../core/util/colors.h"
#include "../../core/util/mem.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int listenfd;
void*      respond(void* arg);
typedef struct {
  int    con;
  int    s;
  in3_t* in3;
} req_t;

#ifdef THREADSAFE
#include <pthread.h>

#define POOL_SIZE 10

pthread_t       thread_pool[POOL_SIZE];
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  queue_cond  = PTHREAD_COND_INITIALIZER;

typedef struct queue {
  struct queue* next;
  req_t*        r;
} queue_t;

queue_t *q_head = NULL, *q_tail = NULL;

static void queue_add(req_t* r) {
  pthread_mutex_lock(&queue_mutex);
  queue_t* q = _malloc(sizeof(queue_t));
  q->next    = NULL;
  q->r       = r;
  if (q_tail)
    q_tail->next = q;
  else
    q_head = q_tail = q;
  pthread_cond_signal(&queue_cond);
  pthread_mutex_unlock(&queue_mutex);
}

static req_t* queue_next() {
  pthread_mutex_lock(&queue_mutex);
  if (!q_head) pthread_cond_wait(&queue_cond, &queue_mutex);

  req_t* r = NULL;
  if (q_head) {
    queue_t* q = q_head;
    q_head     = q_head->next;
    r          = q->r;
    _free(q);
    if (!q_head) q_tail = NULL;
  }
  pthread_mutex_unlock(&queue_mutex);
  return r;
}
static void* thread_run(void* p) {
  UNUSED_VAR(p);
  while (true) {
    req_t* r = queue_next();
    if (r) respond(r);
  }
  return NULL;
}

#else
#define MAX_CON 100
static int clients[MAX_CON];
#endif

//client connection
void* respond(void* arg) {
  req_t* r    = arg;
  char*  buf  = malloc(65535);
  int    rcvd = recv(r->con, buf, 65535, 0);

  if (rcvd < 0) // receive error
    fprintf(stderr, ("recv() error\n"));
  else if (rcvd == 0) // receive socket closedMAX_CON
    fprintf(stderr, "Client disconnected upexpectedly.\n");
  else // message received
  {
    buf[rcvd] = '\0';

    char* method = strtok(buf, " \t\r\n");
    char* uri    = method ? strtok(NULL, " \t") : NULL;
    char* prot   = uri ? strtok(NULL, " \t\r\n") : NULL;
    char* rest   = prot ? strstr(prot + strlen(prot) + 1, "\n\r\n") : NULL;

    dup2(r->con, STDOUT_FILENO);
    //    close(r->con);
    if (rest) {
      rest += 3;
      if (strlen(rest) > 2 && (rest[0] == '{' || rest[0] == '[')) {
        // execute in3
        in3_req_t* req = req_new(r->in3, rest);
        if (req == NULL)
          printf("HTTP/1.1 500 Not Handled\r\n\r\nInvalid request.\r\n");
        else if (req->error)
          printf("HTTP/1.1 500 Not Handled\r\n\r\n%s\r\n", req->error);
        else {
          // execute it
          str_range_t range  = d_to_json(d_get(req->requests[0], key("params")));
          char*       params = range.data ? alloca(range.len) : NULL;
          if (params) {
            memcpy(params, range.data + 1, range.len - 2);
            params[range.len - 2] = 0;
          }
          fprintf(stderr, "RPC %s %s\n", d_get_string(req->requests[0], K_METHOD), params); //conceal typing and save position
          if (in3_send_req(req) == IN3_OK) {
            // the request was succesfull, so we delete interim errors (which can happen in case in3 had to retry)
            if (req->error) _free(req->error);
            req->error            = NULL;
            str_range_t range     = d_to_json(req->responses[0]);
            range.data[range.len] = 0;

            // remove in3
            char* end = strstr(range.data, ",\"in3\":");
            if (end) {
              *end   = '}';
              end[1] = 0;
            }
            range.len = strlen(range.data);
            printf("HTTP/1.1 200\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: %i\r\n\r\n%s\r\n", (int) range.len, range.data);
          }
          else if (req->error)
            printf("HTTP/1.1 500 Not Handled\r\n\r\n%s\r\n", req->error);
          else
            printf("HTTP/1.1 500 Not Handled\r\n\r\nCould not execute\r\n");
        }
        if (req)
          req_free(req);
      }
      else
        rest = NULL;
    }

    if (!rest)
      printf("HTTP/1.1 500 Not Handled\r\n\r\nThe server has no handler to the request.\r\n");

    // tidy up
    fflush(stdout);
    shutdown(STDOUT_FILENO, SHUT_WR);
    close(STDOUT_FILENO);
  }

  //Closing SOCKET
  shutdown(r->con, SHUT_RDWR); //All further send and recieve operations are DISABLED...
  close(r->con);
#ifdef THREADSAFE
  _free(r);
#endif
  return NULL;
}

void http_run_server(const char* port, in3_t* in3) {
  struct sockaddr_in clientaddr;
  socklen_t          addrlen;

  printf(
      "Server started %shttp://127.0.0.1:%s%s\n",
      COLORT_LIGHTGREEN, port, COLORT_RESET);

#ifdef THREADSAFE
  for (int i = 0; i < POOL_SIZE; i++) pthread_create(&thread_pool[i], NULL, thread_run, NULL);
#else
  int s = 0;
  for (int i = 0; i < MAX_CON; i++) clients[i] = -1;
#endif

  // start the serevr
  struct addrinfo hints, *res, *p;

  // getaddrinfo for host
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags    = AI_PASSIVE;
  if (getaddrinfo(NULL, port, &hints, &res) != 0) {
    perror("getaddrinfo() error");
    exit(1);
  }
  // socket and bind
  for (p = res; p != NULL; p = p->ai_next) {
    int option = 1;
    listenfd   = socket(p->ai_family, p->ai_socktype, 0);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if (listenfd == -1) continue;
    if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) break;
  }
  if (p == NULL) {
    perror("socket() or bind()");
    exit(1);
  }
  freeaddrinfo(res);

  // listen for incoming connections
  if (listen(listenfd, 1000000) != 0) {
    perror("listen() error");
    exit(1);
  }
  // Ignore SIGCHLD to avoid zombie threads
  signal(SIGCHLD, SIG_IGN);

  // ACCEPT connections
  while (1) {
    addrlen = sizeof(clientaddr);

#ifdef THREADSAFE
    int con = accept(listenfd, (struct sockaddr*) &clientaddr, &addrlen);
    if (con < 0)
      perror("accept() error");
    else {
      req_t* r = _malloc(sizeof(req_t));
      r->con   = con;
      r->s     = 0;
      r->in3   = in3;
      queue_add(r);
    }

#else
    clients[s] = accept(listenfd, (struct sockaddr*) &clientaddr, &addrlen);

    if (clients[s] < 0) {
      perror("accept() error");
    }
    else {
      req_t r = {.con = clients[s], .s = s, .in3 = in3};
      if (fork() == 0) {
        respond(&r);
        clients[s] = -1;
        exit(0);
      }
    }
    while (clients[s] != -1) s = (s + 1) % MAX_CON;
#endif
  }
}
