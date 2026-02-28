/*!
 * @file AS7331_TFT_demo.ino
 *
 * TFT demo for AS7331 UV Spectral Sensor on Feather ESP32-S2 TFT
 * Displays UVA, UVB, UVC readings as bar graphs with temperature
 *
 * Hardware:
 *  - Adafruit Feather ESP32-S2 TFT
 *  - AS7331 UV Sensor breakout (I2C)
 *
 * Written by Limor Fried/Ladyada for Adafruit Industries.
 * BSD license, all text above must be included in any redistribution
 */

#include <Adafruit_AS7331.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Wire.h>

// Feather ESP32-S2 TFT built-in display
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// AS7331 UV sensor
Adafruit_AS7331 as7331;

// Display layout constants (240x135 landscape)
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

// Bar graph layout
#define BAR_X 48       // Left edge of bars (after labels)
#define BAR_WIDTH 120  // Maximum bar width
#define BAR_HEIGHT 22  // Height of each bar
#define BAR_SPACING 6  // Space between bars

// Y positions
#define TITLE_Y 18
#define UVA_BAR_Y 32
#define UVB_BAR_Y (UVA_BAR_Y + BAR_HEIGHT + BAR_SPACING)
#define UVC_BAR_Y (UVB_BAR_Y + BAR_HEIGHT + BAR_SPACING)
#define TEMP_Y 128

// Colors
#define COLOR_UVA 0x780F    // Purple/violet
#define COLOR_UVB 0x339F    // Bright blue
#define COLOR_UVC 0x07FF    // Cyan
#define COLOR_BAR_BG 0x2104 // Dark gray
#define COLOR_TITLE 0xFFFF  // White

// Fixed scale maximums (uW/cm2)
#define UVA_MAX 500.0
#define UVB_MAX 100.0
#define UVC_MAX 50.0

// Previous values for flicker-free updates
float prevUVA = -1, prevUVB = -1, prevUVC = -1;
float prevTemp = -999;

void setup() {
  Serial.begin(115200);
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000)) {
    delay(10);
  }

  Serial.println(F("AS7331 UV Sensor TFT Demo"));

  // Enable TFT power
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  pinMode(TFT_I2C_POWER, OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);

  // Initialize TFT
  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);

  // Draw title with nice font
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(COLOR_TITLE);
  tft.setCursor(0, TITLE_Y);
  tft.print(F("Adafruit AS7331 UV Sensor"));

  // Initialize I2C and sensor
  Wire.begin();

  if (!as7331.begin(&Wire)) {
    Serial.println(F("Failed to find AS7331 sensor!"));
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(20, 70);
    tft.print(F("Sensor Error!"));
    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(10, 95);
    tft.print(F("Check wiring"));
    while (1) {
      delay(100);
    }
  }

  Serial.println(F("AS7331 found!"));

  // Configure sensor
  as7331.setMeasurementMode(AS7331_MODE_CMD);
  as7331.setGain(AS7331_GAIN_64X);
  as7331.setIntegrationTime(AS7331_TIME_64MS);

  drawStaticUI();
}

void drawStaticUI() {
  tft.setFont(&FreeSansBold9pt7b);

  // Bar labels
  tft.setTextColor(COLOR_UVA);
  tft.setCursor(2, UVA_BAR_Y + 16);
  tft.print(F("UVA"));

  tft.setTextColor(COLOR_UVB);
  tft.setCursor(2, UVB_BAR_Y + 16);
  tft.print(F("UVB"));

  tft.setTextColor(COLOR_UVC);
  tft.setCursor(2, UVC_BAR_Y + 16);
  tft.print(F("UVC"));

  // Bar backgrounds and outlines
  tft.fillRect(BAR_X, UVA_BAR_Y, BAR_WIDTH, BAR_HEIGHT, COLOR_BAR_BG);
  tft.fillRect(BAR_X, UVB_BAR_Y, BAR_WIDTH, BAR_HEIGHT, COLOR_BAR_BG);
  tft.fillRect(BAR_X, UVC_BAR_Y, BAR_WIDTH, BAR_HEIGHT, COLOR_BAR_BG);

  tft.drawRect(BAR_X - 1, UVA_BAR_Y - 1, BAR_WIDTH + 2, BAR_HEIGHT + 2,
               COLOR_UVA);
  tft.drawRect(BAR_X - 1, UVB_BAR_Y - 1, BAR_WIDTH + 2, BAR_HEIGHT + 2,
               COLOR_UVB);
  tft.drawRect(BAR_X - 1, UVC_BAR_Y - 1, BAR_WIDTH + 2, BAR_HEIGHT + 2,
               COLOR_UVC);
}

void updateBar(int y, float value, float maxVal, uint16_t color,
               float *prevValue) {
  int fillWidth = (int)((value / maxVal) * BAR_WIDTH);
  if (fillWidth > BAR_WIDTH)
    fillWidth = BAR_WIDTH;
  if (fillWidth < 0)
    fillWidth = 0;

  int prevFillWidth = 0;
  if (*prevValue >= 0) {
    prevFillWidth = (int)((*prevValue / maxVal) * BAR_WIDTH);
    if (prevFillWidth > BAR_WIDTH)
      prevFillWidth = BAR_WIDTH;
    if (prevFillWidth < 0)
      prevFillWidth = 0;
  }

  if (fillWidth != prevFillWidth) {
    tft.fillRect(BAR_X, y, BAR_WIDTH, BAR_HEIGHT, COLOR_BAR_BG);
    if (fillWidth > 0) {
      tft.fillRect(BAR_X, y, fillWidth, BAR_HEIGHT, color);
    }
  }

  // Numeric value to the right of bar
  tft.fillRect(BAR_X + BAR_WIDTH + 4, y, 68, BAR_HEIGHT, ST77XX_BLACK);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(color);
  tft.setCursor(BAR_X + BAR_WIDTH + 6, y + 16);
  if (value < 10.0) {
    tft.print(value, 2);
  } else if (value < 100.0) {
    tft.print(value, 1);
  } else {
    tft.print(value, 0);
  }

  *prevValue = value;
}

void updateTemperature(float temp) {
  if (abs(temp - prevTemp) > 0.1) {
    tft.fillRect(0, TEMP_Y - 14, SCREEN_WIDTH, 18, ST77XX_BLACK);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(30, TEMP_Y);
    tft.print(F("Temp: ")); tft.print(temp, 1);
    tft.print(F(" C"));
    prevTemp = temp;
  }
}

void loop() {
  float uva, uvb, uvc;

  if (as7331.oneShot_uWcm2(&uva, &uvb, &uvc)) {
    float temp = as7331.readTemperature();

    Serial.print(F("UVA: "));
    Serial.print(uva, 2);
    Serial.print(F(" | UVB: "));
    Serial.print(uvb, 2);
    Serial.print(F(" | UVC: "));
    Serial.print(uvc, 2);
    Serial.print(F(" | Temp: "));
    Serial.print(temp, 1);
    Serial.println(F(" C"));

    updateBar(UVA_BAR_Y, uva, UVA_MAX, COLOR_UVA, &prevUVA);
    updateBar(UVB_BAR_Y, uvb, UVB_MAX, COLOR_UVB, &prevUVB);
    updateBar(UVC_BAR_Y, uvc, UVC_MAX, COLOR_UVC, &prevUVC);
    updateTemperature(temp);
  } else {
    Serial.println(F("Failed to read UV sensor!"));
  }

  delay(200);
}
