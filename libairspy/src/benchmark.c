#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "filters.h"
#include "iqconverter_float.h"

double print_delta_ops(const char *name,struct timespec start, struct timespec end,int ops) {
  double elapsed = end.tv_sec - start.tv_sec +(end.tv_nsec - start.tv_nsec)/1e9;
  printf("%16s %d ops in %f sec (%f ops/sec)\n",name,ops,elapsed, ops/elapsed);
  return elapsed;
}

#define START_TIME(name) struct timespec name ## start, name ## end; \
  clock_gettime(CLOCK_MONOTONIC,&name ## start);

#define END_TIME_OPS(name,var, ops) clock_gettime(CLOCK_MONOTONIC,&name ## end); \
  var = print_delta_ops(#name ,name##start,name##end,ops);

double bench_standard(float *x, int nSamples) {
  double elapsed;
  iqconverter_float_t *conv = iqconverter_float_create(HB_KERNEL_FLOAT, HB_KERNEL_FLOAT_LEN);
  START_TIME(standard);
  iqconverter_float_process_std(conv,x,nSamples);
  END_TIME_OPS(standard,elapsed,nSamples);
  iqconverter_float_free(conv);
  return elapsed;
}

#if USE_SSE2
double bench_sse(float *x, int nSamples) {
  double elapsed;
  iqconverter_float_t *conv = iqconverter_float_create(HB_KERNEL_FLOAT, HB_KERNEL_FLOAT_LEN);
  START_TIME(sse);
  iqconverter_float_process_sse(conv,x,nSamples);
  END_TIME_OPS(sse,elapsed,nSamples);
  iqconverter_float_free(conv);
  return elapsed;
}
#endif

#if USE_NEON
double bench_neon(float *x, int nSamples) {
  double elapsed;
  iqconverter_float_t *conv = iqconverter_float_create(HB_KERNEL_FLOAT, HB_KERNEL_FLOAT_LEN);
  START_TIME(neon);
  iqconverter_float_process_neon(conv,x,nSamples);
  END_TIME_OPS(neon,elapsed,nSamples);
  iqconverter_float_free(conv);
  return elapsed;
}
#endif
void validate(float *x, int nSamples) {
  float *cpyInput = malloc(sizeof(float)*nSamples);
  memcpy(cpyInput,x,sizeof(float)*nSamples);

  iqconverter_float_t *conv = iqconverter_float_create(HB_KERNEL_FLOAT, HB_KERNEL_FLOAT_LEN);
  iqconverter_float_process_std(conv,x,nSamples);
  iqconverter_float_process_std(conv,x,nSamples);
  iqconverter_float_free(conv);


  conv = iqconverter_float_create(HB_KERNEL_FLOAT, HB_KERNEL_FLOAT_LEN);
#ifdef USE_SSE2
  iqconverter_float_process_sse(conv,cpyInput,nSamples);
  iqconverter_float_process_sse(conv,cpyInput,nSamples);
#endif
#ifdef USE_NEON
  iqconverter_float_process_neon(conv,cpyInput,nSamples);
  iqconverter_float_process_neon(conv,cpyInput,nSamples);
#endif
  iqconverter_float_free(conv);

  for (int i = 0; i < nSamples; i++) {
    if (x[i] != cpyInput[i]) {
      printf("differed at %d: %f vs %f\n",i,x[i],cpyInput[i]);
    }
  }

  free(cpyInput);
}
int main() {
  srand(time(NULL));
  // validation
  float *x = malloc(sizeof(float) * 4);
  for (int i = 0; i < 4; i++) {
    x[i] = rand()/(float)RAND_MAX;
  }
  validate(x,4);
  free(x);

  // benchmarks
  for (int sz = 3000000; sz <= 60000000; sz += 10000000) {
    double std;
    double bmark;
    float *x = malloc(sizeof(float) * sz);
    for (int i = 0; i < sz; i++) {
      x[i] = rand()/(float)RAND_MAX;
    }
    std = bench_standard(x,sz);
#if USE_SSE2
    for (int i = 0; i < sz; i++) {
      x[i] = rand()/(float)RAND_MAX;
    }
    bmark = bench_sse(x,sz);
    free(x);
#endif
#if USE_NEON
    for (int i = 0; i < sz; i++) {
      x[i] = rand()/(float)RAND_MAX;
    }
    bmark = bench_neon(x,sz);
    free(x);
#endif
    printf("         speedup %0.2fx\n",std/bmark);
  }

  return 0;
}
