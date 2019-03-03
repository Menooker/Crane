#include "pch.h"
#include <stdio.h>
#include <winsock2.h>
#undef max
#undef min
#include "Devices.h"
#include <algorithm>

struct WinClipboard
{
private:
	bool opened;
	HANDLE hglb=nullptr;
	void* buffer = nullptr;
public:

	HANDLE GetMemObj()
	{
		return hglb;
	}

	void* ReallocData(size_t oldsz, size_t sz)
	{
		auto newhglb = GlobalAlloc(GMEM_MOVEABLE, sz);
		if (newhglb == NULL)
		{
			return nullptr;
		}
		void* newbuffer = GlobalLock(newhglb);
		memcpy(newbuffer, buffer, oldsz);
		UnlockData();
		hglb = newhglb;
		buffer = newbuffer;
		// Place the handle on the clipboard. 
		return buffer;
	}
	void ClearData()
	{
		UnlockData();
		EmptyClipboard();
		opened = OpenClipboard(NULL);
	}
	void UnlockData()
	{
		if (buffer)
		{
			GlobalUnlock(hglb);
			buffer = nullptr;
		}
	}

	void* LockAndGetData(UINT format)
	{
		if (opened)
		{
			hglb = ::GetClipboardData(format);
			if (hglb)
				buffer = GlobalLock(hglb);
		}
		return buffer;
	}

	void CreateAndSetData(char* data, size_t sz)
	{
		UnlockData();
		hglb = GlobalAlloc(GMEM_MOVEABLE,sz);
		if (hglb == NULL)
		{
			return;
		}

		buffer = GlobalLock(hglb);
		memcpy(buffer, data, sz);
		GlobalUnlock(hglb);
		buffer = nullptr;

		// Place the handle on the clipboard. 

		SetClipboardData(CF_TEXT, hglb);
	}

	WinClipboard()
	{
		opened = OpenClipboard(NULL);
	}

	~WinClipboard()
	{
		if (buffer)
			GlobalUnlock(hglb);
		if (opened)
			CloseClipboard();
	}
};

void ProcessClipboardRequest(RemoteRequest* req, SOCKET sClient)
{
	switch (req->cmd)
	{
	case CB_GET:
	{
		WinClipboard cb;
		uint64_t ret_size = 0;
		char* pbuf = (char*)cb.LockAndGetData(CF_TEXT);
		if (pbuf)
		{
			ret_size = strlen(pbuf) + 1;
			if (req->param1 >= ret_size)
				ret_size = 0;
			else
				ret_size = std::min(req->param2, ret_size - req->param1);
		}
		send(sClient, (char*)&ret_size, sizeof(ret_size), 0);
		if (!pbuf)
			pbuf = (char*)&ret_size;
		send(sClient, pbuf + req->param1, ret_size, 0);
		break;
	}
	case CB_SET:
	{
		printf("SET\n");
		WinClipboard cb;
		char* pbuf = (char*)cb.LockAndGetData(CF_TEXT);
		size_t sz = 0;
		uint64_t ret_size = req->param2;
		if (pbuf)
		{
			sz = strlen(pbuf);
			if (req->param1 == SIZE_MAX)
				req->param1 = sz;
		}

		if (req->param1 == SIZE_MAX)
			req->param1 = 0;
		char* recvbuf = new char[req->param1 + req->param2 + 1];
		if (pbuf)
		{
			memcpy(recvbuf, pbuf, std::min(sz, req->param1 + req->param2));
			cb.ClearData();
		}
		else
		{
			memset(recvbuf, 0, req->param1);
		}
		recvbuf[req->param1 + req->param2] = 0;
		recv(sClient, recvbuf + req->param1, req->param2, 0);
		cb.CreateAndSetData(recvbuf, req->param1 + req->param2 + 1);
		delete[]recvbuf;

		send(sClient, (char*)&ret_size, sizeof(ret_size), 0);
		break;
	}
	case CB_SIZE:
	{
		uint64_t ret_size = 0;
		WinClipboard cb;
		char* pbuf = (char*)cb.LockAndGetData(CF_TEXT);
		if (pbuf)
			ret_size = strlen(pbuf) + 1;
		send(sClient, (char*)&ret_size, sizeof(ret_size), 0);
		break;
	}
	case CB_TRUNC:
	{
		printf("trun %lld\n", req->param1);
		if (OpenClipboard(NULL))
		{
			EmptyClipboard();
			CloseClipboard();
		}
		uint64_t ret_size = 0;
		send(sClient, (char*)&ret_size, sizeof(ret_size), 0);
		break;
	}
	}
}