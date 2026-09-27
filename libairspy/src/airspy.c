/*
Copyright (c) 2013, Michael Ossmann <mike@ossmann.com>
Copyright (c) 2012, Jared Boone <jared@sharebrained.com>
Copyright (c) 2014, Youssef Touil <youssef@airspy.com>
Copyright (c) 2014, Benjamin Vernoux <bvernoux@airspy.com>
Copyright (c) 2015, Ian Gilmour <ian@sdrsharp.com>

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

		Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
		Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the
		documentation and/or other materials provided with the distribution.
		Neither the name of AirSpy nor the names of its contributors may be used to endorse or promote products derived from this software
		without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <libusb.h>

#if _MSC_VER > 1700  // To avoid error with Visual Studio 2017/2019 or more define which define timespec as it is already defined in pthread.h
#define HAVE_STRUCT_TIMESPEC
#endif

#include <pthread.h>

#include "airspy.h"
#include "iqconverter_float.h"
#include "iqconverter_int16.h"
#include "filters.h"

#ifndef bool
typedef int bool;
#define true 1
#define false 0
#endif

#define PACKET_SIZE (12)
#define UNPACKED_SIZE (16)
#define RAW_BUFFER_COUNT (8)

#ifdef AIRSPY_BIG_ENDIAN
#define TO_LE(x) __builtin_bswap32(x)
#else
#define TO_LE(x) x
#endif

#define SAMPLE_RESOLUTION 12
#define SAMPLE_ENCAPSULATION 16

#define SAMPLE_SHIFT (SAMPLE_ENCAPSULATION - SAMPLE_RESOLUTION)
#define SAMPLE_SCALE (1.0f / (1 << (15 - SAMPLE_SHIFT)))

#define SERIAL_NUMBER_UNUSED (0ULL)
#define FILE_DESCRIPTOR_UNUSED (-1)

#define USB_PRODUCT_ID (2)
#define STR_DESCRIPTOR_SIZE (250)

#define MIN_SAMPLERATE_BY_VALUE (1000000)
#define SAMPLE_TYPE_IS_IQ(x) ((x) == AIRSPY_SAMPLE_FLOAT32_IQ || (x) == AIRSPY_SAMPLE_INT16_IQ)

#define LIBUSB_CTRL_TIMEOUT_MS (500)

typedef struct {
	uint32_t freq_hz;
} set_freq_params_t;

typedef struct airspy_device
{
	libusb_context* usb_context;
	libusb_device_handle* usb_device;
	struct libusb_transfer** transfers;
	airspy_sample_block_cb_fn callback;
	volatile bool streaming;
	volatile bool stop_requested;
	pthread_t transfer_thread;
	pthread_t consumer_thread;
	bool transfer_thread_running;
	bool consumer_thread_running;
	pthread_cond_t consumer_cv;
	pthread_mutex_t consumer_mp;
	uint32_t supported_samplerate_count;
	uint32_t *supported_samplerates;
	uint32_t transfer_count;
	uint32_t buffer_size;
	uint32_t dropped_buffers;
	uint32_t dropped_buffers_queue[RAW_BUFFER_COUNT];
	uint16_t *received_samples_queue[RAW_BUFFER_COUNT];
	volatile int received_samples_queue_head;
	volatile int received_samples_queue_tail;
	volatile int received_buffer_count;
	void *output_buffer;
	uint16_t *unpacked_samples;
	bool packing_enabled;
	uint8_t packing_mode; /* 0 = 16-bit, 1 = 12-bit packed, 3 = 8-bit */
	bool dev_mem_buffers;
	bool framing_requested;
	bool framing_active;
	uint32_t sof_count_seen; /* last SOF tag count deframed: a change means a new tag */
	uint32_t sof_divider; /* requested SOF tagging divider, sent at every airspy_start_rx() */
	uint8_t *framed_samples;
	airspy_transfer_metadata_t meta;
	bool meta_valid;
	uint64_t expected_sample_index;
	bool expected_sample_index_valid;
	iqconverter_float_t *cnv_f;
	iqconverter_int16_t *cnv_i;
	void* ctx;
	enum airspy_sample_type sample_type;
} airspy_device_t;

static const uint16_t airspy_usb_vid = 0x1d50;
static const uint16_t airspy_usb_pid = 0x60a1;

#define STR_PREFIX_SERIAL_AIRSPY_SIZE (10)

#define SERIAL_AIRSPY_EXPECTED_SIZE (26)

#define GAIN_COUNT (22)

uint8_t airspy_linearity_vga_gains[GAIN_COUNT] = { 13, 12, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 8, 7, 6, 5, 4 };
uint8_t airspy_linearity_mixer_gains[GAIN_COUNT] = { 12, 12, 11, 9, 8, 7, 6, 6, 5, 0, 0, 1, 0, 0, 2, 2, 1, 1, 1, 1, 0, 0 };
uint8_t airspy_linearity_lna_gains[GAIN_COUNT] = { 14, 14, 14, 13, 12, 10, 9, 9, 8, 9, 8, 6, 5, 3, 1, 0, 0, 0, 0, 0, 0, 0 };
uint8_t airspy_sensitivity_vga_gains[GAIN_COUNT] = { 13, 12, 11, 10, 9, 8, 7, 6, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4 };
uint8_t airspy_sensitivity_mixer_gains[GAIN_COUNT] = { 12, 12, 12, 12, 11, 10, 10, 9, 9, 8, 7, 4, 4, 4, 3, 2, 2, 1, 0, 0, 0, 0 };
uint8_t airspy_sensitivity_lna_gains[GAIN_COUNT] = { 14, 14, 14, 14, 14, 14, 14, 14, 14, 13, 12, 12, 9, 9, 8, 7, 6, 5, 3, 2, 1, 0 };

static int cancel_transfers(airspy_device_t* device)
{
	uint32_t transfer_index;

	if (device->transfers != NULL)
	{
		for (transfer_index = 0; transfer_index<device->transfer_count; transfer_index++)
		{
			if (device->transfers[transfer_index] != NULL)
			{
				libusb_cancel_transfer(device->transfers[transfer_index]);
			}
		}
		return AIRSPY_SUCCESS;
	}
	else {
		return AIRSPY_ERROR_OTHER;
	}
}

static void* alloc_usb_buffer(airspy_device_t* device)
{
#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000105)
	if (device->dev_mem_buffers)
	{
		return libusb_dev_mem_alloc(device->usb_device, device->buffer_size);
	}
#endif
	return malloc(device->buffer_size);
}

static void free_usb_buffer(airspy_device_t* device, void* buffer)
{
	if (buffer == NULL)
	{
		return;
	}
#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x01000105)
	if (device->dev_mem_buffers)
	{
		libusb_dev_mem_free(device->usb_device, (unsigned char*)buffer, device->buffer_size);
		return;
	}
#endif
	free(buffer);
}

static int allocate_usb_buffers(airspy_device_t* device, unsigned char** transfer_buffers)
{
	int i;
	uint32_t t;

	for (i = 0; i < RAW_BUFFER_COUNT; i++)
	{
		device->received_samples_queue[i] = (uint16_t*)alloc_usb_buffer(device);
		if (device->received_samples_queue[i] == NULL)
		{
			return -1;
		}
		memset(device->received_samples_queue[i], 0, device->buffer_size);
	}
	for (t = 0; t < device->transfer_count; t++)
	{
		transfer_buffers[t] = (unsigned char*)alloc_usb_buffer(device);
		if (transfer_buffers[t] == NULL)
		{
			return -1;
		}
	}
	return 0;
}

static void release_usb_buffers(airspy_device_t* device, unsigned char** transfer_buffers)
{
	int i;
	uint32_t t;

	for (i = 0; i < RAW_BUFFER_COUNT; i++)
	{
		free_usb_buffer(device, device->received_samples_queue[i]);
		device->received_samples_queue[i] = NULL;
	}
	for (t = 0; t < device->transfer_count; t++)
	{
		free_usb_buffer(device, transfer_buffers[t]);
		transfer_buffers[t] = NULL;
	}
}

static int free_transfers(airspy_device_t* device)
{
	int i;
	uint32_t transfer_index;

	if (device->transfers != NULL)
	{
		// libusb_close() should free all transfers referenced from this array.
		for (transfer_index = 0; transfer_index < device->transfer_count; transfer_index++)
		{
			if (device->transfers[transfer_index] != NULL)
			{
				free_usb_buffer(device, device->transfers[transfer_index]->buffer);
				libusb_free_transfer(device->transfers[transfer_index]);
				device->transfers[transfer_index] = NULL;
			}
		}
		free(device->transfers);
		device->transfers = NULL;

		if (device->output_buffer != NULL)
		{
			free(device->output_buffer);
			device->output_buffer = NULL;
		}

		if (device->unpacked_samples != NULL)
		{
			free(device->unpacked_samples);
			device->unpacked_samples = NULL;
		}

		if (device->framed_samples != NULL)
		{
			free(device->framed_samples);
			device->framed_samples = NULL;
		}

		for (i = 0; i < RAW_BUFFER_COUNT; i++)
		{
			free_usb_buffer(device, device->received_samples_queue[i]);
			device->received_samples_queue[i] = NULL;
		}
	}

	return AIRSPY_SUCCESS;
}

static int allocate_transfers(airspy_device_t* const device)
{
	size_t sample_count;
	uint32_t transfer_index;
	unsigned char** transfer_buffers;

	if (device->transfers != NULL)
	{
		return AIRSPY_ERROR_BUSY;
	}

	transfer_buffers = (unsigned char**)calloc(device->transfer_count, sizeof(unsigned char*));
	if (transfer_buffers == NULL)
	{
		return AIRSPY_ERROR_NO_MEM;
	}

	device->dev_mem_buffers = (getenv("AIRSPY_NO_ZEROCOPY") == NULL);
	if (allocate_usb_buffers(device, transfer_buffers) != 0)
	{
		release_usb_buffers(device, transfer_buffers);
		device->dev_mem_buffers = false;
		if (allocate_usb_buffers(device, transfer_buffers) != 0)
		{
			release_usb_buffers(device, transfer_buffers);
			free(transfer_buffers);
			return AIRSPY_ERROR_NO_MEM;
		}
	}

	if (device->packing_mode == 3)
	{
		sample_count = device->buffer_size;
	}
	else if (device->packing_enabled)
	{
		sample_count = ((device->buffer_size / 2) * 4) / 3;
	}
	else
	{
		sample_count = device->buffer_size / 2;
	}

	device->output_buffer = (float *)malloc(sample_count * sizeof(float));
	if (device->output_buffer == NULL)
	{
		release_usb_buffers(device, transfer_buffers);
		free(transfer_buffers);
		return AIRSPY_ERROR_NO_MEM;
	}

	if (device->packing_enabled)
	{
		device->unpacked_samples = (uint16_t*)malloc(sample_count * sizeof(uint16_t));
		if (device->unpacked_samples == NULL)
		{
			release_usb_buffers(device, transfer_buffers);
			free(transfer_buffers);
			return AIRSPY_ERROR_NO_MEM;
		}
	}

	device->framed_samples = (uint8_t*)malloc(device->buffer_size);
	if (device->framed_samples == NULL)
	{
		release_usb_buffers(device, transfer_buffers);
		free(transfer_buffers);
		return AIRSPY_ERROR_NO_MEM;
	}

	device->transfers = (struct libusb_transfer**) calloc(device->transfer_count, sizeof(struct libusb_transfer));
	if (device->transfers == NULL)
	{
		release_usb_buffers(device, transfer_buffers);
		free(transfer_buffers);
		return AIRSPY_ERROR_NO_MEM;
	}

	for (transfer_index = 0; transfer_index<device->transfer_count; transfer_index++)
	{
		device->transfers[transfer_index] = libusb_alloc_transfer(0);
		if (device->transfers[transfer_index] == NULL)
		{
			free(transfer_buffers);
			return AIRSPY_ERROR_LIBUSB;
		}

		libusb_fill_bulk_transfer(
			device->transfers[transfer_index],
			device->usb_device,
			0,
			transfer_buffers[transfer_index],
			device->buffer_size,
			NULL,
			device,
			0
			);
	}

	free(transfer_buffers);
	return AIRSPY_SUCCESS;
}

static int prepare_transfers(airspy_device_t* device, const uint_fast8_t endpoint_address, libusb_transfer_cb_fn callback)
{
	int error;
	uint32_t transfer_index;
	if (device->transfers != NULL)
	{
		for (transfer_index = 0; transfer_index<device->transfer_count; transfer_index++)
		{
			device->transfers[transfer_index]->endpoint = endpoint_address;
			device->transfers[transfer_index]->callback = callback;

			error = libusb_submit_transfer(device->transfers[transfer_index]);
			if (error != 0)
			{
				return AIRSPY_ERROR_LIBUSB;
			}
		}
		return AIRSPY_SUCCESS;
	}
	else {
		// This shouldn't happen.
		return AIRSPY_ERROR_OTHER;
	}
}

static void convert_samples_int16(uint16_t *src, int16_t *dest, int count)
{
	int i;
	for (i = 0; i < count; i += 4)
	{
		dest[i + 0] = (src[i + 0] - 2048) << SAMPLE_SHIFT;
		dest[i + 1] = (src[i + 1] - 2048) << SAMPLE_SHIFT;
		dest[i + 2] = (src[i + 2] - 2048) << SAMPLE_SHIFT;
		dest[i + 3] = (src[i + 3] - 2048) << SAMPLE_SHIFT;
	}
}

static void convert_samples_float(uint16_t *src, float *dest, int count)
{
	int i;
	for (i = 0; i < count; i += 4)
	{
		dest[i + 0] = (src[i + 0] - 2048) * SAMPLE_SCALE;
		dest[i + 1] = (src[i + 1] - 2048) * SAMPLE_SCALE;
		dest[i + 2] = (src[i + 2] - 2048) * SAMPLE_SCALE;
		dest[i + 3] = (src[i + 3] - 2048) * SAMPLE_SCALE;
	}
}

static inline void unpack_samples(uint32_t *input, uint16_t *output, int length)
{
	int i, j;

	for (i = 0, j = 0; j < length; i += 3, j += 8)
	{
		output[j + 0] = (input[i] >> 20) & 0xfff;
		output[j + 1] = (input[i] >> 8) & 0xfff;
		output[j + 2] = ((input[i] & 0xff) << 4) | ((input[i + 1] >> 28) & 0xf);
		output[j + 3] = ((input[i + 1] & 0xfff0000) >> 16);
		output[j + 4] = ((input[i + 1] & 0xfff0) >> 4);
		output[j + 5] = ((input[i + 1] & 0xf) << 8) | ((input[i + 2] & 0xff000000) >> 24);
		output[j + 6] = ((input[i + 2] >> 12) & 0xfff);
		output[j + 7] = ((input[i + 2] & 0xfff));
	}
}

static int send_framing(airspy_device_t* device, uint8_t value)
{
int result;
uint8_t retval;

result = libusb_control_transfer(
	device->usb_device,
	LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
	AIRSPY_SET_FRAMING,
	0,
	value ? 1 : 0,
	&retval,
	1,
	LIBUSB_CTRL_TIMEOUT_MS);

if (result < 1)
{
	return AIRSPY_ERROR_LIBUSB;
}
return AIRSPY_SUCCESS;
}

static int send_sof_divider(airspy_device_t* device, uint32_t divider)
{
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_SET_SOF_DIVIDER,
		divider & 0xFFFF,
		divider >> 16,
		NULL,
		0,
		LIBUSB_CTRL_TIMEOUT_MS);
	if (result != 0)
		return AIRSPY_ERROR_LIBUSB;
	return AIRSPY_SUCCESS;
}

static inline void unpack_samples_8bit(const uint8_t *input, uint16_t *output, int length)
{
	int i;
	for (i = 0; i < length; i++)
		output[i] = (uint16_t)input[i] << 4;
}

static int deframe_buffer(airspy_device_t* device, const uint8_t* raw)
{
	const uint32_t wire = device->packing_mode == 3 ? AIRSPY_FRAME_WIRE_8BIT : device->packing_enabled ? AIRSPY_FRAME_WIRE_PACKED : AIRSPY_FRAME_WIRE_UNPACKED;
	const uint32_t payload = wire - AIRSPY_FRAME_HEADER_SIZE;
	const uint32_t samples_per_chunk = device->packing_mode == 3 ? payload : device->packing_enabled ? (payload / 3) * 2 : payload / 2;
	const uint32_t chunks = device->buffer_size / wire;
	airspy_transfer_metadata_t* m = &device->meta;
	uint32_t c;
	uint32_t good = 0;
	m->uart_len = 0;
	size_t out_bytes = 0;

	m->chunks = 0;
	m->gap_samples = 0;
	m->duplicate_chunks = 0;
	m->sof_tags_len = 0;

	for (c = 0; c < chunks; c++)
	{
		const uint8_t* chunk = raw + (size_t)c * wire;
		const uint32_t* w = (const uint32_t*)chunk; /* header words, little-endian on the wire */
		uint64_t index;
		uint32_t count;

		if (TO_LE(w[2]) != AIRSPY_FRAME_MAGIC)
		{
			m->sync_errors++;
			continue;
		}
		index = ((uint64_t)TO_LE(w[1]) << 32) | TO_LE(w[0]);
		count = TO_LE(w[4]);

		if (device->expected_sample_index_valid && index < device->expected_sample_index)
		{
			m->duplicate_chunks++;
			continue;
		}
		if (good == 0)
		{
			m->sample_index = index;
			m->flags = TO_LE(w[5]);
			m->freq_hz = TO_LE(w[8]);
		}
		if (device->expected_sample_index_valid && index > device->expected_sample_index)
		{
			m->gap_samples += index - device->expected_sample_index;
		}
		device->expected_sample_index = index + count;
		device->expected_sample_index_valid = true;
		m->lost_chunks = TO_LE(w[6]);
		m->overrun_chunks = TO_LE(w[7]);
		m->pps_sample_index = ((uint64_t)TO_LE(w[10]) << 32) | TO_LE(w[9]);
		m->pps_fraction = TO_LE(w[11]);
		m->pps_count = TO_LE(w[12]);
		m->sof_sample_index = ((uint64_t)TO_LE(w[18]) << 32) | TO_LE(w[17]);
		m->sof_fraction = TO_LE(w[19]);
		m->sof_frame = TO_LE(w[20]);
		m->sof_count = TO_LE(w[21]);
		if (m->sof_count != 0 && m->sof_count != device->sof_count_seen && m->sof_tags_len < AIRSPY_METADATA_SOF_MAX)
		{
			airspy_sof_tag_t* tag = &m->sof_tags[m->sof_tags_len++];
			tag->sample_index = m->sof_sample_index;
			tag->fraction = m->sof_fraction;
			tag->frame = m->sof_frame;
			tag->count = m->sof_count;
		}
		device->sof_count_seen = m->sof_count;
		{
			const uint8_t* b = (const uint8_t*)&w[13];
			uint32_t n = b[0];
			if (n > AIRSPY_FRAME_UART_BYTES)
				n = AIRSPY_FRAME_UART_BYTES;
			if (m->uart_len + n <= AIRSPY_METADATA_UART_MAX)
			{
				memcpy(&m->uart_data[m->uart_len], &b[1], n);
				m->uart_len += n;
			}
		}

		memcpy(device->framed_samples + out_bytes, chunk + AIRSPY_FRAME_HEADER_SIZE, payload);
		out_bytes += payload;
		good++;
	}

	m->chunks = good;
	device->meta_valid = (good > 0);
	return (int)(good * samples_per_chunk);
}

static void* consumer_threadproc(void *arg)
{
	int sample_count;
	uint16_t* input_samples;
	uint32_t dropped_buffers;
	airspy_device_t* device = (airspy_device_t*)arg;
	airspy_transfer_t transfer;

#ifdef _WIN32

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

#endif

	pthread_mutex_lock(&device->consumer_mp);

	while (device->streaming && !device->stop_requested)
	{
		while (device->received_buffer_count == 0 && device->streaming && !device->stop_requested)
		{
			pthread_cond_wait(&device->consumer_cv, &device->consumer_mp);
		}
		if (!device->streaming || device->stop_requested)
		{
			break;
		}

		input_samples = device->received_samples_queue[device->received_samples_queue_tail];
		dropped_buffers = device->dropped_buffers_queue[device->received_samples_queue_tail];
		device->received_samples_queue_tail = (device->received_samples_queue_tail + 1) & (RAW_BUFFER_COUNT - 1);

		pthread_mutex_unlock(&device->consumer_mp);

		if (device->framing_active)
		{
			sample_count = deframe_buffer(device, (const uint8_t*)input_samples);
			if (sample_count == 0)
			{
				pthread_mutex_lock(&device->consumer_mp);
				device->received_buffer_count--;
				continue;
			}
			input_samples = (uint16_t*)device->framed_samples;
		}
		else if (device->packing_mode == 3)
		{
			sample_count = device->buffer_size;
		}
		else if (device->packing_enabled)
		{
			sample_count = ((device->buffer_size / 2) * 4) / 3;
		}
		else
		{
			sample_count = device->buffer_size / 2;
		}

		if (device->packing_mode == 3 && device->sample_type != AIRSPY_SAMPLE_RAW)
		{
			unpack_samples_8bit((const uint8_t*)input_samples, device->unpacked_samples, sample_count);
			input_samples = device->unpacked_samples;
		}
		else if (device->packing_enabled && device->sample_type != AIRSPY_SAMPLE_RAW)
		{
			unpack_samples((uint32_t*)input_samples, device->unpacked_samples, sample_count);
			input_samples = device->unpacked_samples;
		}

		switch (device->sample_type)
		{
		case AIRSPY_SAMPLE_FLOAT32_IQ:
			convert_samples_float(input_samples, (float *)device->output_buffer, sample_count);
			iqconverter_float_process(device->cnv_f, (float *) device->output_buffer, sample_count);
			sample_count /= 2;
			transfer.samples = device->output_buffer;
			break;

		case AIRSPY_SAMPLE_FLOAT32_REAL:
			convert_samples_float(input_samples, (float *)device->output_buffer, sample_count);
			transfer.samples = device->output_buffer;
			break;

		case AIRSPY_SAMPLE_INT16_IQ:
			convert_samples_int16(input_samples, (int16_t *)device->output_buffer, sample_count);
			iqconverter_int16_process(device->cnv_i, (int16_t *) device->output_buffer, sample_count);
			sample_count /= 2;
			transfer.samples = device->output_buffer;
			break;

		case AIRSPY_SAMPLE_INT16_REAL:
			convert_samples_int16(input_samples, (int16_t *)device->output_buffer, sample_count);
			transfer.samples = device->output_buffer;
			break;

		case AIRSPY_SAMPLE_UINT16_REAL:
		case AIRSPY_SAMPLE_RAW:
			transfer.samples = input_samples;
			break;

		case AIRSPY_SAMPLE_END:
			// Just to shut GCC's moaning
			break;
		}

		transfer.device = device;
		transfer.ctx = device->ctx;
		transfer.sample_count = sample_count;
		transfer.sample_type = device->sample_type;
		transfer.dropped_samples = (uint64_t) dropped_buffers * (uint64_t) sample_count;

		if (device->callback(&transfer) != 0)
		{
			device->streaming = false;
		}

		pthread_mutex_lock(&device->consumer_mp);
		device->received_buffer_count--;
	}

	device->streaming = false;

	pthread_mutex_unlock(&device->consumer_mp);

	return NULL;
}

static void airspy_libusb_transfer_callback(struct libusb_transfer* usb_transfer)
{
	uint16_t *temp;
	airspy_device_t* device = (airspy_device_t*)usb_transfer->user_data;

	if (!device->streaming || device->stop_requested)
	{
		return;
	}

	if (usb_transfer->status == LIBUSB_TRANSFER_COMPLETED && usb_transfer->actual_length == usb_transfer->length)
	{
		pthread_mutex_lock(&device->consumer_mp);

		if (device->received_buffer_count < RAW_BUFFER_COUNT)
		{
			temp = device->received_samples_queue[device->received_samples_queue_head];
			device->received_samples_queue[device->received_samples_queue_head] = (uint16_t *)usb_transfer->buffer;
			usb_transfer->buffer = (uint8_t *)temp;

			device->dropped_buffers_queue[device->received_samples_queue_head] = device->dropped_buffers;
			device->dropped_buffers = 0;
			
			device->received_samples_queue_head = (device->received_samples_queue_head + 1) & (RAW_BUFFER_COUNT - 1);
			device->received_buffer_count++;

			pthread_cond_signal(&device->consumer_cv);
		}
		else
		{
			device->dropped_buffers++;
		}

		pthread_mutex_unlock(&device->consumer_mp);

		if (libusb_submit_transfer(usb_transfer) != 0)
		{
			device->streaming = false;
		}
	}
	else
	{
		device->streaming = false;
	}
}

static void* transfer_threadproc(void* arg)
{
	airspy_device_t* device = (airspy_device_t*)arg;
	int error;
	struct timeval timeout = { 0, 500000 };

#ifdef _WIN32

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

#endif

	while (device->streaming && !device->stop_requested)
	{
		error = libusb_handle_events_timeout_completed(device->usb_context, &timeout, NULL);
		if (error < 0)
		{
			if (error != LIBUSB_ERROR_INTERRUPTED)
				device->streaming = false;
		}
	}
	
	device->streaming = false;

	return NULL;
}

static int kill_io_threads(airspy_device_t* device)
{
	struct timeval timeout = { 0, 0 };

	if (device->stop_requested)
	{
		device->stop_requested = false;
		device->streaming = false;
		cancel_transfers(device);

		pthread_mutex_lock(&device->consumer_mp);
		pthread_cond_signal(&device->consumer_cv);
		pthread_mutex_unlock(&device->consumer_mp);

		if (device->transfer_thread_running) {
		    pthread_join(device->transfer_thread, NULL);
		    device->transfer_thread_running = false;
		}
		if (device->consumer_thread_running) {
		    pthread_join(device->consumer_thread, NULL);
		    device->consumer_thread_running = false;
		}

		libusb_handle_events_timeout_completed(device->usb_context, &timeout, NULL);
	}

	return AIRSPY_SUCCESS;
}

static int create_io_threads(airspy_device_t* device, airspy_sample_block_cb_fn callback)
{
	int result;
	pthread_attr_t attr;

	if (!device->streaming && !device->stop_requested)
	{
		device->callback = callback;
		device->streaming = true;

		result = prepare_transfers(device, LIBUSB_ENDPOINT_IN | 1, (libusb_transfer_cb_fn)airspy_libusb_transfer_callback);
		if (result != AIRSPY_SUCCESS)
		{
			return result;
		}

		device->received_samples_queue_head = 0;
		device->received_samples_queue_tail = 0;
		device->received_buffer_count = 0;

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

		result = pthread_create(&device->consumer_thread, &attr, consumer_threadproc, device);
		if (result != 0)
		{
			return AIRSPY_ERROR_THREAD;
		}
		device->consumer_thread_running = true;

		result = pthread_create(&device->transfer_thread, &attr, transfer_threadproc, device);
		if (result != 0)
		{
			return AIRSPY_ERROR_THREAD;
		}
		device->transfer_thread_running = true;

		pthread_attr_destroy(&attr);
	}
	else {
		return AIRSPY_ERROR_BUSY;
	}

	return AIRSPY_SUCCESS;
}

static void airspy_open_exit(airspy_device_t* device)
{
	if (device->usb_device != NULL)
	{
		libusb_release_interface(device->usb_device, 0);
		libusb_close(device->usb_device);
		device->usb_device = NULL;
	}
	libusb_exit(device->usb_context);
	device->usb_context = NULL;
}

static void airspy_open_device(airspy_device_t* device,
	int* ret,
	uint16_t vid,
	uint16_t pid,
	uint64_t serial_number_val)
{
	int i;
	int result;
	libusb_device_handle** libusb_dev_handle;
	int serial_number_len;
	libusb_device_handle* dev_handle;
	libusb_device *dev;
	libusb_device** devices = NULL;

	ssize_t cnt;
	int serial_descriptor_index;
	struct libusb_device_descriptor device_descriptor;
	unsigned char serial_number[SERIAL_AIRSPY_EXPECTED_SIZE + 1];

	libusb_dev_handle = &device->usb_device;
	*libusb_dev_handle = NULL;

	cnt = libusb_get_device_list(device->usb_context, &devices);
	if (cnt < 0)
	{
		*ret = AIRSPY_ERROR_NOT_FOUND;
		return;
	}

	i = 0;
	while ((dev = devices[i++]) != NULL)
	{
		libusb_get_device_descriptor(dev, &device_descriptor);

		if ((device_descriptor.idVendor == vid) &&
			(device_descriptor.idProduct == pid))
		{
			if (serial_number_val != SERIAL_NUMBER_UNUSED)
			{
				serial_descriptor_index = device_descriptor.iSerialNumber;
				if (serial_descriptor_index > 0)
				{
					if (libusb_open(dev, libusb_dev_handle) != 0)
					{
						*libusb_dev_handle = NULL;
						continue;
					}
					dev_handle = *libusb_dev_handle;
					serial_number_len = libusb_get_string_descriptor_ascii(dev_handle,
						serial_descriptor_index,
						serial_number,
						sizeof(serial_number));
					if (serial_number_len == SERIAL_AIRSPY_EXPECTED_SIZE)
					{
						uint64_t serial = 0;
						// use same code to determine device's serial number as in airspy_list_devices()
						{
							char *start, *end;

							serial_number[SERIAL_AIRSPY_EXPECTED_SIZE] = 0;
							start = (char*)(serial_number + STR_PREFIX_SERIAL_AIRSPY_SIZE);
							end = NULL;
							serial = strtoull(start, &end, 16);
						}

						if (serial == serial_number_val)
						{
#ifdef __linux__
							/* Check whether a kernel driver is attached to interface #0. If so, we'll
							* need to detach it.
							*/
							if (libusb_kernel_driver_active(dev_handle, 0))
							{
								libusb_detach_kernel_driver(dev_handle, 0);
							}
#endif
							result = libusb_set_configuration(dev_handle, 1);
							if (result != 0)
							{
								libusb_close(dev_handle);
								*libusb_dev_handle = NULL;
								continue;
							}
							result = libusb_claim_interface(dev_handle, 0);
							if (result != 0)
							{
								libusb_close(dev_handle);
								*libusb_dev_handle = NULL;
								continue;
							}
							break;
						}
						else
						{
							libusb_close(dev_handle);
							*libusb_dev_handle = NULL;
							continue;
						}
					}
					else
					{
						libusb_close(dev_handle);
						*libusb_dev_handle = NULL;
						continue;
					}
				}
			}
			else
			{
				if (libusb_open(dev, libusb_dev_handle) == 0)
				{
					dev_handle = *libusb_dev_handle;
#ifdef __linux__
					/* Check whether a kernel driver is attached to interface #0. If so, we'll
					* need to detach it.
					*/
					if (libusb_kernel_driver_active(dev_handle, 0))
					{
						libusb_detach_kernel_driver(dev_handle, 0);
					}
#endif
					result = libusb_set_configuration(dev_handle, 1);
					if (result != 0)
					{
						libusb_close(dev_handle);
						*libusb_dev_handle = NULL;
						continue;
					}
					result = libusb_claim_interface(dev_handle, 0);
					if (result != 0)
					{
						libusb_close(dev_handle);
						*libusb_dev_handle = NULL;
						continue;
					}
					break;
				}
			}
		}
	}
	libusb_free_device_list(devices, 1);

	dev_handle = device->usb_device;
	if (dev_handle == NULL)
	{
		*ret = AIRSPY_ERROR_NOT_FOUND;
		return;
	}

	*ret = AIRSPY_SUCCESS;
	return;
}

static void airspy_open_device_fd(airspy_device_t* device,
	int* ret,
	int fd)
{
	int result = -1;

#ifdef __ANDROID__
	result = libusb_wrap_sys_device(device->usb_context, (intptr_t)fd, &device->usb_device);
#else
	device->usb_device = NULL;
	*ret = AIRSPY_ERROR_UNSUPPORTED;
	return;
#endif

	if (result != 0 || device->usb_device == NULL)
	{
		*ret = AIRSPY_ERROR_LIBUSB;
		return;
	}

#ifdef __linux__
	/* Check whether a kernel driver is attached to interface #0. If so, we'll
	* need to detach it.
	*/
	if (libusb_kernel_driver_active(device->usb_device, 0))
	{
		libusb_detach_kernel_driver(device->usb_device, 0);
	}
#endif

	result = libusb_set_configuration(device->usb_device, 1);
	if (result != 0)
	{
		libusb_close(device->usb_device);
		device->usb_device = NULL;
		*ret = AIRSPY_ERROR_LIBUSB;
		return;
	}

	result = libusb_claim_interface(device->usb_device, 0);
	if (result != 0)
	{
		libusb_close(device->usb_device);
		device->usb_device = NULL;
		*ret = AIRSPY_ERROR_LIBUSB;
		return;
	}

	*ret = AIRSPY_SUCCESS;
	return;
}

static int airspy_read_samplerates_from_fw(struct airspy_device* device, uint32_t* buffer, const uint32_t len)
{
	int result;

	result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_GET_SAMPLERATES,
		0,
		len,
		(unsigned char*)buffer,
		(len > 0 ? len : 1) * sizeof(uint32_t),
		LIBUSB_CTRL_TIMEOUT_MS);

	if (result < 1)
	{
		return AIRSPY_ERROR_OTHER;
	}

	return AIRSPY_SUCCESS;
}

static int airspy_open_init(airspy_device_t** device, uint64_t serial_number, int fd)
{
	airspy_device_t* lib_device;
	int libusb_error;
	int result;

	*device = NULL;

	lib_device = (airspy_device_t*)calloc(1, sizeof(airspy_device_t));
	if (lib_device == NULL)
	{
		return AIRSPY_ERROR_NO_MEM;
	}

#ifdef __ANDROID__
	// LibUSB does not support device discovery on android
	libusb_set_option(NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY, NULL);
#endif

	libusb_error = libusb_init(&lib_device->usb_context);
	if (libusb_error != 0)
	{
		free(lib_device);
		return AIRSPY_ERROR_LIBUSB;
	}

	if (fd == FILE_DESCRIPTOR_UNUSED) {
		airspy_open_device(lib_device,
			&result,
			airspy_usb_vid,
			airspy_usb_pid,
			serial_number);
	}
	else {
		airspy_open_device_fd(lib_device,
			&result,
			fd);
	}

	if (lib_device->usb_device == NULL)
	{
		libusb_exit(lib_device->usb_context);
		free(lib_device);
		return result;
	}

	lib_device->transfers = NULL;
	lib_device->callback = NULL;
	lib_device->transfer_count = 16;
	lib_device->buffer_size = 262144;
	lib_device->packing_enabled = false;
	lib_device->packing_mode = 0;
	lib_device->dev_mem_buffers = false;
	lib_device->framing_requested = true;
	{
		const char* env_ppb = getenv("AIRSPY_CALIBRATION_PPB");
		if (env_ppb != NULL)
			airspy_set_calibration(lib_device, (int32_t)strtol(env_ppb, NULL, 10));
	}
	lib_device->framing_active = false;
	lib_device->framed_samples = NULL;
	lib_device->meta_valid = false;
	lib_device->expected_sample_index_valid = false;
	lib_device->sof_count_seen = 0;
	lib_device->sof_divider = 0;
	memset(&lib_device->meta, 0, sizeof(lib_device->meta));
	lib_device->streaming = false;
	lib_device->stop_requested = false;
	lib_device->sample_type = AIRSPY_SAMPLE_FLOAT32_IQ;

	result = airspy_read_samplerates_from_fw(lib_device, &lib_device->supported_samplerate_count, 0);
	if (result == AIRSPY_SUCCESS)
	{
		lib_device->supported_samplerates = (uint32_t *) malloc(lib_device->supported_samplerate_count * sizeof(uint32_t));
		result = airspy_read_samplerates_from_fw(lib_device, lib_device->supported_samplerates, lib_device->supported_samplerate_count);
		if (result != AIRSPY_SUCCESS)
		{
			free(lib_device->supported_samplerates);
		}
	}

	if (result != AIRSPY_SUCCESS)
	{
		lib_device->supported_samplerate_count = 2;
		lib_device->supported_samplerates = (uint32_t *) malloc(lib_device->supported_samplerate_count * sizeof(uint32_t));
		lib_device->supported_samplerates[0] = 10000000;
		lib_device->supported_samplerates[1] = 2500000;
	}

	airspy_set_packing(lib_device, 0);

	result = allocate_transfers(lib_device);
	if (result != 0)
	{
		airspy_open_exit(lib_device);
		free(lib_device->supported_samplerates);
		free(lib_device);
		return AIRSPY_ERROR_NO_MEM;
	}

	lib_device->cnv_f = iqconverter_float_create(HB_KERNEL_FLOAT, HB_KERNEL_FLOAT_LEN);
	lib_device->cnv_i = iqconverter_int16_create(HB_KERNEL_INT16, HB_KERNEL_INT16_LEN);

	pthread_cond_init(&lib_device->consumer_cv, NULL);
	pthread_mutex_init(&lib_device->consumer_mp, NULL);

	*device = lib_device;

	return AIRSPY_SUCCESS;
}

#ifdef __cplusplus
extern "C"
{
#endif

	void ADDCALL airspy_lib_version(airspy_lib_version_t* lib_version)
	{
		lib_version->major_version = AIRSPY_VER_MAJOR;
		lib_version->minor_version = AIRSPY_VER_MINOR;
		lib_version->revision = AIRSPY_VER_REVISION;
	}

	/* airspy_init() deprecated */
	int ADDCALL airspy_init(void)
	{
		return AIRSPY_SUCCESS;
	}

	/* airspy_exit() deprecated */
	int ADDCALL airspy_exit(void)
	{
		return AIRSPY_SUCCESS;
	}

int airspy_list_devices(uint64_t *serials, int count)
{
	libusb_device_handle* libusb_dev_handle;
	struct libusb_context *context;
	libusb_device** devices = NULL;
	libusb_device *dev;
	struct libusb_device_descriptor device_descriptor;

	int serial_descriptor_index;
	int serial_number_len;
	int output_count;
	int i;
	unsigned char serial_number[SERIAL_AIRSPY_EXPECTED_SIZE + 1];

	if (serials)
	{
		memset(serials, 0, sizeof(uint64_t) * count);
	}

#ifdef __ANDROID__
	// LibUSB does not support device discovery on android
	libusb_set_option(NULL, LIBUSB_OPTION_NO_DEVICE_DISCOVERY, NULL);
#endif

	if (libusb_init(&context) != 0)
	{
		return AIRSPY_ERROR_LIBUSB;
	}

	if (libusb_get_device_list(context, &devices) < 0)
	{
		return AIRSPY_ERROR_NOT_FOUND;
	}

	i = 0;
	output_count = 0;
	while ((dev = devices[i++]) != NULL && (!serials || output_count < count))
	{
		libusb_get_device_descriptor(dev, &device_descriptor);

		if ((device_descriptor.idVendor == airspy_usb_vid) &&
			(device_descriptor.idProduct == airspy_usb_pid))
		{
			serial_descriptor_index = device_descriptor.iSerialNumber;
			if (serial_descriptor_index > 0)
			{
				if (libusb_open(dev, &libusb_dev_handle) != 0)
				{
					continue;
				}

				serial_number_len = libusb_get_string_descriptor_ascii(libusb_dev_handle,
					serial_descriptor_index,
					serial_number,
					sizeof(serial_number));

				if (serial_number_len == SERIAL_AIRSPY_EXPECTED_SIZE)
				{
					char *start, *end;
					uint64_t serial;

					serial_number[SERIAL_AIRSPY_EXPECTED_SIZE] = 0;
					start = (char*)(serial_number + STR_PREFIX_SERIAL_AIRSPY_SIZE);
					end = NULL;
					serial = strtoull(start, &end, 16);
					if (serial == 0 && start == end)
					{
						libusb_close(libusb_dev_handle);
						continue;
					}

					if (serials)
					{
						serials[output_count] = serial;
					}
					output_count++;
				}

				libusb_close(libusb_dev_handle);
			}
		}
	}

	libusb_free_device_list(devices, 1);
	libusb_exit(context);
	return output_count;
}

	int ADDCALL airspy_open_sn(airspy_device_t** device, uint64_t serial_number)
	{
		int result;

		result = airspy_open_init(device, serial_number, FILE_DESCRIPTOR_UNUSED);
		return result;
	}

	int ADDCALL airspy_open_fd(airspy_device_t** device, int fd)
	{
		int result;

		result = airspy_open_init(device, SERIAL_NUMBER_UNUSED, fd);
		return result;
	}

	int ADDCALL airspy_open(airspy_device_t** device)
	{
		int result;

		result = airspy_open_init(device, SERIAL_NUMBER_UNUSED, FILE_DESCRIPTOR_UNUSED);
		return result;
	}

	int ADDCALL airspy_close(airspy_device_t* device)
	{
		int result;

		result = AIRSPY_SUCCESS;

		if (device != NULL)
		{
			result = airspy_stop_rx(device);

			iqconverter_float_free(device->cnv_f);
			iqconverter_int16_free(device->cnv_i);

			pthread_cond_destroy(&device->consumer_cv);
			pthread_mutex_destroy(&device->consumer_mp);

			free_transfers(device);
			airspy_open_exit(device);
			free(device->supported_samplerates);
			free(device);
		}

		return result;
	}

	int ADDCALL airspy_get_samplerates(struct airspy_device* device, uint32_t* buffer, const uint32_t len)
	{
		uint32_t i;

		if (len == 0)
		{
			*buffer = device->supported_samplerate_count;
		}
		else if (len <= device->supported_samplerate_count)
		{
			memcpy(buffer, device->supported_samplerates, len * sizeof(uint32_t));

			if (!SAMPLE_TYPE_IS_IQ(device->sample_type))
			{
				for (i = 0; i < len; i++)
				{
					buffer[i] *= 2;
				}
			}
		}
		else
		{
			return AIRSPY_ERROR_INVALID_PARAM;
		}

		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_set_samplerate(airspy_device_t* device, uint32_t samplerate)
	{
		int result;
		uint8_t retval;
		uint8_t length;
		uint32_t i;

		if (samplerate >= MIN_SAMPLERATE_BY_VALUE)
		{
			for (i = 0; i < device->supported_samplerate_count; i++)
			{
				if (samplerate == device->supported_samplerates[i])
				{
					samplerate = i;
					break;
				}
			}

			if (samplerate >= MIN_SAMPLERATE_BY_VALUE)
			{
				if (SAMPLE_TYPE_IS_IQ(device->sample_type))
				{
					samplerate *= 2;
				}
				samplerate /= 1000;
			}
		}

		libusb_clear_halt(device->usb_device, LIBUSB_ENDPOINT_IN | 1);

		length = 1;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SET_SAMPLERATE,
			0,
			samplerate,
			&retval,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_set_receiver_mode(airspy_device_t* device, receiver_mode_t value)
	{
		int result;
		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_RECEIVER_MODE,
			value,
			0,
			NULL,
			0,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result != 0)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_start_rx(airspy_device_t* device, airspy_sample_block_cb_fn callback, void* ctx)
	{
		int result;

		iqconverter_float_reset(device->cnv_f);
		iqconverter_int16_reset(device->cnv_i);

		memset(device->dropped_buffers_queue, 0, RAW_BUFFER_COUNT * sizeof(uint32_t));
		device->dropped_buffers = 0;
		memset(&device->meta, 0, sizeof(device->meta));
		device->meta_valid = false;
		device->expected_sample_index_valid = false;
		device->sof_count_seen = 0;

		result = airspy_set_receiver_mode(device, RECEIVER_MODE_OFF);
		if (result != AIRSPY_SUCCESS)
		{
			return result;
		}

		libusb_clear_halt(device->usb_device, LIBUSB_ENDPOINT_IN | 1);

		device->framing_active = false;
		if (device->framing_requested)
		{
			device->framing_active = (send_framing(device, 1) == AIRSPY_SUCCESS);
		}
		if (device->sof_divider != 0)
		{
			result = send_sof_divider(device, device->sof_divider);
			if (result != AIRSPY_SUCCESS)
			{
				return result;
			}
		}

		result = airspy_set_receiver_mode(device, RECEIVER_MODE_RX);
		if (result == AIRSPY_SUCCESS)
		{
			device->ctx = ctx;
			result = create_io_threads(device, callback);
		}

		return result;
	}

	int ADDCALL airspy_stop_rx(airspy_device_t* device)
	{
		int result1, result2;

		device->stop_requested = true;
		result1 = airspy_set_receiver_mode(device, RECEIVER_MODE_OFF);
		result2 = kill_io_threads(device);

		if (result1 != AIRSPY_SUCCESS)
		{
			return result1;
		}
		return result2;
	}

	int ADDCALL airspy_si5351c_read(airspy_device_t* device, uint8_t register_number, uint8_t* value)
	{
		uint8_t temp_value;
		int result;

		temp_value = 0;
		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SI5351C_READ,
			0,
			register_number,
			(unsigned char*)&temp_value,
			1,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < 1)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			*value = temp_value;
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_si5351c_write(airspy_device_t* device, uint8_t register_number, uint8_t value)
	{
		int result;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SI5351C_WRITE,
			value,
			register_number,
			NULL,
			0,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result != 0)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_r820t_read(airspy_device_t* device, uint8_t register_number, uint8_t* value)
	{
		int result;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_R820T_READ,
			0,
			register_number,
			(unsigned char*)value,
			1,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < 1)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_r820t_write(airspy_device_t* device, uint8_t register_number, uint8_t value)
	{
		int result;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_R820T_WRITE,
			value,
			register_number,
			NULL,
			0,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result != 0)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_gpio_read(airspy_device_t* device, airspy_gpio_port_t port, airspy_gpio_pin_t pin, uint8_t* value)
	{
		int result;
		uint8_t port_pin;

		port_pin = ((uint8_t)port) << 5;
		port_pin = port_pin | pin;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_GPIO_READ,
			0,
			port_pin,
			(unsigned char*)value,
			1,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < 1)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_gpio_write(airspy_device_t* device, airspy_gpio_port_t port, airspy_gpio_pin_t pin, uint8_t value)
	{
		int result;
		uint8_t port_pin;

		port_pin = ((uint8_t)port) << 5;
		port_pin = port_pin | pin;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_GPIO_WRITE,
			value,
			port_pin,
			NULL,
			0,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result != 0)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}


	int ADDCALL airspy_gpiodir_read(airspy_device_t* device, airspy_gpio_port_t port, airspy_gpio_pin_t pin, uint8_t* value)
	{
		int result;
		uint8_t port_pin;

		port_pin = ((uint8_t)port) << 5;
		port_pin = port_pin | pin;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_GPIODIR_READ,
			0,
			port_pin,
			(unsigned char*)value,
			1,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < 1)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_gpiodir_write(airspy_device_t* device, airspy_gpio_port_t port, airspy_gpio_pin_t pin, uint8_t value)
	{
		int result;
		uint8_t port_pin;

		port_pin = ((uint8_t)port) << 5;
		port_pin = port_pin | pin;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_GPIODIR_WRITE,
			value,
			port_pin,
			NULL,
			0,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result != 0)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_spiflash_erase(airspy_device_t* device)
	{
		int result;
		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SPIFLASH_ERASE,
			0,
			0,
			NULL,
			0,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result != 0)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_spiflash_erase_sector(airspy_device_t* device, const uint16_t sector_num)
	{
		int result;
		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SPIFLASH_ERASE_SECTOR,
			sector_num,
			0,
			NULL,
			0,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result != 0)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_spiflash_write(airspy_device_t* device, const uint32_t address, const uint16_t length, unsigned char* const data)
	{
		int result;

		if (address > 0x0FFFFF)
		{
			return AIRSPY_ERROR_INVALID_PARAM;
		}

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SPIFLASH_WRITE,
			address >> 16,
			address & 0xFFFF,
			data,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_spiflash_read(airspy_device_t* device, const uint32_t address, const uint16_t length, unsigned char* data)
	{
		int result;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SPIFLASH_READ,
			address >> 16,
			address & 0xFFFF,
			data,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_get_stream_status(airspy_device_t* device, airspy_stream_status_t* status)
	{
		int result;
		uint32_t* words = (uint32_t*)status;
		size_t i;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_GET_STREAM_STATUS,
			0,
			0,
			(unsigned char*)status,
			sizeof(airspy_stream_status_t),
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < (int)(13 * sizeof(uint32_t))) /* an older firmware stops after pps_count */
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		if (result < (int)sizeof(airspy_stream_status_t))
		{
			memset((uint8_t*)status + result, 0, sizeof(airspy_stream_status_t) - result);
		}

		for (i = 0; i < sizeof(airspy_stream_status_t) / sizeof(uint32_t); i++)
		{
			words[i] = TO_LE(words[i]);
		}
		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_board_id_read(airspy_device_t* device, uint8_t* value)
	{
		int result;
		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_BOARD_ID_READ,
			0,
			0,
			value,
			1,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < 1)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_version_string_read(airspy_device_t* device, char* version, uint8_t length)
	{
#define VERSION_LOCAL_SIZE (128)
		int result;
		char version_local[VERSION_LOCAL_SIZE] = "";

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_VERSION_STRING_READ,
			0,
			0,
			(unsigned char*)version_local,
			(VERSION_LOCAL_SIZE - 1),
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < 0)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else
		{
			if (length > 0)
			{
				const int num_bytes_to_copy = (length > VERSION_LOCAL_SIZE ? VERSION_LOCAL_SIZE : length) - 1;
				memcpy(version, version_local, num_bytes_to_copy);
				version[num_bytes_to_copy] = 0;
				return AIRSPY_SUCCESS;
			}
			else
			{
				return AIRSPY_ERROR_INVALID_PARAM;
			}
		}
	}

	int ADDCALL airspy_board_partid_serialno_read(airspy_device_t* device, airspy_read_partid_serialno_t* read_partid_serialno)
	{
		uint8_t length;
		int result;

		length = sizeof(airspy_read_partid_serialno_t);
		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_BOARD_PARTID_SERIALNO_READ,
			0,
			0,
			(unsigned char*)read_partid_serialno,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {

			read_partid_serialno->part_id[0] = TO_LE(read_partid_serialno->part_id[0]);
			read_partid_serialno->part_id[1] = TO_LE(read_partid_serialno->part_id[1]);
			read_partid_serialno->serial_no[0] = TO_LE(read_partid_serialno->serial_no[0]);
			read_partid_serialno->serial_no[1] = TO_LE(read_partid_serialno->serial_no[1]);
			read_partid_serialno->serial_no[2] = TO_LE(read_partid_serialno->serial_no[2]);
			read_partid_serialno->serial_no[3] = TO_LE(read_partid_serialno->serial_no[3]);

			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_set_sample_type(struct airspy_device* device, enum airspy_sample_type sample_type)
	{
		device->sample_type = sample_type;
		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_set_freq(airspy_device_t* device, const uint32_t freq_hz)
	{
		set_freq_params_t set_freq_params;
		uint8_t length;
		int result;

		set_freq_params.freq_hz = TO_LE(freq_hz);
		length = sizeof(set_freq_params_t);

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SET_FREQ,
			0,
			0,
			(unsigned char*)&set_freq_params,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_set_conversion_filter_float32(struct airspy_device* device, const float *kernel, const uint32_t len)
	{
		if (device->streaming)
		{
			return AIRSPY_ERROR_BUSY;
		}

		iqconverter_float_free(device->cnv_f);
		device->cnv_f = iqconverter_float_create(kernel, len);

		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_set_conversion_filter_int16(struct airspy_device* device, const int16_t *kernel, const uint32_t len)
	{
		if (device->streaming)
		{
			return AIRSPY_ERROR_BUSY;
		}

		iqconverter_int16_free(device->cnv_i);
		device->cnv_i = iqconverter_int16_create(kernel, len);

		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_set_lna_gain(airspy_device_t* device, uint8_t value)
	{
		int result;
		uint8_t retval;
		uint8_t length;

		if (value > 14)
			value = 14;

		length = 1;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SET_LNA_GAIN,
			0,
			value,
			&retval,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_set_mixer_gain(airspy_device_t* device, uint8_t value)
	{
		int result;
		uint8_t retval;
		uint8_t length;

		if (value > 15)
			value = 15;

		length = 1;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SET_MIXER_GAIN,
			0,
			value,
			&retval,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_set_vga_gain(airspy_device_t* device, uint8_t value)
	{
		int result;
		uint8_t retval;
		uint8_t length;

		if (value > 15)
			value = 15;

		length = 1;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SET_VGA_GAIN,
			0,
			value,
			&retval,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_set_lna_agc(airspy_device_t* device, uint8_t value)
	{
		int result;
		uint8_t retval;
		uint8_t length;

		length = 1;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SET_LNA_AGC,
			0,
			value,
			&retval,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_set_mixer_agc(airspy_device_t* device, uint8_t value)
	{
		int result;
		uint8_t retval;
		uint8_t length;

		length = 1;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SET_MIXER_AGC,
			0,
			value,
			&retval,
			length,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < length)
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		else {
			return AIRSPY_SUCCESS;
		}
	}

	int ADDCALL airspy_set_linearity_gain(struct airspy_device* device, uint8_t value)
	{
		int rc;

		if (value >= GAIN_COUNT)
		{
			value = GAIN_COUNT - 1;
		}

		value = GAIN_COUNT - 1 - value;

		rc = airspy_set_mixer_agc(device, 0);
		if (rc < 0)
			return rc;

		rc = airspy_set_lna_agc(device, 0);
		if (rc < 0)
			return rc;

		rc = airspy_set_vga_gain(device, airspy_linearity_vga_gains[value]);
		if (rc < 0)
			return rc;

		rc = airspy_set_mixer_gain(device, airspy_linearity_mixer_gains[value]);
		if (rc < 0)
			return rc;

		rc = airspy_set_lna_gain(device, airspy_linearity_lna_gains[value]);
		if (rc < 0)
			return rc;

		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_set_sensitivity_gain(struct airspy_device* device, uint8_t value)
	{
		int rc;

		if (value >= GAIN_COUNT)
		{
			value = GAIN_COUNT - 1;
		}

		value = GAIN_COUNT - 1 - value;

		rc = airspy_set_mixer_agc(device, 0);
		if (rc < 0)
			return rc;

		rc = airspy_set_lna_agc(device, 0);
		if (rc < 0)
			return rc;

		rc = airspy_set_vga_gain(device, airspy_sensitivity_vga_gains[value]);
		if (rc < 0)
			return rc;

		rc = airspy_set_mixer_gain(device, airspy_sensitivity_mixer_gains[value]);
		if (rc < 0)
			return rc;

		rc = airspy_set_lna_gain(device, airspy_sensitivity_lna_gains[value]);
		if (rc < 0)
			return rc;

		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_set_rf_bias(airspy_device_t* device, uint8_t value)
	{
		return airspy_gpio_write(device, GPIO_PORT1, GPIO_PIN13, value);
	}

	int ADDCALL airspy_set_packing(airspy_device_t* device, uint8_t value)
	{
		int result;
		uint8_t retval;
		bool packing_enabled;

		if (device->streaming)
		{
			return AIRSPY_ERROR_BUSY;
		}
		/* 2 (12-bit with a chunk counter) is not supported by this library */
		if (value != 0 && value != 1 && value != 3)
		{
			return AIRSPY_ERROR_INVALID_PARAM;
		}

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_SET_PACKING,
			0,
			value,
			&retval,
			1,
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < 1)
		{
			return  AIRSPY_ERROR_LIBUSB;
		}

		packing_enabled = value ? true : false;
		if (value != device->packing_mode)
		{
			cancel_transfers(device);
			free_transfers(device);

			device->packing_enabled = packing_enabled;
			device->packing_mode = value;
			device->buffer_size = value == 3 ? (4096 * 36) : packing_enabled ? (6144 * 24) : 262144;

			result = allocate_transfers(device);
			if (result != 0)
			{
				return AIRSPY_ERROR_NO_MEM;
			}
		}

		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_set_framing(airspy_device_t* device, uint8_t value)
	{
		if (device->streaming)
		{
			return AIRSPY_ERROR_BUSY;
		}
		device->framing_requested = value ? true : false;
		if (device->framing_requested)
		{
			return send_framing(device, 1);
		}
		return AIRSPY_SUCCESS;
	}

	static int airspy_watchdog_request(airspy_device_t* device, uint16_t feed, airspy_watchdog_status_t* status)
	{
		airspy_watchdog_status_t local;
		airspy_watchdog_status_t* s = status ? status : &local;
		uint32_t* words = (uint32_t*)s;
		size_t i;
		int result;

		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_WATCHDOG,
			0,
			feed,
			(unsigned char*)s,
			sizeof(airspy_watchdog_status_t),
			LIBUSB_CTRL_TIMEOUT_MS);

		if (result < (int)sizeof(airspy_watchdog_status_t))
		{
			return AIRSPY_ERROR_LIBUSB;
		}
		for (i = 0; i < sizeof(airspy_watchdog_status_t) / sizeof(uint32_t); i++)
		{
			words[i] = TO_LE(words[i]);
		}
		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_watchdog_status(airspy_device_t* device, airspy_watchdog_status_t* status)
	{
		return airspy_watchdog_request(device, 0, status);
	}

	int ADDCALL airspy_watchdog_feed(airspy_device_t* device, airspy_watchdog_status_t* status)
	{
		return airspy_watchdog_request(device, 1, status);
	}

	int ADDCALL airspy_transfer_get_metadata(airspy_transfer* transfer, airspy_transfer_metadata_t* metadata)
	{
		airspy_device_t* device;

		if (transfer == NULL || metadata == NULL || transfer->device == NULL)
		{
			return AIRSPY_ERROR_INVALID_PARAM;
		}
		device = transfer->device;
		if (!device->framing_active || !device->meta_valid)
		{
			return AIRSPY_ERROR_UNSUPPORTED;
		}
		*metadata = device->meta;
		return AIRSPY_SUCCESS;
	}

	int ADDCALL airspy_is_streaming(airspy_device_t* device)
	{
		return (device->streaming == true && device->stop_requested == false);
	}

	const char* ADDCALL airspy_error_name(enum airspy_error errcode)
	{
		switch (errcode)
		{
		case AIRSPY_SUCCESS:
			return "AIRSPY_SUCCESS";

		case AIRSPY_TRUE:
			return "AIRSPY_TRUE";

		case AIRSPY_ERROR_INVALID_PARAM:
			return "AIRSPY_ERROR_INVALID_PARAM";

		case AIRSPY_ERROR_NOT_FOUND:
			return "AIRSPY_ERROR_NOT_FOUND";

		case AIRSPY_ERROR_BUSY:
			return "AIRSPY_ERROR_BUSY";

		case AIRSPY_ERROR_NO_MEM:
			return "AIRSPY_ERROR_NO_MEM";

		case AIRSPY_ERROR_LIBUSB:
			return "AIRSPY_ERROR_LIBUSB";

		case AIRSPY_ERROR_THREAD:
			return "AIRSPY_ERROR_THREAD";

		case AIRSPY_ERROR_STREAMING_THREAD_ERR:
			return "AIRSPY_ERROR_STREAMING_THREAD_ERR";

		case AIRSPY_ERROR_STREAMING_STOPPED:
			return "AIRSPY_ERROR_STREAMING_STOPPED";

		case AIRSPY_ERROR_OTHER:
			return "AIRSPY_ERROR_OTHER";

		default:
			return "airspy unknown error";
		}
	}

	const char* ADDCALL airspy_board_id_name(enum airspy_board_id board_id)
	{
		switch (board_id)
		{
		case AIRSPY_BOARD_ID_PROTO_AIRSPY:
			return "AIRSPY";

		case AIRSPY_BOARD_ID_INVALID:
			return "Invalid Board ID";

		default:
			return "Unknown Board ID";
		}
	}

#ifdef __cplusplus
} // __cplusplus defined.
#endif

int ADDCALL airspy_set_sof_divider(struct airspy_device* device, uint32_t divider)
{
	device->sof_divider = divider;
	return send_sof_divider(device, divider);
}

int ADDCALL airspy_set_uart_baud(struct airspy_device* device, uint32_t baud)
{
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_SET_UART_BAUD,
		baud & 0xFFFF,
		baud >> 16,
		NULL,
		0,
		0);
	if (result != 0)
		return AIRSPY_ERROR_LIBUSB;
	return AIRSPY_SUCCESS;
}

int ADDCALL airspy_uart_write(struct airspy_device* device, const uint8_t* data, uint16_t len)
{
	int result;
	if (len == 0 || len > 64)
		return AIRSPY_ERROR_INVALID_PARAM;
	result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_UART_WRITE,
		0,
		0,
		(unsigned char*)data,
		len,
		0);
	if (result < len)
		return AIRSPY_ERROR_LIBUSB;
	return AIRSPY_SUCCESS;
}

int ADDCALL airspy_set_calibration(struct airspy_device* device, int32_t correction_ppb)
{
	uint32_t v = (uint32_t)correction_ppb;
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_SET_CALIBRATION,
		v & 0xFFFF,
		v >> 16,
		NULL,
		0,
		0);
	if (result != 0)
		return AIRSPY_ERROR_LIBUSB;
	return AIRSPY_SUCCESS;
}

int ADDCALL airspy_get_calibration(struct airspy_device* device, airspy_calibration_t* calibration)
{
	int result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_GET_CALIBRATION,
		0,
		0,
		(unsigned char*)calibration,
		sizeof(airspy_calibration_t),
		0);
	if (result < (int)sizeof(airspy_calibration_t))
		return AIRSPY_ERROR_LIBUSB;
	calibration->correction_ppb = (int32_t)TO_LE((uint32_t)calibration->correction_ppb);
	calibration->source = TO_LE(calibration->source);
	return AIRSPY_SUCCESS;
}

int ADDCALL airspy_mem_read(struct airspy_device* device, uint32_t address, void* data, uint16_t len)
{
	int result;
	if (len == 0 || len > 64)
		return AIRSPY_ERROR_INVALID_PARAM;
	result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_MEM_READ,
		address & 0xFFFF,
		address >> 16,
		(unsigned char*)data,
		len,
		0);
	if (result < len)
		return AIRSPY_ERROR_LIBUSB;
	return AIRSPY_SUCCESS;
}

int ADDCALL airspy_mem_write(struct airspy_device* device, uint32_t address, const void* data, uint16_t len)
{
	int result;
	if (len == 0 || len > 64)
		return AIRSPY_ERROR_INVALID_PARAM;
	result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_MEM_WRITE,
		address & 0xFFFF,
		address >> 16,
		(unsigned char*)data,
		len,
		0);
	if (result < len)
		return AIRSPY_ERROR_LIBUSB;
	return AIRSPY_SUCCESS;
}

int ADDCALL airspy_call(struct airspy_device* device, uint32_t core, uint32_t address, const uint32_t args[4], uint32_t* r0, uint32_t timeout_ms)
{
	airspy_call_request_t req;
	airspy_call_result_t res;
	int result;
	uint32_t waited = 0;

	req.core = TO_LE(core);
	req.address = TO_LE(address);
	req.r[0] = TO_LE(args[0]);
	req.r[1] = TO_LE(args[1]);
	req.r[2] = TO_LE(args[2]);
	req.r[3] = TO_LE(args[3]);
	result = libusb_control_transfer(
		device->usb_device,
		LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
		AIRSPY_CALL, 0, 0, (unsigned char*)&req, sizeof(req), 0);
	if (result < (int)sizeof(req))
		return AIRSPY_ERROR_LIBUSB;

	for (;;)
	{
		result = libusb_control_transfer(
			device->usb_device,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
			AIRSPY_CALL, 0, 0, (unsigned char*)&res, sizeof(res), 0);
		if (result < (int)sizeof(res))
			return AIRSPY_ERROR_LIBUSB;
		res.status = TO_LE(res.status);
		if (res.status == 0)
		{
			*r0 = TO_LE(res.r0);
			return AIRSPY_SUCCESS;
		}
		if (res.status != 1)
			return AIRSPY_ERROR_INVALID_PARAM;
		if (waited >= timeout_ms)
			return AIRSPY_ERROR_BUSY;
#ifdef _WIN32
		Sleep(1);
#else
		usleep(1000);
#endif
		waited++;
	}
}
