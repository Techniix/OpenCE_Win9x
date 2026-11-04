/*
WIN9X_SELFTEST.C

halo.exe --selftest (SELFTEST.BAT): what can be checked of this machine
without a window or a level, written to SELFTEST.TXT beside the game:

- the numbers internet play seals its tunnel with: the standards' test
  vectors for SHA-256, HMAC, ChaCha20-Poly1305, X25519 and Ed25519 run on
  this processor (p2p_selftest.c), and the machine's random numbers;
- the clocks the frame budget, the governor and the game's timing read;
- the network: sockets on the loopback (UDP and TCP), the names of the
  brokers and STUN servers, a TCP connection to each broker, and what the
  STUN servers see of this machine: whether it can be reached by hole
  punching (port/linux/src/p2p.c), which is what joining a game over the
  internet needs;
- the files the game wants beside it.

A failing line says which part fails; the same file is what to send back.
*/

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "posix.h"
#include "p2p_internal.h"
#include "../../linux/src/port_config.h"

/* ---------- private code */

static FILE *output;
static int failures, warnings, checks;

static void line(const char *format, ...)
{
	char text[512];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	if (output)
		fprintf(output, "%s\r\n", text);
	fflush(output);
}

static void result(const char *status, const char *name, const char *detail)
{
	line("%-5s %s%s%s", status, name, detail && detail[0] ? ": " : "", detail ? detail : "");
}

static void pass_or_fail(const char *name, int ok, const char *detail)
{
	checks++;
	if (!ok)
		failures++;
	result(ok ? "pass" : "FAIL", name, detail);
}

static void warn(const char *name, const char *detail)
{
	warnings++;
	result("warn", name, detail);
}

static void crypto_report(const char *name, int passed, void *context)
{
	(void)context;
	pass_or_fail(name, passed, "");
}

static double seconds_now(void)
{
	static LARGE_INTEGER frequency;
	LARGE_INTEGER count;

	if (!frequency.QuadPart && !QueryPerformanceFrequency(&frequency))
		return GetTickCount() / 1000.0;
	QueryPerformanceCounter(&count);
	return (double)count.QuadPart / (double)frequency.QuadPart;
}

static void test_clocks(void)
{
	double first, last;
	DWORD tick_first, tick_last;
	char detail[160];
	LARGE_INTEGER frequency;

	line("--- clocks");
	if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
	{
		warn("performance counter", "none: timing uses the millisecond clock (the frame budget is coarser)");
		return;
	}
	sprintf(detail, "%.0f counts a second", (double)frequency.QuadPart);
	result("info", "performance counter", detail);
	tick_first = GetTickCount();
	first = seconds_now();
	Sleep(250);
	last = seconds_now();
	tick_last = GetTickCount();
	sprintf(detail, "250 ms slept: counter %.0f ms, tick count %lu ms", (last - first) * 1000.0,
		(unsigned long)(tick_last - tick_first));
	/* (an emulator's counter may run fast or slow against the real time: the two clocks must agree) */
	pass_or_fail("clocks agree", (last - first) * 1000.0 > 200.0 && (last - first) * 1000.0 < 400.0 &&
		(tick_last - tick_first) > 200 && (tick_last - tick_first) < 400, detail);
}

static int wait_for(int socket, int writing, long seconds)
{
	int list[1] = { socket }, count = 1;

	if (writing)
	{
		if (posix_socket_select(NULL, NULL, list, &count, NULL, NULL, seconds, 0, 0) < 0)
			return 0;
	}
	else if (posix_socket_select(list, &count, NULL, NULL, NULL, NULL, seconds, 0, 0) < 0)
		return 0;
	return count > 0;
}

static void make_address(struct sockaddr_in *address, unsigned long ip, unsigned short port)
{
	memset(address, 0, sizeof(*address));
	address->sin_family = AF_INET;
	address->sin_addr.s_addr = ip;
	address->sin_port = htons(port);
}

static void test_loopback(void)
{
	int udp_a, udp_b, listener, client, accepted;
	struct sockaddr_in address, bound;
	int length = sizeof(bound);
	char buffer[32];

	line("--- sockets on this machine");
	udp_a = posix_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	udp_b = posix_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udp_a < 0 || udp_b < 0)
	{
		pass_or_fail("UDP sockets open", 0, "WSAGetLastError says why");
		return;
	}
	make_address(&address, htonl(INADDR_LOOPBACK), 0);
	posix_socket_bind(udp_a, &address, sizeof(address));
	posix_socket_getsockname(udp_a, &bound, &length);
	posix_socket_sendto(udp_b, "ping", 4, 0, &bound, sizeof(bound));
	memset(buffer, 0, sizeof(buffer));
	pass_or_fail("UDP on the loopback", wait_for(udp_a, 0, 2) &&
		posix_socket_recvfrom(udp_a, buffer, sizeof(buffer), 0, &address, &length) == 4 && !memcmp(buffer, "ping", 4),
		"");
	posix_socket_close(udp_a);
	posix_socket_close(udp_b);

	listener = posix_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	client = posix_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	make_address(&address, htonl(INADDR_LOOPBACK), 0);
	length = sizeof(bound);
	if (listener < 0 || client < 0 || posix_socket_bind(listener, &address, sizeof(address)) < 0 ||
		posix_socket_listen(listener, 1) < 0 || posix_socket_getsockname(listener, &bound, &length) < 0)
	{
		pass_or_fail("TCP on the loopback", 0, "a listening socket cannot be made");
		return;
	}
	accepted = -1;
	if (posix_socket_connect(client, &bound, sizeof(bound)) == 0 || 1)
	{
		length = sizeof(address);
		if (wait_for(listener, 0, 2))
			accepted = posix_socket_accept(listener, &address, &length);
	}
	memset(buffer, 0, sizeof(buffer));
	posix_socket_send(client, "tcp", 3, 0);
	pass_or_fail("TCP on the loopback", accepted >= 0 && wait_for(accepted, 0, 2) &&
		posix_socket_recv(accepted, buffer, sizeof(buffer), 0) == 3 && !memcmp(buffer, "tcp", 3), "");
	if (accepted >= 0)
		posix_socket_close(accepted);
	posix_socket_close(client);
	posix_socket_close(listener);
	{
		posix_ulong local = posix_local_ipv4_address();

		sprintf(buffer, "%lu.%lu.%lu.%lu", local & 255, (local >> 8) & 255, (local >> 16) & 255, local >> 24);
		if (local)
			result("info", "this machine's address", buffer);
		else
			warn("this machine's address", "none found: no network adapter, or no address yet");
	}
}

/* a host:port in text -> the address and port; the port may be given as default */
static int split_server(const char *text, char *host, int host_size, int *port, int default_port)
{
	const char *colon = strrchr(text, ':');
	int length = colon ? (int)(colon - text) : (int)strlen(text);

	if (length <= 0 || length >= host_size)
		return 0;
	memcpy(host, text, (size_t)length);
	host[length] = 0;
	*port = colon && atoi(colon + 1) > 0 ? atoi(colon + 1) : default_port;
	return 1;
}

static void each_item(const char *list, void (*function)(const char *item, void *context), void *context)
{
	char item[200];
	const char *cursor = list;

	while (*cursor)
	{
		int length = 0;

		while (*cursor == ',' || *cursor == ' ' || *cursor == '\r' || *cursor == '\n' || *cursor == '\t')
			cursor++;
		while (cursor[length] && cursor[length] != ',' && cursor[length] != '\r' && cursor[length] != '\n' &&
			length < (int)sizeof(item) - 1)
			length++;
		if (!length)
			break;
		memcpy(item, cursor, (size_t)length);
		item[length] = 0;
		cursor += length;
		function(item, context);
	}
}

static void test_broker(const char *item, void *context)
{
	char host[200], detail[200];
	int port, socket, outcome = 0;
	unsigned long ip;
	struct sockaddr_in address;
	double started = seconds_now();

	(void)context;
	if (!split_server(item, host, sizeof(host), &port, 1883) || !strncmp(item, "ws", 2))
		return;
	ip = posix_resolve_ipv4(host);
	if (!ip)
	{
		sprintf(detail, "%s: the name does not resolve (DNS: this machine's name server, or no network)", host);
		pass_or_fail("broker name", 0, detail);
		return;
	}
	socket = posix_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	make_address(&address, ip, (unsigned short)port);
	posix_socket_set_nonblocking(socket, 1);
	posix_socket_connect(socket, &address, sizeof(address));
	if (wait_for(socket, 1, 6))
	{
		int error = 0, size = sizeof(error);

		posix_socket_getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &size);
		outcome = error == 0;
	}
	posix_socket_close(socket);
	sprintf(detail, "%s:%d %s (%.1f s)", host, port, outcome ? "reached" : "not reached (blocked, down, or no route)",
		seconds_now() - started);
	/* (one broker down is not a failure: any one is enough) */
	if (outcome)
		pass_or_fail("broker", 1, detail);
	else
		warn("broker", detail);
}

struct stun_mapping
{
	int socket;
	unsigned short ports[8];
	unsigned long addresses[8];
	int count;
};

static void test_stun(const char *item, void *context)
{
	struct stun_mapping *mapping = context;
	char host[200], detail[240];
	int port, tries;
	unsigned long ip;
	unsigned char request[20], packet[512];
	struct sockaddr_in address, from;

	if (!split_server(item, host, sizeof(host), &port, 3478) || mapping->count >= 8)
		return;
	ip = posix_resolve_ipv4(host);
	if (!ip)
	{
		sprintf(detail, "%s: the name does not resolve", host);
		warn("STUN name", detail);
		return;
	}
	memset(request, 0, sizeof(request));
	request[1] = 1;
	request[4] = 0x21; request[5] = 0x12; request[6] = 0xA4; request[7] = 0x42;
	posix_random_bytes(request + 8, 12);
	make_address(&address, ip, (unsigned short)port);
	for (tries = 0; tries < 3; tries++)
	{
		posix_socket_sendto(mapping->socket, request, sizeof(request), 0, &address, sizeof(address));
		while (wait_for(mapping->socket, 0, tries == 2 ? 2 : 1))
		{
			int from_length = sizeof(from), size = posix_socket_recvfrom(mapping->socket, packet, sizeof(packet), 0,
				&from, &from_length), offset;

			if (size < 20 || packet[0] != 1 || packet[1] != 1 || memcmp(packet + 8, request + 8, 12))
				continue;
			for (offset = 20; offset + 4 <= size; )
			{
				int type = packet[offset] << 8 | packet[offset + 1], length = packet[offset + 2] << 8 | packet[offset + 3];
				const unsigned char *value = packet + offset + 4;

				if (offset + 4 + length > size)
					break;
				if ((type == 0x0020 || type == 0x0001) && length >= 8 && value[1] == 1)
				{
					unsigned char ip_bytes[4], port_bytes[2];
					int index;

					memcpy(port_bytes, value + 2, 2);
					memcpy(ip_bytes, value + 4, 4);
					if (type == 0x0020)
					{
						for (index = 0; index < 2; index++)
							port_bytes[index] ^= packet[4 + index];
						for (index = 0; index < 4; index++)
							ip_bytes[index] ^= packet[4 + index];
					}
					mapping->ports[mapping->count] = (unsigned short)(port_bytes[0] << 8 | port_bytes[1]);
					memcpy(&mapping->addresses[mapping->count], ip_bytes, 4);
					sprintf(detail, "%s sees this machine as %d.%d.%d.%d:%d", host, ip_bytes[0], ip_bytes[1],
						ip_bytes[2], ip_bytes[3], mapping->ports[mapping->count]);
					mapping->count++;
					pass_or_fail("STUN", 1, detail);
					return;
				}
				offset += 4 + ((length + 3) & ~3);
			}
		}
	}
	sprintf(detail, "%s: no answer (UDP out blocked, or no route)", host);
	warn("STUN", detail);
}

static void test_network(void)
{
	char brokers[1024] = "", detail[256];
	struct stun_mapping mapping;
	FILE *file;
	int index;
	const char *stun = config_string("network.stun_servers");

	line("--- the network internet play needs");
	file = fopen("brokers.txt", "rb");
	if (file)
	{
		size_t size = fread(brokers, 1, sizeof(brokers) - 1, file);

		brokers[size] = 0;
		fclose(file);
		/* (comments out) */
		{
			char *cursor = brokers;
			int comment = 0;
			char cleaned[1024];
			int out = 0;

			for (; *cursor; cursor++)
			{
				if (*cursor == '#')
					comment = 1;
				if (*cursor == '\n' || *cursor == '\r')
				{
					comment = 0;
					cleaned[out++] = ',';
				}
				else if (!comment && out < (int)sizeof(cleaned) - 2)
					cleaned[out++] = *cursor;
			}
			cleaned[out] = 0;
			strcpy(brokers, cleaned);
		}
		result("info", "brokers.txt", "read beside halo.exe");
	}
	else
	{
		strcpy(brokers, "opence.milenko.org:1883,broker.emqx.io:1883,broker.hivemq.com:1883,test.mosquitto.org:1883");
		result("info", "brokers.txt", "not beside halo.exe: the game's own list is used");
	}
	each_item(brokers, test_broker, NULL);
	memset(&mapping, 0, sizeof(mapping));
	mapping.socket = posix_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (mapping.socket >= 0)
	{
		struct sockaddr_in any;

		make_address(&any, htonl(INADDR_ANY), 0);
		posix_socket_bind(mapping.socket, &any, sizeof(any));
		each_item(stun, test_stun, &mapping);
		posix_socket_close(mapping.socket);
	}
	if (mapping.count >= 2)
	{
		int same = 1;

		for (index = 1; index < mapping.count; index++)
			same &= mapping.ports[index] == mapping.ports[0];
		sprintf(detail, same ? "the same port to every server (%d): hole punching can work" :
			"a different port for each server: a symmetric NAT, which hole punching cannot cross; "
			"host or join with a router that forwards the port (UPnP on, or Direct Link by address)", mapping.ports[0]);
		if (same)
			pass_or_fail("NAT type", 1, detail);
		else
			warn("NAT type", detail);
	}
	else if (mapping.count == 1)
		warn("NAT type", "only one STUN server answered: it cannot be told");
	else
		warn("NAT type", "no STUN server answered: internet play cannot find this machine's address "
			"(Direct Link by address still works)");
}

/* (winmm's, looked up and declared here: the headers this unit is built with leave mmsystem out) */
struct pcm_format
{
	unsigned short tag, channels;
	unsigned long samples_per_second, bytes_per_second;
	unsigned short block_align, bits;
	unsigned short extra;
};
typedef unsigned (WINAPI *wave_count_function)(void);
typedef unsigned (WINAPI *wave_open_function)(void *handle, unsigned device, const struct pcm_format *format,
	unsigned long callback, unsigned long instance, unsigned long flags);

static void test_sound_and_display(void)
{
	static const unsigned long rates[] = { 11025, 22050, 32000, 44100, 48000 };
	static unsigned short ramp[3][256];
	int index;
	char detail[160];
	HDC context;
	DEVMODEA mode;
	int modes = 0, has_640 = 0, bits_max = 0;

	line("--- sound and display");
	{
		HMODULE winmm = LoadLibraryA("winmm.dll");
		wave_count_function count = winmm ? (wave_count_function)GetProcAddress(winmm, "waveOutGetNumDevs") : NULL;
		wave_open_function open = winmm ? (wave_open_function)GetProcAddress(winmm, "waveOutOpen") : NULL;

		sprintf(detail, "%u waveOut device(s)", count ? count() : 0);
		pass_or_fail("sound device", count && count() > 0, detail);
		for (index = 0; open && count && count() > 0 && index < 5; index++)
		{
			struct pcm_format format;
			unsigned status;

			memset(&format, 0, sizeof(format));
			format.tag = 1;
			format.channels = 2;
			format.samples_per_second = rates[index];
			format.bits = 16;
			format.block_align = 4;
			format.bytes_per_second = rates[index] * 4;
			/* (WAVE_MAPPER, WAVE_FORMAT_QUERY: asks without opening) */
			status = open(NULL, 0xFFFFFFFFu, &format, 0, 0, 1);
			sprintf(detail, "%lu Hz 16-bit stereo %s", rates[index], status == 0 ? "accepted" :
				"refused (the game falls back to a rate the card takes)");
			result("info", "sound format", detail);
		}
	}
	context = GetDC(NULL);
	if (context)
	{
		int ok = GetDeviceGammaRamp(context, ramp);

		sprintf(detail, ok ? "the display's gamma ramp can be read; Brightness will work" :
			"the display's gamma ramp cannot be read: Brightness will not change anything here (no harm)");
		if (ok)
			pass_or_fail("gamma ramp", SetDeviceGammaRamp(context, ramp), detail);
		else
			warn("gamma ramp", detail);
		sprintf(detail, "%d x %d, %d bits", GetDeviceCaps(context, HORZRES), GetDeviceCaps(context, VERTRES),
			GetDeviceCaps(context, BITSPIXEL));
		result("info", "desktop", detail);
		ReleaseDC(NULL, context);
	}
	memset(&mode, 0, sizeof(mode));
	mode.dmSize = sizeof(mode);
	for (index = 0; EnumDisplaySettingsA(NULL, index, &mode); index++)
	{
		modes++;
		if (mode.dmPelsWidth == 640 && mode.dmPelsHeight == 480)
			has_640 = 1;
		if ((int)mode.dmBitsPerPel > bits_max)
			bits_max = (int)mode.dmBitsPerPel;
	}
	sprintf(detail, "%d modes, up to %d bits; 640x480 %s", modes, bits_max, has_640 ? "present" : "ABSENT");
	pass_or_fail("display modes", modes > 0 && has_640, detail);
}

static void test_files(void)
{
	static const char *const wanted[] = { "halo.exe", "config.toml", "brokers.txt", "HALO.log", "BENCH.TXT" };
	int index;

	line("--- files beside halo.exe");
	for (index = 0; index < 5; index++)
	{
		DWORD attributes = GetFileAttributesA(wanted[index]);

		result(attributes != 0xFFFFFFFFUL ? "info" : "info", wanted[index],
			attributes != 0xFFFFFFFFUL ? "present" : "absent");
	}
}

/* ---------- public code */

int win9x_selftest_run(void)
{
	char summary[256];
	DWORD version = GetVersion();
	MEMORYSTATUS memory;

	output = fopen("SELFTEST.TXT", "wb");
	if (!output)
	{
		MessageBoxA(NULL, "SELFTEST.TXT cannot be written here (a read-only folder, such as a CD?).",
			"Halo self test", MB_OK | MB_ICONERROR);
		return 1;
	}
	memory.dwLength = sizeof(memory);
	GlobalMemoryStatus(&memory);
	line("Halo self test (halo.exe --selftest)");
	line("Windows %lu.%lu build %lu, %lu MB of memory", (unsigned long)(version & 255), (unsigned long)((version >> 8) & 255),
		(unsigned long)(version >> 16 & 0x7FFF), (unsigned long)(memory.dwTotalPhys >> 20));
	line("--- the numbers internet play seals its tunnel with");
	failures += p2p_selftest(crypto_report, NULL);
	test_clocks();
	test_loopback();
	test_network();
	test_sound_and_display();
	test_files();
	line("--- done");
	sprintf(summary, "%d failed, %d warnings", failures, warnings);
	line("%s", summary);
	fclose(output);
	output = NULL;
	{
		char message[400];

		sprintf(message, "The self test is done: %s.\n\nSELFTEST.TXT is beside halo.exe; send it back with HALO.log.",
			summary);
		MessageBoxA(NULL, message, "Halo self test", MB_OK | (failures ? MB_ICONWARNING : MB_ICONINFORMATION));
	}
	return failures != 0;
}
