#include "pch.h"
#include <stdio.h>
#include <winsock2.h>
#undef max
#undef min
#include "Devices.h"
#include <algorithm>

#pragma comment(lib,"ws2_32.lib")

extern void ProcessClipboardRequest(RemoteRequest* data, SOCKET sClient);

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
	RemoteRequest revData;
    while (true)
    {
		sClient = accept(slisten, (SOCKADDR *)&remoteAddr, &nAddrlen);
		if (sClient == INVALID_SOCKET)
		{
			printf("accept error !");
			return 0;
		}
        int ret = recv(sClient, (char*)&revData, sizeof(RemoteRequest), 0);
        if(ret == sizeof(RemoteRequest))
        {
			ProcessClipboardRequest(&revData,sClient);
        }
		else
		{
			perror("Recv error");
		}
		
        closesocket(sClient);
    }
    
    closesocket(slisten);
    WSACleanup();
    return 0;
}