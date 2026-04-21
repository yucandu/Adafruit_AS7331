/*!
 * @file Adafruit_AS7331.cpp
 *
 * @mainpage Adafruit AS7331 UV Spectral Sensor Library
 *
 * @section intro_sec Introduction
 *
 * This is a library for the AS7331 UV Spectral Sensor from ams-OSRAM.
 * It provides UVA, UVB, and UVC measurements with configurable gain
 * and integration time, plus irradiance conversion to µW/cm².
 *
 * @section author Author
 *
 * Written by Limor Fried/Ladyada for Adafruit Industries.
 *
 * @section license License
 *
 * BSD license, all text above must be included in any redistribution
 */

#include "Adafruit_AS7331.h"

/**
 * Base irradiance responsivity values from AS7331 datasheet (DS001047 v4-00)
 * Table in Section 5 "Electrical Characteristics", parameter ReGAIN2048.
 * Units: counts per (µW/cm²) at GAIN=2048x, TIME=64ms (65536 clocks),
 * fCLK=1.024MHz
 *
 * These are typical values at the peak wavelengths:
 *   UVA (λ=360nm): 385 counts/(µW/cm²)
 *   UVB (λ=300nm): 347 counts/(µW/cm²)
 *   UVC (λ=260nm): 794 counts/(µW/cm²)
 */
/** @brief Typical UVA responsivity counts/(µW/cm²) at GAIN=2048x, TIME=64ms */
#define AS7331_SENS_UVA 385.0f
/** @brief Typical UVB responsivity counts/(µW/cm²) at GAIN=2048x, TIME=64ms */
#define AS7331_SENS_UVB 347.0f
/** @brief Typical UVC responsivity counts/(µW/cm²) at GAIN=2048x, TIME=64ms */
#define AS7331_SENS_UVC 794.0f

/**
 * @brief Construct a new Adafruit_AS7331 object
 */
Adafruit_AS7331::Adafruit_AS7331() {}

/**
 * @brief Initialize the AS7331 sensor
 * @param wire Pointer to the I2C bus
 * @param addr I2C address of the sensor
 * @return true if initialization succeeded, false otherwise
 */
bool Adafruit_AS7331::begin(TwoWire *wire, uint8_t addr) {
  if (_i2c_dev) {
    delete _i2c_dev;
    _i2c_dev = nullptr;
  }

  _i2c_dev = new Adafruit_I2CDevice(addr, wire);
  if (!_i2c_dev->begin()) {
    return false;
  }

  // Software reset to ensure we're in Configuration State.
  // The sensor retains state across Arduino resets, so we must
  // reset it to read AGEN (which shares address with MRES1).
  // Write OSR = 0x0A (SW_RES=1, DOS=010 for config state)
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR);
  if (!osr.write(0x0A)) {
    return false;
  }
  delay(10); // Wait for reset to complete

  // Cache default gain/time/clock values
  Adafruit_BusIO_Register creg1(_i2c_dev, AS7331_REG_CREG1);
  uint8_t creg1_val = 0;
  creg1.read(&creg1_val);
  _cached_gain = (creg1_val >> 4) & 0x0F;
  _cached_time = creg1_val & 0x0F;

  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  uint8_t creg3_val = 0;
  creg3.read(&creg3_val);
  _cached_clock = creg3_val & 0x03;

  // Verify we're in config state by reading AGEN (0x02)
  // AGEN contains DEVID (bits 7:4 = 0010) and MUT (bits 3:0 = 0001)
  // Combined: 0x21
  Adafruit_BusIO_Register agen(_i2c_dev, AS7331_REG_AGEN);
  uint8_t agen_val = 0;
  if (!agen.read(&agen_val)) {
    return false;
  }

  // Check that DEVID nibble is 0x02 (expected for AS7331)
  return ((agen_val & 0xF0) >> 4) == 0x02;
}

/**
 * @brief Perform a software reset
 * @return true if the reset command succeeded, false otherwise
 */
bool Adafruit_AS7331::reset(void) {
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR);
  if (!osr.write(0x0A)) { // SW_RES=1, DOS=010 for config state
    return false;
  }
  delay(10); // Wait for reset to complete
  return true;
}

/**
 * @brief Read the device ID (AGEN register)
 * @return AGEN register value (DEVID nibble 7:4 should be 0x02, MUT nibble 3:0)
 *
 * Note: This returns the full AGEN register (0x02 bits 7:4 + 0x01 bits 3:0 = 0x21),
 * not just the DEVID nibble. To check only the device type, mask bits 7:4 and
 * compare to 0x20.
 */
uint8_t Adafruit_AS7331::getDeviceID(void) {
  Adafruit_BusIO_Register agen(_i2c_dev, AS7331_REG_AGEN);
  uint8_t id = 0;
  agen.read(&id);
  return id;
}

/**
 * @brief Get the current measurement mode
 * @return Current measurement mode
 */
as7331_mode_t Adafruit_AS7331::getMeasurementMode(void) {
  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  Adafruit_BusIO_RegisterBits mode_bits(&creg3, 2, 6);
  return (as7331_mode_t)mode_bits.read();
}

/**
 * @brief Enter or exit power-down mode
 * @param pd True to power down, false to wake
 * @return true if the operation succeeded, false otherwise
 *
 * When waking from power-down (pd=false), this only switches to measurement state
 * and waits for startup. It does NOT automatically start a measurement.
 * Call startMeasurement() separately if needed.
 * Startup time is up to 2ms per datasheet; actual delay may be shorter.
 */
bool Adafruit_AS7331::powerDown(bool pd) {
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR);
  Adafruit_BusIO_RegisterBits ss_bit(&osr, 1, 7);
  Adafruit_BusIO_RegisterBits pd_bit(&osr, 1, 6);
  Adafruit_BusIO_RegisterBits dos_bits(&osr, 3, 0);

  if (!pd_bit.write(pd)) {
    return false;
  }

  if (pd) {
    // Power down: stop measurement and return to config state
    if (!ss_bit.write(false)) {
      return false;
    }
    if (!dos_bits.write(0x02)) { // DOS=010 (configuration state)
      return false;
    }
  } else {
    // Wake up: switch to measurement state (but don't start measurement)
    if (!dos_bits.write(0x03)) { // DOS=011 (measurement state)
      return false;
    }
    delay(2); // Wait for startup (max 2ms per datasheet)
  }

  return true;
}

/**
 * @brief Set the measurement mode
 * @param mode Measurement mode selection
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::setMeasurementMode(as7331_mode_t mode) {
  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  Adafruit_BusIO_RegisterBits mode_bits(&creg3, 2, 6);
  return mode_bits.write(mode);
}

/**
 * @brief Set the sensor gain
 * @param gain Gain setting
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::setGain(as7331_gain_t gain) {
  Adafruit_BusIO_Register creg1(_i2c_dev, AS7331_REG_CREG1);
  Adafruit_BusIO_RegisterBits gain_bits(&creg1, 4, 4);
  if (gain_bits.write(gain)) {
    _cached_gain = gain;
    return true;
  }
  return false;
}

/**
 * @brief Get the current sensor gain
 * @return Gain setting
 */
as7331_gain_t Adafruit_AS7331::getGain(void) {
  Adafruit_BusIO_Register creg1(_i2c_dev, AS7331_REG_CREG1);
  Adafruit_BusIO_RegisterBits gain_bits(&creg1, 4, 4);
  return (as7331_gain_t)gain_bits.read();
}

/**
 * @brief Set the integration time
 * @param time Integration time setting
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::setIntegrationTime(as7331_time_t time) {
  Adafruit_BusIO_Register creg1(_i2c_dev, AS7331_REG_CREG1);
  Adafruit_BusIO_RegisterBits time_bits(&creg1, 4, 0);
  if (time_bits.write(time)) {
    _cached_time = time;
    return true;
  }
  return false;
}

/**
 * @brief Get the current integration time
 * @return Integration time setting
 */
as7331_time_t Adafruit_AS7331::getIntegrationTime(void) {
  Adafruit_BusIO_Register creg1(_i2c_dev, AS7331_REG_CREG1);
  Adafruit_BusIO_RegisterBits time_bits(&creg1, 4, 0);
  return (as7331_time_t)time_bits.read();
}

/**
 * @brief Set the clock frequency
 * @param clock Clock frequency setting
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::setClockFrequency(as7331_clock_t clock) {
  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  Adafruit_BusIO_RegisterBits cclk(&creg3, 2, 0);
  if (cclk.write(clock)) {
    _cached_clock = clock;
    return true;
  }
  return false;
}

/**
 * @brief Get the clock frequency setting
 * @return Clock frequency setting
 */
as7331_clock_t Adafruit_AS7331::getClockFrequency(void) {
  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  Adafruit_BusIO_RegisterBits cclk(&creg3, 2, 0);
  return (as7331_clock_t)cclk.read();
}

/**
 * @brief Read the UVA channel counts
 * @return UVA measurement counts
 */
uint16_t Adafruit_AS7331::readUVA(void) {
  Adafruit_BusIO_Register mres1(_i2c_dev, AS7331_REG_MRES1, 2, LSBFIRST);
  return mres1.read();
}

/**
 * @brief Read the UVB channel counts
 * @return UVB measurement counts
 */
uint16_t Adafruit_AS7331::readUVB(void) {
  Adafruit_BusIO_Register mres2(_i2c_dev, AS7331_REG_MRES2, 2, LSBFIRST);
  return mres2.read();
}

/**
 * @brief Read the UVC channel counts
 * @return UVC measurement counts
 */
uint16_t Adafruit_AS7331::readUVC(void) {
  Adafruit_BusIO_Register mres3(_i2c_dev, AS7331_REG_MRES3, 2, LSBFIRST);
  return mres3.read();
}

/**
 * @brief Read all UV channels
 * @param uva Optional storage for UVA counts
 * @param uvb Optional storage for UVB counts
 * @param uvc Optional storage for UVC counts
 * @return true on success, false on read failure
 */
bool Adafruit_AS7331::readAllUV(uint16_t *uva, uint16_t *uvb, uint16_t *uvc) {
  uint8_t buffer[6] = {0};
  uint8_t reg = AS7331_REG_MRES1;
  if (!_i2c_dev->write_then_read(&reg, 1, buffer, sizeof(buffer))) {
    return false;
  }

  if (uva) {
    *uva = (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
  }
  if (uvb) {
    *uvb = (uint16_t)buffer[2] | ((uint16_t)buffer[3] << 8);
  }
  if (uvc) {
    *uvc = (uint16_t)buffer[4] | ((uint16_t)buffer[5] << 8);
  }

  return true;
}

/**
 * @brief Convert raw ADC counts to irradiance in µW/cm²
 *
 * Algorithm from AS7331 datasheet (DS001047 v4-00), Section 7.4
 * "Measurement Result".
 *
 * The effective sensitivity scales with gain, integration time, and clock:
 *   effective_sens = base_sens × (gain_factor / 2048) × (time_factor) × (clock_factor)
 *
 * Where:
 *   - base_sens: Responsivity at GAIN=2048x, TIME=64ms, fCLK=1.024 MHz
 *     (from datasheet Table 5)
 *   - gain_factor = 2^(11 - gain_setting) for valid gains at current clock
 *   - time_factor = 2^time_setting / 64, where TIME_64MS=6 gives factor=1.0
 *     Special case: TIME=15 (0b1111) wraps to TIME=0 per datasheet Fig 28-32
 *   - clock_factor = 1.024 / actual_clock_mhz (base sensitivities are at 1.024 MHz)
 *     Clock values: 0=1.024, 1=2.048, 2=4.096, 3=8.192 MHz
 *
 * Note: At higher clock frequencies (2.048 MHz+), certain GAIN settings collapse
 * (datasheet Fig 33). This function uses the programmed gain value; the hardware
 * automatically maps invalid gains, and the effective gain is reduced. The actual
 * count output already reflects this hardware behavior, so we apply the programmed
 * gain formula as-is (it will be naturally scaled by the reduced hardware gain).
 *
 * Irradiance = counts / effective_sensitivity
 *
 * @param counts Raw ADC value from MRES register
 * @param baseSensitivity Base responsivity for the channel
 * (AS7331_SENS_UVA/B/C)
 * @return Irradiance in µW/cm²
 */
float Adafruit_AS7331::_countsToIrradiance(uint16_t counts,
                                           float baseSensitivity) {
  // Use cached values instead of reading registers
  uint8_t gain_setting = _cached_gain;
  uint8_t time_setting = _cached_time;
  uint8_t clock_setting = _cached_clock;

  // TIME=15 (0b1111) wraps to TIME=0 per datasheet Figure 28, 30, 32
  if (time_setting == 15) {
    time_setting = 0;
  }

  // Gain factor: 2^(11 - gain_setting), valid range [1, 2048]
  float gain_factor = (float)(1 << (11 - gain_setting));

  // Time factor: 2^time_setting / 64
  // TIME_64MS (6) gives 2^6 / 64 = 64 / 64 = 1.0 (reference condition)
  float time_factor = (float)(1 << time_setting) / 64.0f;

  // Clock factor: base sensitivities are measured at 1.024 MHz
  // Clock setting 0=1.024 MHz (factor=1.0), 1=2.048 MHz (factor=0.5), etc.
  // If clock frequency doubles, each count represents twice the irradiance,
  // so we divide by 2 (factor *= 0.5)
  float clock_factor = 1.0f / (1 << clock_setting);

  // Effective sensitivity combines all factors
  float effective_sens = baseSensitivity * (gain_factor / 2048.0f) * time_factor * clock_factor;

  if (effective_sens < 0.001f) {
    return 0.0f;
  }
  return (float)counts / effective_sens;
}

/**
 * @brief Read UVA and convert to irradiance (µW/cm²)
 * @return UVA irradiance in µW/cm²
 */
float Adafruit_AS7331::readUVA_uWcm2(void) {
  uint16_t counts = readUVA();
  return _countsToIrradiance(counts, AS7331_SENS_UVA);
}

/**
 * @brief Read UVB and convert to irradiance (µW/cm²)
 * @return UVB irradiance in µW/cm²
 */
float Adafruit_AS7331::readUVB_uWcm2(void) {
  uint16_t counts = readUVB();
  return _countsToIrradiance(counts, AS7331_SENS_UVB);
}

/**
 * @brief Read UVC and convert to irradiance (µW/cm²)
 * @return UVC irradiance in µW/cm²
 */
float Adafruit_AS7331::readUVC_uWcm2(void) {
  uint16_t counts = readUVC();
  return _countsToIrradiance(counts, AS7331_SENS_UVC);
}

/**
 * @brief Read all UV channels and convert to irradiance (µW/cm²)
 * @param uva Optional storage for UVA irradiance
 * @param uvb Optional storage for UVB irradiance
 * @param uvc Optional storage for UVC irradiance
 * @return True on success, false on read failure
 */
bool Adafruit_AS7331::readAllUV_uWcm2(float *uva, float *uvb, float *uvc) {
  uint16_t uva_raw = 0;
  uint16_t uvb_raw = 0;
  uint16_t uvc_raw = 0;
  if (!readAllUV(&uva_raw, &uvb_raw, &uvc_raw)) {
    return false;
  }
  if (uva) {
    *uva = _countsToIrradiance(uva_raw, AS7331_SENS_UVA);
  }
  if (uvb) {
    *uvb = _countsToIrradiance(uvb_raw, AS7331_SENS_UVB);
  }
  if (uvc) {
    *uvc = _countsToIrradiance(uvc_raw, AS7331_SENS_UVC);
  }
  return true;
}

/**
 * @brief Perform a single measurement in command mode
 * @param uva Optional storage for UVA counts
 * @param uvb Optional storage for UVB counts
 * @param uvc Optional storage for UVC counts
 * @return true on success, false on timeout or read failure
 */
bool Adafruit_AS7331::oneShot(uint16_t *uva, uint16_t *uvb, uint16_t *uvc) {
  // Ensure we're in CMD mode and powered up
  powerDown(true);  // Power down and go to config state
  setMeasurementMode(AS7331_MODE_CMD);  // Set to command mode (only in config)
  powerDown(false); // Wake up (goes to measurement state, doesn't start measurement)

  // Start single measurement
  startMeasurement();

  // Wait for measurement to complete (poll isDataReady or use timeout)
  uint32_t start = millis();
  while (!isDataReady()) {
    if (millis() - start > 20000) { // 20 second timeout for longest integration
      return false;
    }
    delay(1);
  }

  // Read results
  return readAllUV(uva, uvb, uvc);
}

/**
 * @brief Perform a single measurement and convert to irradiance (µW/cm²)
 * @param uva Optional storage for UVA irradiance
 * @param uvb Optional storage for UVB irradiance
 * @param uvc Optional storage for UVC irradiance
 * @return true on success, false on timeout or read failure
 */
bool Adafruit_AS7331::oneShot_uWcm2(float *uva, float *uvb, float *uvc) {
  uint16_t uva_raw, uvb_raw, uvc_raw;
  if (!oneShot(&uva_raw, &uvb_raw, &uvc_raw)) {
    return false;
  }
  if (uva) {
    *uva = _countsToIrradiance(uva_raw, AS7331_SENS_UVA);
  }
  if (uvb) {
    *uvb = _countsToIrradiance(uvb_raw, AS7331_SENS_UVB);
  }
  if (uvc) {
    *uvc = _countsToIrradiance(uvc_raw, AS7331_SENS_UVC);
  }
  return true;
}

/**
 * @brief Read the sensor temperature in degrees Celsius
 * @return Temperature in degrees Celsius
 */
float Adafruit_AS7331::readTemperature(void) {
  Adafruit_BusIO_Register temp_reg(_i2c_dev, AS7331_REG_TEMP, 2, LSBFIRST);
  uint16_t raw = temp_reg.read() & 0x0FFF;
  return (raw * 0.05f) - 66.9f;
}

/**
 * @brief Check if new data is ready
 * @return true if data is ready, false otherwise
 */
bool Adafruit_AS7331::isDataReady(void) {
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR, 2, LSBFIRST);
  Adafruit_BusIO_RegisterBits nready(&osr, 1, 10);
  return !nready.read();
}

/**
 * @brief Read the status register byte
 * @return Status register value
 */
uint8_t Adafruit_AS7331::getStatus(void) {
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR, 2, LSBFIRST);
  Adafruit_BusIO_RegisterBits status_bits(&osr, 8, 8);
  return status_bits.read();
}

/**
 * @brief Check for any overflow condition
 * @return true if an overflow condition is present, false otherwise
 */
bool Adafruit_AS7331::hasOverflow(void) {
  uint8_t status = getStatus();
  return (status & (AS7331_STATUS_OUTCONVOF | AS7331_STATUS_MRESOF |
                    AS7331_STATUS_ADCOF)) != 0;
}

/**
 * @brief Check if new data is available
 * @return true if new data is available, false otherwise
 */
bool Adafruit_AS7331::hasNewData(void) {
  uint8_t status = getStatus();
  return (status & AS7331_STATUS_NDATA) != 0;
}

/**
 * @brief Configure the READY pin drive mode
 * @param openDrain True for open-drain, false for push-pull
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::setReadyPinOpenDrain(bool openDrain) {
  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  Adafruit_BusIO_RegisterBits rdyod(&creg3, 1, 3);
  return rdyod.write(openDrain);
}

/**
 * @brief Get the READY pin drive mode
 * @return true if open-drain, false if push-pull
 */
bool Adafruit_AS7331::getReadyPinOpenDrain(void) {
  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  Adafruit_BusIO_RegisterBits rdyod(&creg3, 1, 3);
  return rdyod.read();
}

/**
 * @brief Set the break time between consecutive measurements
 * @param breakTime Break time register value, 0-255 (each unit = 8µs)
 * @return true if the operation succeeded, false otherwise
 *
 * Break time inserts a delay between consecutive measurements in CONT, SYNS,
 * and SYND modes. This provides time for I²C data reads without interfering
 * with measurements. Range: 0–255, where each unit = 8µs.
 * Suggested: 100–255 to avoid measurement disturbance during data fetch.
 */
bool Adafruit_AS7331::setBreakTime(uint8_t breakTime) {
  Adafruit_BusIO_Register brk(_i2c_dev, AS7331_REG_BREAK);
  return brk.write(breakTime);
}

/**
 * @brief Read the break time register
 * @return Break time register value (x8 for microseconds)
 */
uint8_t Adafruit_AS7331::getBreakTime(void) {
  Adafruit_BusIO_Register brk(_i2c_dev, AS7331_REG_BREAK);
  uint8_t val = 0;
  brk.read(&val);
  return val;
}

/**
 * @brief Set the edge count for SYND mode
 * @param edges Edge count value (1-255; 0 is treated as 1 per datasheet)
 * @return true if the operation succeeded, false otherwise
 *
 * The datasheet specifies that EDGES=0 is invalid and treated as 1 by hardware.
 * This function enforces that constraint by automatically converting 0 to 1.
 */
bool Adafruit_AS7331::setEdgeCount(uint8_t edges) {
  // Datasheet: EDGES=0 is illegal, treated as 1 by hardware
  if (edges == 0) {
    edges = 1;
  }
  Adafruit_BusIO_Register reg(_i2c_dev, AS7331_REG_EDGES);
  return reg.write(edges);
}

/**
 * @brief Get the edge count register value
 * @return Edge count register value
 */
uint8_t Adafruit_AS7331::getEdgeCount(void) {
  Adafruit_BusIO_Register reg(_i2c_dev, AS7331_REG_EDGES);
  uint8_t val = 0;
  reg.read(&val);
  return val;
}

/**
 * @brief Start measurements by setting SS
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::startMeasurement(void) {
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR);
  Adafruit_BusIO_RegisterBits ss(&osr, 1, 7);
  return ss.write(1);
}

/**
 * @brief Stop measurements by clearing SS
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::stopMeasurement(void) {
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR);
  Adafruit_BusIO_RegisterBits ss(&osr, 1, 7);
  return ss.write(0);
}

/**
 * @brief Check if data was lost
 * @return true if data was lost, false otherwise
 */
bool Adafruit_AS7331::hasLostData(void) {
  uint8_t status = getStatus();
  return (status & AS7331_STATUS_LDATA) != 0;
}

/**
 * @brief Enable or disable the divider
 * @param enable True to enable, false to disable
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::enableDivider(bool enable) {
  Adafruit_BusIO_Register creg2(_i2c_dev, AS7331_REG_CREG2);
  Adafruit_BusIO_RegisterBits en_div(&creg2, 1, 3);
  return en_div.write(enable);
}

/**
 * @brief Set the divider value
 * @param div Divider setting (0-7)
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::setDivider(uint8_t div) {
  if (div > 7) {
    return false;
  }
  Adafruit_BusIO_Register creg2(_i2c_dev, AS7331_REG_CREG2);
  Adafruit_BusIO_RegisterBits div_bits(&creg2, 3, 0);
  return div_bits.write(div);
}

/**
 * @brief Get the divider value
 * @return Divider setting
 */
uint8_t Adafruit_AS7331::getDivider(void) {
  Adafruit_BusIO_Register creg2(_i2c_dev, AS7331_REG_CREG2);
  Adafruit_BusIO_RegisterBits div_bits(&creg2, 3, 0);
  return div_bits.read();
}

/**
 * @brief Read the output conversion time (OUTCONV register)
 * @return Conversion time in internal clock counts
 *
 * This register (addresses 0x05-0x06) contains the measured conversion time
 * when EN_TM bit in CREG2 is set to 1. Critical for SYND mode and accurate
 * irradiance calculation (datasheet Eq 4).
 */
uint16_t Adafruit_AS7331::readOutConversionTime(void) {
  Adafruit_BusIO_Register outconv(_i2c_dev, AS7331_REG_OUTCONV1, 2, LSBFIRST);
  return outconv.read();
}

/**
 * @brief Enable or disable conversion time measurement
 * @param enable True to enable EN_TM, false to disable
 * @return true if the operation succeeded, false otherwise
 *
 * When enabled, the OUTCONV register (0x05-0x06) is populated with the
 * actual conversion time in internal clock counts. This is critical for:
 * - SYND mode measurements
 * - More accurate irradiance calculation (eliminates clock tolerance)
 */
bool Adafruit_AS7331::enableConversionTimeMeasurement(bool enable) {
  Adafruit_BusIO_Register creg2(_i2c_dev, AS7331_REG_CREG2);
  Adafruit_BusIO_RegisterBits en_tm(&creg2, 1, 4);
  return en_tm.write(enable);
}

/**
 * @brief Check if conversion time measurement is enabled
 * @return true if EN_TM is set, false otherwise
 */
bool Adafruit_AS7331::getConversionTimeMeasurementEnabled(void) {
  Adafruit_BusIO_Register creg2(_i2c_dev, AS7331_REG_CREG2);
  Adafruit_BusIO_RegisterBits en_tm(&creg2, 1, 4);
  return en_tm.read();
}

/**
 * @brief Check standby state from STATUS register
 * @return true if in standby state, false otherwise
 *
 * STATUS bit 1 indicates whether standby mode is active.
 */
bool Adafruit_AS7331::getStandbyState(void) {
  uint8_t status = getStatus();
  return (status & AS7331_STATUS_STANDBYSTATE) != 0;
}

/**
 * @brief Check power state from STATUS register
 * @return true if powered (not in power-down), false if power-down active
 *
 * STATUS bit 0 indicates the device power state.
 * 0 = power-down state, 1 = normal operation
 */
bool Adafruit_AS7331::getPowerState(void) {
  uint8_t status = getStatus();
  return (status & AS7331_STATUS_POWERSTATE) != 0;
}

/**
 * @brief Switch to Configuration state (DOS=010)
 * @return true if the operation succeeded, false otherwise
 *
 * In Configuration state, you can modify CREG1, CREG2, CREG3 registers.
 * Measurement is not possible. Use changeToMeasurementState() to switch back.
 */
bool Adafruit_AS7331::changeToConfigurationState(void) {
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR);
  Adafruit_BusIO_RegisterBits dos_bits(&osr, 3, 0);
  return dos_bits.write(0x02);
}

/**
 * @brief Switch to Measurement state (DOS=011)
 * @return true if the operation succeeded, false otherwise
 *
 * In Measurement state, you can read result registers and start measurements.
 * Configuration registers (CREG1, CREG2, CREG3) are read-only.
 * This function does NOT start a measurement; call startMeasurement() separately.
 */
bool Adafruit_AS7331::changeToMeasurementState(void) {
  Adafruit_BusIO_Register osr(_i2c_dev, AS7331_REG_OSR);
  Adafruit_BusIO_RegisterBits dos_bits(&osr, 3, 0);
  return dos_bits.write(0x03);
}

/**
 * @brief Set standby mode
 * @param enable True to enable standby, false to disable
 * @return true if the operation succeeded, false otherwise
 */
bool Adafruit_AS7331::setStandby(bool enable) {
  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  Adafruit_BusIO_RegisterBits sb(&creg3, 1, 4);
  return sb.write(enable);
}

/**
 * @brief Get standby mode state
 * @return true if standby is enabled, false otherwise
 */
bool Adafruit_AS7331::getStandby(void) {
  Adafruit_BusIO_Register creg3(_i2c_dev, AS7331_REG_CREG3);
  Adafruit_BusIO_RegisterBits sb(&creg3, 1, 4);
  return sb.read();
}
