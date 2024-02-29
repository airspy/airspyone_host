/*
 * Copyright 2024 Benjamin Vernoux <bvernoux@gmail.com>
 *
 * This file is part of AirSpy.
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
#include <sys/types.h>
#include <time.h>

#ifndef bool
typedef int bool;
#define true 1
#define false 0
#endif

#define AIRSPY_FLASH_CALIB_OFFSET (0x20000) /* After 128KB (Reserved for Firmware + 64KB Spare) */
#define AIRSPY_FLASH_CALIB_HEADER (0xCA1B0001)

typedef struct
{
	uint32_t header; /* Shall be equal to AIRSPY_FLASH_CALIB_HEADER */
	uint32_t timestamp; /* Epoch Unix Time Stamp */
	int32_t correction_ppb;
} airspy_calib_t;

static void usage()
{
	printf("Usage:\n");
	printf("\t-r: Read and display calibration data.\n");
	printf("\t-w <calibration in ppb>: Erase and Write calibration in ppb.\n");
}

int main(int argc, char **argv)
{
	int opt;
	struct airspy_device* device = NULL;
	int result;
	bool read = false;
	bool write = false;
	int32_t calibration_ppb = 0;
	airspy_calib_t calib;

	while ((opt = getopt(argc, argv, "rw:")) != EOF)
	{
		switch (opt) {
		case 'r':
			read = true;
			break;

		case 'w':
			write = true;
			calibration_ppb = atoi(optarg);
			break;

		default:
			fprintf(stderr, "opt error: %d\n", opt);
			usage();
			return EXIT_FAILURE;
		}
	}

	if (write == read) {
		if (write == true) {
			fprintf(stderr, "Read and write options are mutually exclusive.\n");
		} else {
			fprintf(stderr, "Specify either read or write option.\n");
		}
		usage();
		return EXIT_FAILURE;
	}

	result = airspy_init();
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_init() failed: %s (%d)\n", airspy_error_name(result), result);
		return EXIT_FAILURE;
	}

	result = airspy_open(&device);
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "Failed to open airspy device.\n");
		return 1;
	}

	if (read) {
		printf("Reading %d bytes from 0x%06x.\n", (int)sizeof(calib), AIRSPY_FLASH_CALIB_OFFSET);
		result = airspy_spiflash_read(device, AIRSPY_FLASH_CALIB_OFFSET, (int)sizeof(calib), (uint8_t *)&calib);
		if (result != AIRSPY_SUCCESS) {
			fprintf(stderr, "airspy_spiflash_read() failed: %s (%d)\n", airspy_error_name(result), result);
			return EXIT_FAILURE;
		}
		time_t epoch_time = calib.timestamp;
		struct tm *local_time = localtime(&epoch_time);
		printf("Calibration timestamp: %04d/%02d/%02d %02d:%02d:%02d\nCalibration correction in ppb: %d\n", 
				local_time->tm_year + 1900,
				local_time->tm_mon + 1,
				local_time->tm_mday,
				local_time->tm_hour,
				local_time->tm_min,
				local_time->tm_sec,
				calib.correction_ppb);
	}
	if(write) {
		printf("Erasing sector 2 (calibration) in SPI flash.\n");
		result = airspy_spiflash_erase_sector(device, 2);
		if (result != AIRSPY_SUCCESS) {
			fprintf(stderr, "Failed to erase sector 2.\n");
			return 1;
		}

		calib.header = AIRSPY_FLASH_CALIB_HEADER;
		calib.timestamp = (uint32_t)time(NULL);
		calib.correction_ppb = calibration_ppb;

		printf("Writing calibration %d bytes at 0x%06x.\n", (int)sizeof(calib), AIRSPY_FLASH_CALIB_OFFSET);
		time_t epoch_time = calib.timestamp;
		struct tm *local_time = localtime(&epoch_time);
		printf("Calibration timestamp: %04d/%02d/%02d %02d:%02d:%02d\nCalibration correction in ppb: %d\n", 
				local_time->tm_year + 1900,
				local_time->tm_mon + 1,
				local_time->tm_mday,
				local_time->tm_hour,
				local_time->tm_min,
				local_time->tm_sec,
				calib.correction_ppb);
		result = airspy_spiflash_write(device, AIRSPY_FLASH_CALIB_OFFSET, sizeof(calib), (uint8_t *)&calib);
		if (result != AIRSPY_SUCCESS) {
			fprintf(stderr, "Failed to write calibration data.\n");
			return 1;
		}
	}

	result = airspy_close(device);
	if (result != AIRSPY_SUCCESS) {
		fprintf(stderr, "airspy_close() failed: %s (%d)\n", airspy_error_name(result), result);
		return EXIT_FAILURE;
	}

	airspy_exit();

	return EXIT_SUCCESS;
}

