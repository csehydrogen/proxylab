/*
 * proxy.c - CS:APP Web proxy
 *
 * Student ID: 2013-11395
 *         Name: HeeHoon Kim
 * 
 * A simple concurrent echo proxy. For each client, a thread is
 * assigned to forward request to server, receive response from
 * server, and send response back to client. Every request is
 * logged in file.
 */ 

#include "csapp.h"
#include <time.h>

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"
/* Maximum arguments from client */
#define MAXARGS 8
/* Maximum length of time in string representation */
#define MAXTIMELEN 32
/* Undefine this if you don't want debugging output */
#define DEBUG

/* struct for passing client data to thread routine */
typedef struct{
    char addr[16];
    uint16_t port;
    int connfd;
} client_info;

/* log file */
static FILE *flog;
/* mutex for wrting log */
static sem_t fmutex;
/* mutex for open_clientfd_ts */
static sem_t open_clientfd_mutex;

/*
 * Functions to define
 */
void *process_request(void* vargp);
static void proxy(client_info *client, char *prefix);
static int parseline(char *line, char **argv);
static void sigpipe_handler(int sig);
static void unix_warn(char *msg);
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void    Rio_writen_w(int fd, void *usrbuf, size_t n);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int port, listenfd, clientlen;
    struct sockaddr_in clientaddr;
    struct hostent *hp;
    client_info *client;
    pthread_t tid;

    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);

    /* ignore SIGPIPE */
    Signal(SIGPIPE, sigpipe_handler);
    /* open log file*/
    flog = Fopen(PROXY_LOG, "a");
    /* init semaphores */
    Sem_init(&fmutex, 0, 1);
    Sem_init(&open_clientfd_mutex, 0, 1);
    /* open listen socket */
    listenfd = Open_listenfd(port);

    while(1){
        /* accept client and start thread */
        clientlen = sizeof(clientaddr);
        client = Malloc(sizeof(client_info));
        client->connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
                sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        strcpy(client->addr, inet_ntoa(clientaddr.sin_addr));
        client->port = ntohs(clientaddr.sin_port);
        printf("Server connected to %s (%s), port %d\n",
                hp->h_name, client->addr, client->port);
        Pthread_create(&tid, NULL, process_request, client);
    }

    /* close log file */
    Fclose(flog);
    /* close listen socket */
    Close(listenfd);

    return 0;
}

/*
 * handler for ignoring SIGPIPE
 */
static void sigpipe_handler(int sig){
    printf("SIGPIPE received\n");
}

/*
 * parse line into argv form
 */
static int parseline(char *line, char **argv){
    char *saveptr;

    /* parse host */
    if((argv[0] = strtok_r(line, " ", &saveptr)) == NULL){
        return 0;
    }

    /* parse port */
    if((argv[1] = strtok_r(NULL, " ", &saveptr)) == NULL){
        return 1;
    }

    /* parse message */
    if((argv[2] = strtok_r(NULL, "", &saveptr)) == NULL){
        return 2;
    }

    return 3;
}

/*
 * parse input from client, send msg to server,
 * receive echo from server, and send echo to client
 */
static void proxy(client_info *client, char *prefix){
    int argc, port, clientfd;
    char buf[MAXLINE], line[MAXLINE], *argv[MAXARGS], *host, *msg, buftime[MAXTIMELEN];
    size_t n; 
    time_t curtime;
    rio_t rio, rio_server;

    Rio_readinitb(&rio, client->connfd);
    while((n = Rio_readlineb_w(&rio, buf, MAXLINE)) != 0) {
        /* parse input from client */
        memcpy(line, buf, sizeof(buf));
        argc = parseline(line, argv);

        /* wrong argument from client */
        if(argc != 3){
            printf("%sreceived wrong arguments : %s", prefix, buf);
            sprintf(buf, "proxy usage: <host> <port> <message>\n");
            Rio_writen_w(client->connfd, buf, strlen(buf));
            continue;
        }

        /* arguments */
        host = argv[0];
        port = atoi(argv[1]);
        msg = argv[2]; // containing '\n'
        printf("%sreceived %s", prefix, buf);

        /* send msg to server */
        if((clientfd = open_clientfd_ts(host, port, &open_clientfd_mutex)) < 0){
            sprintf(buf, "ERROR, cannot open %s:%d\n", host, port);
            fprintf(stderr, buf);
            Rio_writen_w(client->connfd, buf, strlen(buf));
            continue;
        }
        Rio_readinitb(&rio_server, clientfd);
        Rio_writen_w(clientfd, msg, strlen(msg));
        printf("%ssent %s:%d %s", prefix, host, port, msg);

        /* receive echo from server */
        if(Rio_readlineb_w(&rio_server, buf, MAXLINE) == 0){
            sprintf(buf, "ERROR, server didn't echo\n");
            fprintf(stderr, buf);
            Rio_writen_w(client->connfd, buf, strlen(buf));
            Close(clientfd);
            continue;
        }
        printf("%sreceived %s", prefix, buf);
        Close(clientfd);

        /* write log (need mutex) */
        /* e.g. Sun 27 Nov 2013 02:51:02 KST: 128.2.111.38 38421 11 HELLOWORLD! */
        curtime = time(NULL);
        strftime(buftime, MAXTIMELEN, "%a %d %b %Y %T %Z", gmtime(&curtime));
        P(&fmutex);
        fprintf(flog, "%s: %s %hu %d %s",
                buftime, client->addr, client->port, strlen(buf), buf); 
        V(&fmutex);
        fflush(flog);

        /* send echo to client */
        Rio_writen_w(client->connfd, buf, strlen(buf));
        printf("%ssent %s:%d %s", prefix, client->addr, client->port, buf);
    }
}

/*
 * thread routine
 */
void *process_request(void* vargp){
    char prefix[40];
    pthread_t tid;
    client_info client;

    /* copy info from main thread and free it */
    client = *(client_info*)vargp;
    Free(vargp);

    /* detach to prevent memory leak */
    tid = pthread_self();
    Pthread_detach(tid); 

    sprintf(prefix, "Thread %d ", (int)tid);

    /* start proxy routine */
    printf("Served by thread %d\n", (int)tid);
    proxy(&client, prefix);
    printf("Thread %d ends\n", (int)tid);

    Close(client.connfd);
    return NULL;
}

/*
 * thread safe version of open_clientfd using mutex
 */
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp){
    int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; /* check errno for cause of error */

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;

    /* lock before calling gethostbyname */
    P(mutexp);

    /* Fill in the server's IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL){
        V(mutexp);
        return -2; /* check h_errno for cause of error */
    }
    bcopy((char *)hp->h_addr_list[0], 
            (char *)&serveraddr.sin_addr.s_addr, hp->h_length);

    /* all data copied from hp, so unlock */
    V(mutexp);

    serveraddr.sin_port = htons(port);

    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    return clientfd;
}

/*
 * unix_error without exit, it just prints warning
 */
static void unix_warn(char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
}

/*
 * Rio_readn warning version
 */
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes){
    ssize_t n;
  
    if ((n = rio_readn(fd, ptr, nbytes)) < 0){
        unix_warn("rio_readn error");
        return 0;
    }
    return n;
}

/*
 * Rio_readlineb warning version
 */
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen){
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0){
        unix_warn("rio_readlineb error");
        return 0;
    }
    return rc;
}

/*
 * Rio_writen warning version
 */
void Rio_writen_w(int fd, void *usrbuf, size_t n){
    if (rio_writen(fd, usrbuf, n) != n){
        unix_warn("rio_writen error");
    }
}
