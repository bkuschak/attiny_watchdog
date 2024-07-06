# attiny_watchdog
This project implements a long-duration digital watchdog timer with I2C interface.  It runs on the inexpensive ATTINY412-SSN.  It can be added to a custom PCB to provide a flexible watchdog functionality for a system.

## Device Firmware
Compile with Arduino IDE. Tested with Arduino 1.8.13. Install using a UPDI programmer.  

Prerequisite libraries:
- [megaAtTiny](https://github.com/SpenceKonde/megaTinyCore)
- [ATtiny_TimerInterrupt](https://github.com/khoih-prog/ATtiny_TimerInterrupt)

## Device Interface
The device has a 7-bit I2C slave address of ```0x32```. There are three internal registers:
```
0x00  VERSION      Read-only. 4 MSBs major version, 4 LSBs minor version.
0x01  CONFIG       Enable the outputs (alert, reset, powercycle).
0x02  WDT          Refresh and set the next timeout period.
```

The 8-bit timer decrements every timer tick.  The first time it reaches zero the ALERT output is triggered (if enabled).  The second time it reachs zero the RESET and/or POWERCYCLE outputs are triggered (if enabled).  The timer tick period is 250 milliseconds (```#define WDT_TICK_INTERVAL_MSEC```), resulting in a maximum timeout of 64 seconds.

## Linux Driver
The Linux driver provides a standard ```/dev/watchdog``` interface. Build and install the driver:
```
cd driver
make
make install
```
Any userspace watchdog daemon can be used to control/refresh the watchdog, such as the Debian [watchdog](https://sources.debian.org/src/watchdog/5.16-1/) utility.
Refer to the daemon docs for installation and configuration instructions.

The driver logs a message every time the watchdog is refreshed.  On a system using ```/dev/watchdog2``` at I2C address ```0x32```:
```
[578491.970519] attiny_wdt 2-0032: watchdog2: staring timer
```
