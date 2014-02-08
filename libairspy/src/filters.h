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

#define HB_KERNEL_FLOAT_LEN 47

const float HB_KERNEL_FLOAT[HB_KERNEL_FLOAT_LEN] =
{
	-1.975722792229084e-004,
	0,
	5.760704520844917e-004,
	0,
	-1.350975944268072e-003,
	0,
	2.727516389781759e-003,
	0,
	-4.986043929625469e-003,
	0,
	8.496970950995700e-003,
	0,
	-1.378583469426858e-002,
	0,
	2.171044581860376e-002,
	0,
	-3.397756532486159e-002,
	0,
	5.494288268556362e-002,
	0,
	-1.006560861576759e-001,
	0,
	3.164569315564292e-001,
	5.000000000000000e-001,
	3.164569315564292e-001,
	0,
	-1.006560861576759e-001,
	0,
	5.494288268556362e-002,
	0,
	-3.397756532486159e-002,
	0,
	2.171044581860376e-002,
	0,
	-1.378583469426858e-002,
	0,
	8.496970950995700e-003,
	0,
	-4.986043929625469e-003,
	0,
	2.727516389781759e-003,
	0,
	-1.350975944268072e-003,
	0,
	5.760704520844917e-004,
	0,
	-1.975722792229084e-004
};

#define HB_KERNEL_INT16_LEN 47

const int16_t HB_KERNEL_INT16[HB_KERNEL_INT16_LEN] =
{
	-6,
	0,
	19,
	0,
	-44,
	0,
	89,
	0,
	-163,
	0,
	278,
	0,
	-452,
	0,
	711,
	0,
	-1113,
	0,
	1800,
	0,
	-3298,
	0,
	10370,
	16384,
	10370,
	0,
	-3298,
	0,
	1800,
	0,
	-1113,
	0,
	711,
	0,
	-452,
	0,
	278,
	0,
	-163,
	0,
	89,
	0,
	-44,
	0,
	19,
	0,
	-6
};

#endif // FILTERS_H
