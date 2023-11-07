#include "csapp.h"
#define SBUFSIZE 8196
typedef struct item{
    int ID;
    int left_stock;
    int price;
    int readcnt;
    struct item* L;
    struct item* R;
    sem_t mutex;
}data;
typedef struct {
    fd_set rd_set;
    fd_set rdy_set;
    int nready;
    int maxi;
    int maxfd;
    int clientfd[1024];
    rio_t clientrio[1024];
} pool;
typedef struct {
    int* buf;
    int n;
    int fr;
    int rear;
    sem_t mutex;
    sem_t aslot;
    sem_t aitem;
} sharedbuf;

sharedbuf sbuf;
data* node = NULL;
volatile int ccount = 0;
int readcnt = 0;
sem_t s, w, r;

void sharedbuf_init(sharedbuf* sbp, int k)
{
    sbp->buf = Calloc(k, sizeof(int));
    sbp->n = k;
    sbp->fr = 0;
    sbp->rear = 0;
    Sem_init(&(sbp->mutex), 0, 1);
    Sem_init(&(sbp->aslot), 0, k);
    Sem_init(&(sbp->aitem), 0, 0);

}
void sharedbufInsert(sharedbuf* sbp, int connfd)
{
    P(&(sbp->aslot));
    P(&(sbp->mutex));
    sbp->buf[(++sbp->rear) % (sbp->n)] = connfd;
    V(&(sbp->mutex));
    V(&(sbp->aitem));
}
int sharedbufRemove(sharedbuf* sbp)
{
    int connfd;
    P(&(sbp->aitem));
    P(&(sbp->mutex));
    connfd = sbp->buf[(++sbp->fr) % (sbp->n)];
    V(&(sbp->mutex));
    V(&(sbp->aslot));

    return connfd;
}
data* search(data* nd, int id)
{
    if (nd == NULL)
        return NULL;
    if (nd->ID == id)
        return nd;
    else if (nd->ID > id)
        return search(nd->L, id);
    else
        return search(nd->R, id);
}
void show(data* nd, char output[])
{
    if (nd == NULL)
        return;
    else
    {
        int x, y, z;
        x = nd->ID;
        y = nd->left_stock;
        z = nd->price;

        char temp[MAXLINE];
        memset(temp, '\0', sizeof(temp));
        P(&(nd->mutex));
        sprintf(temp, "%d %d %d", x, y, z);
        strcat(output, temp);
        strcat(output, "\n");
        V(&(nd->mutex));
        show(nd->L, output);
        show(nd->R, output);
    }
}

void Command(int connfd)
{
    int k, num, id, cnt; 
    char buf[MAXLINE], res[MAXLINE], command[MAXLINE];
    rio_t rio;
    Rio_readinitb(&rio, connfd);
    while ((k = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
	    printf("server received %d bytes\n", k);
        memset(command, '\0', MAXLINE);
        num = sscanf(buf, "%s %d %d", command, &id, &cnt);
        if (num == 3)
        {
            data* target = search(node, id);
            if (strcmp(command, "buy") == 0)
            {
		P(&w);
                if (target->left_stock >= cnt)
                {
                    
                    P(&(target->mutex));
                    target->left_stock -= cnt;
                    V(&(target->mutex));
                  
                    char output[] = "[buy] success\n";
                    strcpy(res, output);
                }
                else
                {
                    char output[] = "Not enough left stocks\n";
                    strcpy(res, output);
                }
		Rio_writen(connfd, res, MAXLINE);
                memset(buf, '\0', MAXLINE);
		V(&w);
            }
            else if (strcmp(command, "sell") == 0)
            {
              
                P(&w);
                P(&(target->mutex));
                target->left_stock += cnt;
                V(&(target->mutex));
              
                char output[] = "[sell] success\n";
                strcpy(res, output);
		Rio_writen(connfd, res, MAXLINE);
                memset(buf, '\0', MAXLINE);
		V(&w);
            }
        }
        else if (num == 1)
        {
            if (strcmp(command, "show") == 0)
            {
		P(&r);
                readcnt++;
                if (readcnt == 1)
                    P(&w);
                V(&r);
                char output[MAXLINE];
                memset(output, '\0', sizeof(output));
                show(node, output);
                strcpy(res, output);
		Rio_writen(connfd, res, MAXLINE);
                memset(buf, '\0', MAXLINE);
		P(&r);
                readcnt--;
                if (readcnt == 0)
                    V(&w);
                V(&r);
            }
            else if (strcmp(command, "exit") == 0)
                break;
        }
    }
}
data* DataInsert(data** nd, int id, int remain, int pr)
{
    if ((*nd) == NULL)
    {
        (*nd) = (data*)malloc(sizeof(data));
        (*nd)->ID = id;;
        (*nd)->left_stock = remain;
        (*nd)->price = pr;
        (*nd)->readcnt = 0;
        (*nd)->L = NULL;
        (*nd)->R = NULL;
        Sem_init(&((*nd)->mutex), 0, 1);
        return (*nd);
    }
    else
    {
        if (id < (*nd)->ID)
        {
            (*nd)->L = DataInsert(&((*nd)->L), id, remain, pr);
            return (*nd);
        }
        else
        {
            (*nd)->R = DataInsert(&((*nd)->R), id, remain, pr);
            return (*nd);
        }
    }
}
void* peerthread(void* vargp)
{
    Pthread_detach(pthread_self());
    while (1)
    {
        int connfd = sharedbufRemove(&sbuf);
        Command(connfd);
        Close(connfd);
	P(&s);
        ccount--;
        if (ccount == 0)
        {
            char new[MAXLINE];
            memset(new, '\0', sizeof(new));
            FILE* h = Fopen("stock.txt", "w");
            show(node, new);
            Fputs(new, h);
            Fclose(h);   
        }
        V(&s);
    }
    
    return NULL;
}
int main(int argc, char** argv) 
{
    sem_init(&s, 0, 1);
    sem_init(&r, 0, 1);
    sem_init(&w, 0, 1);
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    pthread_t tid;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    char buf[MAXLINE];
    memset(buf, '\0', sizeof(buf));
     
    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }
    int id = 0, remain = 0, pr = 0;
    FILE* h = Fopen("stock.txt", "r");
    while (Fgets(buf, MAXLINE, h) != NULL)
    {
        sscanf(buf, "%d %d %d", &id, &remain, &pr);
        DataInsert(&node, id, remain, pr);
        memset(buf, '\0', sizeof(buf));
    }
    Fclose(h);
    listenfd = Open_listenfd(argv[1]);
    sharedbuf_init(&sbuf, SBUFSIZE);
    for (int i = 0; i < 16; i++)
    {
        Pthread_create(&tid, NULL, peerthread, NULL);
    }
    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage); 
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
       	sharedbufInsert(&sbuf, connfd);
        P(&s);
        ccount++;
        V(&s);
    }
    exit(0);
}
