/*
 * Copyright 2012 Jared Boone <jared@sharebrained.com>
 * Copyright 2014 Benjamin Vernoux <bvernoux@airspy.com>
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
#include <string.h>
#include <getopt.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#ifndef bool
typedef int bool;
#define true 1
#define false 0
#endif

#ifdef _WIN32
#include <windows.h>

#ifdef _MSC_VER

#ifdef _WIN64
typedef int64_t ssize_t;
#else
typedef int32_t ssize_t;
#endif

#define strtoull _strtoui64
#define snprintf _snprintf

int gettimeofday(struct timeval *tv, void* ignored)
{
	FILETIME ft;
	unsigned __int64 tmp = 0;
	if (NULL != tv) {
		GetSystemTimeAsFileTime(&ft);
		tmp |= ft.dwHighDateTime;
		tmp <<= 32;
		tmp |= ft.dwLowDateTime;
		tmp /= 10;
		tmp -= 11644473600000000Ui64;
		tv->tv_sec = (long)(tmp / 1000000UL);
		tv->tv_usec = (long)(tmp % 1000000UL);
	}
	return 0;
}

#endif
#endif

#if defined(__GNUC__)
#include <unistd.h>
#include <sys/time.h>
#endif

#include <signal.h>

#define FD_BUFFER_SIZE (8*1024)

#define FREQ_ONE_MHZ (1000000ul)

#define DEFAULT_FREQ_HZ (900000000ul) /* 900MHz */
#define FREQ_MIN_HZ	  (24000000ul) /* 24MHz */
#define FREQ_MAX_HZ	(1750000000ul) /* 1750MHz */

#define SAMPLES_TO_XFER_MAX (0x8000000000000000ull) /* Max value */

#if defined _WIN32
	#define sleep(a) Sleep( (a*1000) )
#endif

#define DEFAULT_SAMPLE_RATE_HZ (10000000) /* 10MHz airspy sample rate */

/* WAVE or RIFF WAVE file format containing IQ 2x8bits data for AirSpy compatible with SDR# Wav IQ file */
typedef struct 
{
		char groupID[4]; /* 'RIFF' */
		uint32_t size; /* File size + 8bytes */
		char riffType[4]; /* 'WAVE'*/
} t_WAVRIFF_hdr;

#define FormatID "fmt "   /* chunkID for Format Chunk. NOTE: There is a space at the end of this ID. */

typedef struct {
	char		chunkID[4]; /* 'fmt ' */
	uint32_t	chunkSize; /* 16 fixed */

	uint16_t	wFormatTag; /* 1 fixed */
	uint16_t	wChannels;  /* 2 fixed */
	uint32_t	dwSamplesPerSec; /* Freq Hz sampling */
	uint32_t	dwAvgBytesPerSec; /* Freq Hz sampling x 2 */
	uint16_t	wBlockAlign; /* 2 fixed */
	uint16_t	wBitsPerSample; /* 16 fixed */
} t_FormatChunk;

typedef struct 
{
		char		chunkID[4]; /* 'data' */
		uint32_t	chunkSize; /* Size of data in bytes */
	/* Samples I(8bits) then Q(8bits), I, Q ... */
} t_DataChunk;

typedef struct
{
	t_WAVRIFF_hdr hdr;
	t_FormatChunk fmt_chunk;
	t_DataChunk data_chunk;
} t_wav_file_hdr;

t_wav_file_hdr wave_file_hdr = 
{
	/* t_WAVRIFF_hdr */
	{
		{ 'R', 'I', 'F', 'F' }, /* groupID */
		0, /* size to update later */
		{ 'W', 'A', 'V', 'E' }
	},
	/* t_FormatChunk */
	{
		{ 'f', 'm', 't', ' ' }, /* char		chunkID[4];  */
		16, /* uint32_t	chunkSize; */
		1, /* uint16_t	wFormatTag; 1 fixed */
		2, /* uint16_t	wChannels; 2 fixed */
		0, /* uint32_t	dwSamplesPerSec; Freq Hz sampling to update later */
		0, /* uint32_t	dwAvgBytesPerSec; Freq Hz sampling x 2 to update later */
		4, /* uint16_t	wBlockAlign; 4 fixed */
		16, /* uint16_t	wBitsPerSample; 16 fixed */
	},
	/* t_DataChunk */
	{
	    { 'd', 'a', 't', 'a' }, /* char chunkID[4]; */
		0, /* uint32_t	chunkSize; to update later */
	}
};

#define U64TOA_MAX_DIGIT (31)
typedef struct 
{
		char data[U64TOA_MAX_DIGIT+1];
} t_u64toa;

receiver_mode_t receiver_mode = RECEIVER_MODE_RX;

unsigned int vga_gain=0;
unsigned int lna_gain=8;
unsigned int mixer_gain=8;

static float
TimevalDiff(const struct timeval *a, const struct timeval *b)
{
	 return (a->tv_sec - b->tv_sec) + 1e-6f * (a->tv_usec - b->tv_usec);
}

int parse_u64(char* s, uint64_t* const value) {
	uint_fast8_t base = 10;
	char* s_end;
	uint64_t u64_value;

	if( strlen(s) > 2 ) {
		if( s[0] == '0' ) {
			if( (s[1] == 'x') || (s[1] == 'X') ) {
				base = 16;
				s += 2;
			} else if( (s[1] == 'b') || (s[1] == 'B') ) {
				base = 2;
				s += 2;
			}
		}
	}

	s_end = s;
	u64_value = strtoull(s, &s_end, base);
	if( (s != s_end) && (*s_end == 0) ) {
		*value = u64_value;
		return AIRSPY_SUCCESS;
	} else {
		return AIRSPY_ERROR_INVALID_PARAM;
	}
}

int parse_u32(char* s, uint32_t* const value) {
	uint_fast8_t base = 10;
	char* s_end;
	uint64_t ulong_value;

	if( strlen(s) > 2 ) {
		if( s[0] == '0' ) {
			if( (s[1] == 'x') || (s[1] == 'X') ) {
				base = 16;
				s += 2;
			} else if( (s[1] == 'b') || (s[1] == 'B') ) {
				base = 2;
				s += 2;
			}
		}
	}

	s_end = s;
	ulong_value = strtoul(s, &s_end, base);
	if( (s != s_end) && (*s_end == 0) ) {
		*value = (uint32_t)ulong_value;
		return AIRSPY_SUCCESS;
	} else {
		return AIRSPY_ERROR_INVALID_PARAM;
	}
}

static char *stringrev(char *str)
{
	char *p1, *p2;

	if(! str || ! *str)
		return str;

	for(p1 = str, p2 = str + strlen(str) - 1; p2 > p1; ++p1, --p2)
	{
		*p1 ^= *p2;
		*p2 ^= *p1;
		*p1 ^= *p2;
	}
	return str;
}

char* u64toa(uint64_t val, t_u64toa* str)
{
	#define BASE (10ull) /* Base10 by default */
	uint64_t sum;
	int pos;
	int digit;
	int max_len;
	char* res;

	sum = val;
	max_len = U64TOA_MAX_DIGIT;
	pos = 0;

	do
	{
		digit = (sum % BASE);
		str->data[pos] = digit + '0';
		pos++;

		sum /= BASE;
	}while( (sum>0) && (pos < max_len) );

	if( (pos == max_len) && (sum>0) )
		return NULL;

	str->data[pos] = '\0';
	res = stringrev(str->data);

	return res;
}

volatile bool do_exit = false;

FILE* fd = NULL;
volatile uint32_t byte_count = 0;

bool receive = false;
bool receive_wav = false;

struct timeval time_start;
struct timeval t_start;
	
bool freq = false;
uint64_t freq_hz;

bool limit_num_samples = false;
uint64_t samples_to_xfer = 0;
uint64_t bytes_to_xfer = 0;

#define RX_BUFFER_SIZE (32*1024*1024)
uint16_t rx_buffer[RX_BUFFER_SIZE];
#define SAMPLE_SCALE_FLOAT_TO_INT ( (8192.0f) )

enum airspy_sample_type sample_type = AIRSPY_SAMPLE_INT16_IQ;

int rx_callback(airspy_transfer_t* transfer)
{
	int i;
	uint32_t bytes_to_write;
	uint32_t nb_data;
	uint16_t* rx_samples_u16;

	if( fd != NULL ) 
	{
		ssize_t bytes_written;

		switch(sample_type)
		{
			case AIRSPY_SAMPLE_INT16_IQ:
				/* Data are set to AIRSPY_SAMPLE_INT16_IQ (16bits Fixed IQ) */
				nb_data = transfer->sample_count*2;
				/* Check for safety to avoid buffer overflow */
				if(nb_data >= RX_BUFFER_SIZE)
				{
					nb_data = RX_BUFFER_SIZE;
				}

				rx_samples_u16 = (uint16_t*)(transfer->samples);
				for(i=0; i<nb_data; i++)
				{
					rx_buffer[i] = rx_samples_u16[i];
				}

				byte_count += nb_data*(sizeof(uint16_t));
				bytes_to_write = nb_data*(sizeof(uint16_t));
			break;

			default:
				bytes_to_write = 0;
			break;
		}

		if (limit_num_samples) {
			if (bytes_to_write >= bytes_to_xfer) {
				bytes_to_write = (int)bytes_to_xfer;
			}
			bytes_to_xfer -= bytes_to_write;
		}

		bytes_written = fwrite(rx_buffer, 1, bytes_to_write, fd);
		if ((bytes_written != bytes_to_write)
				|| (limit_num_samples && (bytes_to_xfer == 0))) {
			return -1;
		} else {
			return 0;
		}
	} else {
		return -1;
	}
}

#define MAX_VGA_GAIN (15)
#define MAX_MIXER_GAIN (15)
#define MAX_LNA_GAIN (14)

static void usage() {
	printf("Usage:\n");
	printf("\t-r <filename> # Receive data into file.\n");
	printf("\t-w # Receive data into file with WAV header and automatic name.\n");
	printf("\t   # This is for SDR# compatibility and may not work with other software.\n");
	printf("\t[-f set_freq_hz] # Set Freq in Hz between [%luMHz, %luMHz].\n", FREQ_MIN_HZ/FREQ_ONE_MHZ, FREQ_MAX_HZ/FREQ_ONE_MHZ);
	printf("\t[-v gain] # Set VGA gain, 0-%d (default %d)\n", MAX_VGA_GAIN, vga_gain);
	printf("\t[-m gain] # Set Mixer gain, 0-%d (default %d)\n", MAX_MIXER_GAIN, mixer_gain);
	printf("\t[-l gain] # Set LNA gain, 0-%d (default %d)\n", MAX_LNA_GAIN, lna_gain);
	printf("\t[-n num_samples] # Number of samples to transfer (default is unlimited).\n");
}

struct airspy_device* device = NULL;

#ifdef _MSC_VER
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stdout, "Caught signal %d\n", signum);
		do_exit = true;
		return TRUE;
	}
	return FALSE;
}
#else
void sigint_callback_handler(int signum) 
{
	fprintf(stdout, "Caught signal %d\n", signum);
	do_exit = true;
}
#endif

#define PATH_FILE_MAX_LEN (FILENAME_MAX)
#define DATE_TIME_MAX_LEN (32)

int main(int argc, char** argv)
{
	int opt;
	char path_file[PATH_FILE_MAX_LEN];
	char date_time[DATE_TIME_MAX_LEN];
	t_u64toa ascii_u64_data1;
	t_u64toa ascii_u64_data2;
	const char* path = NULL;
	int result;
	time_t rawtime;
	struct tm * timeinfo;
	uint32_t file_pos;
	int exit_code = EXIT_SUCCESS;
	struct timeval t_end;
	float time_diff;

	while( (opt = getopt(argc, argv, "wr:f:n:v:m:l:")) != EOF )
	{
		result = AIRSPY_SUCCESS;
		switch( opt ) 
		{
		case 'w':
			receive_wav = true;
			break;

		case 'r':
			receive = true;
			path = optarg;
			break;

		case 'f':
			freq = true;
			result = parse_u64(optarg, &freq_hz);
			break;

		case 'v':
			result = parse_u32(optarg, &vga_gain);
			break;

		case 'l':
			result = parse_u32(optarg, &lna_gain);
			break;

		case 'm':
			result = parse_u32(optarg, &mixer_gain);
			break;

		case 'n':
			limit_num_samples = true;
			result = parse_u64(optarg, &samples_to_xfer);
			bytes_to_xfer = samples_to_xfer * 2;
			break;

		default:
			printf("unknown argument '-%c %s'\n", opt, optarg);
			usage();
			return EXIT_FAILURE;
		}
		
		if( result != AIRSPY_SUCCESS ) {
			printf("argument error: '-%c %s' %s (%d)\n", opt, optarg, airspy_error_name(result), result);
			usage();
			return EXIT_FAILURE;
		}		
	}

	if (samples_to_xfer >= SAMPLES_TO_XFER_MAX) {
		printf("argument error: num_samples must be less than %s/%sMio\n",
				u64toa(SAMPLES_TO_XFER_MAX, &ascii_u64_data1), u64toa(SAMPLES_TO_XFER_MAX/(FREQ_ONE_MHZ), &ascii_u64_data2) );
		usage();
		return EXIT_FAILURE;
	}

	if( freq ) {
		if( (freq_hz >= FREQ_MAX_HZ) || (freq_hz < FREQ_MIN_HZ) )
		{
			printf("argument error: set_freq_hz shall be between [%s, %s[.\n", u64toa(FREQ_MIN_HZ, &ascii_u64_data1), u64toa(FREQ_MAX_HZ, &ascii_u64_data2));
			usage();
			return EXIT_FAILURE;
		}
	}else
	{
		/* Use default freq */
		freq_hz = DEFAULT_FREQ_HZ;
	}

	receiver_mode = RECEIVER_MODE_RX;

	if( receive_wav ) 
	{
		time (&rawtime);
		timeinfo = localtime (&rawtime);
		receiver_mode = RECEIVER_MODE_RX;
		/* File format AirSpy Year(2013), Month(11), Day(28), Hour Min Sec+Z, Freq kHz, IQ.wav */
		strftime(date_time, DATE_TIME_MAX_LEN, "%Y%m%d_%H%M%S", timeinfo);
		snprintf(path_file, PATH_FILE_MAX_LEN, "AirSpy_%sZ_%ukHz_IQ.wav", date_time, (uint32_t)(freq_hz/(1000ull)) );
		path = path_file;
		printf("Receive wav file: %s\n", path);
	}	

	if( path == NULL ) {
		printf("specify a path to a file to receive\n");
		usage();
		return EXIT_FAILURE;
	}
	
	if(vga_gain > MAX_VGA_GAIN) {
		printf("vga_gain out of range\n");
		usage();
		return EXIT_FAILURE;
	}

	if(mixer_gain > MAX_MIXER_GAIN) {
		printf("mixer_gain out of range\n");
		usage();
		return EXIT_FAILURE;
	}

	if(lna_gain > MAX_LNA_GAIN) {
		printf("lna_gain out of range\n");
		usage();
		return EXIT_FAILURE;
	}

	result = airspy_init();
	if( result != AIRSPY_SUCCESS ) {
		printf("airspy_init() failed: %s (%d)\n", airspy_error_name(result), result);
		usage();
		return EXIT_FAILURE;
	}

	result = airspy_open(&device);
	if( result != AIRSPY_SUCCESS ) {
		printf("airspy_open() failed: %s (%d)\n", airspy_error_name(result), result);
		usage();
		return EXIT_FAILURE;
	}
	
	result = airspy_set_sample_type(device, sample_type);
	if( result != AIRSPY_SUCCESS ) {
		printf("airspy_open() failed: %s (%d)\n", airspy_error_name(result), result);
		usage();
		return EXIT_FAILURE;
	}

	fd = fopen(path, "wb");

	if( fd == NULL ) {
		printf("Failed to open file: %s\n", path);
		return EXIT_FAILURE;
	}
	/* Change fd buffer to have bigger one to store or read data on/to HDD */
	result = setvbuf(fd , NULL , _IOFBF , FD_BUFFER_SIZE);
	if( result != 0 ) {
		printf("setvbuf() failed: %d\n", result);
		usage();
		return EXIT_FAILURE;
	}
	
	/* Write Wav header */
	if( receive_wav ) 
	{
		fwrite(&wave_file_hdr, 1, sizeof(t_wav_file_hdr), fd);
	}
	
#ifdef _MSC_VER
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#else
	signal(SIGINT, &sigint_callback_handler);
	signal(SIGILL, &sigint_callback_handler);
	signal(SIGFPE, &sigint_callback_handler);
	signal(SIGSEGV, &sigint_callback_handler);
	signal(SIGTERM, &sigint_callback_handler);
	signal(SIGABRT, &sigint_callback_handler);
#endif

	printf("call airspy_set_vga_gain(%u)\n", vga_gain);
	result = airspy_set_vga_gain(device, vga_gain);
	if( result != AIRSPY_SUCCESS ) {
		printf("airspy_set_vga_gain() failed: %s (%d)\n", airspy_error_name(result), result);
		//usage();
		//return EXIT_FAILURE;
	}

	printf("call airspy_set_mixer_gain(%u)\n", mixer_gain);
	result = airspy_set_mixer_gain(device, mixer_gain);
	if( result != AIRSPY_SUCCESS ) {
		printf("airspy_set_mixer_gain() failed: %s (%d)\n", airspy_error_name(result), result);
		//usage();
		//return EXIT_FAILURE;
	}

	printf("call airspy_set_lna_gain(%u)\n", lna_gain);
	result = airspy_set_lna_gain(device, lna_gain);
	if( result != AIRSPY_SUCCESS ) {
		printf("airspy_set_lna_gain() failed: %s (%d)\n", airspy_error_name(result), result);
		//usage();
		//return EXIT_FAILURE;
	}

	result = airspy_start_rx(device, rx_callback, NULL);
	if( result != AIRSPY_SUCCESS ) {
		printf("airspy_start_rx() failed: %s (%d)\n", airspy_error_name(result), result);
		usage();
		return EXIT_FAILURE;
	}

	printf("call airspy_set_freq(%s Hz / %.03f MHz)\n", u64toa(freq_hz, &ascii_u64_data1),((double)freq_hz/(double)FREQ_ONE_MHZ) );
	result = airspy_set_freq(device, freq_hz);
	if( result != AIRSPY_SUCCESS ) {
		printf("airspy_set_freq() failed: %s (%d)\n", airspy_error_name(result), result);
		usage();
		return EXIT_FAILURE;
	}
	
	if( limit_num_samples ) {
		printf("samples_to_xfer %s/%sMio\n", u64toa(samples_to_xfer, &ascii_u64_data1), u64toa((samples_to_xfer/FREQ_ONE_MHZ), &ascii_u64_data2) );
	}
	
	gettimeofday(&t_start, NULL);
	gettimeofday(&time_start, NULL);

	printf("Stop with Ctrl-C\n");
	while( (airspy_is_streaming(device) == AIRSPY_TRUE) &&
			(do_exit == false) ) 
	{
		uint32_t byte_count_now;
		struct timeval time_now;
		float time_difference, rate;
		sleep(1);
		
		gettimeofday(&time_now, NULL);
		
		byte_count_now = byte_count;
		byte_count = 0;
		
		time_difference = TimevalDiff(&time_now, &time_start);
		rate = (float)byte_count_now / time_difference;
		printf("%4.1f MiB / %5.3f sec = %4.1f MiB/second\n",
				(byte_count_now / 1e6f), time_difference, (rate / 1e6f) );

		time_start = time_now;

		if (byte_count_now == 0) {
			exit_code = EXIT_FAILURE;
			printf("\nCouldn't transfer any bytes for one second.\n");
			break;
		}
	}
	
	result = airspy_is_streaming(device);	
	if (do_exit)
	{
		printf("\nUser cancel, exiting...\n");
	} else {
		printf("\nExiting... airspy_is_streaming() result: %s (%d)\n", airspy_error_name(result), result);
	}
	
	gettimeofday(&t_end, NULL);
	time_diff = TimevalDiff(&t_end, &t_start);
	printf("Total time: %5.5f s\n", time_diff);
	
	if(device != NULL)
	{
		result = airspy_stop_rx(device);
		if( result != AIRSPY_SUCCESS ) {
			printf("airspy_stop_rx() failed: %s (%d)\n", airspy_error_name(result), result);
		}else {
			printf("airspy_stop_rx() done\n");
		}

		result = airspy_close(device);
		if( result != AIRSPY_SUCCESS ) 
		{
			printf("airspy_close() failed: %s (%d)\n", airspy_error_name(result), result);
		}else {
			printf("airspy_close() done\n");
		}
		
		airspy_exit();
		printf("airspy_exit() done\n");
	}
		
	if(fd != NULL)
	{
		if( receive_wav ) 
		{
			/* Get size of file */
			file_pos = ftell(fd);
			/* Update Wav Header */
			wave_file_hdr.hdr.size = file_pos+8;
			wave_file_hdr.fmt_chunk.dwSamplesPerSec = (uint32_t)DEFAULT_SAMPLE_RATE_HZ;
			wave_file_hdr.fmt_chunk.dwAvgBytesPerSec = wave_file_hdr.fmt_chunk.dwSamplesPerSec*2;
			wave_file_hdr.data_chunk.chunkSize = file_pos - sizeof(t_wav_file_hdr);
			/* Overwrite header with updated data */
			rewind(fd);
			fwrite(&wave_file_hdr, 1, sizeof(t_wav_file_hdr), fd);
		}	
		fclose(fd);
		fd = NULL;
		printf("fclose(fd) done\n");
	}
	printf("exit\n");
	return exit_code;
}
