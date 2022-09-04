AirSpy
======

A tiny and efficient software defined radio.

This repository contains host software (Linux/Windows) for AirSpy, a project to
produce a low cost, open source software radio platform.

AirSpy: http://www.airspy.com

## How to build host software on Windows:

### For VisualStudio 2013 or later:

* `git clone https://github.com/airspy/airspyone_host.git host`
* Download https://github.com/libusb/libusb/releases/download/v1.0.20/libusb-1.0.20.7z
* Extract **libusb-1.0.20.7z** to host directory
  * You should have **host\libusb-1.0.20**
    * Inside this directory there is README.txt, libusb-1.0.def and some directories
* Download ftp://mirrors.kernel.org/sourceware/pthreads-win32/pthreads-w32-2-9-1-release.zip
* Extract **pthreads-w32-2-9-1-release.zip** to host directory
  * You should have **host\libpthread-2-9-1-win**
    * Inside this directory there is lot of README files and some directories (dll, include, lib)
* Launch host\libairspy\vc\airspy_2013.sln with VisualStudio 2013, choose **Release** and **Build Solution**

### Windows MSYS2/mingw64
- Install MSYS2/mingw64 from https://www.msys2.org
- Start mingw64 console
- `pacman -Syu`
- `pacman -S git mingw-w64-x86_64-make mingw-w64-x86_64-pkgconf mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb`

- For Windows OS less than Vista Install Windows driver for AirSpy hardware or use Zadig see http://sourceforge.net/projects/libwdi/files/zadig
  - If you want to use Zadig  select AirSpy USB device and just install/replace it with WinUSB driver.
 
>**Note for Windows build:**
 You shall always execute airspy_tools from Windows command shell and not from Cygwin or Mingw shell because on Cygwin/Mingw
 Ctrl C is not managed correctly and especially for airspy_rx the Ctrl C(abort) will not stop correctly and will corrupt the file.

```
git clone https://github.com/airspy/airspyone_host.git host
cd host
mkdir build
cd build
```

#### Release version
```
cmake ../ -G "MinGW Makefiles"
mingw32-make.exe clean all
```
- Optional copy libusb-1.0.dll & libwinpthread-1.dll to same directory as the executable(if it is not in your path)
  - `cp /mingw64/bin/libusb-1.0.dll ./airspy-tools/src/`
  - `cp /mingw64/bin/libwinpthread-1.dll ./airspy-tools/src/`

#### Debug version
```
cmake ../ -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
mingw32-make.exe clean all
```
- Optional copy libusb-1.0.dll & libwinpthread-1.dll to same directory as the executable(if it is not in your path)
  - `cp /mingw64/bin/libusb-1.0.dll ./airspy-tools/src/`
  - `cp /mingw64/bin/libwinpthread-1.dll ./airspy-tools/src/`

## How to build the host software on Linux:

### Prerequisites for Linux (Debian/Ubuntu/Raspbian):


`sudo apt-get install build-essential cmake libusb-1.0-0-dev pkg-config`


### Build host software on Linux:

```
wget https://github.com/airspy/airspyone_host/archive/master.zip
unzip master.zip
cd airspyone_host-master
mkdir build
cd build
cmake ../ -DINSTALL_UDEV_RULES=ON
make
sudo make install
sudo ldconfig
```

## Clean CMake temporary files/dirs:
```
cd airspyone_host-master/build
rm -rf *
```
## How to build host software on FreeBSD.

### Get the prerequisites (from root)

`pkg install git cmake`

### Build
```
git clone https:\\github.com\airspy\airspyone_host.git
cd airspyone_host
mkdir build
cd build
cmake .. -DLIBUSB_LIBRARIES=/usr/lib/libusb.so
make
```
(from root)
`make install`

### Add users to group plugdev

(from root)
`usermod -a -G plugdev <user>`

## Principal authors:

Benjamin Vernoux <bvernoux@gmail.com> and Youssef Touil <youssef@airspy.com> 


http://www.airspy.com

This file is part of AirSpy (based on HackRF project see http://greatscottgadgets.com/hackrf/).
