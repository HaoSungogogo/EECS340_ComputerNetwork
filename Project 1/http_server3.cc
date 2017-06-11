#include "minet_socket.h"
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>


#define FILENAMESIZE 100
#define BUFSIZE 1024

typedef enum \
{NEW,READING_HEADERS,WRITING_RESPONSE,READING_FILE,WRITING_FILE,CLOSED} states;

typedef struct connection_s connection;
typedef struct connection_list_s connection_list;

struct connection_s
{
  int sock;
  int fd;
  char filename[FILENAMESIZE+1];
  char buf[BUFSIZE+1];
  char *endheaders;
  bool ok;
  long filelen;
  states state;
  int headers_read,response_written,file_read,file_written, file_sent;

  connection *next;
};

struct connection_list_s
{
  connection *first,*last;
};

void add_connection(int,connection_list *);
void insert_connection(int,connection_list *);
void init_connection(connection *con);


int writenbytes(int,char *,int);
int readnbytes(int,char *,int);
void read_headers(connection *);
void write_response(connection *);
void read_file(connection *);
void write_file(connection *);

int main(int argc,char *argv[])
{
  int server_port;
  int sock_accept,sock_connect;
  struct sockaddr_in sa_accept,sa_connect;
  int rc;
  fd_set readlist,writelist;
  connection_list connections;
  connection *con;
  int maxfd;
  int i;

  /* parse command line args */
  if (argc != 3)
  {
    fprintf(stderr, "usage: http_server3 k|u port\n");
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
  printf("initialize and make socke\n");
  /* set server address*/
  memset(&sa_accept, 0, sizeof(sa_accept));
  sa_accept.sin_port = htons(server_port);
  sa_accept.sin_addr.s_addr = htonl(INADDR_ANY);
  sa_accept.sin_family = AF_INET;
  
  printf("set server address\n");
  /* bind listening socket */
  if(minet_bind(sock_accept, (struct sockaddr_in *)&sa_accept) < 0) {
    return -1;
  }
  printf("bind listening socket\n");
  /* start listening */
  if(minet_listen(sock_accept, 5) == -1) {
    fprintf(stderr, "Error Listening Socket.\n");
  }
  printf("start listenin\n");
  
  connections.first = NULL;
  connections.last = NULL;
  insert_connection(sock_accept, &connections);
  printf("start handling loop\n");
  
  /* connection handling loop */
  while(1)
  {
    /* create read and write lists */
    FD_ZERO(&readlist);
    FD_ZERO(&writelist);
    fcntl(sock_accept, F_SETFL, O_NONBLOCK);
    FD_SET(sock_accept, &readlist);
    maxfd = sock_accept;
    connection *temp = connections.first;
    
    while(temp != NULL) {
      if(temp->state == NEW || temp->state == READING_HEADERS || temp->state == READING_FILE) {
	FD_SET(temp->sock, &readlist);
	if(temp->fd > 0) FD_SET(temp->fd, &readlist);
	if(temp->fd > maxfd) {
	maxfd = temp->fd;
	printf("fd = %d\n", temp->fd);
	}
	if(temp->sock > maxfd) {
	  maxfd = temp->sock;
	  printf("sock = %d\n", temp->sock);
	}
      }
      if(temp->state == WRITING_RESPONSE || temp->state == WRITING_FILE) {
	FD_SET(temp->sock, &writelist);
	if(temp->fd > maxfd) {
	  maxfd = temp->fd;
	  printf("fd = %d\n", temp->fd);
	}
	if(temp->sock > maxfd) {
	  maxfd = temp->sock;
	  printf("sock = %d\n", temp->sock);
	}
      }
      temp = temp->next;
    }
    printf("create read and write lists maxfd = %d\n", maxfd);
    /* do a select */
    if(minet_select(maxfd+1, &readlist, &writelist, NULL, NULL) == -1) {
      fprintf(stderr, "Error Select Socket.\n");
      exit(-1);
    }
    printf("do a select\n");
    /* process sockets that are ready */
    for(i = 0; i <= maxfd; i++) {
      if(FD_ISSET(i, &readlist)) {
	/* for the accept socket, add accepted connection to connections */
	if (i == sock_accept)
	  {
	    if((sock_connect = minet_accept(sock_accept, (struct sockaddr_in *)&sa_connect)) == -1) {
	      fprintf(stderr, "Error Accept Socket.\n");
	    } else {
	      fcntl(sock_connect, F_SETFL, O_NONBLOCK);
	      insert_connection(sock_connect, &connections);
	      printf("New connection on socket %d.\n", sock_connect);
	    }
	  }
	else/* for a connection socket, handle the connection */
	  {
	    connection *temp2 = connections.first;
	    while(temp2->next != NULL) {
	      if(temp2->sock == i)
		break;
	      if(temp2->fd == i)
		break;
	      temp2 = temp2->next;
	    }
	    if(temp2->state == NEW) {
	      printf("Select NEW\n");
	      read_headers(temp2);
	    }
	    if(temp2->state == READING_HEADERS) {
	      printf("Select READING_HEADERS\n");
	      read_headers(temp2);
	    }
	    if(temp2->state == READING_FILE) {
	      printf("Select READING_FILE\n");
	      read_file(temp2);
	    }
	  }
      } else if (FD_ISSET(i, &writelist)) {
	connection *temp3 = connections.first;
	while(temp3->next != NULL) {
	  if(temp3->sock == i)
	    break;
	  temp3 = temp3->next;
	}
	if(temp3->state == WRITING_RESPONSE) {
	  printf("Select WRITING_RESPONSE\n");
	  write_response(temp3);
	}
	if(temp3->state == WRITING_FILE) {
	  printf("Select WRITING_FILE\n");
	  write_file(temp3);
	}
      }
    }
  }
}

void read_headers(connection *con)
{
  printf("Reading headers\n");
  con->state = READING_HEADERS;
  int startfile;
  /* first read loop -- get request and headers*/
  int rc = minet_read(con->sock, con->buf, BUFSIZE);
  printf("read_headers rc = %d\n", rc);
  if(rc < 0) {
    if(errno == EAGAIN)
      return;
    fprintf(stderr,"error reading requested in socket%d\n",con->sock);
    return;
  }
  else if(rc == 0) {
    /* parse request to get file name */
    /* Assumption: this is a GET request and filename contains no spaces*/
  L_HREAD:
    memset(&con->filename, 0, sizeof(con->filename));
    if(con->buf[4] == '/') {
      startfile = 5;
    } 
    else {
      startfile = 4;
    }
    for(int i = startfile; con->buf[i] != ' '; i++) {
      con->filename[i - startfile] = con->buf[i];
    }
    /* get file name and size, set to non-blocking */
  
    /* get name */
  
    /* try opening the file */
  
    /* set to non-blocking, get size */
    if((con->fd = open(con->filename, O_RDONLY)) == -1) {
      printf("open() failed.\n");
      con->ok = false;
    } 
    else {
      con->ok = true;
      printf("New fd = %d\n", con->fd);
      fcntl(con->fd, F_SETFL, O_NONBLOCK);
      con->filelen = lseek(con->fd, 0, SEEK_END);
      lseek(con->fd, 0, SEEK_SET);
    }
    write_response(con);
  }
  else if(rc == BUFSIZE) {
    con->headers_read += rc;
    read_headers(con);
  }
  else if(rc < BUFSIZE) {
    goto L_HREAD;
  }
}

void write_response(connection *con)
{
  printf("Writing response\n");
  con->state = WRITING_RESPONSE;
  int sock = con->sock;
  int rc;
  int written = con->response_written;
  char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                      "Content-type: text/plain\r\n"\
                      "Content-length: %d \r\n\r\n";
  char ok_response[100];
  char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                         "Content-type: text/html\r\n\r\n"\
                         "<html><body bgColor=black text=white>\n"\
                         "<h2>404 FILE NOT FOUND</h2>\n"\
                         "</body></html>\n";

  /* send response */
  if (con->ok)
  {
    /* send headers */
    sprintf(ok_response, ok_response_f, con->filelen);
    rc = writenbytes(sock, ok_response, strlen(ok_response));
    if(rc < 0) {
      if(errno == EAGAIN) {
	return;
      }
      minet_perror("error writing response ");
      return;
    }
    else if ((con->response_written + rc) < strlen(ok_response)) {
      con->response_written += rc;
      write_response(con);
    }
    else {
      read_file(con);
    }
  }
  else
  {
    rc = writenbytes(sock, notok_response, strlen(notok_response));
    if(rc < 0) {
      if(errno == EAGAIN) {
	return;
      }
      minet_perror("error writing response ");
      return;
    }
    else if ((con->response_written + rc) < strlen(notok_response)) {
      con->response_written += rc;
      write_response(con);
    }
    con->state = CLOSED;
    minet_close(con->sock);
  }
}

void read_file(connection *con)
{
  printf("Reading file\n");
  con->state = READING_FILE;
  con->file_written = 0;
  int rc = read(con->fd,con->buf,BUFSIZE);

  if (rc < 0)
  { 
    if (errno == EAGAIN)
      return;
    fprintf(stderr,"error reading requested file %s\n",con->filename);
    return;
  }
  else
  {
    con->file_read = rc;
    write_file(con);
    //con->state = WRITING_FILE;
  }
}

void write_file(connection *con)
{
  printf("Writing file\n");
  con->state = WRITING_FILE;
  int towrite = con->file_read;
  int written = con->file_written;
  int rc;
  rc  = writenbytes(con->sock, con->buf+written, towrite-written);
  if (rc < 0) {
    if (errno == EAGAIN)
      return;
    minet_perror("error writing file ");
    con->state = CLOSED;
    minet_close(con->sock);
    printf("fd closed:%d\n", con->fd);
    return;
  }
  else {
    con->file_written += rc;
    con->file_sent += rc;
    if (con->file_sent == con->filelen) {
      close(con->fd);
      con->fd = 0;
      con->state = CLOSED;
      minet_close(con->sock);
      printf("Close socket\n");
      return;
    }
    else if (con->file_written == con->file_read)
      read_file(con);
  }
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


// inserts a connection in place of a closed connection
// if there are no closed connections, appends the connection 
// to the end of the list

void insert_connection(int sock,connection_list *con_list)
{
  connection *i;
  for (i = con_list->first; i != NULL; i = i->next)
  {
    if (i->state == CLOSED)
    {
      i->sock = sock;
      i->state = NEW;
      init_connection(i);
      return;
    }
  }
  add_connection(sock,con_list);
}
 
void add_connection(int sock,connection_list *con_list)
{
  connection *con = (connection *) malloc(sizeof(connection));
  init_connection(con);
  con->next = NULL;
  con->state = NEW;
  con->sock = sock;
  if (con_list->first == NULL)
    con_list->first = con;
  if (con_list->last != NULL)
  {
    con_list->last->next = con;
    con_list->last = con;
  }
  else
    con_list->last = con;
}

void init_connection(connection *con)
{
  con->headers_read = 0;
  con->response_written = 0;
  con->file_read = 0;
  con->file_written = 0;
  con->file_sent = 0;
}
