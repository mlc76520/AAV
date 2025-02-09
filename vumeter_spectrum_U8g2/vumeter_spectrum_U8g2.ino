#include <SPI.h>
#include <Wire.h>
#include <AverageValue.h>
#include "graphics.h"
#include <U8g2lib.h>

// Define screen pins
#define CS1 2     // Left Display Chip Select
#define DC1 3     // Data/Command pour l'écran 1
#define RESET1 4  // Broche Reset pour l'écran 1
#define CS2 5     // Right Display Chip Select
#define DC2 6     // Data/Command pour l'écran 2
#define RESET2 7  // Broche Reset pour l'écran 2

U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI displayLeft(U8G2_R0, CS1, DC1, RESET1);   // Left Display
U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI displayRight(U8G2_R0, CS2, DC2, RESET2);  // Right Display

// Command headers
#define CMD_SPECTRUM 0x1A
#define CMD_SLEEP_MODE 0x1C
#define CMD_ENCODER_1 0x1D
#define CMD_ENCODER_2 0x1E
#define CMD_SWITCH_1 0x1F
#define CMD_SWITCH_2 0x20
#define CMD_SONG_TITLE 0x21
#define CMD_SONG_ARTIST 0x22
#define CMD_SONG_ALBUM 0x23
#define CMD_SONG_TRACK 0x24
#define CMD_SONG_ENCODED 0x25
#define CMD_SONG_BITRATE 0x26
#define CMD_SONG_OUTRATE 0x27
#define CMD_SONG_STATE 0x28
#define CMD_SONG_ELAPSED 0x29
#define CMD_SONG_DURATION 0x2A
#define CMD_SONG_VOLUME 0x2B
#define CMD_SONG_REPEAT 0x2C
#define CMD_SONG_RANDOM 0x2D
#define CMD_SONG_SINGLE 0x2E
#define CMD_SONG_CONSUME 0x2F
#define CMD_SONG_PLAYLIST 0x30
#define CMD_SONG_PLAYLISTLENGHT 0x31
#define CMD_SOURCE 0x32

// Maximum lengths for buffers
#define SPECTRUM_SIZE 7

// Variables pour stocker les données reçues
char songTitle[31] = "";   // Titre de la chanson (max 30 caractères + null-terminator)
char songArtist[31] = "";  // Artiste de la chanson (max 30 caractères + null-terminator)
char songAlbum[31] = "";   // Album de la chanson (max 30 caractères + null-terminator)
char songTrack[31] = "";
char songEncoded[15] = "";
char songBitrate[15] = "";
char songOutrate[15] = "";
char songState[8] = "";
char songElapsed[10] = "";
char songDuration[10] = "";
char songVolume[10] = "";
char songRepeat[10] = "";
char songRandom[10] = "";
char songSingle[10] = "";
char songConsume[10] = "";
char songPlaylist[10] = "";
char songPlaylistlenght[10] = "";

// Data buffers
const int BUFFER_SIZE = 32;
uint8_t buffer[BUFFER_SIZE];
uint8_t levelsL[SPECTRUM_SIZE];
uint8_t levelsR[SPECTRUM_SIZE];

// Screensaver
unsigned long sleepPreviousMillis = 0;
const long sleepDelayInterval = 5000;  // 5 seconds in milliseconds
bool SLEEP = true;                     // screens sleep mode function

//peakFalling
const long peakDelayInterval = 30;  // 50 milliseconds
unsigned long lastPeakDecreaseTimeR[7];
unsigned long lastPeakDecreaseTimeL[7];

//I2C
int i2c_addr = 0x13;

//spectrumBars
volatile byte audio_bar_heightL[7];  // sizes for the individual bars
volatile byte audio_bar_peakL[7];    // positions for the individual peaks (lines over the bars)
volatile byte audio_bar_heightR[7];  // sizes for the individual bars
volatile byte audio_bar_peakR[7];    // positions for the individual peaks (lines over the bars)

// VuMeter PI (PID loop without D)
unsigned int cnt = 0;
unsigned int val0 = 0, val1 = 0;
int pos0 = 0, pos1 = 0;
int err_accum0 = 0, err_accum1 = 0;
int err0 = 0, err1 = 0;

//Average level
volatile byte averagelevelL = 0;
volatile byte averagelevelR = 0;
const long MAX_VALUES_NUM = 7;
AverageValue<long> averageValueL(MAX_VALUES_NUM);
AverageValue<long> averageValueR(MAX_VALUES_NUM);

// Buttons and encoder
int encNumber = 0;
int encValue = 0;

int PHYS = 2;  //enable needle mass spring physics response, 0 no physics, 1 underdamped, 2 overdamped
// p  ,i
// 0.2,0.8 - lots of overshoot/oscillations around setpoint, slower settling, underdamped
// 0.5,0.1 - quicker settling, little overshoot/oscillations, overdamped
double p_gain = 0.2;
double i_gain = 0.8;

void setup() {
  // Initialize displays
  displayLeft.begin();
  displayRight.begin();

  // Configure display settings
  displayLeft.setContrast(0xff);  // Set initial contrast
  displayRight.setContrast(0xff);
  displayLeft.clearBuffer();
  displayRight.clearBuffer();
  displayLeft.drawXBMP(0, 0, 128, 64, welcome);
  displayRight.drawXBMP(0, 0, 128, 64, welcome);
  displayLeft.sendBuffer();
  displayRight.sendBuffer();

  Serial.begin(115200);
  Wire.begin(i2c_addr);          // join i2c bus with address i2c_addr
  Wire.onReceive(receiveEvent);  // register event
  delay(1500);
  displayLeft.clearDisplay();
  displayRight.clearDisplay();
}

void receiveEvent(int dataLength) {
  // Sanity check for buffer overflow
  if (dataLength > BUFFER_SIZE) {
    while (Wire.available()) Wire.read();  // Clear the buffer
    Serial.println("Error: Buffer overflow");
    return;
  }

  // Read all bytes into buffer
  for (int i = 0; i < dataLength; i++) {
    buffer[i] = Wire.read();
  }

  // Process command based on first byte
  switch (buffer[0]) {
    case CMD_SPECTRUM:
      if (dataLength >= 15) {  // 1 command byte + 7 left + 7 right
        handleSpectrum();
      }
      break;

    case CMD_SLEEP_MODE:
      if (dataLength >= 2) {  // 1 command byte + 1 state byte
        handleSleepMode();
      }
      break;

    /* Commented since no values with destination to arduino
    case CMD_ENCODER_1: 
      encNumber = 1;
      handleEncoder("Encoder ", encNumber, dataLength);
      break;
    */
    case CMD_ENCODER_2:
      encNumber = 2;
      handleEncoder("Encoder ", encNumber, dataLength);
      break;

    case CMD_SWITCH_1:
      handleSwitch("Switch 1", dataLength);
      break;

    case CMD_SWITCH_2:
      handleSwitch("Switch 2", dataLength);
      break;

    case CMD_SONG_TITLE:
      if (dataLength > 1) {  // At least command byte + 1 character
        decodeStringData(buffer, dataLength, songTitle, sizeof(songTitle));
        Serial.print("Title: ");
        Serial.println(songTitle);
      }
      break;

    case CMD_SONG_ARTIST:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songArtist, sizeof(songArtist));
        Serial.print("Artist: ");
        Serial.println(songArtist);
      }
      break;

    case CMD_SONG_ALBUM:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songAlbum, sizeof(songAlbum));
        Serial.print("Album: ");
        Serial.println(songAlbum);
      }
      break;

    case CMD_SONG_TRACK:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songTrack, sizeof(songTrack));
        Serial.print("Track: ");
        Serial.println(songTrack);
      }
      break;

    case CMD_SONG_ENCODED:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songEncoded, sizeof(songEncoded));
        Serial.print("Encoded: ");
        Serial.println(songEncoded);
      }
      break;

    case CMD_SONG_BITRATE:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songBitrate, sizeof(songBitrate));
        Serial.print("Bitrate: ");
        Serial.println(songBitrate);
      }
      break;

    case CMD_SONG_OUTRATE:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songOutrate, sizeof(songOutrate));
        Serial.print("Outrate: ");
        Serial.println(songOutrate);
      }
      break;

    case CMD_SONG_STATE:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songState, sizeof(songState));
        Serial.print("State: ");
        Serial.println(songState);
      }
      break;

    case CMD_SONG_ELAPSED:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songElapsed, sizeof(songElapsed));
        Serial.print("Elapsed time: ");
        Serial.println(songElapsed);
      }
      break;

    case CMD_SONG_DURATION:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songDuration, sizeof(songDuration));
        Serial.print("Duration time: ");
        Serial.println(songDuration);
      }
      break;

    case CMD_SONG_VOLUME:
      if (dataLength > 1) {  // At least command byte + 1 digit
        decodeStringData(buffer, dataLength, songVolume, sizeof(songVolume));
        Serial.print("Volume: ");
        Serial.println(songVolume);
      }
      break;

    default:
      Serial.print("Unknown command: 0x");
      Serial.println(buffer[0], HEX);
      break;
  }
}

void handleSpectrum() {
  for (int i = 0; i < SPECTRUM_SIZE; i++) {
    levelsL[i] = buffer[i + 1];
    levelsR[i] = buffer[i + 8];
    averageValueL.push(levelsL[i]);
    averageValueR.push(levelsR[i]);
  }
}

void handleSleepMode() {
  SLEEP = (buffer[1] == 1);
  Serial.print("Sleep mode: ");
  Serial.println(SLEEP ? "ON" : "OFF");
  Serial.print("Sleep Buffer [0]=0x");
  Serial.print(buffer[0], HEX);
  Serial.print(", [1]=");
  Serial.println(buffer[1]);
}

void handleEncoder(const char *name, int encNumber, int dataLength) {
  Serial.print(name);
  Serial.print(encNumber);
  Serial.print(" value: ");
  encValue = buffer[1];
  Serial.println(buffer[1]);
}

void handleSwitch(const char *name, int dataLength) {
  Serial.print(name);
  Serial.print(" state: ");
  Serial.println(buffer[1]);
}

void decodeStringData(uint8_t *data, int length, char *output, int maxLength) {
  // Skip both command byte AND length byte
  data += 2;    // Skip two bytes instead of one
  length -= 2;  // Decrease length by 2

  // Ensure we don't overflow the output buffer
  int copyLength = min(length, maxLength - 1);

  // Copy the data, handling special characters
  for (int i = 0; i < copyLength; i++) {
    output[i] = (char)data[i];
  }

  output[copyLength] = '\0';  // Null terminate the string
}

int decodeIntegerData(uint8_t *data, int length) {  // Function to decode integer data

  // Skip command byte
  data++;
  length--;

  // Safety check
  if (length <= 0) return 0;

  char temp[10] = { 0 };            // Temporary buffer for string conversion
  int copyLength = min(length, 9);  // Limit to 9 digits plus null terminator

  // Copy data to temp buffer
  memcpy(temp, data, copyLength);
  temp[copyLength] = '\0';  // Ensure null termination

  return atoi(temp);
}

// Visualization functions
void infos() {

  displayLeft.clearBuffer();
  displayLeft.setFontMode(0);                        // enable transparent mode, which is faster
  displayLeft.setFont(u8g2_font_nerhoe_tr);          // set font
  displayLeft.setFontMode(0);                        // enable transparent mode, which is faster
  //displayLeft.drawUTF8(0, 11, songTrack);
  //displayLeft.drawUTF8(0, 22, songTitle);
  String trackAndTitle = String (songTrack) + " - " + String (songTitle);
  displayLeft.drawUTF8(0, 40, trackAndTitle.c_str());
  displayLeft.drawUTF8(0, 50, songArtist);
  displayLeft.drawUTF8(0, 60, songAlbum);
  displayLeft.sendBuffer();

  displayRight.clearBuffer();
  displayRight.setFontMode(0);
  displayRight.setFont(u8g2_font_nerhoe_tr);
  displayRight.drawUTF8(0, 10, songState);
  String Duration = "/ " + String(songDuration);
  displayRight.drawUTF8(0, 47, Duration.c_str());
  displayRight.setFontMode(0);
  displayRight.setFont(u8g2_font_inr16_mr);
  displayRight.drawUTF8(0, 32, songElapsed);
  displayRight.setFont(u8g2_font_nerhoe_tr);
  displayRight.drawUTF8(0, 60, songEncoded);
  displayRight.drawUTF8(69, 60, songBitrate);
  displayRight.sendBuffer();
}

void spectrumbars() {
  // Left display
  displayLeft.clearBuffer();

  // draw the individual labels Left
  displayLeft.setFont(u8g2_font_nerhoe_tr);  // set font
  displayLeft.drawStr(1, 64, "16K");
  displayLeft.drawStr(18, 64, "6.3K");
  displayLeft.drawStr(38, 64, "2.5K");
  displayLeft.drawStr(60, 64, "1K");
  displayLeft.drawStr(75, 64, "400");
  displayLeft.drawStr(96, 64, "160");
  displayLeft.drawStr(116, 64, "63");

  // Write the current stereo channel Left
  //displayLeft.setFont(u8g2_font_5x7_mr);
  //displayLeft.drawUTF8(0, 8, songTitle);

  unsigned long currentTimeL = millis();

  for (int i = 0; i < 7; i++) {
    // Smoothly update bar heights
    audio_bar_heightL[i] += (map(levelsL[i], 0, 255, 0, 53) - audio_bar_heightL[i]);

    // Update peak logic
    if (audio_bar_peakL[i] < audio_bar_heightL[i]) {
      audio_bar_peakL[i] = audio_bar_heightL[i];
    } else if (audio_bar_peakL[i] > audio_bar_heightL[i]) {
      if (currentTimeL - lastPeakDecreaseTimeL[i] >= peakDelayInterval) {
        audio_bar_peakL[i]--;
        lastPeakDecreaseTimeL[i] = currentTimeL;
      }
    }

    // Draw bars
    if (encNumber == 2 && encValue == 1) {
      for (int j = 2; j < 12; j++) {
        displayLeft.drawVLine(j + (i * 19), 53 - audio_bar_heightL[i], audio_bar_heightL[i]);
      }
      displayLeft.drawHLine(2 + (i * 19), 53 - audio_bar_peakL[i], 10);
    } else {
      displayLeft.drawHLine(2 + (i * 19), 53 - audio_bar_heightL[i], 10);
      displayLeft.drawVLine(2 + (i * 19), 53 - audio_bar_heightL[i], audio_bar_heightL[i]);
      displayLeft.drawVLine(11 + (i * 19), 53 - audio_bar_heightL[i], audio_bar_heightL[i]);
    }
  }

  displayLeft.sendBuffer();

  // Right display
  displayRight.clearBuffer();

  // draw the individual labels Right
  displayLeft.setFont(u8g2_font_nerhoe_tr);  // set font
  displayLeft.drawStr(2, 64, "63");
  displayLeft.drawStr(19, 64, "160");
  displayLeft.drawStr(37, 64, "400");
  displayLeft.drawStr(60, 64, "1K");
  displayLeft.drawStr(75, 64, "2.5K");
  displayLeft.drawStr(95, 64, "6.3K");
  displayLeft.drawStr(115, 64, "16K");

  // ... Similar code for right channel bars
  unsigned long currentTimeR = millis();

  for (int i = 0; i < 7; i++) {
    // Smoothly update bar heights
    audio_bar_heightR[i] += (map(levelsR[i], 0, 255, 0, 53) - audio_bar_heightR[i]);

    // Update peak logic
    if (audio_bar_peakR[i] < audio_bar_heightR[i]) {
      audio_bar_peakR[i] = audio_bar_heightR[i];
    } else if (audio_bar_peakR[i] > audio_bar_heightR[i]) {
      if (currentTimeR - lastPeakDecreaseTimeR[i] >= peakDelayInterval) {
        audio_bar_peakR[i]--;
        lastPeakDecreaseTimeR[i] = currentTimeR;
      }
    }
    // Draw bars
    if (encNumber == 2 && encValue == 1) {
      for (int j = 2; j < 12; j++) {
        displayRight.drawVLine(j + (i * 19), 53 - audio_bar_heightR[i], audio_bar_heightR[i]);
      }
      displayRight.drawHLine(2 + (i * 19), 53 - audio_bar_peakR[i], 10);
    } else {
      displayRight.drawHLine(2 + (i * 19), 53 - audio_bar_heightR[i], 10);
      displayRight.drawVLine(2 + (i * 19), 53 - audio_bar_heightR[i], audio_bar_heightR[i]);
      displayRight.drawVLine(11 + (i * 19), 53 - audio_bar_heightR[i], audio_bar_heightR[i]);
    }
  }
  displayRight.sendBuffer();
}

void physics(int PHYS) {
  averagelevelL = averageValueL.average();
  averagelevelR = averageValueR.average();

  //Vumeter
  val0 = map(averagelevelL, 0, 255, 0, 127);  //Grab Digital Input Measurement (scaled to 0-127)
  val1 = map(averagelevelR, 0, 255, 0, 127);  //Grab Digital Input Measurement (scaled to 0-127)

  if (PHYS) {
    if (PHYS == 1) {  // underdamped
      p_gain = 0.2;
      i_gain = 0.8;
    }                 //Set gain values for current mode
    if (PHYS == 2) {  // overdamped
      p_gain = 0.5;
      i_gain = 0.1;
    }

    err0 = val0 - pos0;  //Calculate Error
    err1 = val1 - pos1;

    err_accum0 += i_gain * err0;  //Calculate PI
    pos0 += (int)(p_gain * err0 + err_accum0);

    err_accum1 += i_gain * err1;
    pos1 += (int)(p_gain * err1 + err_accum1);

    if (pos0 > 127) { pos0 = 127; }  //max min limiter
    if (pos0 < 0) { pos0 = 0; }
    if (pos1 > 127) { pos1 = 127; }
    if (pos1 < 0) { pos1 = 0; }
  } else {
    pos0 = val0;
    pos1 = val1;
  }
}

void needdleL(int pos0) {
  // Left Needle
  int startX = 71 - (127 - pos0) / 8;
  int startY = 63;
  int endX = pos0;
  int curveHeight = pos0 * (127 - pos0);
  int endY = 20 - curveHeight / 200;
  displayLeft.drawLine(startX, startY, endX, endY);
  displayLeft.sendBuffer();
}

void needdleR(int pos1) {
  // Left Needle
  int startX = 71 - (127 - pos0) / 8;
  int startY = 63;
  int endX = pos0;
  int curveHeight = pos0 * (127 - pos0);
  int endY = 20 - curveHeight / 200;
  displayRight.drawLine(startX, startY, endX, endY);
  displayRight.sendBuffer();
}

void vumeter() {

  physics(PHYS);
  // Draw left VU meter
  displayLeft.clearBuffer();
  displayLeft.setDrawColor(1);
  displayLeft.setFont(u8g2_font_trixel_square_tn);
  displayLeft.clearBuffer();  // Clear OLED 1

  // Background 1
  displayLeft.drawArc(64, 143, 120, 55, 82);         // Main arc
  displayLeft.drawArc(64, 143, 120, 45, 53);         // Main arc part 2
  displayLeft.drawArc(64, 142, 120, 45, 53);         // Main arc part 2 bold
  displayLeft.drawArc(64, 141, 120, 45, 53);         // Main arc part 2 bold
  displayLeft.drawLine(119, 36, 134, 23);            // graduation +3 Bold
  displayLeft.drawLine(118, 36, 133, 23);            // graduation +3
  displayLeft.drawLine(128 - 20, 31, 128 - 15, 26);  // graduation vide
  displayLeft.drawLine(105, 17, 97, 27);             // graduation 0 Bold
  displayLeft.drawLine(104, 17, 128 - 32, 27);       // graduation 0
  displayLeft.drawVLine(64, 15, 8);                  // graduation -5
  displayLeft.drawLine(24, 17, 32, 27);              // graduation -15
  displayLeft.drawLine(14, 27, 18, 31);              // graduation -20
  displayLeft.drawStr(0, 31 - 10, "-20");
  displayLeft.drawStr(20, 13, "-15");
  displayLeft.drawStr(61, 8, "-5");
  displayLeft.drawStr(103, 13, "0");
  displayLeft.drawStr(119, 20, "+3");
  displayLeft.setFont(u8g2_font_missingplanet_tr);
  displayLeft.drawStr(52, 52, "Left");
  displayLeft.drawStr(57, 42, "dB");

  needdleL(pos0);

  // Draw right VU meter
  displayRight.clearBuffer();
  displayRight.setDrawColor(1);
  displayRight.setFont(u8g2_font_trixel_square_tn);
  displayRight.clearBuffer();  // Clear OLED 1

  // Background 1
  displayRight.drawArc(64, 143, 120, 55, 82);         // Main arc
  displayRight.drawArc(64, 143, 120, 45, 53);         // Main arc part 2
  displayRight.drawArc(64, 142, 120, 45, 53);         // Main arc part 2 bold
  displayRight.drawArc(64, 141, 120, 45, 53);         // Main arc part 2 bold
  displayRight.drawLine(119, 36, 134, 23);            // graduation +3 Bold
  displayRight.drawLine(118, 36, 133, 23);            // graduation +3
  displayRight.drawLine(128 - 20, 31, 128 - 15, 26);  // graduation vide
  displayRight.drawLine(105, 17, 97, 27);             // graduation 0 Bold
  displayRight.drawLine(104, 17, 128 - 32, 27);       // graduation 0
  displayRight.drawVLine(64, 15, 8);                  // graduation -5
  displayRight.drawLine(24, 17, 32, 27);              // graduation -15
  displayRight.drawLine(14, 27, 18, 31);              // graduation -20
  displayRight.drawStr(0, 31 - 10, "-20");
  displayRight.drawStr(20, 13, "-15");
  displayRight.drawStr(61, 8, "-5");
  displayRight.drawStr(103, 13, "0");
  displayRight.drawStr(119, 20, "+3");
  displayRight.setFont(u8g2_font_missingplanet_tr);
  displayRight.drawStr(52, 52, "Right");
  displayRight.drawStr(57, 42, "dB");

  needdleR(pos1);
}

void vumeter2() {

  physics(PHYS);

  // Draw LEFT VU meter
  displayLeft.clearBuffer();
  displayLeft.setDrawColor(1);
  displayLeft.clearBuffer();
  // Background
  displayLeft.setFont(u8g2_font_6x10_tr);
  displayLeft.drawStr(55, 35, "VU");
  displayLeft.setFont(u8g2_font_missingplanet_tr);
  displayLeft.drawStr(40, 60, ".::mlc:HiFi::.");

  displayLeft.setFont(u8g2_font_trixel_square_tn);
  displayLeft.drawStr(0, 5, "20");
  displayLeft.drawVLine(0, 8, 3);  // 20
  displayLeft.drawStr(21, 5, "10");
  displayLeft.drawVLine(23, 8, 3);  // 10
  displayLeft.drawStr(43, 5, "7");
  displayLeft.drawVLine(43, 8, 3);  // 7
  displayLeft.drawStr(57, 5, "5");
  displayLeft.drawVLine(57, 8, 3);  // 5
  displayLeft.drawStr(70, 5, "3");
  displayLeft.drawVLine(70, 8, 3);  // 2
  displayLeft.drawStr(84, 5, "2");
  displayLeft.drawVLine(84, 8, 3);  // 2
  displayLeft.drawStr(94, 5, "1");
  displayLeft.drawVLine(95, 8, 3);  // 1

  displayLeft.drawStr(104, 5, "0");
  displayLeft.drawStr(111, 5, "1");
  displayLeft.drawStr(118, 5, "2");
  displayLeft.drawStr(125, 5, "3");

  displayLeft.drawHLine(105, 9, 127);
  displayLeft.drawVLine(105, 8, 3);
  displayLeft.drawVLine(112, 8, 3);
  displayLeft.drawVLine(119, 8, 3);
  displayLeft.drawVLine(127, 8, 3);

  displayLeft.drawHLine(0, 10, 128);
  displayLeft.drawHLine(0, 12, 128);

  displayLeft.drawVLine(0, 13, 2);    //0
  displayLeft.drawVLine(21, 13, 2);   //20
  displayLeft.drawVLine(42, 13, 2);   //40
  displayLeft.drawVLine(63, 13, 2);   //60
  displayLeft.drawVLine(84, 13, 2);   //80
  displayLeft.drawVLine(105, 13, 2);  //100
  displayLeft.drawVLine(127, 13, 2);  //128

  displayLeft.drawStr(0, 21, "0");
  displayLeft.drawStr(18, 21, "20");
  displayLeft.drawStr(39, 21, "40");
  displayLeft.drawStr(60, 21, "60");
  displayLeft.drawStr(81, 21, "80");
  displayLeft.drawStr(100, 21, "100");

  displayLeft.drawStr(125, 25, "+");
  displayLeft.drawStr(0, 25, "-");

  needdleL(pos0);

  // Draw right VU meter
  displayRight.clearBuffer();
  displayRight.setDrawColor(1);
  displayRight.clearBuffer();
  // Background
  displayRight.setFont(u8g2_font_6x10_tr);
  displayRight.drawStr(55, 35, "VU");
  displayRight.setFont(u8g2_font_missingplanet_tr);
  displayRight.drawStr(40, 60, ".::mlc:HiFi::.");

  displayRight.setFont(u8g2_font_trixel_square_tn);
  displayRight.drawStr(0, 5, "20");
  displayRight.drawVLine(0, 8, 3);  // 20
  displayRight.drawStr(21, 5, "10");
  displayRight.drawVLine(23, 8, 3);  // 10
  displayRight.drawStr(43, 5, "7");
  displayRight.drawVLine(43, 8, 3);  // 7
  displayRight.drawStr(57, 5, "5");
  displayRight.drawVLine(57, 8, 3);  // 5

  displayRight.drawStr(70, 5, "3");
  displayRight.drawVLine(70, 8, 3);  // 2

  displayRight.drawStr(84, 5, "2");
  displayRight.drawVLine(84, 8, 3);  // 2

  displayRight.drawStr(94, 5, "1");
  displayRight.drawVLine(95, 8, 3);  // 1

  displayRight.drawStr(104, 5, "0");
  displayRight.drawStr(111, 5, "1");
  displayRight.drawStr(118, 5, "2");
  displayRight.drawStr(125, 5, "3");

  displayRight.drawHLine(105, 9, 127);
  displayRight.drawVLine(105, 8, 3);
  displayRight.drawVLine(112, 8, 3);
  displayRight.drawVLine(119, 8, 3);
  displayRight.drawVLine(127, 8, 3);

  displayRight.drawHLine(0, 10, 128);
  displayRight.drawHLine(0, 12, 128);

  displayRight.drawVLine(0, 13, 2);    //0
  displayRight.drawVLine(21, 13, 2);   //20
  displayRight.drawVLine(42, 13, 2);   //40
  displayRight.drawVLine(63, 13, 2);   //60
  displayRight.drawVLine(84, 13, 2);   //80
  displayRight.drawVLine(105, 13, 2);  //100
  displayRight.drawVLine(127, 13, 2);  //128

  displayRight.drawStr(0, 21, "0");
  displayRight.drawStr(18, 21, "20");
  displayRight.drawStr(39, 21, "40");
  displayRight.drawStr(60, 21, "60");
  displayRight.drawStr(81, 21, "80");
  displayRight.drawStr(100, 21, "100");

  displayRight.drawStr(125, 25, "+");
  displayRight.drawStr(0, 25, "-");

  needdleR(pos1);
}

void infos2() {
  // Peak meter visualization
}

void waterfall() {
  // Waterfall visualization
}

typedef void (*VisualizationFunction)();

// Structure to map encoder settings to visualization functions
struct VisualizationMapping {
  uint8_t encoderNumber;
  uint8_t encoderValue;
  VisualizationFunction function;
};

// Mapping table - easy to add new modes
const VisualizationMapping VISUALIZATION_MAP[] = {
  { 2, 0, infos },         // Encoder 2, value 0 -> Spectrum
  { 2, 1, spectrumbars },  // Encoder 2, value 1 -> Spectrum (alternative settings?)
  { 2, 2, spectrumbars },  // Encoder 2, value 2 -> VU meter
  { 2, 3, vumeter },       // Encoder 2, value 3 -> VU meter (alternative settings?)
  { 2, 4, vumeter2 },      // Example new mode
  { 2, 5, infos2 },        // Example new mode
  { 2, 6, waterfall }      // Example new mode
};

// Calculate the size of the mapping array
const size_t VISUALIZATION_COUNT = sizeof(VISUALIZATION_MAP) / sizeof(VISUALIZATION_MAP[0]);

void loop() {
  static bool sleeping = false;

  if (SLEEP && !sleeping) {
    unsigned long sleepCurrentMillis = millis();
    if (sleepCurrentMillis - sleepPreviousMillis >= sleepDelayInterval) {
      sleeping = true;
      displayLeft.clearDisplay();
      displayRight.clearDisplay();
    }
  } else if (!SLEEP) {
    sleeping = false;
    sleepPreviousMillis = millis();
  }

  if (sleeping) return;

  // Default to vumeter if no matching configuration is found
  VisualizationFunction currentVisualization = infos;

  // Look for matching configuration in the mapping table
  for (size_t i = 0; i < VISUALIZATION_COUNT; i++) {
    if (VISUALIZATION_MAP[i].encoderNumber == encNumber && VISUALIZATION_MAP[i].encoderValue == encValue) {
      currentVisualization = VISUALIZATION_MAP[i].function;
      break;
    }
  }
  currentVisualization();
}
