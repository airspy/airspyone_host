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

#ifndef FILTERS_H
#define FILTERS_H

#include <stdint.h>

#define HB_KERNEL_FLOAT_LEN 63

const float HB_KERNEL_FLOAT[HB_KERNEL_FLOAT_LEN] =
{
	-1.344381109451842e-004,
	0,
	2.991185525531317e-004,
	0,
	-6.124599257746754e-004,
	0,
	1.119361527824933e-003,
	0,
	-1.892305996920845e-003,
	0,
	3.019503628265370e-003,
	0,
	-4.608869413493569e-003,
	0,
	6.796466674122750e-003,
	0,
	-9.763716448335385e-003,
	0,
	1.377446283266286e-002,
	0,
	-1.925584351116803e-002,
	0,
	2.699290217719472e-002,
	0,
	-3.865925716690642e-002,
	0,
	5.862682016413107e-002,
	0,
	-1.030126050180239e-001,
	0,
	3.172678141686106e-001,
	5.000000000000000e-001,
	3.172678141686106e-001,
	0,
	-1.030126050180239e-001,
	0,
	5.862682016413107e-002,
	0,
	-3.865925716690642e-002,
	0,
	2.699290217719472e-002,
	0,
	-1.925584351116803e-002,
	0,
	1.377446283266286e-002,
	0,
	-9.763716448335385e-003,
	0,
	6.796466674122750e-003,
	0,
	-4.608869413493569e-003,
	0,
	3.019503628265370e-003,
	0,
	-1.892305996920845e-003,
	0,
	1.119361527824933e-003,
	0,
	-6.124599257746754e-004,
	0,
	2.991185525531317e-004,
	0,
	-1.344381109451842e-004
};

#define HB_KERNEL_INT16_LEN 47

const int16_t HB_KERNEL_INT16[HB_KERNEL_INT16_LEN] =
{
	-1,
	0,
	3,
	0,
	-11,
	0,
	31,
	0,
	-72,
	0,
	153,
	0,
	-295,
	0,
	537,
	0,
	-940,
	0,
	1653,
	0,
	-3198,
	0,
	10334,
	16384,
	10334,
	0,
	-3198,
	0,
	1653,
	0,
	-940,
	0,
	537,
	0,
	-295,
	0,
	153,
	0,
	-72,
	0,
	31,
	0,
	-11,
	0,
	3,
	0,
	-1
};

#endif // FILTERS_H
