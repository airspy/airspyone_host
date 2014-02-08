/*
 * Copyright 2012 Jared Boone <jared@sharebrained.com>
 * Copyright 2013 Michael Ossmann <mike@ossmann.com>
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

int main(int argc, char** argv)
{
	struct airspy_device* device = NULL;
	int result = AIRSPY_SUCCESS;
	uint8_t board_id = AIRSPY_BOARD_ID_INVALID;
	char version[255 + 1];
	airspy_read_partid_serialno_t read_partid_serialno;

	result = airspy_init();
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_init() failed: %s (%d)\n",
				airspy_error_name(result), result);
		return EXIT_FAILURE;
	}

	result = airspy_open(&device);
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_open() failed: %s (%d)\n",
				airspy_error_name(result), result);
		return EXIT_FAILURE;
	}

	printf("Found AirSpy board.\n");

	result = airspy_board_id_read(device, &board_id);
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_board_id_read() failed: %s (%d)\n",
				airspy_error_name(result), result);
		return EXIT_FAILURE;
	}
	printf("Board ID Number: %d (%s)\n", board_id,
			airspy_board_id_name(board_id));

	result = airspy_version_string_read(device, &version[0], 255);
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_version_string_read() failed: %s (%d)\n",
				airspy_error_name(result), result);
		return EXIT_FAILURE;
	}
	printf("Firmware Version: %s\n", version);

	result = airspy_board_partid_serialno_read(device, &read_partid_serialno);	
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_board_partid_serialno_read() failed: %s (%d)\n",
				airspy_error_name(result), result);
		return EXIT_FAILURE;
	}
	printf("Part ID Number: 0x%08X 0x%08X\n", 
				read_partid_serialno.part_id[0],
				read_partid_serialno.part_id[1]);
	printf("Serial Number: 0x%08X 0x%08X 0x%08X 0x%08X\n", 
				read_partid_serialno.serial_no[0],
				read_partid_serialno.serial_no[1],
				read_partid_serialno.serial_no[2],
				read_partid_serialno.serial_no[3]);
	
	result = airspy_close(device);
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_close() failed: %s (%d)\n",
				airspy_error_name(result), result);
		return EXIT_FAILURE;
	}

	airspy_exit();

	return EXIT_SUCCESS;
}
