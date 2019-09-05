sieve: project.cpp socket.o
	g++ -std=c++11 socket.o project.cpp -o sieve

pocket.o: socket.cpp socket.h
	g++ socket.cpp -c

clean:
	rm *.o sieve

