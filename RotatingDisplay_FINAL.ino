#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <ESP32Servo.h>
#include <pgmspace.h>

#include "image1.h"
#include "image2.h"
#include "image3.h"
#include "image4.h"
#include "image5.h"
#include "image6.h"
#include "image7.h"
#include "image8.h"
#include "image9.h"
#include "image10.h"
#include "image11.h"
#include "image12.h"
#include "image13.h"
#include "image14.h"
#include "image15.h"

#define TFT_CS       7
#define TFT_RST     10
#define TFT_DC       8
#define TFT_MOSI     6
#define TFT_SCK      4
#define TFT_MISO     5
#define PIN_DF_TX   21
#define PIN_DF_RX   20
#define PIN_SERVO    9

#define IMG_W        240
#define IMG_H        320
#define NUM_IMAGES    15
#define IMG_PIXELS   (IMG_W * IMG_H)

#define FADE_STEPS    16
#define FADE_STEP_MS  12

#define NUM_SONGS     15

#define SERVO_STOP_US      1500
#define SERVO_CW_US        1350
#define SERVO_CCW_US       1650
#define SERVO_MIN_US        500
#define SERVO_MAX_US       2500
#define SERVO_ZERO_PULSE_US 1500
#define SERVO_ZERO_HOLD_MS  1000
#define ROTATE_TIME_MS     9000
#define SERVO_PAUSE_MS     3500

#define IMAGES_PER_SWEEP   3
#define IMAGE_STEP_MS      (ROTATE_TIME_MS / IMAGES_PER_SWEEP)

#define COL_BG_TOP     0x000D
#define COL_BG_BOT     0x2008
#define COL_PINK       0xF81F
#define COL_GOLD       0xFFE0
#define COL_WHITE      0xFFFF
#define COL_LAVENDER   0xC81F
#define COL_ROSE       0xF8B2

const uint16_t* const IMAGE_TABLE[NUM_IMAGES] PROGMEM = {
  image1,  image2,  image3,  image4,  image5,
  image6,  image7,  image8,  image9,  image10,
  image11, image12, image13, image14, image15
};

Adafruit_ILI9341    tft(TFT_CS, TFT_DC, TFT_RST);
HardwareSerial      dfSerial(1);
DFRobotDFPlayerMini dfPlayer;
Servo               rotateServo;

uint8_t       currentImg           = 0;
bool          dfReady              = false;
uint8_t       volume               = 22;
uint8_t       currentSong         = 1;

uint16_t rowBuf[IMG_W];

enum ServoState { SRV_CW, SRV_PAUSE1, SRV_CCW, SRV_PAUSE2 };
ServoState    servoState           = SRV_CW;
unsigned long servoStateStart      = 0;

uint8_t       imagesShownThisSweep = 0;

void forceRGBMode() {
  uint8_t madctl = 0xE0;
  tft.sendCommand(0x36, &madctl, 1);
}

void showImage(const uint16_t* img) {
  tft.startWrite();
  tft.setAddrWindow(0, 0, IMG_W, IMG_H);
  for (uint32_t i = 0; i < IMG_PIXELS; i += IMG_W) {
    for (int x = 0; x < IMG_W; x++) {
      rowBuf[x] = pgm_read_word(&img[i + x]);
    }
    tft.writePixels(rowBuf, IMG_W, true, false);
  }
  tft.endWrite();
}

void crossFade(const uint16_t* imgA, const uint16_t* imgB) {
  uint16_t* blnd = (uint16_t*)malloc(IMG_W * sizeof(uint16_t));
  if (!blnd) { showImage(imgB); return; }

  for (uint8_t step = 0; step <= FADE_STEPS; step++) {
    uint8_t wB = (step * 255) / FADE_STEPS;
    uint8_t wA = 255 - wB;

    tft.startWrite();
    tft.setAddrWindow(0, 0, IMG_W, IMG_H);

    for (int row = 0; row < IMG_H; row++) {
      uint32_t off = (uint32_t)row * IMG_W;
      for (int x = 0; x < IMG_W; x++) {
        uint16_t pA = pgm_read_word(&imgA[off + x]);
        uint16_t pB = pgm_read_word(&imgB[off + x]);

        uint8_t rA = (pA >> 11) & 0x1F;
        uint8_t gA = (pA >>  5) & 0x3F;
        uint8_t bA =  pA        & 0x1F;
        uint8_t rB = (pB >> 11) & 0x1F;
        uint8_t gB = (pB >>  5) & 0x3F;
        uint8_t bB =  pB        & 0x1F;

        blnd[x] = (uint16_t)(
          (((rA * wA + rB * wB) >> 8) << 11) |
          (((gA * wA + gB * wB) >> 8) <<  5) |
          (( bA * wA + bB * wB) >> 8)
        );
      }
      tft.writePixels(blnd, IMG_W, true, false);
    }
    tft.endWrite();
    delay(FADE_STEP_MS);
  }
  free(blnd);
}

void goToImage(uint8_t target) {
  target = target % NUM_IMAGES;
  if (target == currentImg) return;
  const uint16_t* a = (const uint16_t*)pgm_read_ptr(&IMAGE_TABLE[currentImg]);
  const uint16_t* b = (const uint16_t*)pgm_read_ptr(&IMAGE_TABLE[target]);
  crossFade(a, b);
  currentImg = target;
}

void playNextSong() {
  currentSong = (currentSong % NUM_SONGS) + 1;
  if (dfReady) dfPlayer.play(currentSong);
}

void updateServo() {
  unsigned long el = millis() - servoStateStart;

  switch (servoState) {

    case SRV_CW:
      rotateServo.writeMicroseconds(SERVO_CW_US);
      if (imagesShownThisSweep < IMAGES_PER_SWEEP) {
        unsigned long nextImageAt = (unsigned long)imagesShownThisSweep * IMAGE_STEP_MS;
        if (el >= nextImageAt) {
          goToImage(currentImg + 1);
          imagesShownThisSweep++;
        }
      }
      if (el >= ROTATE_TIME_MS) {
        rotateServo.writeMicroseconds(SERVO_STOP_US);
        servoState      = SRV_PAUSE1;
        servoStateStart = millis();
      }
      break;

    case SRV_PAUSE1:
      if (el >= SERVO_PAUSE_MS) {
        imagesShownThisSweep = 0;
        servoState      = SRV_CCW;
        servoStateStart = millis();
      }
      break;

    case SRV_CCW:
      rotateServo.writeMicroseconds(SERVO_CCW_US);
      if (imagesShownThisSweep < IMAGES_PER_SWEEP) {
        unsigned long nextImageAt = (unsigned long)imagesShownThisSweep * IMAGE_STEP_MS;
        if (el >= nextImageAt) {
          goToImage(currentImg + 1);
          imagesShownThisSweep++;
        }
      }
      if (el >= ROTATE_TIME_MS) {
        rotateServo.writeMicroseconds(SERVO_STOP_US);
        servoState      = SRV_PAUSE2;
        servoStateStart = millis();
      }
      break;

    case SRV_PAUSE2:
      if (el >= SERVO_PAUSE_MS) {
        imagesShownThisSweep = 0;
        servoState      = SRV_CW;
        servoStateStart = millis();
      }
      break;
  }
}

void drawFilledHeart(int cx, int cy, int s, uint16_t col) {
  tft.fillCircle(cx - s / 2, cy - s / 4, s / 2, col);
  tft.fillCircle(cx + s / 2, cy - s / 4, s / 2, col);
  tft.fillTriangle(
    cx - s, cy - s / 4,
    cx + s, cy - s / 4,
    cx,     cy + s,
    col
  );
}

void drawSparkle(int cx, int cy, int r, uint16_t col) {
  tft.drawLine(cx,         cy - r,     cx,         cy + r,     col);
  tft.drawLine(cx - r,     cy,         cx + r,     cy,         col);
  tft.drawLine(cx - r / 2, cy - r / 2, cx + r / 2, cy + r / 2, col);
  tft.drawLine(cx + r / 2, cy - r / 2, cx - r / 2, cy + r / 2, col);
}

void drawGradientBg() {
  uint8_t rT = (COL_BG_TOP >> 11) & 0x1F;
  uint8_t gT = (COL_BG_TOP >>  5) & 0x3F;
  uint8_t bT =  COL_BG_TOP        & 0x1F;
  uint8_t rB = (COL_BG_BOT >> 11) & 0x1F;
  uint8_t gB = (COL_BG_BOT >>  5) & 0x3F;
  uint8_t bB =  COL_BG_BOT        & 0x1F;

  for (int y = 0; y < 320; y++) {
    uint8_t r = rT + (uint8_t)((rB - rT) * y / 319);
    uint8_t g = gT + (uint8_t)((gB - gT) * y / 319);
    uint8_t b = bT + (uint8_t)((bB - bT) * y / 319);
    uint16_t col = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
    tft.drawFastHLine(0, y, 240, col);
  }
}

// ════════════════════════════════════════════════════════
//  WELCOME MESSAGE
// ════════════════════════════════════════════════════════
void showWelcomeMessage() {
  drawGradientBg();

  tft.drawRect(3,  3, 234, 314, COL_PINK);
  tft.drawRect(6,  6, 228, 308, COL_LAVENDER);

  drawSparkle( 18,  18, 7, COL_GOLD);
  drawSparkle(222,  18, 7, COL_GOLD);
  drawSparkle( 18, 302, 7, COL_GOLD);
  drawSparkle(222, 302, 7, COL_GOLD);

  drawSparkle(120, 16, 5, COL_ROSE);

  tft.setTextWrap(false);
  tft.setTextSize(3);
  tft.setTextColor(COL_GOLD);
  tft.setCursor(64, 20);
  tft.print("Hi!!!");

  tft.drawFastHLine(15, 62, 210, COL_LAVENDER);

  drawFilledHeart( 30, 110, 24, COL_PINK);
  drawFilledHeart(210, 110, 24, COL_PINK);

  tft.setTextSize(2);
  tft.setTextColor(COL_WHITE);
  tft.setCursor(64, 102);
  tft.print("Your Name");

  drawFilledHeart( 55, 135, 9, COL_ROSE);
  drawFilledHeart(185, 135, 9, COL_ROSE);

  tft.drawFastHLine(15, 158, 210, COL_LAVENDER);

  tft.setTextSize(2);
  tft.setTextColor(COL_PINK);
  tft.setCursor(20, 175);
  tft.print("wait for your");

  tft.setTextSize(2);
  tft.setTextColor(COL_GOLD);
  tft.setCursor(52, 205);
  tft.print("surprise");

  drawSparkle( 40, 238, 5, COL_GOLD);
  drawSparkle( 80, 234, 4, COL_ROSE);
  drawSparkle(120, 239, 6, COL_GOLD);
  drawSparkle(160, 234, 4, COL_ROSE);
  drawSparkle(200, 238, 5, COL_GOLD);

  drawFilledHeart( 50, 265, 12, COL_PINK);
  drawFilledHeart( 90, 269, 10, COL_ROSE);
  drawFilledHeart(120, 265, 14, COL_PINK);
  drawFilledHeart(150, 269, 10, COL_ROSE);
  drawFilledHeart(190, 265, 12, COL_PINK);

  tft.setTextSize(1);
  tft.setTextColor(COL_LAVENDER);
  tft.setCursor(65, 305);
  tft.print("ONLY FOR YOU!!!");

  delay(4000);
  tft.fillScreen(0x0000);
}

// ════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(400);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);

  tft.begin();
  tft.setRotation(0);
  forceRGBMode();
  tft.invertDisplay(false);
  tft.fillScreen(0x0000);

  showWelcomeMessage();

  tft.setRotation(3);
  forceRGBMode();

  dfSerial.begin(9600, SERIAL_8N1, PIN_DF_RX, PIN_DF_TX);
  delay(500);
  if (dfPlayer.begin(dfSerial, false, true)) {
    dfReady = true;
    dfPlayer.volume(volume);
    dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
    currentSong = 1;
    dfPlayer.play(currentSong);
  }

  // Servo: go to zero degrees first, hold 1 second, then start cycle
  rotateServo.attach(PIN_SERVO, SERVO_MIN_US, SERVO_MAX_US);
  rotateServo.writeMicroseconds(SERVO_ZERO_PULSE_US);
  delay(SERVO_ZERO_HOLD_MS);

  imagesShownThisSweep = 0;
  servoState      = SRV_CW;
  servoStateStart = millis();

  const uint16_t* first = (const uint16_t*)pgm_read_ptr(&IMAGE_TABLE[0]);
  showImage(first);
  imagesShownThisSweep = 1;
}

// ════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════
void loop() {
  updateServo();

  if (dfReady && dfPlayer.available()) {
    if (dfPlayer.readType() == DFPlayerPlayFinished) {
      playNextSong();
    }
  }
}