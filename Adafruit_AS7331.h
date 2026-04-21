/*!
 * @file Adafruit_AS7331.h
 *
 * This is a library for the AS7331 UV Spectral Sensor
 * https://www.adafruit.com/product/XXXX
 *
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * Written by Limor Fried/Ladyada for Adafruit Industries.
 * BSD license, all text above must be included in any redistribution
 */

#pragma once

#include <Adafruit_BusIO_Register.h>
#include <Arduino.h>
#include <Wire.h>

#define AS7331_DEFAULT_ADDRESS 0x74 ///< Default I2C address

#define AS7331_REG_OSR 0x00 ///< Operational State Register
#define AS7331_REG_STATUS                                                      \
  0x00 ///< Status byte when reading 16-bits from OSR in measurement state
#define AS7331_REG_TEMP 0x01  ///< Temperature register
#define AS7331_REG_AGEN 0x02  ///< Analog gain enable register
#define AS7331_REG_MRES1 0x02 ///< Measurement result 1 register
#define AS7331_REG_MRES2 0x03 ///< Measurement result 2 register
#define AS7331_REG_MRES3 0x04 ///< Measurement result 3 register
#define AS7331_REG_CREG1 0x06 ///< Configuration register 1
#define AS7331_REG_CREG2 0x07 ///< Configuration register 2
#define AS7331_REG_CREG3 0x08 ///< Configuration register 3
#define AS7331_REG_BREAK 0x09 ///< Break time register
#define AS7331_REG_EDGES 0x0A ///< Edge count register
#define AS7331_REG_OUTCONV1 0x05 ///< Output conversion low byte register
#define AS7331_REG_OUTCONV2 0x06 ///< Output conversion high byte register
#define AS7331_REG_OPTREG 0x0B ///< Option register

#define AS7331_STATUS_OUTCONVOF (1 << 7) ///< Output conversion overflow
#define AS7331_STATUS_MRESOF (1 << 6)    ///< Measurement result overflow
#define AS7331_STATUS_ADCOF (1 << 5)     ///< ADC overflow
#define AS7331_STATUS_LDATA (1 << 4)     ///< Data in OUTCONV registers
#define AS7331_STATUS_NDATA (1 << 3)     ///< New data available
#define AS7331_STATUS_NOTREADY (1 << 2)  ///< Not ready (conversion in progress)
#define AS7331_STATUS_STANDBYSTATE (1 << 1) ///< Standby state active
#define AS7331_STATUS_POWERSTATE (1 << 0)   ///< Power state (0=power down, 1=normal)

#define AS7331_PART_ID 0x21 ///< AS7331 part ID

/** Gain settings for the AS7331 */
typedef enum {
  AS7331_GAIN_2048X = 0, ///< 2048x gain (highest sensitivity)
  AS7331_GAIN_1024X = 1, ///< 1024x gain
  AS7331_GAIN_512X = 2,  ///< 512x gain
  AS7331_GAIN_256X = 3,  ///< 256x gain
  AS7331_GAIN_128X = 4,  ///< 128x gain
  AS7331_GAIN_64X = 5,   ///< 64x gain
  AS7331_GAIN_32X = 6,   ///< 32x gain
  AS7331_GAIN_16X = 7,   ///< 16x gain
  AS7331_GAIN_8X = 8,    ///< 8x gain
  AS7331_GAIN_4X = 9,    ///< 4x gain
  AS7331_GAIN_2X = 10,   ///< 2x gain
  AS7331_GAIN_1X = 11,   ///< 1x gain (lowest sensitivity)
} as7331_gain_t;

/** Integration time settings for the AS7331 */
typedef enum {
  AS7331_TIME_1MS = 0,     ///< 1 ms integration time
  AS7331_TIME_2MS = 1,     ///< 2 ms integration time
  AS7331_TIME_4MS = 2,     ///< 4 ms integration time
  AS7331_TIME_8MS = 3,     ///< 8 ms integration time
  AS7331_TIME_16MS = 4,    ///< 16 ms integration time
  AS7331_TIME_32MS = 5,    ///< 32 ms integration time
  AS7331_TIME_64MS = 6,    ///< 64 ms integration time
  AS7331_TIME_128MS = 7,   ///< 128 ms integration time
  AS7331_TIME_256MS = 8,   ///< 256 ms integration time
  AS7331_TIME_512MS = 9,   ///< 512 ms integration time
  AS7331_TIME_1024MS = 10, ///< 1024 ms integration time
  AS7331_TIME_2048MS = 11, ///< 2048 ms integration time
  AS7331_TIME_4096MS = 12, ///< 4096 ms integration time
  AS7331_TIME_8192MS = 13, ///< 8192 ms integration time
  AS7331_TIME_16384MS = 14 ///< 16384 ms integration time
} as7331_time_t;

/** Measurement mode settings for the AS7331 */
typedef enum {
  AS7331_MODE_CONT = 0, ///< Continuous measurement mode
  AS7331_MODE_CMD = 1,  ///< Command mode (single measurement)
  AS7331_MODE_SYNS = 2, ///< Synchronized start mode
  AS7331_MODE_SYND = 3, ///< Synchronized data mode
} as7331_mode_t;

/** Clock frequency settings for the AS7331 */
typedef enum {
  AS7331_CLOCK_1024KHZ = 0, ///< 1.024 MHz clock (note: MHz, not KHz in the value)
  AS7331_CLOCK_2048KHZ = 1, ///< 2.048 MHz clock
  AS7331_CLOCK_4096KHZ = 2, ///< 4.096 MHz clock
  AS7331_CLOCK_8192KHZ = 3, ///< 8.192 MHz clock
} as7331_clock_t;

/**
 * @brief Class for interacting with the AS7331 UV Spectral Sensor
 */
class Adafruit_AS7331 {
public:
  Adafruit_AS7331();

  bool begin(TwoWire *wire = &Wire, uint8_t addr = AS7331_DEFAULT_ADDRESS);

  bool powerDown(bool pd);
  bool setMeasurementMode(as7331_mode_t mode);
  as7331_mode_t getMeasurementMode(void); // Get current measurement mode

  bool reset(void);          // Software reset
  uint8_t getDeviceID(void); // Returns part ID (expect 0x21)

  bool setGain(as7331_gain_t gain);
  as7331_gain_t getGain(void);

  bool setIntegrationTime(as7331_time_t time);
  as7331_time_t getIntegrationTime(void);

  bool setClockFrequency(as7331_clock_t clock);
  as7331_clock_t getClockFrequency(void);

  uint16_t readUVA(void);
  uint16_t readUVB(void);
  uint16_t readUVC(void);
  bool readAllUV(uint16_t *uva, uint16_t *uvb, uint16_t *uvc);

  float readUVA_uWcm2(void); // Read UVA and convert to µW/cm²
  float readUVB_uWcm2(void); // Read UVB and convert to µW/cm²
  float readUVC_uWcm2(void); // Read UVC and convert to µW/cm²
  bool readAllUV_uWcm2(float *uva, float *uvb, float *uvc);

  bool oneShot(uint16_t *uva, uint16_t *uvb,
               uint16_t *uvc); // Single measurement in CMD mode
  bool oneShot_uWcm2(float *uva, float *uvb,
                     float *uvc); // Single measurement with µW/cm² conversion

  float readTemperature(void);
  bool isDataReady(void);
  uint8_t getStatus(void);
  bool hasOverflow(void);
  bool hasNewData(void);

  bool setReadyPinOpenDrain(bool openDrain); // true=open-drain, false=push-pull
  bool getReadyPinOpenDrain(void);

  bool setBreakTime(uint8_t breakTime); // 0-255, time = breakTime * 8µs
  uint8_t getBreakTime(void);

  bool setEdgeCount(uint8_t edges); // 1-255 for SYND mode, 0 treated as 1
  uint8_t getEdgeCount(void);
  bool startMeasurement(void); // Set SS=1
  bool stopMeasurement(void);  // Set SS=0
  bool hasLostData(void);      // STATUS:LDATA flag

  uint16_t readOutConversionTime(void); // Read OUTCONV register (conversion time in clocks)
  bool enableConversionTimeMeasurement(bool enable); // Enable EN_TM in CREG2
  bool getConversionTimeMeasurementEnabled(void);

  bool getStandbyState(void);  // STATUS:STANDBYSTATE bit
  bool getPowerState(void);    // STATUS:POWERSTATE bit

  bool changeToConfigurationState(void); // Switch DOS to 010 (config)
  bool changeToMeasurementState(void);   // Switch DOS to 011 (measurement)

  bool enableDivider(bool enable);
  bool setDivider(uint8_t div); // 0-7, factor = 2^(1+div)
  uint8_t getDivider(void);

  bool setStandby(bool enable);
  bool getStandby(void);

private:
  float _countsToIrradiance(uint16_t counts, float baseSensitivity);

  Adafruit_I2CDevice *_i2c_dev = nullptr; ///< Pointer to I2C device
  uint8_t _cached_gain = 10;              ///< Cached gain setting
  uint8_t _cached_time = 6;               ///< Cached integration time setting
  uint8_t _cached_clock = 0;              ///< Cached clock frequency setting
};
