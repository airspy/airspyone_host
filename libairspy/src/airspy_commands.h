/*
* Copyright (c) 2013/2014, Benjamin Vernoux <bvernoux@gmail.com>
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

#ifndef __AIRSPY_COMMANDS_H__
#define __AIRSPY_COMMANDS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
	RECEIVER_MODE_OFF = 0,
	RECEIVER_MODE_RX = 1
} receiver_mode_t;

typedef enum 
{
	AIRSPY_SAMPLERATE_10MSPS = 0, /* 12bits 10MHz IQ */
	AIRSPY_SAMPLERATE_2_5MSPS = 1, /* 12bits 2.5MHz IQ */
	AIRSPY_SAMPLERATE_END = 2 /* End index for sample rate (corresponds to number of samplerate) */
} airspy_samplerate_t;
#define AIRSPY_CONF_CMD_SHIFT_BIT (3) // Up to 3bits=8 samplerates (airspy_samplerate_t enum shall not exceed 7)

// Commands (usb vendor request) shared between Firmware and Host.
#define AIRSPY_CMD_MAX (24)
typedef enum
{
	AIRSPY_INVALID                    = 0 ,
	AIRSPY_RECEIVER_MODE              = 1 ,
	AIRSPY_SI5351C_WRITE              = 2 ,
	AIRSPY_SI5351C_READ               = 3 ,
	AIRSPY_R820T_WRITE                = 4 ,
	AIRSPY_R820T_READ                 = 5 ,
	AIRSPY_SPIFLASH_ERASE             = 6 ,
	AIRSPY_SPIFLASH_WRITE             = 7 ,
	AIRSPY_SPIFLASH_READ              = 8 ,
	AIRSPY_BOARD_ID_READ              = 9 ,
	AIRSPY_VERSION_STRING_READ        = 10,
	AIRSPY_BOARD_PARTID_SERIALNO_READ = 11,
	AIRSPY_SET_SAMPLERATE             = 12,
	AIRSPY_SET_FREQ                   = 13,
	AIRSPY_SET_LNA_GAIN               = 14,
	AIRSPY_SET_MIXER_GAIN             = 15,
	AIRSPY_SET_VGA_GAIN               = 16,
	AIRSPY_SET_LNA_AGC                = 17,
	AIRSPY_SET_MIXER_AGC              = 18,
	AIRSPY_MS_VENDOR_CMD              = 19,
	AIRSPY_SET_RF_BIAS_CMD            = 20,
	AIRSPY_GPIO_WRITE                 = 21,
	AIRSPY_GPIO_READ                  = 22,
	AIRSPY_GPIODIR_WRITE              = 23,
	AIRSPY_GPIODIR_READ               = AIRSPY_CMD_MAX
} airspy_vendor_request;

typedef enum
{
	GPIO_PORT0 = 0,
	GPIO_PORT1 = 1,
	GPIO_PORT2 = 2,
	GPIO_PORT3 = 3,
	GPIO_PORT4 = 4,
	GPIO_PORT5 = 5,
	GPIO_PORT6 = 6,
	GPIO_PORT7 = 7
} airspy_gpio_port_t;

typedef enum
{
	GPIO_PIN0 = 0,
	GPIO_PIN1 = 1,
	GPIO_PIN2 = 2,
	GPIO_PIN3 = 3,
	GPIO_PIN4 = 4,
	GPIO_PIN5 = 5,
	GPIO_PIN6 = 6,
	GPIO_PIN7 = 7,
	GPIO_PIN8 = 8,
	GPIO_PIN9 = 9,
	GPIO_PIN10 = 10,
	GPIO_PIN11 = 11,
	GPIO_PIN12 = 12,
	GPIO_PIN13 = 13,
	GPIO_PIN14 = 14,
	GPIO_PIN15 = 15,
	GPIO_PIN16 = 16,
	GPIO_PIN17 = 17,
	GPIO_PIN18 = 18,
	GPIO_PIN19 = 19,
	GPIO_PIN20 = 20,
	GPIO_PIN21 = 21,
	GPIO_PIN22 = 22,
	GPIO_PIN23 = 23,
	GPIO_PIN24 = 24,
	GPIO_PIN25 = 25,
	GPIO_PIN26 = 26,
	GPIO_PIN27 = 27,
	GPIO_PIN28 = 28,
	GPIO_PIN29 = 29,
	GPIO_PIN30 = 30,
	GPIO_PIN31 = 31
} airspy_gpio_pin_t;

#ifdef __cplusplus
} // __cplusplus defined.
#endif

#endif//__AIRSPY_COMMANDS_H__
