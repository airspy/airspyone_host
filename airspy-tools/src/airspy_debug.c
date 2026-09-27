#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <airspy.h>

static uint32_t num(const char* s) { return (uint32_t)strtoul(s, NULL, 0); }

static int usage(void)
{
	fprintf(stderr, "usage: airspy_debug read <addr> [words] | bytes <addr> [count] | write <addr> <word>... | call <core> <addr> [r0 r1 r2 r3]\n");
	return 1;
}

int main(int argc, char** argv)
{
	struct airspy_device* device = NULL;
	int result, ret = 0;

	if (argc < 3)
		return usage();
	result = airspy_init();
	if (result != AIRSPY_SUCCESS) { fprintf(stderr, "airspy_init() failed: %s\n", airspy_error_name(result)); return 1; }
	result = airspy_open(&device);
	if (result != AIRSPY_SUCCESS) { fprintf(stderr, "airspy_open() failed: %s\n", airspy_error_name(result)); return 1; }

	if (strcmp(argv[1], "read") == 0 || strcmp(argv[1], "bytes") == 0)
	{
		int words = strcmp(argv[1], "read") == 0;
		uint32_t addr = num(argv[2]);
		uint32_t count = argc > 3 ? num(argv[3]) : 1;
		uint32_t total = words ? count * 4 : count;
		uint32_t done = 0;
		uint8_t buf[64];
		while (done < total)
		{
			uint32_t n = total - done > 64 ? 64 : total - done;
			result = airspy_mem_read(device, addr + done, buf, (uint16_t)n);
			if (result != AIRSPY_SUCCESS) { fprintf(stderr, "read at 0x%08x failed: %s\n", addr + done, airspy_error_name(result)); ret = 1; break; }
			if (words)
			{
				uint32_t i;
				for (i = 0; i < n; i += 4)
				{
					uint32_t w; memcpy(&w, &buf[i], 4);
					if (((done + i) % 16) == 0) printf("%s%08x:", (done + i) ? "\n" : "", addr + done + i);
					printf(" %08x", w);
				}
			}
			else
			{
				uint32_t i;
				for (i = 0; i < n; i++)
				{
					if (((done + i) % 16) == 0) printf("%s%08x:", (done + i) ? "\n" : "", addr + done + i);
					printf(" %02x", buf[i]);
				}
			}
			done += n;
		}
		printf("\n");
	}
	else if (strcmp(argv[1], "write") == 0)
	{
		uint32_t addr = num(argv[2]);
		int i;
		for (i = 3; i < argc; i++)
		{
			uint32_t w = num(argv[i]);
			result = airspy_mem_write(device, addr, &w, 4);
			if (result != AIRSPY_SUCCESS) { fprintf(stderr, "write at 0x%08x failed: %s\n", addr, airspy_error_name(result)); ret = 1; break; }
			addr += 4;
		}
	}
	else if (strcmp(argv[1], "call") == 0 && argc >= 4)
	{
		uint32_t args[4] = { 0, 0, 0, 0 };
		uint32_t r0 = 0;
		int i;
		for (i = 0; i < 4 && argc > 4 + i; i++)
			args[i] = num(argv[4 + i]);
		if (num(argv[3]) < 0x1000) { fprintf(stderr, "refusing to call address 0x%08x\n", num(argv[3])); airspy_close(device); airspy_exit(); return 1; }
		result = airspy_call(device, num(argv[2]), num(argv[3]), args, &r0, 2000);
		if (result != AIRSPY_SUCCESS) { fprintf(stderr, "call failed: %s\n", airspy_error_name(result)); ret = 1; }
		else printf("r0 = 0x%08x (%u)\n", r0, r0);
	}
	else
		ret = usage();

	airspy_close(device);
	airspy_exit();
	return ret;
}
