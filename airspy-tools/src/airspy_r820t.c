/*
 * Copyright 2013/2014 Benjamin Vernoux <bvernoux@airspy.com>
 *
 * This file is part of AirSpy (based on HackRF project).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include <airspy.h>

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

static void usage() {
	printf("\nUsage:\n");
	printf("\t-n, --register <n>: set register number for subsequent read/write operations\n");
	printf("\t-r, --read: read register specified by last -n argument, or all registers\n");
	printf("\t-w, --write <v>: write register specified by last -n argument with value <v>\n");
	printf("\t-c, --config: configure registers to r820t default mode for test\n");
	printf("\nExamples:\n");
	printf("\t<command> -n 12 -r    # reads from register 12\n");
	printf("\t<command> -r          # reads all registers\n");
	printf("\t<command> -n 10 -w 22 # writes register 10 with 22 decimal\n");
}

static struct option long_options[] = {
	{ "register", required_argument, 0, 'n' },
	{ "write", required_argument, 0, 'w' },
	{ "read", no_argument, 0, 'r' },
	{ "config", no_argument, 0, 'c' },
	{ 0, 0, 0, 0 },
};

int parse_int(char* const s, uint8_t* const value) {
	char* s_end = s;
	const long long_value = strtol(s, &s_end, 10);
	if( (s != s_end) && (*s_end == 0) ) {
		*value = (uint8_t)long_value;
		return AIRSPY_SUCCESS;
	} else {
		return AIRSPY_ERROR_INVALID_PARAM;
	}
}

int dump_register(struct airspy_device* device, const uint8_t register_number)
{
	uint8_t register_value;
	int result = airspy_r820t_read(device, register_number, &register_value);

	if( result == AIRSPY_SUCCESS ) {
		printf("[%3d] -> 0x%02X\n", register_number, register_value);
	} else {
		printf("airspy_r820t_read() failed: %s (%d)\n", airspy_error_name(result), result);
	}

	return result;
}

int dump_registers(struct airspy_device* device)
{
	uint8_t register_number;
	int result = AIRSPY_SUCCESS;

	for(register_number=0; register_number<32; register_number++)
	{
		result = dump_register(device, register_number);
		if( result != AIRSPY_SUCCESS ) {
			break;
		}
	}

	return result;
}

int write_register(struct airspy_device* device, const uint8_t register_number, const uint8_t register_value)
{
	int result = AIRSPY_SUCCESS;
	result = airspy_r820t_write(device, register_number, register_value);

	if( result == AIRSPY_SUCCESS ) {
		printf("0x%02X -> [%3d]\n", register_value, register_number);
	} else {
		printf("airspy_r820t_write() failed: %s (%d)\n", airspy_error_name(result), result);
	}

	return result;
}

#define CONF_R820T_START_REG  (5)
uint8_t conf_r820t[] =
{
	0x12, 0x32, 0x75,       /* 05 to 07 */
	0xc0, 0x40, 0xd6, 0x6c, /* 08 to 11 */
	0x40, 0x63, 0x75, 0x68, /* 12 to 15 */
	0x6c, 0x83, 0x80, 0x00, /* 16 to 19 */
	0x0f, 0x00, 0xc0, 0x30, /* 20 to 23 */
	0x48, 0xcc, 0x60, 0x00, /* 24 to 27 */
	0x54, 0xae, 0x4a, 0xc0  /* 28 to 31 */
};

int configure_registers(struct airspy_device* device)
{
	int i, j;
	uint8_t register_number;
	uint8_t register_value;
	int result = AIRSPY_SUCCESS;

	j=0;
	for(i=0; i<sizeof(conf_r820t); i++)
	{
		register_number = i + CONF_R820T_START_REG;
		register_value = conf_r820t[j];
		j++;
		result = airspy_r820t_write(device, register_number, register_value);
		if( result == AIRSPY_SUCCESS )
		{
			printf("0x%02X -> [%3d]\n", register_value, register_number);
		} else {
			printf("airspy_r820t_write() failed: %s (%d)\n", airspy_error_name(result), result);
			return result;
		}
	}
	return result;
}

#define REGISTER_INVALID 255

int main(int argc, char** argv) {
	int opt;
	uint8_t register_number = REGISTER_INVALID;
	uint8_t register_value;
	struct airspy_device* device = NULL;
	int option_index = 0;

	int result = airspy_init();
	if( result ) {
		printf("airspy_init() failed: %s (%d)\n", airspy_error_name(result), result);
		return -1;
	}

	result = airspy_open(&device);
	if( result ) {
		printf("airspy_open() failed: %s (%d)\n", airspy_error_name(result), result);
		return -1;
	}

	while( (opt = getopt_long(argc, argv, "cn:rw:", long_options, &option_index)) != EOF ) {
		switch( opt ) {
		case 'n':
			result = parse_int(optarg, &register_number);
			break;

		case 'w':
			result = parse_int(optarg, &register_value);
			if( result == AIRSPY_SUCCESS ) {
				result = write_register(device, register_number, register_value);
			}
			break;

		case 'r':
			if( register_number == REGISTER_INVALID ) {
				result = dump_registers(device);
			} else {
				result = dump_register(device, register_number);
			}
			break;

		case 'c':
				result = configure_registers(device);
			break;

		default:
			usage();
		}

		if( result != AIRSPY_SUCCESS )
		{
			printf("argument error: %s (%d)\n", airspy_error_name(result), result);
			break;
		}
	}

	result = airspy_close(device);
	if( result ) {
		printf("airspy_close() failed: %s (%d)\n", airspy_error_name(result), result);
		return -1;
	}

	airspy_exit();

	return 0;
}

