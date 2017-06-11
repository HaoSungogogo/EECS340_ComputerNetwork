#include "minet_socket.h"
#include <stdlib.h>
#include <ctype.h>

#define BUFSIZE 1024

int write_n_bytes(int fd, char * buf, int count);

int main(int argc, char * argv[]) {
    char * server_name = NULL;
    int server_port = 0;
    char * server_path = NULL;

    int i = 0;
    int sock = 0;
    int rc = -1;
    int datalen = 0;
    int requestlen = 0;
    bool ok = true;
    struct sockaddr_in sa;
    FILE * wheretoprint = stdout;
    struct hostent * site = NULL;
    char * req = NULL;

    char buf[BUFSIZE + 1];
    char request[BUFSIZE + 1];
    char http[10];
    char res[4];
    char hbuf[2];
    char hend[4];
    char * endheaders = NULL;
   
    struct timeval timeout;
    fd_set set;
  
    /*parse args */
    if (argc != 5) {
	fprintf(stderr, "usage: http_client k|u server port path\n");
	exit(-1);
    }
 
    server_name = argv[2];
    server_port = atoi(argv[3]);
    server_path = argv[4];
 
    /* initialize minet */
    if (toupper(*(argv[1])) == 'K') { 
	minet_init(MINET_KERNEL);
    } else if (toupper(*(argv[1])) == 'U') { 
	minet_init(MINET_USER);
    } else {
	fprintf(stderr, "First argument must be k or u\n");
	exit(-1);
    }
  
    /*Initialize fd_set*/
    FD_ZERO(&set);

    /* create socket */
    sock = minet_socket(SOCK_STREAM);
    FD_SET(sock, &set);
  
    // Do DNS lookup
    /* Hint: use gethostbyname() */
    site = gethostbyname(server_name);
    if(site == NULL) {
      close(sock);
      return -1;
    }

    /* set address */
    memset(&sa, 0, sizeof(sa));
    sa.sin_port = htons(server_port);
    sa.sin_addr.s_addr = *(unsigned long *)site->h_addr_list[0];
    sa.sin_family = AF_INET;

    /* connect socket */
    if(minet_connect(sock, &sa) != 0) {
      minet_close(sock);
      return -1;
    }

    /* send request */
    sprintf(request, "GET /%s HTTP/1.0\r\n\r\n", server_path);
    rc = write_n_bytes(sock, request, strlen(request));

    /* wait till socket can be read */
    /* Hint: use select(), and ignore timeout for now. */
    
    switch(minet_select(sock+1, &set, NULL, NULL, NULL)) {
    case -1: printf("Select() error.\n"); break;
    case  0: printf("Select() timeout.\n"); break;
    default: if(FD_ISSET(sock, &set)) {
	;
	}
    }
    
    /* first read loop -- read headers */
    rc = minet_read(sock, http, 9);
    http[9] = '\0';
    printf("http:%s", http);
    rc = minet_read(sock, res, 3);
    res[3] = '\0';
    printf("%s", res);
    while(1) {
      rc = minet_read(sock, hbuf, 1);
      hbuf[1] = '\0';
      printf("%s", hbuf);
      if(hbuf[0] == '\r') {
	minet_read(sock, hend, 3);
	hend[3] = '\0';
	printf("%s", hend);
	if((hend[0] == '\n')&&(hend[1] == '\r')&&(hend[2] == '\n')) {
	  break;
	}
      }
    }
    /* examine return code */
    //Skip "HTTP/1.0"
    //remove the '\0'
    // Normal reply has return code 200
    ok = false;
    if((res[0] == '2')&&(res[1] == '0')&&(res[2] == '0'))
      ok = true;
    /* print first part of response */

    /* second read loop -- print out the rest of the response */
    while((rc = minet_read(sock, buf, BUFSIZE)) != 0) {
      buf[rc] = '\0';
      printf("%s", buf);
    }
    buf[rc] = '\0';
    printf("%s", buf);
    
    /*close socket and deinitialize */
    minet_close(sock);
    minet_deinit();
    printf("\nOK = %d\n", ok);
    if (ok) {
	return 0;
    } else {
	return -1;
    }
}

int write_n_bytes(int fd, char * buf, int count) {
    int rc = 0;
    int totalwritten = 0;

    while ((rc = minet_write(fd, buf + totalwritten, count - totalwritten)) > 0) {
	totalwritten += rc;
    }
    
    if (rc < 0) {
	return -1;
    } else {
	return totalwritten;
    }
}
