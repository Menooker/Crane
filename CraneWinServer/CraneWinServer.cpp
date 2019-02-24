#include "pch.h"
#include <stdio.h>
#include <winsock2.h>
#undef max
#undef min
#include "ClipboardDevice.h"
#include <algorithm>

#pragma comment(lib,"ws2_32.lib")

int main(int argc, char* argv[])
{
    WORD sockVersion = MAKEWORD(2,2);
    WSADATA wsaData;
    if(WSAStartup(sockVersion, &wsaData)!=0)
    {
        return 0;
    }

    SOCKET slisten = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(slisten == INVALID_SOCKET)
    {
        printf("socket error !");
        return 0;
    }

    sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(13578);
    sin.sin_addr.S_un.S_addr = INADDR_ANY; 
    if(bind(slisten, (LPSOCKADDR)&sin, sizeof(sin)) == SOCKET_ERROR)
    {
        printf("bind error !");
    }

    if(listen(slisten, 5) == SOCKET_ERROR)
    {
        printf("listen error !");
        return 0;
    }

    SOCKET sClient;
    sockaddr_in remoteAddr;
    int nAddrlen = sizeof(remoteAddr);
    char revData[255]; 
	char data[] = "hello, 1234567890";

    while (true)
    {
		sClient = accept(slisten, (SOCKADDR *)&remoteAddr, &nAddrlen);
		if (sClient == INVALID_SOCKET)
		{
			printf("accept error !");
			return 0;
		}
        int ret = recv(sClient, revData, 255, 0);        
        if(ret > 0)
        {
			ClipboardRequest* req = (ClipboardRequest*)revData;
			switch (req->cmd)
			{
			case CB_GET:
			{
				printf("GET\n");
				uint64_t ret_size;
				if (req->param1 >= strlen(data) + 1)
					ret_size = 0;
				else
					ret_size = std::min(req->param2, strlen(data) + 1 - req->param1);
				send(sClient, (char*)&ret_size, sizeof(ret_size), 0);
				send(sClient, data + req->param1, ret_size, 0);
				break;
			}
			case CB_SET:
			{
				printf("SET\n");
				uint64_t ret_size;
				if (req->param1 >= sizeof(data) + 1)
					ret_size = 0;
				else
					ret_size = std::min(req->param2, sizeof(data) + 1 - req->param1);
				recv(sClient, data + req->param1, ret_size, 0);
				send(sClient, (char*)&ret_size, sizeof(ret_size), 0);
				break;
			}
			case CB_SIZE:
			{
				uint64_t ret_size = strlen(data);
				send(sClient, (char*)&ret_size, sizeof(ret_size), 0);
				break;
			}
			}
        }
		
        closesocket(sClient);
    }
    
    closesocket(slisten);
    WSACleanup();
    return 0;
}