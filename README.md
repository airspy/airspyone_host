AirSpy
======

A tiny and efficient software defined radio.

This repository contains host software (Linux/Windows) for AirSpy, a project to
produce a low cost, open source software radio platform.

AirSpy: http://www.airspy.com

##How to build host software on Windows:

###Prerequisite for cygwin or mingw:

* cmake-2.8.12.1 or more see http://www.cmake.org/cmake/resources/software.html
* libusbx-1.0.18 or more see http://sourceforge.net/projects/libusbx/files/latest/download?source=files
* Install Windows driver for AirSpy hardware or use Zadig see http://sourceforge.net/projects/libwdi/files/zadig
  - If you want to use Zadig  select AirSpy USB device and just install/replace it with WinUSB driver.

>**Note for Windows build:**
 You shall always execute airspy_tools from Windows command shell and not from Cygwin or Mingw shell because on Cygwin/Mingw
 Ctrl C is not managed correctly and especially for airspy_rx the Ctrl C(abort) will not stop correctly and will corrupt the file.

###For VisualStudio 2012 or later (only to build library):

* Download http://sourceforge.net/projects/libusbx/files/releases/1.0.18/binaries/libusbx-1.0.18-win.7z/download
* Extract libusbx-1.0.18-win.7z to host directory
* Download ftp://mirrors.kernel.org/sourceware/pthreads-win32/pthreads-w32-2-9-1-release.zip
* Extract pthreads-w32-2-9-1-release.zip to host directory
* Launch host\libairspy\vc\airspy.sln with VisualStudio, choose Release and Build


###For Cygwin:

`cmake -G "Unix Makefiles" -DCMAKE_LEGACY_CYGWIN_WIN32=1 -DLIBUSB_INCLUDE_DIR=/usr/local/include/libusb-1.0/`

`make`

`make install`


###For Mingw:

Normal version:

* 
`cmake -G "MSYS Makefiles" -DLIBUSB_INCLUDE_DIR=/usr/local/include/libusb-1.0/`

Debug version:

* 
`cmake -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug -DLIBUSB_INCLUDE_DIR=/usr/local/include/libusb-1.0/`

`make`

`make install`


##How to build host software on Linux:

###Prerequisite for Linux(Ubuntu or other):


`apt-get install g++`

`apt-get install cmake`

`apt-get install libusb-1.0`


###Build host software on Linux:


`cmake ./`

`make`

`make install`

##Clean Cmake temporary files/dirs:
`cd host`

`find . -name CMakeCache.txt -type f -exec rm -v {} \;`

`find . -name cmake_install.cmake -type f -exec rm -v {} \;`

`find . -name cmake_uninstall.cmake -type f -exec rm -v {} \;`

`find . -name Makefile -type f -exec rm -v {} \;`

`find . -type d -name CMakeFiles -exec rm -rf {} +`


##Principal authors:

Benjamin Vernoux <bvernoux@airspy.com> and Youssef Touil <youssef@airspy.com> 


http://www.airspy.com

This file is part of AirSpy (based on HackRF project see http://greatscottgadgets.com/hackrf/).
