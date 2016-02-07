AirSpy
======

A tiny and efficient software defined radio.

This repository contains host software (Linux/Windows) for AirSpy, a project to
produce a low cost, open source software radio platform.

AirSpy: http://www.airspy.com

##How to build host software on Windows:

###Prerequisites for mingw:

* cmake-2.8.12.1 or more see http://www.cmake.org/cmake/resources/software.html
* libusb-1.0.19 or more see http://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.19/libusb-1.0.19.7z/download
* For Windows OS less than Vista Install Windows driver for AirSpy hardware or use Zadig see http://sourceforge.net/projects/libwdi/files/zadig
  - If you want to use Zadig  select AirSpy USB device and just install/replace it with WinUSB driver.

>**Note for Windows build:**
 You shall always execute airspy_tools from Windows command shell and not from Cygwin or Mingw shell because on Cygwin/Mingw
 Ctrl C is not managed correctly and especially for airspy_rx the Ctrl C(abort) will not stop correctly and will corrupt the file.

###For VisualStudio 2013 or later:

* Download http://sourceforge.net/projects/libusb/files/libusb-1.0/libusb-1.0.19/libusb-1.0.19.7z/download
* Extract libusb-1.0.19.7z to host directory
* Download ftp://mirrors.kernel.org/sourceware/pthreads-win32/pthreads-w32-2-9-1-release.zip
* Extract pthreads-w32-2-9-1-release.zip to host directory
* Launch host\libairspy\vc\airspy.sln with VisualStudio, choose Release and Build

###For MinGW:

`cd host`

`mkdir build`

`cd build`

Normal version:

* 
`cmake ../ -G "MSYS Makefiles" -DLIBUSB_INCLUDE_DIR=/usr/local/include/libusb-1.0/`

Debug version:

* 
`cmake ../ -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Debug -DLIBUSB_INCLUDE_DIR=/usr/local/include/libusb-1.0/`

`make`

`make install`


##How to build the host software on Linux:

###Prerequisites for Linux (Debian/Ubuntu/Raspbian):


`sudo apt-get install build-essential cmake libusb-1.0-0-dev pkg-config`


###Build host software on Linux:

`wget https://github.com/airspy/host/archive/master.zip`

`unzip host-master`

`cd host-master`

`mkdir build`

`cd build`

`cmake ../ -DINSTALL_UDEV_RULES=ON`

`make`

`sudo make install`

`sudo ldconfig`

##Clean CMake temporary files/dirs:

`cd host-master/build`

`rm -rf *`


##Principal authors:

Benjamin Vernoux <bvernoux@airspy.com> and Youssef Touil <youssef@airspy.com> 


http://www.airspy.com

This file is part of AirSpy (based on HackRF project see http://greatscottgadgets.com/hackrf/).
