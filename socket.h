int setupServerSocket(int portno); // Like new ServerSocket in Java
int callServer(char* host, int portno); // Like new Socket in Java
int serverSocketAccept(int serverSocket); // Like ss.accept() in Java
void writeLong(unsigned long x, int socket); // Write an int over the given socket
unsigned long readLong(int socket); // Read an int from the given socket
