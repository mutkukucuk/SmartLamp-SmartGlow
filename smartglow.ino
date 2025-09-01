#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

TFT_eSPI tft = TFT_eSPI();
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS);

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FONT_SIZE 2
#define NUM_LEDS_1 30  // First strip with 30 LEDs
#define LED_PIN_1 5   // GPIO5 for first strip
#define NUM_LEDS_2 32  // Second strip with 32 LEDs
#define LED_PIN_2 13  // GPIO13 for second strip
CRGB leds1[NUM_LEDS_1];
CRGB leds2[NUM_LEDS_2];
Preferences preferences;

#define BRIGHT_UP_X 30
#define BRIGHT_UP_Y 90
#define BRIGHT_DOWN_X 190
#define BRIGHT_DOWN_Y 90
#define MODE_X 15
#define MODE_Y 180
#define COLOR_X 115
#define COLOR_Y 180
#define ONOFF_X 215
#define ONOFF_Y 180
#define BUTTON_W 90
#define BUTTON_H 60
#define STATUS_Y 10
#define TIME_Y 10
#define INFO_Y 30
#define COORD_Y 50
#define BUTTON_RADIUS 10

// Touch calibration (adjust based on your observed coordinates)
#define TOUCH_X_MIN 100
#define TOUCH_X_MAX 4000
#define TOUCH_Y_MIN 100
#define TOUCH_Y_MAX 4000

// WiFi and MQTT settings
const char* ssid = ""; // Replace with your WiFi SSID
const char* password = ""; // Replace with your WiFi password
const char* mqtt_server = ""; // Replace with your MQTT broker IP
const int mqtt_port = 1883;
const char* mqtt_user = ""; // Replace with your MQTT username
const char* mqtt_password = ""; // Replace with your MQTT password
const char* mqtt_client_id = "smart_lamp";
WiFiClient espClient;
PubSubClient client(espClient);

// NTP Client to get real time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 10800, 60000); // UTC+3 for Turkey, update interval 1 minute

int brightness = 153; // Start at 60% (153/255) for both strips
int animationMode = 0; // 0: Static, 1: Breath, 2: Rainbow, 3: Chase, 4: Blink, 5: Wave
bool ledsOn = true; // LED on/off state for both strips
CRGB currentColor = CRGB::White; // Color for both strips
const char* modes[] = {"Static", "Breath", "Rainbow", "Chase", "Blink", "Wave"};
const CRGB colors[] = {
  CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White, CRGB::Yellow,
  CRGB::Orange, CRGB::Purple, CRGB::Cyan, CRGB::Magenta, CRGB::Lime,
  CRGB::Pink, CRGB::Teal, CRGB::Violet, CRGB::Gold, CRGB::Coral
};
const char* colorNames[] = {
  "Red", "Green", "Blue", "White", "Yellow",
  "Orange", "Purple", "Cyan", "Magenta", "Lime",
  "Pink", "Teal", "Violet", "Gold", "Coral"
};
int colorIndex = 3; // Start with White
unsigned long lastUpdate = 0;
int fadeBrightness = 128;
bool fadeUp = true;
uint8_t hue = 0;
int chaseIndex = 0;
bool blinkState = true;
unsigned long lastTouch = 0;
const unsigned long DEBOUNCE_MS = 500;
unsigned long lastStatusUpdate = 0; // For limiting Serial output
unsigned long lastMqttUpdate = 0; // For MQTT debounce
unsigned long lastTimeUpdate = 0; // For time update interval
bool ntpInitialized = false; // Flag to check NTP initialization

void drawInterface() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(FONT_SIZE);
  
  tft.fillRoundRect(BRIGHT_UP_X, BRIGHT_UP_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_NAVY);
  tft.drawRoundRect(BRIGHT_UP_X + 2, BRIGHT_UP_Y + 2, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Up", BRIGHT_UP_X + 25, BRIGHT_UP_Y + 20, FONT_SIZE);
  tft.fillRoundRect(BRIGHT_DOWN_X, BRIGHT_DOWN_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_NAVY);
  tft.drawRoundRect(BRIGHT_DOWN_X + 2, BRIGHT_DOWN_Y + 2, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_DARKGREY);
  tft.drawString("Down", BRIGHT_DOWN_X + 15, BRIGHT_DOWN_Y + 20, FONT_SIZE);
  
  tft.fillRoundRect(MODE_X, MODE_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_DARKGREEN);
  tft.drawRoundRect(MODE_X + 2, MODE_Y + 2, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_DARKGREY);
  tft.drawString("Mode", MODE_X + 15, MODE_Y + 20, FONT_SIZE);
  tft.fillRoundRect(COLOR_X, COLOR_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_RED);
  tft.drawRoundRect(COLOR_X + 2, COLOR_Y + 2, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_DARKGREY);
  tft.drawString("Color", COLOR_X + 15, COLOR_Y + 20, FONT_SIZE);
  tft.fillRoundRect(ONOFF_X, ONOFF_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_ORANGE);
  tft.drawRoundRect(ONOFF_X + 2, ONOFF_Y + 2, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_DARKGREY);
  tft.drawString("OnOff", ONOFF_X + 20, ONOFF_Y + 20, FONT_SIZE);
}

void updateStatus() {
  if (WiFi.status() == WL_CONNECTED && !ntpInitialized) {
    timeClient.begin();
    ntpInitialized = timeClient.update();
    if (ntpInitialized) {
      Serial.println("NTP initialized successfully");
    } else {
      Serial.println("NTP initialization failed");
    }
  }

  String timeDisplay = "Time: --:--";
  if (ntpInitialized) {
    if (millis() - lastTimeUpdate >= 30000) { // Update every 30 seconds
      timeClient.update();
      lastTimeUpdate = millis();
    }
    String fullTime = timeClient.getFormattedTime();
    timeDisplay = "Time: " + fullTime.substring(0, 5); // Get hh:mm only
  }

  tft.fillRect(0, TIME_Y, SCREEN_WIDTH, 20, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, TIME_Y);
  tft.printf("%s", timeDisplay.c_str());

  tft.fillRect(0, INFO_Y, SCREEN_WIDTH, 20, TFT_BLACK);
  tft.setCursor(10, INFO_Y);
  tft.printf("Bright: %d%% Mode: %s Color: %s", 
             (int)((brightness / 255.0) * 100 + 0.5), modes[animationMode], colorNames[colorIndex]);
}

void drawTouchCoords(int x, int y) {
  tft.fillRect(0, COORD_Y, SCREEN_WIDTH, 20, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(10, COORD_Y);
  tft.printf("Touch: (%d, %d)", x, y);
}

void updateLEDs() {
  static unsigned long lastAnimUpdate = 0;
  if (!ledsOn) {
    fill_solid(leds1, NUM_LEDS_1, CRGB::Black);
    fill_solid(leds2, NUM_LEDS_2, CRGB::Black);
    FastLED.show();
    return;
  }

  if (millis() - lastAnimUpdate < 100) return;

  if (animationMode == 0) { // Static
    FastLED.setBrightness(brightness);
    fill_solid(leds1, NUM_LEDS_1, currentColor);
    fill_solid(leds2, NUM_LEDS_2, currentColor);
  } else if (animationMode == 1) { // Breath
    if (millis() - lastUpdate > 50) {
      if (fadeUp) {
        fadeBrightness += 5;
        if (fadeBrightness >= 255) {
          fadeBrightness = 255;
          fadeUp = false;
        }
      } else {
        fadeBrightness -= 5;
        if (fadeBrightness <= 10) {
          fadeBrightness = 10;
          fadeUp = true;
        }
      }
      FastLED.setBrightness(fadeBrightness);
      fill_solid(leds1, NUM_LEDS_1, currentColor);
      fill_solid(leds2, NUM_LEDS_2, currentColor);
      lastUpdate = millis();
    }
  } else if (animationMode == 2) { // Rainbow
    if (millis() - lastUpdate > 50) {
      hue++;
      for (int i = 0; i < NUM_LEDS_1; i++) {
        leds1[i] = CHSV(hue + (i * 10), 255, 255);
      }
      for (int i = 0; i < NUM_LEDS_2; i++) {
        leds2[i] = CHSV(hue + (i * 10), 255, 255);
      }
      FastLED.setBrightness(brightness);
      lastUpdate = millis();
    }
  } else if (animationMode == 3) { // Chase
    if (millis() - lastUpdate > 400) {
      fill_solid(leds1, NUM_LEDS_1, CRGB::Black);
      fill_solid(leds2, NUM_LEDS_2, CRGB::Black);
      leds1[chaseIndex] = currentColor;
      leds2[chaseIndex % NUM_LEDS_2] = currentColor;
      chaseIndex = (chaseIndex + 1) % NUM_LEDS_1;
      FastLED.setBrightness(brightness);
      lastUpdate = millis();
    }
  } else if (animationMode == 4) { // Blink
    if (millis() - lastUpdate > 2000) {
      blinkState = !blinkState;
      fill_solid(leds1, NUM_LEDS_1, blinkState ? currentColor : CRGB::Black);
      fill_solid(leds2, NUM_LEDS_2, blinkState ? currentColor : CRGB::Black);
      FastLED.setBrightness(brightness);
      lastUpdate = millis();
    }
  } else if (animationMode == 5) { // Wave
    if (millis() - lastUpdate > 200) {
      for (int i = 0; i < NUM_LEDS_1; i++) {
        int waveBrightness = (sin((millis() / 1000.0) + (i * 0.5)) * 127.5 + 127.5);
        leds1[i] = currentColor;
        leds1[i].fadeToBlackBy(255 - waveBrightness);
      }
      for (int i = 0; i < NUM_LEDS_2; i++) {
        int waveBrightness = (sin((millis() / 1000.0) + (i * 0.5)) * 127.5 + 127.5);
        leds2[i] = currentColor;
        leds2[i].fadeToBlackBy(255 - waveBrightness);
      }
      FastLED.setBrightness(brightness);
      lastUpdate = millis();
    }
  }

  FastLED.show();
  lastAnimUpdate = millis();
  if (millis() - lastStatusUpdate > 10000) {
    Serial.printf("LEDs updated: Bright=%d, Color=%s, Mode=%s, On=%d\n",
                  brightness, colorNames[colorIndex], modes[animationMode], ledsOn);
    lastStatusUpdate = millis();
  }
}

void saveSettings() {
  preferences.begin("lamp", false);
  preferences.putInt("brightness", brightness);
  preferences.putInt("animationMode", animationMode);
  preferences.putInt("colorIndex", colorIndex);
  preferences.putBool("ledsOn", ledsOn);
  preferences.end();
}

void loadSettings() {
  preferences.begin("lamp", true);
  brightness = preferences.getInt("brightness", 153);
  animationMode = preferences.getInt("animationMode", 0);
  colorIndex = preferences.getInt("colorIndex", 3);
  ledsOn = preferences.getBool("ledsOn", true);
  currentColor = colors[colorIndex];
  preferences.end();
  Serial.println("Settings loaded");
}

void publishState() {
  char buffer[16];
  client.publish("home/lamp/state", ledsOn ? "ON" : "OFF");
  snprintf(buffer, sizeof(buffer), "%d", (int)((brightness / 255.0) * 100 + 0.5));
  client.publish("home/lamp/brightness", buffer);
  snprintf(buffer, sizeof(buffer), "%d,%d,%d", currentColor.r, currentColor.g, currentColor.b);
  client.publish("home/lamp/color", buffer);
  client.publish("home/lamp/effect", modes[animationMode]);
  client.publish("home/lamp/color_name", colorNames[colorIndex]);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id, mqtt_user, mqtt_password)) {
      Serial.println("MQTT connected");
      client.subscribe("home/lamp/state");
      client.subscribe("home/lamp/brightness");
      client.subscribe("home/lamp/color");
      client.subscribe("home/lamp/effect");
      publishState();
    } else {
      Serial.print("MQTT connection failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (millis() - lastMqttUpdate < 500) return;
  char message[32];
  if (length >= sizeof(message)) length = sizeof(message) - 1;
  strncpy(message, (char*)payload, length);
  message[length] = '\0';
  Serial.printf("MQTT received: %s = %s\n", topic, message);

  bool stateChanged = false;
  if (strcmp(topic, "home/lamp/state") == 0) {
    if (strcmp(message, "ON") == 0 && !ledsOn) {
      ledsOn = true;
      brightness = 153;
      colorIndex = 3;
      currentColor = CRGB::White;
      animationMode = 0;
      stateChanged = true;
      Serial.println("State set to ON from MQTT, reset to defaults");
    } else if (strcmp(message, "OFF") == 0 && ledsOn) {
      ledsOn = false;
      stateChanged = true;
      Serial.println("State set to OFF from MQTT");
    }
  } else if (strcmp(topic, "home/lamp/brightness") == 0) {
    int newBrightness = atoi(message);
    if (newBrightness >= 0 && newBrightness <= 100) {
      int oldBrightness = brightness;
      brightness = (newBrightness * 255) / 100;
      if (brightness != oldBrightness) {
        stateChanged = true;
        Serial.printf("Brightness set to %d%% from MQTT\n", newBrightness);
      }
    } else {
      Serial.println("Invalid brightness value from MQTT");
    }
  } else if (strcmp(topic, "home/lamp/color") == 0) {
    int r, g, b;
    if (sscanf(message, "%d,%d,%d", &r, &g, &b) == 3) {
      if (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255) {
        CRGB newColor = CRGB(r, g, b);
        if (newColor != currentColor) {
          currentColor = newColor;
          for (int i = 0; i < 15; i++) {
            if (currentColor == colors[i]) {
              colorIndex = i;
              break;
            }
          }
          stateChanged = true;
          Serial.printf("Color set to RGB(%d,%d,%d) from MQTT\n", r, g, b);
        }
      } else {
        Serial.println("Invalid color value from MQTT");
      }
    }
  } else if (strcmp(topic, "home/lamp/effect") == 0) {
    for (int i = 0; i < 6; i++) {
      if (strcmp(message, modes[i]) == 0 && animationMode != i) {
        animationMode = i;
        fadeBrightness = 128;
        hue = 0;
        chaseIndex = 0;
        blinkState = true;
        stateChanged = true;
        Serial.printf("Effect set to %s from MQTT\n", modes[i]);
        break;
      }
    }
    if (!stateChanged) Serial.println("Invalid effect value from MQTT");
  }

  if (stateChanged) {
    updateLEDs();
    updateStatus();
    saveSettings();
    if (client.connected()) {
      publishState();
    }
    lastMqttUpdate = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(21, OUTPUT); // Backlight
  digitalWrite(21, HIGH);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  if (!touchscreen.begin(touchscreenSPI)) {
    Serial.println("Touchscreen failed to initialize!");
    while (1);
  }
  Serial.println("Touchscreen initialized successfully");
  touchscreen.setRotation(1);
  FastLED.addLeds<WS2812B, LED_PIN_1, GRB>(leds1, NUM_LEDS_1);
  FastLED.addLeds<WS2812B, LED_PIN_2, GRB>(leds2, NUM_LEDS_2);
  FastLED.setBrightness(brightness);
  loadSettings();
  updateLEDs();
  drawInterface();
  updateStatus(); // Initial time display

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 40) {
    delay(500);
    Serial.print(".");
    if (wifiAttempts % 5 == 0) {
      Serial.printf(" Attempt %d, IP: %s\n", wifiAttempts, WiFi.localIP().toString().c_str());
    }
    wifiAttempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected, IP: " + WiFi.localIP().toString());
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    reconnect();
  } else {
    Serial.println("\nWiFi connection failed after 40 attempts");
    Serial.printf("Status: %d, SSID: %s, IP: %s\n", WiFi.status(), ssid, WiFi.localIP().toString().c_str());
  }

  Serial.println("Setup complete");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    reconnect();
  }
  client.loop();

  if (touchscreen.touched() && (millis() - lastTouch > DEBOUNCE_MS)) {
    TS_Point p = touchscreen.getPoint();
    int x = map(p.x, TOUCH_X_MAX, TOUCH_X_MIN, 0, SCREEN_WIDTH);
    int y = map(p.y, TOUCH_Y_MAX, TOUCH_Y_MIN, 0, SCREEN_HEIGHT);
    Serial.printf("Touch at raw (%d, %d) mapped (%d, %d)\n", p.x, p.y, x, y);
    drawTouchCoords(x, y);
    
    bool stateChanged = false;
    if (x >= BRIGHT_UP_X && x <= (BRIGHT_UP_X + BUTTON_W) && y >= BRIGHT_UP_Y && y <= (BRIGHT_UP_Y + BUTTON_H)) {
      tft.drawRoundRect(BRIGHT_UP_X, BRIGHT_UP_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_WHITE);
      delay(100);
      tft.drawRoundRect(BRIGHT_UP_X, BRIGHT_UP_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_NAVY);
      Serial.println("Processing Up button");
      if (brightness <= 229) brightness += 26;
      else brightness = 255;
      stateChanged = true;
    } else if (x >= BRIGHT_DOWN_X && x <= (BRIGHT_DOWN_X + BUTTON_W) && y >= BRIGHT_DOWN_Y && y <= (BRIGHT_DOWN_Y + BUTTON_H)) {
      tft.drawRoundRect(BRIGHT_DOWN_X, BRIGHT_DOWN_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_WHITE);
      delay(100);
      tft.drawRoundRect(BRIGHT_DOWN_X, BRIGHT_DOWN_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_NAVY);
      Serial.println("Processing Down button");
      if (brightness >= 26) brightness -= 26;
      else brightness = 0;
      stateChanged = true;
    } else if (x >= MODE_X && x <= (MODE_X + BUTTON_W) && y >= MODE_Y && y <= (MODE_Y + BUTTON_H)) {
      tft.drawRoundRect(MODE_X, MODE_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_WHITE);
      delay(100);
      tft.drawRoundRect(MODE_X, MODE_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_DARKGREY);
      Serial.println("Processing Mode button");
      animationMode = (animationMode + 1) % 6;
      fadeBrightness = 128;
      hue = 0;
      chaseIndex = 0;
      blinkState = true;
      stateChanged = true;
      Serial.printf("Mode changed to: %s\n", modes[animationMode]);
    } else if (x >= COLOR_X && x <= (COLOR_X + BUTTON_W) && y >= COLOR_Y && y <= (COLOR_Y + BUTTON_H)) {
      tft.drawRoundRect(COLOR_X, COLOR_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_WHITE);
      delay(100);
      tft.drawRoundRect(COLOR_X, COLOR_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_RED);
      Serial.println("Processing Color button");
      colorIndex = (colorIndex + 1) % 15;
      currentColor = colors[colorIndex];
      stateChanged = true;
    } else if (x >= ONOFF_X && x <= (ONOFF_X + BUTTON_W) && y >= ONOFF_Y && y <= (ONOFF_Y + BUTTON_H)) {
      tft.drawRoundRect(ONOFF_X, ONOFF_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_WHITE);
      delay(100);
      tft.drawRoundRect(ONOFF_X, ONOFF_Y, BUTTON_W, BUTTON_H, BUTTON_RADIUS, TFT_ORANGE);
      Serial.println("Processing On/Off button");
      ledsOn = !ledsOn;
      if (ledsOn) {
        brightness = 153;
        colorIndex = 3;
        currentColor = CRGB::White;
        animationMode = 0;
      }
      stateChanged = true;
    }

    if (stateChanged) {
      updateLEDs();
      updateStatus();
      saveSettings();
      if (client.connected()) {
        publishState();
      }
    }
    lastTouch = millis();
  }

  // Update time periodically
  if (ntpInitialized && millis() - lastTimeUpdate >= 30000) {
    updateStatus();
  }

  updateLEDs(); // Continuously update LEDs for animations
}