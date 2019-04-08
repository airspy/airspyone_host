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

#ifdef USE_SSE2
#include <xmmintrin.h>
#endif
#ifdef USE_NEON
#include <arm_neon.h>
#endif

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
#elif defined(__FreeBSD__)
  #define _inline inline
  #define _aligned_free(mem) free(mem)
void *_aligned_malloc(size_t size, size_t alignment)
{
    void *result;
    if (posix_memalign(&result, alignment, size) == 0)
        return result;
    return 0;
}
#elif defined(__GNUC__) && !defined(__MINGW64_VERSION_MAJOR)
  #include <malloc.h>
  #define _aligned_malloc(size, alignment) memalign(alignment, size)
  #define _aligned_free(mem) free(mem)
  #define _inline inline
#endif

#define SIZE_FACTOR 32
#define DEFAULT_ALIGNMENT 16
#define HPF_COEFF 0.01f

#if defined(_MSC_VER)
	#define ALIGNED __declspec(align(DEFAULT_ALIGNMENT))
#else
	#define ALIGNED
#endif

iqconverter_float_t *iqconverter_float_create(const float *hb_kernel, int len)
{
	int i, j;
	size_t buffer_size;
	iqconverter_float_t *cnv = (iqconverter_float_t *) _aligned_malloc(sizeof(iqconverter_float_t), DEFAULT_ALIGNMENT);

	cnv->len = len / 2 + 1;
	cnv->hbc = hb_kernel[len / 2];

	buffer_size = cnv->len * sizeof(float);

	cnv->fir_kernel = (float *) _aligned_malloc(buffer_size, DEFAULT_ALIGNMENT);
	cnv->fir_queue = (float *) _aligned_malloc(buffer_size * SIZE_FACTOR, DEFAULT_ALIGNMENT);
	cnv->delay_line = (float *) _aligned_malloc(buffer_size / 2, DEFAULT_ALIGNMENT);

	iqconverter_float_reset(cnv);

	for (i = 0, j = 0; i < cnv->len; i++, j += 2)
	{
		cnv->fir_kernel[i] = hb_kernel[j];
	} 

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

static _inline float process_fir_taps_std(const float *kernel, const float *queue, int len)
{
	int i;

	float sum = 0.0f;

	if (len >= 8)
	{
		int it = len >> 3;

		for (i = 0; i < it; i++)
		{
			sum += kernel[0] * queue[0]
				+ kernel[1] * queue[1]
				+ kernel[2] * queue[2]
				+ kernel[3] * queue[3]
				+ kernel[4] * queue[4]
				+ kernel[5] * queue[5]
				+ kernel[6] * queue[6]
				+ kernel[7] * queue[7];

			queue += 8;
			kernel += 8;
		}
		len &= 7;
	}

	if (len >= 4)
	{

		sum += kernel[0] * queue[0]
			+ kernel[1] * queue[1]
			+ kernel[2] * queue[2]
			+ kernel[3] * queue[3];

		kernel += 4;
		queue += 4;
		len &= 3;
	}

	if (len >= 2)
	{
		sum += kernel[0] * queue[0]
			+ kernel[1] * queue[1];

	}

	return sum;
}

static void fir_interleaved_4(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	int fir_index = cnv->fir_index;
	int fir_len = cnv->len;
	float *fir_kernel = cnv->fir_kernel;
	float *fir_queue = cnv->fir_queue;
	float *queue;
	float acc;

	for (i = 0; i < len; i += 2)
	{
		queue = fir_queue + fir_index;

		queue[0] = samples[i];

		acc = fir_kernel[0] * (queue[0] + queue[4 - 1])
			+ fir_kernel[1] * (queue[1] + queue[4 - 2]);

		samples[i] = acc;

		if (--fir_index < 0)
		{
			fir_index = fir_len * (SIZE_FACTOR - 1);
			memcpy(fir_queue + fir_index + 1, fir_queue, (fir_len - 1) * sizeof(float));
		}
	}

	cnv->fir_index = fir_index;
}

static void fir_interleaved_8(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	int fir_index = cnv->fir_index;
	int fir_len = cnv->len;
	float *fir_kernel = cnv->fir_kernel;
	float *fir_queue = cnv->fir_queue;
	float *queue;
	float acc;

	for (i = 0; i < len; i += 2)
	{
		queue = fir_queue + fir_index;

		queue[0] = samples[i];

		acc = fir_kernel[0] * (queue[0] + queue[8 - 1])
			+ fir_kernel[1] * (queue[1] + queue[8 - 2])
			+ fir_kernel[2] * (queue[2] + queue[8 - 3])
			+ fir_kernel[3] * (queue[3] + queue[8 - 4]);

		samples[i] = acc;

		if (--fir_index < 0)
		{
			fir_index = fir_len * (SIZE_FACTOR - 1);
			memcpy(fir_queue + fir_index + 1, fir_queue, (fir_len - 1) * sizeof(float));
		}
	}

	cnv->fir_index = fir_index;
}

static void fir_interleaved_12(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	int fir_index = cnv->fir_index;
	int fir_len = cnv->len;
	float *fir_kernel = cnv->fir_kernel;
	float *fir_queue = cnv->fir_queue;
	float *queue;
	float acc = 0;

	for (i = 0; i < len; i += 2)
	{
		queue = fir_queue + fir_index;

		queue[0] = samples[i];

		acc = fir_kernel[0]  * (queue[0]  + queue[12 - 1])
			+ fir_kernel[1]  * (queue[1]  + queue[12 - 2])
			+ fir_kernel[2]  * (queue[2]  + queue[12 - 3])
			+ fir_kernel[3]  * (queue[3]  + queue[12 - 4])
			+ fir_kernel[4]  * (queue[4]  + queue[12 - 5])
			+ fir_kernel[5]  * (queue[5]  + queue[12 - 6]);

		samples[i] = acc;

		if (--fir_index < 0)
		{
			fir_index = fir_len * (SIZE_FACTOR - 1);
			memcpy(fir_queue + fir_index + 1, fir_queue, (fir_len - 1) * sizeof(float));
		}
	}

	cnv->fir_index = fir_index;
}

static void fir_interleaved_24(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	int fir_index = cnv->fir_index;
	int fir_len = cnv->len;
	float *fir_kernel = cnv->fir_kernel;
	float *fir_queue = cnv->fir_queue;
	float *queue;
	float acc = 0;

	for (i = 0; i < len; i += 2)
	{
		queue = fir_queue + fir_index;

		queue[0] = samples[i];

		acc = fir_kernel[0]  * (queue[0]  + queue[24 - 1])
			+ fir_kernel[1]  * (queue[1]  + queue[24 - 2])
			+ fir_kernel[2]  * (queue[2]  + queue[24 - 3])
			+ fir_kernel[3]  * (queue[3]  + queue[24 - 4])
			+ fir_kernel[4]  * (queue[4]  + queue[24 - 5])
			+ fir_kernel[5]  * (queue[5]  + queue[24 - 6])
			+ fir_kernel[6]  * (queue[6]  + queue[24 - 7])
			+ fir_kernel[7]  * (queue[7]  + queue[24 - 8])
			+ fir_kernel[8]  * (queue[8]  + queue[24 - 9])
			+ fir_kernel[9]  * (queue[9]  + queue[24 - 10])
			+ fir_kernel[10] * (queue[10] + queue[24 - 11])
			+ fir_kernel[11] * (queue[11] + queue[24 - 12]);

		samples[i] = acc;

		if (--fir_index < 0)
		{
			fir_index = fir_len * (SIZE_FACTOR - 1);
			memcpy(fir_queue + fir_index + 1, fir_queue, (fir_len - 1) * sizeof(float));
		}
	}

	cnv->fir_index = fir_index;
}

#define FIR_INTERLEAVED_GENERIC(x)                                             \
  static void fir_interleaved_generic_ ##x(iqconverter_float_t *cnv,                \
                                      float *samples, int len) {               \
    int i;                                                                     \
    int fir_index = cnv->fir_index;                                            \
    int fir_len = cnv->len;                                                    \
    float *fir_kernel = cnv->fir_kernel;                                       \
    float *fir_queue = cnv->fir_queue;                                         \
    float *queue;                                                              \
                                                                               \
    for (i = 0; i < len; i += 2) {                                             \
      queue = fir_queue + fir_index;                                           \
                                                                               \
      queue[0] = samples[i];                                                   \
                                                                               \
      samples[i] = process_fir_taps_##x(fir_kernel, queue, fir_len);           \
                                                                               \
      if (--fir_index < 0) {                                                   \
        fir_index = fir_len * (SIZE_FACTOR - 1);                               \
        memcpy(fir_queue + fir_index + 1, fir_queue,                           \
               (fir_len - 1) * sizeof(float));                                 \
      }                                                                        \
    }                                                                          \
                                                                               \
    cnv->fir_index = fir_index;                                                \
  }

#define FIR_INTERLEAVED(x)                                                     \
  static void fir_interleaved_ ## x(iqconverter_float_t *cnv, float *samples,        \
                              int len) {                                       \
    switch (cnv->len) {                                                        \
    case 4:                                                                    \
      fir_interleaved_4(cnv, samples, len);                                    \
      break;                                                                   \
    case 8:                                                                    \
      fir_interleaved_8(cnv, samples, len);                                    \
      break;                                                                   \
    case 12:                                                                   \
      fir_interleaved_12(cnv, samples, len);                                   \
      break;                                                                   \
    case 24:                                                                   \
      fir_interleaved_24(cnv, samples, len);                                   \
      break;                                                                   \
    default:                                                                   \
      fir_interleaved_generic_ ## x(cnv, samples, len);                        \
      break;                                                                   \
    }                                                                          \
  }

FIR_INTERLEAVED_GENERIC(std);
FIR_INTERLEAVED(std);

static void delay_interleaved(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	ALIGNED int index;
	ALIGNED int half_len;
	ALIGNED float res;
	
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
	ALIGNED float avg = cnv->avg;

	for (i = 0; i < len; i++)
	{
		samples[i] -= avg;
		avg += SCALE * samples[i];
	}

	cnv->avg = avg;
}


static void translate_fs_4(iqconverter_float_t *cnv, float *samples, int len)
{
  int i;
  float hbc = cnv->hbc;

  int j;

  for (i = 0; i < len / 4; i++)
  {
    j = i << 2;
    samples[j + 0] = -samples[j + 0];
    samples[j + 1] = -samples[j + 1] * hbc;
    //samples[j + 2] = samples[j + 2];
    samples[j + 3] = samples[j + 3] * hbc;
  }

  fir_interleaved_std(cnv, samples, len);
  delay_interleaved(cnv, samples + 1, len);
}

#ifdef USE_SSE2
static _inline float process_fir_taps_sse(const float *kernel, const float *queue, int len)
{
	int i;

	__m128 acc = _mm_set_ps(0, 0, 0, 0);

	if (len >= 8)
	{
		int it = len >> 3;

		for (i = 0; i < it; i++)
		{
			__m128 head1 = _mm_loadu_ps(queue);
			__m128 kern1 = _mm_load_ps(kernel);
			__m128 head2 = _mm_loadu_ps(queue + 4);
			__m128 kern2 = _mm_load_ps(kernel + 4);

			__m128 mul1 = _mm_mul_ps(kern1, head1);
			__m128 mul2 = _mm_mul_ps(kern2, head2);

			mul1 = _mm_add_ps(mul1, mul2);

			acc = _mm_add_ps(acc, mul1);

			queue += 8;
			kernel += 8;
		}

		len &= 7;
	}

	if (len >= 4)
	{

		__m128 head = _mm_loadu_ps(queue);
		__m128 kern = _mm_load_ps(kernel);
		__m128 mul = _mm_mul_ps(kern, head);
		acc = _mm_add_ps(acc, mul);


		kernel += 4;
		queue += 4;
		len &= 3;
	}

	__m128 t = _mm_add_ps(acc, _mm_movehl_ps(acc, acc));
	acc = _mm_add_ss(t, _mm_shuffle_ps(t, t, 1));

#ifdef _MSC_VER
    float sum = acc.m128_f32[0];
#else
    float sum = acc[0];
#endif

	if (len >= 2)
	{
		sum += kernel[0] * queue[0]
			+ kernel[1] * queue[1];

		//kernel += 2;
		//queue += 2;
		//len &= 1;
	}

	return sum;
}

FIR_INTERLEAVED_GENERIC(sse);
FIR_INTERLEAVED(sse);

static void remove_dc_sse(iqconverter_float_t *cnv, float *samples, int len)
{

  int i;
  float *buf = samples;
  __m128 avg = _mm_set1_ps(cnv->avg);
  __m128 sc = _mm_set1_ps(SCALE);
  __m128 vec,scaled;
  for (i = 0; i < len/4; i++, buf += 4)
  {
    vec = _mm_load_ps(buf);
    vec = _mm_sub_ps(vec,avg);
    _mm_store_ps(buf,vec);
    scaled = _mm_mul_ps(sc,vec);
    avg = _mm_add_ps(avg,scaled);
    avg = _mm_set1_ps((avg[0]+avg[1]+avg[2]+avg[3])/4);
  }

  // all the elements in average are the same, so we just grab the first one
  cnv->avg = avg[0];
}
static void translate_fs_4_sse(iqconverter_float_t *cnv, float *samples, int len)
{
	int i;
	ALIGNED float hbc = cnv->hbc;

	float *buf = samples;
	ALIGNED __m128 vec;
	ALIGNED __m128 rot = _mm_set_ps(hbc, 1.0f, -hbc, -1.0f);

	for (i = 0; i < len / 4; i++, buf +=4)
	{
		vec = _mm_loadu_ps(buf);
		vec = _mm_mul_ps(vec, rot);
		_mm_storeu_ps(buf, vec);
	}

	fir_interleaved_sse(cnv, samples, len);
	delay_interleaved(cnv, samples + 1, len);
}
#endif


#ifdef USE_NEON
static _inline float process_fir_taps_neon(const float *kernel, const float *queue, int len)
{
  int i;

  float32x4_t acc = vmovq_n_f32(0);
  float32x4_t shuffled;
  if (len >= 8)
  {
    int it = len >> 3;

    for (i = 0; i < it; i++)
    {
      float32x4_t head1 = vld1q_f32(queue);
      float32x4_t kern1 = vld1q_f32(kernel);
      float32x4_t head2 = vld1q_f32(queue + 4);
      float32x4_t kern2 = vld1q_f32(kernel + 4);

      float32x4_t mul1 = vmulq_f32(kern1, head1);
      float32x4_t mul2 = vmulq_f32(kern2, head2);

      mul1 = vaddq_f32(mul1, mul2);

      acc = vaddq_f32(acc, mul1);

      queue += 8;
      kernel += 8;
    }

    len &= 7;
  }

  if (len >= 4)
  {

    float32x4_t head = vld1q_f32(queue);
    float32x4_t kern = vld1q_f32(kernel);
    float32x4_t mul = vmulq_f32(kern, head);
    acc = vaddq_f32(acc, mul);


    kernel += 4;
    queue += 4;
    len &= 3;
  }


  float32x4_t t = vaddq_f32(acc, vcombine_f32(vget_high_f32(acc), vget_high_f32(acc)));
  shuffled[0] = t[1];
  shuffled[1] = t[0];
  shuffled[2] = t[1];
  shuffled[3] = t[0];

  acc = vaddq_f32(t, shuffled);

  float sum = acc[0];

  if (len >= 2)
  {
    sum += kernel[0] * queue[0]
           + kernel[1] * queue[1];
  }

  return sum;
}

static void remove_dc_neon(iqconverter_float_t *cnv, float *samples, int len)
{

  int i;
  float *buf = samples;
  float32x4_t avg = vmovq_n_f32(cnv->avg);
  float32x4_t sc = vmovq_n_f32(SCALE);
  float32x4_t vec,scaled;
  for (i = 0; i < len/4; i++, buf += 4)
  {
    vec = vld1q_f32(buf);
    vec = vsubq_f32(vec,avg);
    vst1q_f32(buf,vec);
    scaled = vmulq_f32(sc,vec);
    avg = vaddq_f32(avg,scaled);
    avg = vmovq_n_f32((avg[0]+avg[1]+avg[2]+avg[3])/4.0f);
  }

  // all the elements in average are the same, so we just grab the first one
  cnv->avg = avg[0];
}

FIR_INTERLEAVED_GENERIC(neon);
FIR_INTERLEAVED(neon);
static void translate_fs_4_neon(iqconverter_float_t *cnv, float *samples, int len)
{
  int i;
  float hbc = cnv->hbc;

  float *buf = samples;
  float32x4_t vec;
  float32x4_t rot = {-1.0, -hbc, 1.0f, hbc};
  for (i = 0; i < len / 4; i++, buf +=4)
  {
    vec = vld1q_f32(buf);
    vec = vmulq_f32(vec, rot);
    vst1q_f32(buf,vec);
  }

  fir_interleaved_neon(cnv, samples, len);
  delay_interleaved(cnv, samples + 1, len);
}
#endif

void iqconverter_float_process_std(iqconverter_float_t *cnv, float *samples, int len)
{
	remove_dc(cnv, samples, len);
	translate_fs_4(cnv, samples, len);
}

#ifdef USE_SSE2
void iqconverter_float_process_sse(iqconverter_float_t *cnv, float *samples, int len)
{
#ifdef FAST_DC_REMOVAL
  remove_dc_sse(cnv, samples, len);
#else
  remove_dc(cnv, samples, len);
#endif
  translate_fs_4_sse(cnv, samples, len);
}
#endif

#ifdef USE_NEON
void iqconverter_float_process_neon(iqconverter_float_t *cnv, float *samples, int len)
{
#ifdef FAST_DC_REMOVAL
  remove_dc_neon(cnv, samples, len);
#else
  remove_dc(cnv, samples, len);
#endif
  translate_fs_4_neon(cnv, samples, len);
}
#endif
