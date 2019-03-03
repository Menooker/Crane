#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include "Crane.h"
#include <CompilerHappy.h>
#include <Devices.h>

int main(int argc, char const *argv[])
{

	if (!CraneIsServer())
	{
		printf("A server is running or crane file is not deleted.\n");
		exit(2);
	}
	int server_fd, new_socket, valread;
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);
	char buffer[1024] = { 0 };


	// Creating socket file descriptor 
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
	{
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(13579);

	// Forcefully attaching socket to the port 8080 
	if (bind(server_fd, (struct sockaddr *)&address,
		sizeof(address)) < 0)
	{
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	if (listen(server_fd, 3) < 0)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}
	for (;;)
	{
		if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
			(socklen_t*)&addrlen)) < 0)
		{
			perror("accept");
			exit(EXIT_FAILURE);
		}
		RemoteRequest req;
		valread = read(new_socket, &req, sizeof(req));
		//process it
		close(new_socket);
	}
	return 0;
}