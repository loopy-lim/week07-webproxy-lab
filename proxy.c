#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

int sendProxy(char *client_hostname, char *client_port, int connfd);
void parseUrl(char *uri, char *host, char *port, char *path);
int deliveryProxy(int connfd, int proxyfd);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, connfd, proxyfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  char client_hostname[MAXLINE], client_port[MAXLINE];

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(struct sockaddr_storage);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    printf("Connected to (%s, %s)\n", client_hostname, client_port);
    proxyfd = sendProxy(client_hostname, client_port, connfd);

    if (proxyfd != -1)
    {
      deliveryProxy(connfd, proxyfd);
      Close(connfd);
      Close(proxyfd);
    }
    else
    {
      printf("Proxy failed\n");
      Close(connfd);
    }
  }
  exit(0);
}

int deliveryProxy(int connfd, int proxyfd)
{
  char buf[MAXLINE];
  rio_t rio;
  Rio_readinitb(&rio, proxyfd);
  size_t n;
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
  {
    // printf("server received %d bytes \n", (int)n);
    Rio_writen(connfd, buf, n);
  }
}

int sendProxy(char *client_hostname, char *client_port, int connfd)
{
  int clientfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s \n", buf);

  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET"))
  {
    printf("Proxy does not implement this method\n");
    return -1;
  }

  parseUrl(uri, host, port, path);
  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);
  sprintf(buf, "%s\r\n", buf);

  // printf("request buf: \n %s \n", buf);

  clientfd = Open_clientfd(host, port);
  Rio_writen(clientfd, buf, strlen(buf));

  return clientfd;
}

void parseUrl(char *uri, char *host, char *port, char *path)
{
  char *ptr = strstr(uri, "//");
  if (ptr == NULL)
  {
    ptr = uri;
  }
  else
  {
    ptr += 2;
  }

  char *ptr2 = strstr(ptr, ":");
  if (ptr2 == NULL)
  {
    ptr2 = strstr(ptr, "/");
    strcpy(port, "80");
  }
  else
  {
    *ptr2 = '\0';
    strcpy(port, ptr2 + 1);
    ptr2 = strstr(port, "/");
  }

  if (ptr2 == NULL)
  {
    strcpy(path, "/");
  }
  else
  {
    strcpy(path, ptr2);
    *ptr2 = '\0';
  }

  strcpy(host, ptr);
}
