// ============================================================
//  HS-01  Animal TV  — cat / dog / bird channels (animated)
//  ESP32-S3 + ST7789 240x240 SPI TFT
//
//  Button (BOOT, GPIO0):
//    1 click  -> next channel
//    2 clicks -> previous channel
//    3 clicks -> back to CAT
//
//  Each animal bounces around the screen and blinks.
// ============================================================

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ---- HS-01 display pins ----
#define TFT_SCLK  21
#define TFT_MOSI  47
#define TFT_DC    40
#define TFT_CS    41
#define TFT_BL    42          // backlight — MUST be driven HIGH
#define TFT_RST   45

// ---- button ----
#define BTN        0          // BOOT button, active LOW

// ---- animation region (below the text labels) ----
#define AREA_TOP   60
#define SCREEN_W   240
#define SCREEN_H   240
#define FRAME_MS   33         // ~30 fps

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

int  tv_channel = 0;          // 0 = cat, 1 = dog, 2 = bird
bool pressed    = false;
int  clickCount = 0;
unsigned long lastClick = 0;

// ---- animation state ----
float x, y;                   // current top-left of the animal box
float vx, vy;                 // velocity (px/frame)
int   boxW, boxH;             // bounding box for the current animal
int   prevX, prevY;           // where we drew last frame (to erase)
unsigned long lastFrame = 0;
unsigned long lastBlink = 0;
bool  blinking = false;

// forward declarations
void showTV();
void animate();
void drawAnimal(int ox, int oy, bool blink);
void drawCat(int ox, int oy, bool blink);
void drawDog(int ox, int oy, bool blink);
void drawBird(int ox, int oy, bool blink);

void setup() {
  Serial.begin(115200);
  pinMode(BTN, INPUT_PULLUP);

  // ---- TFT init (order matters) ----
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);                // backlight ON
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS); // remap SPI to HS-01 pins
  tft.init(240, 240, SPI_MODE3);             // ST7789 (NOT initR)
  tft.setRotation(2);
  tft.fillScreen(ST77XX_BLACK);

  showTV();
}

void loop() {
  // ---- button handling ----
  int state = digitalRead(BTN);
  if (state == LOW && !pressed) pressed = true;
  if (state == HIGH && pressed) {
    pressed = false;
    clickCount++;
    lastClick = millis();
  }
  if (clickCount > 0 && millis() - lastClick > 400) {
    if (clickCount == 1)      tv_channel = (tv_channel + 1) % 3;  // next
    else if (clickCount == 2) tv_channel = (tv_channel + 2) % 3;  // previous
    else                      tv_channel = 0;                     // reset
    showTV();
    clickCount = 0;
  }

  // ---- animation frame ----
  if (millis() - lastFrame >= FRAME_MS) {
    lastFrame = millis();
    animate();
  }
}

// ---- set up a channel: static header + label, reset motion ----
void showTV() {
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.println("MINI TV");

  tft.setTextSize(2);
  tft.setCursor(10, 35);
  if (tv_channel == 0) { tft.setTextColor(ST77XX_YELLOW); tft.println("CAT");  boxW = 80;  boxH = 80; }
  if (tv_channel == 1) { tft.setTextColor(ST77XX_CYAN);   tft.println("DOG");  boxW = 100; boxH = 80; }
  if (tv_channel == 2) { tft.setTextColor(ST77XX_GREEN);  tft.println("BIRD"); boxW = 80;  boxH = 40; }

  // start centered, moving diagonally
  x = (SCREEN_W - boxW) / 2;
  y = AREA_TOP + 20;
  vx = 2.2f;
  vy = 1.6f;
  prevX = (int)x;
  prevY = (int)y;
  blinking = false;
  lastBlink = millis();

  drawAnimal((int)x, (int)y, false);
}

// ---- one animation step ----
void animate() {
  // move
  x += vx;
  y += vy;

  // bounce off the walls
  if (x < 0)                 { x = 0;                 vx = -vx; }
  if (x > SCREEN_W - boxW)   { x = SCREEN_W - boxW;   vx = -vx; }
  if (y < AREA_TOP)          { y = AREA_TOP;          vy = -vy; }
  if (y > SCREEN_H - boxH)   { y = SCREEN_H - boxH;   vy = -vy; }

  // blink about every 2.5 s for ~150 ms
  unsigned long now = millis();
  if (!blinking && now - lastBlink > 2500) { blinking = true;  lastBlink = now; }
  if (blinking  && now - lastBlink > 150)  { blinking = false; lastBlink = now; }

  // erase old position, draw new
  tft.fillRect(prevX, prevY, boxW, boxH, ST77XX_BLACK);
  drawAnimal((int)x, (int)y, blinking);
  prevX = (int)x;
  prevY = (int)y;
}

void drawAnimal(int ox, int oy, bool blink) {
  if (tv_channel == 0) drawCat(ox, oy, blink);
  else if (tv_channel == 1) drawDog(ox, oy, blink);
  else drawBird(ox, oy, blink);
}

// ---- CAT (box 80x80) ----
void drawCat(int ox, int oy, bool blink) {
  tft.fillRect(ox + 10, oy + 20, 60, 60, ST77XX_ORANGE);   // head
  tft.fillRect(ox + 0,  oy + 0,  20, 20, ST77XX_ORANGE);   // ear L
  tft.fillRect(ox + 60, oy + 0,  20, 20, ST77XX_ORANGE);   // ear R
  if (blink) {
    tft.fillRect(ox + 25, oy + 42, 6, 2, ST77XX_BLACK);    // eyes closed
    tft.fillRect(ox + 45, oy + 42, 6, 2, ST77XX_BLACK);
  } else {
    tft.fillRect(ox + 25, oy + 40, 6, 6, ST77XX_BLACK);    // eyes open
    tft.fillRect(ox + 45, oy + 40, 6, 6, ST77XX_BLACK);
  }
  tft.fillRect(ox + 35, oy + 55, 10, 5, ST77XX_RED);       // mouth
}

// ---- DOG (box 100x80) ----
void drawDog(int ox, int oy, bool blink) {
  tft.fillRect(ox + 10, oy + 10, 80, 70, ST77XX_MAGENTA);  // head
  tft.fillRect(ox + 0,  oy + 0,  20, 40, ST77XX_MAGENTA);  // ear L
  tft.fillRect(ox + 80, oy + 0,  20, 40, ST77XX_MAGENTA);  // ear R
  if (blink) {
    tft.fillRect(ox + 30, oy + 44, 8, 2, ST77XX_BLACK);
    tft.fillRect(ox + 60, oy + 44, 8, 2, ST77XX_BLACK);
  } else {
    tft.fillRect(ox + 30, oy + 40, 8, 8, ST77XX_BLACK);
    tft.fillRect(ox + 60, oy + 40, 8, 8, ST77XX_BLACK);
  }
}

// ---- BIRD (box 80x40) ----
void drawBird(int ox, int oy, bool blink) {
  tft.fillRect(ox + 0,  oy + 0,  60, 40, ST77XX_YELLOW);   // body
  tft.fillRect(ox + 60, oy + 10, 20, 20, ST77XX_ORANGE);   // beak
  if (blink)
    tft.fillRect(ox + 20, oy + 22, 6, 2, ST77XX_BLACK);    // eye closed
  else
    tft.fillRect(ox + 20, oy + 20, 6, 6, ST77XX_BLACK);    // eye open
}
