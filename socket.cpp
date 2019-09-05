#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "socket.h"

int setupServerSocket(int portno) {
  //get a socket of the right type
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    printf("ERROR opening socket");
    exit(1);
  }

  //server address structure
  struct sockaddr_in serv_addr;

  //set all the values in the server address to 0
  memset(&serv_addr, '0', sizeof(serv_addr)); 

  //Setup the type of socket (internet vs filesystem)
  serv_addr.sin_family = AF_INET;
    
  //Basically the machine we are on...
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  //Setup the port number
  //htons - is host to network byt order
  //network byte order is most sig byte first
  //  which might be host or might not be
  serv_addr.sin_port = htons(portno);  

  //Bind the socket to the given port
  if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    printf("ERROR on binding\n");
    exit(1);
  }

  //set it up to listen
  listen(sockfd,1);
 
  return sockfd;
}

int callServer(char* host, int portno) {
  //Socket pointer
  int sockfd;
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    fprintf(stderr,"ERROR opening socket\n");
    exit(0);
  }

  //server address structure
  struct sockaddr_in serv_addr;

  //Set all the values in the server address to 0
  memset(&serv_addr, '0', sizeof(serv_addr)); 

  //Setup the type of socket (internet vs filesystem)
  serv_addr.sin_family = AF_INET;
 
  //Setup the port number
  serv_addr.sin_port = htons(portno);  

  //Setup the server host address
  struct hostent *server;
  server = gethostbyname(host);
  if (server == NULL) {
    fprintf(stderr,"ERROR, no such host\n");
    exit(0);
  }

  //server->h_addr = "10.35.195.47";
  struct in_addr* temp = (struct in_addr*) server->h_addr_list[0];
  //printf("%s\n", inet_ntoa(*temp));
  //printf("%s\n", server->h_name);

  //dest, src, size
  memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

  //Connect to the server
  if (connect(sockfd,(struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    printf("ERROR connecting\n");
    exit(0);
  }

  return sockfd;
}

int serverSocketAccept(int serverSocket) { 
  int newsockfd;
  struct sockaddr_in cli_addr;
  socklen_t clilen = sizeof(cli_addr); 
 
  //wait for a call
  newsockfd = accept(serverSocket, (struct sockaddr *) &cli_addr, &clilen);
  if (newsockfd < 0) {
    printf("ERROR on accept");
    exit(1);
  }

  return newsockfd;
}

void writeLong(unsigned long x, int socket){
  int n = write(socket,&x,sizeof(long));
  if (n < 0) {
    printf("ERROR writing to socket\n");
    exit(1);
  }
}

unsigned long readLong(int socket) {
  unsigned long message = 0;
  unsigned long n = read(socket,&message,sizeof(long));
  if (n < 0) {
    printf("ERROR reading from socket\n");
    exit(0);
  }
  return message;
}
