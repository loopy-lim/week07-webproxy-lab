#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

int sendProxy(char *client_hostname, char *client_port, int connfd);
void parseUrl(char *uri, char *host, char *port, char *path);
int deliveryProxy(int connfd, int proxyfd);
void useCache(int connfd, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void serveStatic(int fd, char *file_name);
void get_filetype(char *filename, char *filetype);
void init();
int pathToFileName(char *uri, char *filename);
void serveStatic(int fd, char *file_name);
void sigchild_handler(int sig);

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
  init();

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
    if (Fork() == 0) /* Child Process */
    {
      proxyfd = sendProxy(client_hostname, client_port, connfd);

      if (proxyfd > 0)
      {
        deliveryProxy(connfd, proxyfd);
        Close(proxyfd);
      }
      Close(connfd);
    }
    Close(connfd); /* Parent Process Clean up */
  }
  exit(0);
}

void init()
{
  Signal(SIGPIPE, SIG_IGN);
  Signal(SIGCHLD, sigchild_handler);
}

int deliveryProxy(int connfd, int proxyfd)
{
  printf("\n============ Delivery Proxy ============\n");
  char buf[MAXLINE];
  rio_t rio;
  Rio_readinitb(&rio, proxyfd);
  size_t n;
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
  {
    printf("server received %d bytes \n", (int)n);
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
  printf("\n============ Check Proxy ============\n");
  printf("Request headers: \n");
  printf("%s \n", buf);

  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET"))
  {
    printf("Proxy does not implement this method\n");
    return -1;
  }
  parseUrl(uri, host, port, path);
  clientfd = open_clientfd(host, port);
  if (clientfd < 0)
  {
    printf("Proxy could not connect to server\nTry to use cache\n");
    useCache(connfd, path);
    return -1;
  }

  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);
  sprintf(buf, "%s\r\n", buf);

  // printf("request buf: \n %s \n", buf);

  Rio_writen(clientfd, buf, strlen(buf));
  return clientfd;
}

void parseUrl(char *uri, char *host, char *port, char *path)
{
  char *ptr = strstr(uri, "//");
  if (ptr == NULL)
  {
    if (uri != NULL)
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
    if (port != NULL)
      strcpy(port, "80");
  }
  else
  {
    *ptr2 = '\0';
    if (port != NULL)
      strcpy(port, ptr2 + 1);
    if (port != NULL)
      ptr2 = strstr(port, "/");
  }

  if (ptr2 == NULL)
  {
    if (path != NULL)
      strcpy(path, "/");
  }
  else
  {
    if (path != NULL)
      strcpy(path, ptr2);
    *ptr2 = '\0';
  }
  if (host != NULL)
    strcpy(host, ptr);
}

void serveStatic(int fd, char *file_name)
{
  int srcfd, filesize;
  struct stat sbuf;
  char *srcp, filetype[MAXLINE], buf[MAXBUF], paths[MAXLINE];

  printf("\n============ Serve Static File ============\n");
  if (stat(file_name, &sbuf) < 0)
  {
    printf("ServeStatic error: file not exist\n");
    printf("file name: %s\n", file_name);
    clienterror(fd, file_name, "404", "Not found", "Proxy couldn't find this file");
    return;
  }

  filesize = sbuf.st_size;

  /* Send response headers to client */
  get_filetype(file_name, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Proxy Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* Send response body to client */
  srcfd = Open(file_name, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

void useCache(int connfd, char *path)
{
  char filename[MAXLINE];
  printf("\n============ Use Cache ============\n");
  printf("fd: %d \n", connfd);
  pathToFileName(path, filename);
  printf("filename: %s paths: %s\n", filename, path);

  serveStatic(connfd, filename);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Error Error</title>");
  sprintf(body, "%s<body bgcolor='ffffff'>\r\n ",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<hr><em>The Proxy server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  sprintf(buf, "Content-type: text/html\r\n");
  sprintf(buf, "Content-length: %d\r\n", (int)strlen(body));
  sprintf(buf, "%s\r\n", buf);

  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

int pathToFileName(char *uri, char *path)
{
  strcpy(path, "./.proxy");
  strcat(path, uri);
  return 1;
}

void sigchild_handler(int sig)
{
  while (waitpid(-1, 0, WNOHANG) > 0)
    ;
  return;
}