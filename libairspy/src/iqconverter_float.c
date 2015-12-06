/*
Copyright (C) 2014, Youssef Touil <youssef@airspy.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "iqconverter_float.h"
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#if defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR)
  #include <malloc.h>
  #define _aligned_malloc __mingw_aligned_malloc
  #define _aligned_free  __mingw_aligned_free
  #define _inline inline
  #define FIR_STANDARD
#elif defined(__APPLE__)
  #include <malloc/malloc.h>
  #define _aligned_malloc(size, alignment) malloc(size)
  #define _aligned_free(mem) free(mem)
  #define _inline inline
  #define FIR_STANDARD
#elif defined(__GNUC__) && !defined(__MINGW64_VERSION_MAJOR)
  #include <malloc.h>
  #define _aligned_malloc(size, alignment) memalign(alignment, size)
  #define _aligned_free(mem) free(mem)
  #define _inline inline
  #define FIR_STANDARD
  //#define FIR_AUTO_VECTOR
#else
	#if (_MSC_VER >= 1800)
		#define USE_SSE2
		#include <immintrin.h>
	#else
		#define FIR_AUTO_VECTOR
	#endif
#endif

#define SIZE_FACTOR 32
#define DEFAULT_ALIGNMENT 16
#define HPF_COEFF 0.01f

iqconverter_float_t *iqconverter_float_create(const float *hb_kernel, int len)
{
	int i, j;

#ifdef USE_SSE2

	int original_length;
	int padding;

#endif
	size_t buffer_size;
	iqconverter_float_t *cnv = (iqconverter_float_t *) _aligned_malloc(sizeof(iqconverter_float_t), DEFAULT_ALIGNMENT);

	cnv->len = len / 2 + 1;
	cnv->hbc = hb_kernel[len / 2];

#ifdef USE_SSE2

	original_length = cnv->len;
	padding = 0;

	while (cnv->len % 8 != 0)
	{
		cnv->len++;
		padding++;
	}

#endif

	buffer_size = cnv->len * sizeof(float);

	cnv->fir_kernel = (float *) _aligned_malloc(buffer_size, DEFAULT_ALIGNMENT);
	cnv->fir_queue = (float *) _aligned_malloc(buffer_size * SIZE_FACTOR, DEFAULT_ALIGNMENT);
	cnv->delay_line = (float *) _aligned_malloc(buffer_size / 2, DEFAULT_ALIGNMENT);

	iqconverter_float_reset(cnv);

#ifdef USE_SSE2

	for (i = 0; i < padding / 2; i++)
	{
		cnv->fir_kernel[cnv->len - i - 1] = 0;
		cnv->fir_kernel[i] = 0;
	}

	for (i = 0, j = 0; i < original_length; i++, j += 2)
	{
		cnv->fir_kernel[i + padding / 2] = hb_kernel[j];
	}

#else

	for (i = 0, j = 0; i < cnv->len; i++, j += 2)
	{
		cnv->fir_kernel[i] = hb_kernel[j];
	}

#endif

	return cnv;
}

void iqconverter_float_free(iqconverter_float_t *cnv)
{
	_aligned_free(cnv->fir_kernel);
	_aligned_free(cnv->fir_queue);
	_aligned_free(cnv->delay_line);
	_aligned_free(cnv);
}

void iqconverter_float_reset(iqconverter_float_t *cnv)
{
	cnv->avg = 0.0f;
	cnv->fir_index = 0;
	cnv->delay_index = 0;
	memset(cnv->delay_line, 0, cnv->len * sizeof(float) / 2);
	memset(cnv->fir_queue, 0, cnv->len * sizeof(float) * SIZE_FACTOR);
}

#ifdef USE_SSE2 /* VC only */

_inline float process_folded_fir_sse2(const float *fir_kernel, const float *queue_head, const float *queue_tail, int len)
{
	__m128 acc = _mm_set_ps(0, 0, 0, 0);

	queue_tail -= 3;

	len >>= 2;
	while (len > 0)
	{
		__m128 head = _mm_loadu_ps(queue_head);
		__m128 tail = _mm_loadu_ps(queue_tail);
		__m128 kern = _mm_load_ps(fir_kernel);

		tail = _mm_shuffle_ps(tail, tail, 0x1b);  // swap the order
		__m128 t1 = _mm_add_ps(tail, head);       // add the head
		t1 = _mm_mul_ps(t1, kern);                // mul
		acc = _mm_add_ps(acc, t1);                // add

		queue_head += 4;
		queue_tail -= 4;
		fir_kernel += 4;
		len--;
	}

	// horizontal sum
	const __m128 t = _mm_add_ps(acc, _mm_movehl_ps(acc, acc));
	const __m128 sum = _mm_add_ss(t, _mm_shuffle_ps(t, t, 1));

	return sum.m128_f32[0];
}

#endif

static void fir_interleaved(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;

#ifdef FIR_AUTO_VECTOR

	int j;
#endif

	int fir_index;
	int fir_len;
	float *queue;
	float acc;

#if defined(USE_SSE2) | defined(FIR_STANDARD)

	float *ptr1;
	float *ptr2;
	float *ptr3;

#endif

	fir_index = cnv->fir_index;

	for (i = 0; i < len; i += 2)
	{
		queue = cnv->fir_queue + fir_index;

		queue[0] = samples[i];

		fir_len = cnv->len;

#ifdef USE_SSE2

		ptr1 = cnv->fir_kernel;
		ptr2 = queue;
		ptr3 = queue + fir_len - 1;
		fir_len /= 2;

		acc = process_folded_fir_sse2(ptr1, ptr2, ptr3, fir_len);

#endif

#ifdef FIR_AUTO_VECTOR

		acc = 0;

		// Auto vectorization works on VS2012, VS2013, VS2015 and GCC
		for (j = 0; j < fir_len; j++)
		{
			acc += cnv->fir_kernel[j] * queue[j];
		}

#endif

#ifdef FIR_STANDARD

		ptr1 = cnv->fir_kernel;
		ptr2 = queue;
		ptr3 = queue + fir_len - 1;
		fir_len /= 2;
		acc = 0;
		
		do
		{
			acc += ptr1[0] * (ptr2[0] + ptr3[0])
				+ ptr1[1] * (ptr2[1] + ptr3[-1])
				+ ptr1[2] * (ptr2[2] + ptr3[-2])
				+ ptr1[3] * (ptr2[3] + ptr3[-3]);

			ptr1 += 4;
			ptr2 += 4;
			ptr3 -= 4;
		} while ((fir_len -= 4) >= 4);

		while (fir_len-- > 0)
		{
			acc += *ptr1++ * (*ptr2++ + *ptr3--);
		}

#endif

		if (--fir_index < 0)
		{
			fir_index = cnv->len * (SIZE_FACTOR - 1);
			memcpy(cnv->fir_queue + fir_index + 1, cnv->fir_queue, (cnv->len - 1) * sizeof(float));
		}

		samples[i] = acc;
	}

	cnv->fir_index = fir_index;
}

static void delay_interleaved(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	int index;
	int half_len;
	float res;
	
	half_len = cnv->len >> 1;
	index = cnv->delay_index;

	for (i = 0; i < len; i += 2)
	{
		res = cnv->delay_line[index];
		cnv->delay_line[index] = samples[i];
		samples[i] = res;

		if (++index >= half_len)
		{
			index = 0;
		}
	}
	
	cnv->delay_index = index;
}

#define SCALE (0.01f)

static void remove_dc(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	float avg = cnv->avg;
	float sample;

	for (i = 0; i < len; i++)
	{
		sample = *samples - avg;
		avg += SCALE * sample;
		*samples++ = sample;
	}

	cnv->avg = avg;
}

static void translate_fs_4(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	float hbc = cnv->hbc;

#if defined(USE_SSE2)

	float *buf = samples;
	__m128 vec;
	__m128 rot = _mm_set_ps(hbc, 1.0f, -hbc, -1.0f);

	for (i = 0; i < len / 4; i++, buf +=4)
	{
		vec = _mm_loadu_ps(buf);
		vec = _mm_mul_ps(vec, rot);
		_mm_storeu_ps(buf, vec);
	}

#else

	int j;

	for (i = 0; i < len / 4; i++)
	{
		j = i << 2;
		samples[j + 0] = -samples[j + 0];
		samples[j + 1] = -samples[j + 1] * hbc;
		//samples[j + 2] = samples[j + 2];
		samples[j + 3] = samples[j + 3] * hbc;
	}

#endif

	fir_interleaved(cnv, samples, len);
	delay_interleaved(cnv, samples + 1, len);
}

void iqconverter_float_process(iqconverter_float_t *cnv, float *samples, int len)
{
	remove_dc(cnv, samples, len);
	translate_fs_4(cnv, samples, len);
}
