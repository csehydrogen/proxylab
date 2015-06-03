/*
 * proxy.c - CS:APP Web proxy
 *
 * Student ID: 2013-11395
 *         Name: HeeHoon Kim
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"
#include "echo.h"
#include <time.h>

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"

#define MAXARGS 8
#define MAXTIMELEN 32

/* Undefine this if you don't want debugging output */
#define DEBUG

typedef struct{
    char addr[16];
    uint16_t port;
    int connfd;
} client_info;

static FILE *flog;

/*
 * Functions to define
 */
void *process_request(void* vargp);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp);
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);
static int parseline(char *line, char **argv);
static void proxy(client_info *client, char *prefix);

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

    /* alloc resources */
    if((flog = fopen(PROXY_LOG, "a")) == NULL){
        fprintf(stderr, "ERROR opening log file\n");
        exit(-1);
    }

    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);

    while(1){
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

    /* free resources */
    Close(listenfd);

    fclose(flog);

    return 0;
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
    char buf[MAXLINE], *argv[MAXARGS], *host, *msg, buftime[MAXTIMELEN];
    size_t n; 
    time_t curtime;
    rio_t rio, rio_server;

    Rio_readinitb(&rio, client->connfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        /* parse input from client */
        argc = parseline(buf, argv);

        /* wrong argument from client */
        if(argc != 3){
            printf("%sreceived wrong arguments : %s", prefix, buf);
            sprintf(buf, "proxy usage: <host> <port> <message>\n");
            Rio_writen(client->connfd, buf, strlen(buf));
            continue;
        }

        /* arguments */
        host = argv[0];
        port = atoi(argv[1]);
        msg = argv[2]; // containing '\n'
        printf("%sreceived %s, %d, %s", prefix, host, port, msg);

        /* send msg to server */
        clientfd = Open_clientfd(host, port);
        Rio_readinitb(&rio_server, clientfd);
        Rio_writen(clientfd, msg, strlen(msg));

        /* receive echo from server */
        Rio_readlineb(&rio_server, buf, MAXLINE);
        Close(clientfd);

        /* write log */
        /* e.g. Sun 27 Nov 2013 02:51:02 KST: 128.2.111.38 38421 11 HELLOWORLD! */
        curtime = time(NULL);
        strftime(buftime, MAXTIMELEN, "%a %d %b %Y %T %Z", gmtime(&curtime));
        fprintf(flog, "%s: %s %hu %d %s",
                buftime, client->addr, client->port, strlen(buf), buf); 
        fflush(flog);

        /* send echo to client */
        Rio_writen(client->connfd, buf, strlen(buf));
    }
}

/*
 * thread routine
 */
void *process_request(void* vargp){
    char prefix[40];
    pthread_t tid;
    client_info client;

    client = *(client_info*)vargp;
    Free(vargp);

    tid = pthread_self();
    Pthread_detach(tid); 

    printf("Served by thread %d\n", (int)tid);
    sprintf(prefix, "Thread %d ", (int)tid);

    proxy(&client, prefix);

    Close(client.connfd);
    return NULL;
}
