#include "minet_socket.h"
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>

#define BUFSIZE 1024
#define FILENAMESIZE 100

int handle_connection(int);
int writenbytes(int,char *,int);
int readnbytes(int,char *,int);

int main(int argc,char *argv[])
{
  int server_port;
  int sock_accept, sock_connect;
  struct sockaddr_in sa_accept, sa_connect;
  int rc;
 
  /* parse command line args */
  if (argc != 3)
  {
    fprintf(stderr, "usage: http_server1 k|u port\n");
    exit(-1);
  }
  server_port = atoi(argv[2]);
  if (server_port < 1500)
  {
    fprintf(stderr,"INVALID PORT NUMBER: %d; can't be < 1500\n",server_port);
    exit(-1);
  }

  /* initialize and make socket */
  if (toupper(*(argv[1])) == 'K') { 
      minet_init(MINET_KERNEL);
  } else if (toupper(*(argv[1])) == 'U') { 
      minet_init(MINET_USER);
  } else {
      fprintf(stderr, "First argument must be k or u\n");
      exit(-1);
  }
  sock_accept  = minet_socket(SOCK_STREAM);
  sock_connect = minet_socket(SOCK_STREAM);

  /* set server address*/
  memset(&sa_accept, 0, sizeof(sa_accept));
  sa_accept.sin_port = htons(server_port);
  sa_accept.sin_addr.s_addr = htonl(INADDR_ANY);
  sa_accept.sin_family = AF_INET;
  
  memset(&sa_connect, 0, sizeof(sa_connect));

  /* bind listening socket */
  if(minet_bind(sock_accept, (struct sockaddr_in *)&sa_accept) < 0) {
    fprintf(stderr, "Error Bind Socket.\n");
    return -1;
  }
  /* start listening */
  if(minet_listen(sock_accept, 5) == -1) {
    fprintf(stderr, "Error Listening Socket.\n");
    return -1;
  }

  /* connection handling loop */
  while(1)
  {
    sock_connect = minet_accept(sock_accept, (struct sockaddr_in *)&sa_connect);
    /* handle connections */
    rc = handle_connection(sock_connect);
  }
}

int handle_connection(int sock_connect)
{
  char filename[FILENAMESIZE+1];
  int rc;
  int fd;
  int startfile;
  struct stat filestat;
  char buf[BUFSIZE+1];
  char *headers;
  char *endheaders;
  char *fbuf;
  int datalen=0;
  char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                      "Content-type: text/plain\r\n"\
                      "Content-length: %d \r\n\r\n";
  char ok_response[100];
  char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                         "Content-type: text/html\r\n\r\n"\
                         "<html><body bgColor=black text=white>\n"\
                         "<h2>404 FILE NOT FOUND</h2>\n"
                         "</body></html>\n";
  bool ok=true;

  /* first read loop -- get request and headers*/
  //TODO: test large file
  while((rc = minet_read(sock_connect, buf, BUFSIZE)) == BUFSIZE) {
    fprintf(stderr,"%s\n", buf);
  }
 
  /* parse request to get file name */
  /* Assumption: this is a GET request and filename contains no spaces*/
  memset(&filename, 0, sizeof(filename));
  if(buf[4] == '/') {
    startfile = 5;
  } else {
    startfile = 4;
  }
  for(int i = startfile; buf[i] != ' '; i++) {
    filename[i - startfile] = buf[i];
  }
  /* try opening the file */
  if((fd = open(filename, O_RDONLY)) ==  -1) {
    ok = false;
  } else {
    ok = true;
    datalen = lseek(fd, 0, SEEK_END);
    fbuf = new char[datalen + 1];
    lseek(fd, 0, SEEK_SET);
    rc = read(fd, fbuf, datalen);
    close(fd);
  }
  /* send response */
  if (ok)
  {
    /* send headers */
    sprintf(ok_response, ok_response_f, datalen);
    rc = writenbytes(sock_connect, ok_response, strlen(ok_response));
    /* send file */
    rc = writenbytes(sock_connect, fbuf, strlen(fbuf));
  }
  else // send error response
  {
    rc = writenbytes(sock_connect, notok_response, strlen(notok_response));
  }

  /* close socket and free space */
  minet_close(sock_connect);
  
  if (ok)
    return 0;
  else
    return -1;
}

int readnbytes(int fd,char *buf,int size)
{
  int rc = 0;
  int totalread = 0;
  while ((rc = minet_read(fd,buf+totalread,size-totalread)) > 0)
    totalread += rc;

  if (rc < 0)
  {
    return -1;
  }
  else
    return totalread;
}

int writenbytes(int fd,char *str,int size)
{
  int rc = 0;
  int totalwritten =0;
  while ((rc = minet_write(fd,str+totalwritten,size-totalwritten)) > 0)
    totalwritten += rc;

  if (rc < 0)
    return -1;
  else
    return totalwritten;
}

