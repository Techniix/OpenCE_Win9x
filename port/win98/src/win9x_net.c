/*
WIN9X_NET.C

getaddrinfo, freeaddrinfo and getnameinfo (Windows XP's ws2_32.dll; and
wspiapi.h's WspiapiGetAddrInfo, which miniupnpc and Mbed TLS's sockets call
for them), on what Windows 98's has: gethostbyname, gethostbyaddr, inet_addr.
IPv4 only (Windows 9x has no IPv6): a request for AF_INET6 fails as an
unsupported family.
*/

#define WINSOCK_API_LINKAGE
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef AI_NUMERICSERV
#define AI_NUMERICSERV 0x00000008
#endif

/* an address and the addrinfo that holds it, freed together */
struct win9x_addrinfo
{
	struct addrinfo information;
	struct sockaddr_in address;
};

static int service_port(const char *service, const struct addrinfo *hints, unsigned short *port)
{
	char *end;
	long number;

	*port = 0;
	if (!service || !*service)
		return 0;
	number = strtol(service, &end, 10);
	if (!*end && number >= 0 && number <= 65535)
	{
		*port = htons((unsigned short)number);
		return 0;
	}
	if (hints && (hints->ai_flags & AI_NUMERICSERV))
		return EAI_NONAME;
	{
		struct servent *entry = getservbyname(service,
			hints && hints->ai_socktype == SOCK_DGRAM ? "udp" : "tcp");

		if (!entry)
			return EAI_SERVICE;
		*port = (unsigned short)entry->s_port;
	}
	return 0;
}

static struct addrinfo *addrinfo_new(unsigned long address, unsigned short port, const struct addrinfo *hints)
{
	struct win9x_addrinfo *made = (struct win9x_addrinfo *)calloc(1, sizeof(*made));

	if (!made)
		return NULL;
	made->address.sin_family = AF_INET;
	made->address.sin_port = port;
	made->address.sin_addr.s_addr = address;
	made->information.ai_family = AF_INET;
	made->information.ai_socktype = hints ? hints->ai_socktype : 0;
	made->information.ai_protocol = hints ? hints->ai_protocol : 0;
	made->information.ai_addrlen = sizeof(made->address);
	made->information.ai_addr = (struct sockaddr *)&made->address;
	return &made->information;
}

void WSAAPI freeaddrinfo(struct addrinfo *information)
{
	while (information)
	{
		struct addrinfo *next = information->ai_next;

		free(information->ai_canonname);
		free(information);
		information = next;
	}
}

int WSAAPI getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
	struct addrinfo **result)
{
	unsigned short port;
	unsigned long numeric;
	int status;

	*result = NULL;
	if (hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET)
		return EAI_FAMILY;
	if (!node && !service)
		return EAI_NONAME;
	if ((status = service_port(service, hints, &port)) != 0)
		return status;

	if (!node)
	{
		/* (no host: any address to listen on, or this machine's loopback) */
		*result = addrinfo_new(hints && (hints->ai_flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK),
			port, hints);
		return *result ? 0 : EAI_MEMORY;
	}
	numeric = inet_addr(node);
	if (numeric != INADDR_NONE || !strcmp(node, "255.255.255.255"))
	{
		*result = addrinfo_new(numeric, port, hints);
		return *result ? 0 : EAI_MEMORY;
	}
	if (hints && (hints->ai_flags & AI_NUMERICHOST))
		return EAI_NONAME;
	{
		struct hostent *host = gethostbyname(node);
		struct addrinfo **tail = result;
		int index;

		if (!host || host->h_addrtype != AF_INET || !host->h_addr_list[0])
			return WSAGetLastError() == WSATRY_AGAIN ? EAI_AGAIN : EAI_NONAME;
		for (index = 0; host->h_addr_list[index]; index++)
		{
			unsigned long address;

			memcpy(&address, host->h_addr_list[index], sizeof(address));
			*tail = addrinfo_new(address, port, hints);
			if (!*tail)
			{
				freeaddrinfo(*result);
				*result = NULL;
				return EAI_MEMORY;
			}
			if (index == 0 && hints && (hints->ai_flags & AI_CANONNAME) && host->h_name)
				(*tail)->ai_canonname = _strdup(host->h_name);
			tail = &(*tail)->ai_next;
		}
	}
	return 0;
}

int WSAAPI getnameinfo(const struct sockaddr *address, socklen_t length, char *host, DWORD host_size,
	char *service, DWORD service_size, int flags)
{
	const struct sockaddr_in *inet = (const struct sockaddr_in *)address;

	if (!address || length < (socklen_t)sizeof(*inet) || inet->sin_family != AF_INET)
		return EAI_FAMILY;
	if (host && host_size)
	{
		struct hostent *entry = NULL;
		const char *name;

		if (!(flags & NI_NUMERICHOST))
			entry = gethostbyaddr((const char *)&inet->sin_addr, sizeof(inet->sin_addr), AF_INET);
		if (!entry && (flags & NI_NAMEREQD))
			return EAI_NONAME;
		name = entry ? entry->h_name : inet_ntoa(inet->sin_addr);
		if (strlen(name) >= host_size)
			return EAI_FAIL;
		strcpy(host, name);
	}
	if (service && service_size)
	{
		char text[8];

		_snprintf(text, sizeof(text), "%u", ntohs(inet->sin_port));
		text[sizeof(text) - 1] = '\0';
		if (strlen(text) >= service_size)
			return EAI_FAIL;
		strcpy(service, text);
	}
	return 0;
}

/* (wspiapi.h's: the same, under its names) */
int WINAPI WspiapiGetAddrInfo(const char *node, const char *service, const struct addrinfo *hints,
	struct addrinfo **result)
{
	return getaddrinfo(node, service, hints, result);
}

void WINAPI WspiapiFreeAddrInfo(struct addrinfo *information)
{
	freeaddrinfo(information);
}

/* WSASocketW: on Windows 9x, socket's (whose sockets take overlapped
operations, as the flag asks); elsewhere ws2_32.dll's own, without the flag
Windows 7 SP1 added if the system does not know it */
#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif

SOCKET WSAAPI WSASocketW(int family, int type, int protocol, LPWSAPROTOCOL_INFOW information, GROUP group,
	DWORD flags)
{
	typedef SOCKET (WSAAPI *proc)(int, int, int, LPWSAPROTOCOL_INFOW, GROUP, DWORD);
	static proc system_socket;
	SOCKET result;

	if ((GetVersion() & 0x80000000) || information)
	{
		if (information)
		{
			WSASetLastError(WSAEINVAL);
			return INVALID_SOCKET;
		}
		return socket(family, type, protocol);
	}
	if (!system_socket)
		system_socket = (proc)GetProcAddress(GetModuleHandleA("ws2_32.dll"), "WSASocketW");
	if (!system_socket)
		return socket(family, type, protocol);
	result = system_socket(family, type, protocol, information, group, flags);
	if (result == INVALID_SOCKET && (flags & WSA_FLAG_NO_HANDLE_INHERIT) && WSAGetLastError() == WSAEINVAL)
		result = system_socket(family, type, protocol, information, group, flags & ~WSA_FLAG_NO_HANDLE_INHERIT);
	return result;
}
