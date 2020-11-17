#include "t.h"
#include "rio.h"
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* use them for style points, they're useless */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

int connect_endserver(char *hostname,int port);
void doit(int connfd);
void parse_str(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char*path,int port,rio_t *client_rio);
void *threads(void *vargp);

int main(int argc,char **argv)
{
	pthread_t tid;
	int listenfd,* connfd;
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	
	
	if(argc != 2)
	{
		printf("usage: %s <port>\n",argv[0]); /* open_listenfd on this port */
		exit(0);
	}
	listenfd = open_listenfd(argv[1]);

	/* first we just write a easy version, not cope with multiple concurrent requests */
	while(1)
	{
		connfd = malloc(sizeof(int));
		clientlen = sizeof(struct sockaddr);
		*connfd = accept(listenfd,(struct sockaddr *)&clientaddr,&clientlen);
		pthread_create(&tid,NULL,threads,connfd);	
	}

    return 0;
}

void doit(int connfd)
{
	char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE];
	char host[MAXLINE],path[MAXLINE],header[MAXLINE];
	int port;
	rio_t rio_a,rio_b; /* a is client, and b is end server */

	rio_readinitb(&rio_a,connfd);
	rio_readlineb(&rio_a,buf,MAXLINE);
	sscanf(buf,"%s %s %s",method,uri,version);

	if(strcmp(method,"GET"))
	{
		printf("proxy does not implement the method\n");
		return;
		/* exit(0); we shouldn't terminate in such long-running program easily */
	}

	parse_str(uri,host,path,&port);

	build_http_header(header,host,path,port,&rio_a);

	int end_serverfd = connect_endserver(host,port);
	if(end_serverfd < 0)
	{
		printf("connection failed\n");
		return;
	}

	rio_readinitb(&rio_b,end_serverfd);
	rio_writen(end_serverfd,header,strlen(header));
	
	ssize_t n;
	while((n = rio_readlineb(&rio_b,buf,MAXLINE)) != 0)
	{
		printf("proxy received %ld bytes, then send\n",n);
		rio_writen(connfd,buf,n);
	}

	close(end_serverfd); /* this program won't close, so you must close to avoid memory leaking */	
}

void parse_str(char *uri,char *hostname,char *path,int *port)
{
	*port = 80; /* the default port nuber */
	
	char *pos = strstr(uri,"//");
	pos = ((pos != NULL) ? pos + 2 : uri);
	
	char *pos2 = strstr(pos,":");
	if(pos2 != NULL)
	{
		*pos2 = '\0';
		sscanf(pos,"%s",hostname);
		sscanf(pos2 + 1,"%d%s",port,path); /* note that port is a pointer */ 
	}
	else
	{
		pos2 = strstr(pos,"/");
		if(pos2 != NULL)
		{
			*pos2 = '\0';
			sscanf(pos,"%s",hostname);
			*pos2 = '/';
			sscanf(pos2,"%s",path);
		}
		else
			sscanf(pos,"%s",hostname);
		
	}
}

void build_http_header(char *header,char *hostname,char *path,int port,rio_t *client_rio)
{
    char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];
    /*request line*/
    sprintf(request_hdr,requestlint_hdr_format,path);
    /*get other request header for client rio and change it */
    while(rio_readlineb(client_rio,buf,MAXLINE)>0)
    {
        if(strcmp(buf,endof_hdr) == 0) break;/*EOF*/

        if(!strncasecmp(buf,host_key,strlen(host_key)))/*Host:*/
        {
            strcpy(host_hdr,buf);
            continue;
        }

        if(!strncasecmp(buf,connection_key,strlen(connection_key))
                &&!strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key))
                &&!strncasecmp(buf,user_agent_key,strlen(user_agent_key)))
        {
            strcat(other_hdr,buf);
        }
    }
    if(strlen(host_hdr) == 0)
    {
        sprintf(host_hdr,host_hdr_format,hostname);
    }
    sprintf(header,"%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);

    return ;
}

int connect_endserver(char *hostname,int port)
{
	char port_str[15];
	sprintf(port_str,"%d",port);
	return open_clientfd(hostname,port_str);
}

void *threads(void *vargp){
	pthread_detach(pthread_self());

	int connfd = *((int *)vargp);
	free(vargp);
	doit(connfd);
	close(connfd);

	return NULL;
}

