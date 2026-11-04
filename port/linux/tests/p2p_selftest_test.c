/* p2p_selftest.c on the host (run by p2p_selftest_test.py) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "posix.h"
#include "p2p_internal.h"

void posix_random_bytes(void *buffer, posix_ulong size)
{
	FILE *f = fopen("/dev/urandom", "rb");

	if (!f || fread(buffer, 1, size, f) != size)
		abort();
	fclose(f);
}

static void report(const char *name, int passed, void *context)
{
	(void)context;
	printf("%s: %s\n", passed ? "pass" : "FAIL", name);
}

int main(void)
{
	int failures = p2p_selftest(report, NULL);

	puts(failures ? "FAILED" : "ok");
	return failures != 0;
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int i;

	for (i = 0; i < size; i++)
		sprintf(text + 2 * i, "%02x", bytes[i]);
}
