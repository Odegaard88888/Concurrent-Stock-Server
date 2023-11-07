#include <stdint.h>
#include "csapp.h"
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

data* node = NULL;
volatile int ccount = 0;
sem_t s;

data* search(data* nd, int id)
{
    if (nd == NULL)
        return NULL;
    if (id == nd->ID)
        return nd;
    else if (id < nd->ID)
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

void Command(char b[], char res[])
{
    int num;
    char command[MAXLINE];
    int id, cnt;
    num = sscanf(b, "%s %d %d", command, &id, &cnt);
    if (num == 3)
    {
        data* target = search(node, id);
        if (strcmp(command, "buy") == 0)
        {
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
        }
        else if (strcmp(command, "sell") == 0)
        {
            P(&(target->mutex));
            target->left_stock += cnt;
            V(&(target->mutex));
            char output[] = "[sell] success\n";
            strcpy(res, output);
        }
    }
    else if (num == 1)
    {
        if (strcmp(command, "show") == 0)
        {
            char output[MAXLINE];
            memset(output, '\0', sizeof(output));
            show(node, output);
	    strcpy(res, output);
        }
    }
}
void check(pool* pool)
{
    char buf[MAXLINE], res[MAXLINE];
    memset(buf, '\0', sizeof(buf));
    int k, connfd;
    rio_t rio;
    for (int i=0; (i <= pool->maxi) && (pool->nready > 0); i++)
    {
        connfd = pool->clientfd[i];
        rio = pool->clientrio[i];
        if ((connfd > 0) && (FD_ISSET(connfd, &pool->rdy_set)))
        {
            pool->nready--;
            if ((k = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
            {
                int byte = (int)strlen(buf);
                printf("server received by %d bytes\n", byte);
	        	
                if (strcmp(buf, "exit") == 0)
                {
                    Close(connfd);
                    FD_CLR(connfd, &pool->rd_set);
                    pool->clientfd[i] = -1;
                    P(&s);
                    ccount--;
                    V(&s);
		    return;
                }
                else
                {
		    
                    Command(buf, res);
		    
		    Rio_writen(connfd, res, MAXLINE);
		}
            }
	    else
            {
                Close(connfd);
                FD_CLR(connfd, &pool->rd_set);
                pool->clientfd[i] = -1;
                P(&s);
                ccount--;
                V(&s);
            }
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

int main(int argc, char **argv) 
{ 
    sem_init(&s, 0, 1);
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    static pool pool;
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
    
    pool.maxi = -1;
    for (int i=0; i < 1024; i++)
        pool.clientfd[i] = -1;
    pool.maxfd = listenfd;
    FD_ZERO(&pool.rd_set);
    FD_SET(listenfd, &pool.rd_set);
    
    while (1) {
        pool.rdy_set = pool.rd_set;
        pool.nready = Select(pool.maxfd+1, &pool.rdy_set, NULL, NULL, NULL);
       	if (FD_ISSET(listenfd, &pool.rdy_set))
        {
            clientlen = sizeof(clientaddr); 
	    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
            Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
            printf("Connected to (%s, %s)\n", client_hostname, client_port);
	    pool.nready--;
	    int j;
            for (j= 0; j < 1024; j++)
            {
                if (pool.clientfd[j] < 0)
                {
                    pool.clientfd[j] = connfd;
                    Rio_readinitb(&pool.clientrio[j], connfd);
                    FD_SET(connfd, &pool.rd_set);

                    if (connfd > pool.maxfd)
                        pool.maxfd = connfd;
                    if (j > pool.maxi)
                        pool.maxi = j;

                    P(&s);
                    ccount++;
                    V(&s);
                    break;
                }
            }
            if (j == 1024)
                app_error("add_client error: Too many clients");
        }
        check(&pool);

        if (ccount == 0)
        {
            char new[MAXLINE];
            memset(new, '\0', sizeof(new));
            P(&s);
            FILE* h = Fopen("stock.txt", "w");
            show(node, new);
            Fputs(new, h);
            Fclose(h);
            V(&s);
	    
	}
    }
    exit(0);
}
