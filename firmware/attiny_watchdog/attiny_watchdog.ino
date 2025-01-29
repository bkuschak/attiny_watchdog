// Implements watchdog timer with alert, reboot, and power cycle capability.
// First WDT timeout triggers ALERT.
// Second WDT timeout triggers POWERCYCLE.
//
// For use with ATTINY212-SSN or ATTINY412-SSN.
// ATTINY412-SSN (originally ATTINY212-SSN - just barely fits)
// Program using UPDI programmer.
//
// Pinout:
// 1: VDD
// 2: PA6 - WDT_ALERT 		(active high)
// 3: PA7 - WDT_POWERCYCLE 	(active high)
// 4: PA1 - I2C_SDA		
// 5: PA2 - I2C_SCL
// 6: PA0 - UPDI
// 7: PA3 - RESET_OUT		(active high)
// 8: GND
// 
// Set these options, so that the code fits in flash:
//   clock: 8 MHz internal
//   startup time: 8 msec
//   millis() / micros() disabled
//   printf() default.
//   Wire:  master or slave
//   BOD: 2.6V, enabled / enabled
//   startup time: 8msec
//   EEPROM retained
//   WDT disabled / no delay
//   PWM pins:  default
//   attach interrupts: all
//
// Prerequisite libraries:
//   megaAtTiny  https://github.com/SpenceKonde/megaTinyCore/
//   ATtiny_TimerInterrupt  https://github.com/khoih-prog/ATtiny_TimerInterrupt

#include <avr/io.h>
#include <Wire.h>

// For ATtiny_TimerInterrupt
#define USING_FULL_CLOCK true
#define USING_HALF_CLOCK false
#define USE_TIMER_0 true
#define USE_TIMER_1 false
#define CurrentTimer ITimer0
#include "ATtiny_TimerInterrupt.h"
#include "ATtiny_ISR_Timer.h"

#define FW_VERSION                  0x10      // 4.4 bits major.minor.

// Pin assignments:
#define PORTA_PIN_RESET             (1<<3)
#define PORTA_PIN_ALERT             (1<<6)
#define PORTA_PIN_POWERCYCLE        (1<<7)

#define I2C_SLAVE_ADDR              0x32      // our addr as a slave.

// WDT timer period = 256 * WDT_TICK_INTERVAL_MSEC.
#define WDT_TICK_INTERVAL_MSEC      250       // 64 seconds timeout, 

#define ENABLE_CONFIG_REGISTER

// Register addresses
#define REG_VERSION                 0         // read only
#define REG_CONFIG                  1         // config
#define REG_WDT                     2         // refresh the watchdog

// REG_CONFIG bits:
#define CONFIG_ENABLE_RESET         (1<<0)
#define CONFIG_ENABLE_POWERCYCLE    (1<<1)
#define CONFIG_ENABLE_ALERT         (1<<2)

uint8_t wdt_counter = ~0;   // Default timeout is 256 ticks.
uint8_t wdt_expirations;
uint8_t register_pointer;

// By default enable ALERT and POWERCYCLE
uint8_t config_reg = CONFIG_ENABLE_ALERT | CONFIG_ENABLE_POWERCYCLE;

// RESET is PA3, active high.
void assert_reset() 
{
  PORTA.OUT |= PORTA_PIN_RESET;
  PORTA.DIR |= PORTA_PIN_RESET;
}

void deassert_reset() 
{
  PORTA.OUT &= ~PORTA_PIN_RESET;
  PORTA.DIR |= PORTA_PIN_RESET;
}

// POWERCYCLE is PA7, active high.
void assert_powercycle() 
{
  PORTA.OUT |= PORTA_PIN_POWERCYCLE;
  PORTA.DIR |= PORTA_PIN_POWERCYCLE;
}

void deassert_powercycle() 
{
  PORTA.OUT &= ~PORTA_PIN_POWERCYCLE;
  PORTA.DIR |= PORTA_PIN_POWERCYCLE;
}

// ALERT is PA6, active high.
void assert_alert() 
{
  PORTA.OUT |= PORTA_PIN_ALERT;
  PORTA.DIR |= PORTA_PIN_ALERT;
}

void deassert_alert() 
{
  PORTA.OUT &= ~PORTA_PIN_ALERT;
  PORTA.DIR |= PORTA_PIN_ALERT;
}

// I2C slave mode write.
void i2c_receive_event(int howMany) 
{
  // First byte is address.
  if(Wire.available())
    register_pointer = Wire.read();

  // Return if empty write.
  if(!Wire.available())
    return;

  // Use first data byte. Discard others.
  uint8_t data = Wire.read();
  while(Wire.available()) {
    Wire.read();
  }

  // Apply data.
  switch(register_pointer) {
    case REG_WDT:
      cli();    // critical section
      wdt_counter = data;
      // Also deassert any pending alert.
      deassert_alert();
      sei();
      break;
#ifdef ENABLE_CONFIG_REGISTER
    case REG_CONFIG:
      cli();    // critical section
      config_reg = data;
      sei();
      break;
#endif
    default:
      // Ignore - other registers are read-only.
      break;
  }
}

// I2C slave mode read.  Respond with data.
void i2c_request_event() 
{
  uint8_t tx = 0;
  switch(register_pointer) {
    case REG_VERSION: {
      tx = FW_VERSION;
      break;
    }
#ifdef ENABLE_CONFIG_REGISTER
    case REG_CONFIG: {
      tx = config_reg;
      break;
    }
#endif
    case REG_WDT: {
      tx = wdt_counter;
      break;
    }
    default:
      break;
  }
  Wire.write(tx);
}

// For ATtiny_TimerInterrupt library.
void timer_handler()
{  
  // Decrement the watchdog counter once per loop. Let it wraparound.
  if(--wdt_counter == 0) {
    wdt_expirations++;

#ifdef ENABLE_CONFIG_REGISTER
    if(wdt_expirations == 1) {
      // The first expiration results in an alert
      if (config_reg & CONFIG_ENABLE_ALERT)
          assert_alert();
    } 
    else {
      if(config_reg & CONFIG_ENABLE_RESET) {
        assert_reset();
        delay(50);  // msec
        deassert_reset();
        wdt_expirations = 0;  // reset counter
      }
      if(config_reg & CONFIG_ENABLE_POWERCYCLE) {
        assert_powercycle();
        // We will probably not get here, if the power cycle occurs...
        delay(50);  // msec
        deassert_powercycle();
        wdt_expirations = 0;  // reset counter
      }
    }
#else
    assert_powercycle();  
    // TODO - board will power cycle here.
#endif
  }
}

void setup() 
{
  // Clear outputs.
  PORTA.OUT = 0;
  PORTA.DIR = PORTA_PIN_RESET | PORTA_PIN_ALERT | PORTA_PIN_POWERCYCLE;

  // Set I2C slave mode.
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(i2c_receive_event);
  Wire.onRequest(i2c_request_event);

  // Set up timer library for WDT counter.
  ITimer0.init();
  ITimer0.attachInterruptInterval(WDT_TICK_INTERVAL_MSEC, timer_handler);
}

void loop() 
{
  // Nothing to do here.
}
