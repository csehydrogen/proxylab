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

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"

/* Undefine this if you don't want debugging output */
#define DEBUG

/*
 * Functions to define
 */
void *process_request(void* vargp);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp);
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int port, listenfd, *connfdp, clientlen, client_port;
    char *haddrp;
    struct sockaddr_in clientaddr;
    struct hostent *hp;
    pthread_t tid;

    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);

    while(1){
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
                sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        haddrp = inet_ntoa(clientaddr.sin_addr);
        client_port = ntohs(clientaddr.sin_port);
        printf("Server connected to %s (%s), port %d\n",
                hp->h_name, haddrp, client_port);
        Pthread_create(&tid, NULL, process_request, connfdp);
    }

    return 0;
}

int parseline(char *line, char **argv){
}

void upper_case(char *s)
{
  while (*s) {
    *s = toupper(*s);
    s++;
  }
}

void echoclient(int connfd, char *prefix) 
{
    size_t n; 
    char buf[MAXLINE]; 
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        printf("%sreceived %d bytes (%d total)\n", prefix, (int)n);
        upper_case(buf);
        Rio_writen(connfd, buf, n);
    }
}

/*
 * thread routine
 */
void *process_request(void* vargp)
{
    int connfd;
    char prefix[40];
    pthread_t tid;

    connfd = *(int*)vargp;
    tid = pthread_self();
    Pthread_detach(tid); 
    Free(vargp);

    printf("Served by thread %d\n", (int) tid);
    sprintf(prefix, "Thread %d ", (int) tid);

    echo(connfd, prefix);

    Close(connfd);
    return NULL;
}
