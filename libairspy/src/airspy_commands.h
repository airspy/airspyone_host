/*
Copyright (c) 2013 - 2014, Benjamin Vernoux <bvernoux@airspy.com>

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the 
	documentation and/or other materials provided with the distribution.
    Neither the name of Great Scott Gadgets nor the names of its contributors may be used to endorse or promote products derived from this software
	without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __AIRSPY_COMMANDS_H__
#define __AIRSPY_COMMANDS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum {
	RECEIVER_MODE_OFF = 0,
	RECEIVER_MODE_RX = 1
} receiver_mode_t;

#define AIRSPY_CMD_MAX (18)

// Commands (usb vendor request) shared between Firmware and Host.
typedef enum {
	AIRSPY_INVALID                       = 0 ,
	AIRSPY_RECEIVER_MODE                 = 1 ,
	AIRSPY_SI5351C_WRITE                 = 2 ,
	AIRSPY_SI5351C_READ                  = 3 ,
	AIRSPY_R820T_WRITE                   = 4 ,
	AIRSPY_R820T_READ                    = 5 ,
	AIRSPY_SPIFLASH_ERASE                = 6 ,
	AIRSPY_SPIFLASH_WRITE                = 7 ,
	AIRSPY_SPIFLASH_READ                 = 8 ,
	AIRSPY_BOARD_ID_READ                 = 9 ,
	AIRSPY_VERSION_STRING_READ           = 10,
	AIRSPY_BOARD_PARTID_SERIALNO_READ    = 11,
	AIRSPY_SET_SAMPLE_RATE               = 12,
	AIRSPY_SET_FREQ                      = 13,
	AIRSPY_SET_LNA_GAIN                  = 14,
	AIRSPY_SET_MIXER_GAIN                = 15,
	AIRSPY_SET_VGA_GAIN                  = 16,
	AIRSPY_SET_LNA_AGC                   = 17,
	AIRSPY_SET_MIXER_AGC                 = AIRSPY_CMD_MAX
} airspy_vendor_request;

#ifdef __cplusplus
} // __cplusplus defined.
#endif

#endif//__AIRSPY_COMMANDS_H__
