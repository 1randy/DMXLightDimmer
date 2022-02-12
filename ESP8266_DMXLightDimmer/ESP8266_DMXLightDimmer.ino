// - - - - -
// DMXLightDimmer
// A ESP8266 based 4 Channel DMX dimmer with countdown function for DimmerPacks
// https://github.com/1randy/DMXLightDimmer
//
// based on:
// ESPDMX: A Arduino library for sending and receiving DMX using the builtin serial hardware port.
// https://github.com/Rickgg/ESP-Dmx
//
// PCF8574: Arduino library for PCF8574 - 8 channel I2C IO expander
// https://github.com/RobTillaart/PCF8574
// 
// LiquidCrystal_PCF8574: Arduino Library for LiquidCrystal displays with I2C PCF8574 adapter
// https://github.com/mathertel/LiquidCrystal_PCF8574
//
// - - - - -

// last update: 20220211


#include <DNSServer.h>
#include <ESPUI.h>
#include <ESP8266WiFi.h>
#include <ESPDMX.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <PCF8574.h>

// --------------------------------------------
// configuration

#define NUM_LIGHTS 4
#define NUM_KEYS 4

// set if you use filament LEDs and they are not really off when dimmer is off
//#define LED_FIX true

// DMX first channel addess
#define DMX_START 1
// how fast should on/off fade
#define DIM_STEP 2
// inital brigthness
#define DIM_BRIGTH_INIT 0

#define I2C_LCD 0x27
#define I2C_LED 0x26

// LED output pins
#define LedOut0 4
#define LedOut1 5
#define LedOut2 6
#define LedOut3 7

// button input pins
#define KeyIn0 3
#define KeyIn1 2
#define KeyIn2 1
#define KeyIn3 0

// WLAN Settings
const char *ssid = "DMXDimmer";
const char *password = "changeme!";
const char *hostname = "esp8266";
// default ip address if hostAP mode
IPAddress apIP(192, 168, 1, 1);

// preset dim time per button 1-4
unsigned long DIM_TIME[4] = {
  7200,  // 2 hours
  14400, // 4 hours
  21600, // 6 hours
  28800  // 8 hours
};



//--------------------------------------------


const byte DNS_PORT = 53;
DNSServer dnsServer;
LiquidCrystal_PCF8574 lcd(I2C_LCD);
PCF8574 led(I2C_LED);
DMXESPSerial dmx;

unsigned long oldTime = 0;
unsigned long idleTime = 0;
unsigned long secondsMillis = 0;
unsigned long halfsecMillis = 0;
unsigned long previousMillis = 0;
bool led_state[4] = { LOW, LOW, LOW, LOW };

unsigned long keyPrevMillis = 0;
const unsigned long keySampleIntervalMs = 50;
byte longKeyPressCountMax = 10;
byte KeyPressCount[4] = { 0, 0, 0, 0 };

byte currKeyState[4] = { LOW, LOW, LOW, LOW };
byte prevKeyState[4] = { HIGH, HIGH, HIGH, HIGH }; // buttons are active low

byte dim_running = LOW;
byte isIdle = LOW;

unsigned long  dim_time = 0;
unsigned long  dim_time_left = 0;

byte dim_brigthness = DIM_BRIGTH_INIT; // 0-255 brigthness 
byte light_state[4]= { LOW, LOW, LOW, LOW };

/// espui
uint16_t status;
uint16_t switchDimmer;
uint16_t switchOne;
uint16_t switchTwo;
uint16_t switchThree;
uint16_t switchFour;

// called when button is kept pressed for less than .5 seconds
void shortKeyPress(int chan) {
  Serial.print("short ");
  Serial.println(chan);
  
  if (light_state[chan]) {
    dim_down(chan);
    light_state[chan] = LOW;
  } else {
    dim_up(chan);
    light_state[chan] = HIGH;
  }
}

// called when button is kept pressed for 2 seconds or more
void longKeyPress(int chan) {
  Serial.print("long ");
  Serial.println(chan);
  if (dim_running) {
    dim_stop();
  } else {
    dim_time = DIM_TIME[chan];
    dim_start();
  }
}

// called when key goes from not pressed to pressed
void keyPress(int key) {
  Serial.print("key press ");
  Serial.println(key);
  KeyPressCount[key] = 0;
}

// called when key goes from pressed to not pressed
void keyRelease(int key) {
  Serial.print("key release ");
  Serial.println(key);
  if (KeyPressCount[key] < longKeyPressCountMax) {
    shortKeyPress(key);
  } else {
    longKeyPress(key);
  }
}

void dim_down(int chan) {
  Serial.print("DIM DOWN ");
  Serial.print(chan + 1);  
  Serial.print(" ...");  
  for (int i=255; i>dim_brigthness; i = i - DIM_STEP) {
    dmx.write(chan + DMX_START, i);
    dmx.update();
  }
  dmx.write(chan + DMX_START, dim_brigthness); 
  led_state[chan] = LOW;
  lcd_chan_state(chan, LOW);
  led_write();
  Serial.println("done");
}

void dim_down_dim(int chan) {
  Serial.print("DIM DOWN (dim) ");
  Serial.print(chan + 1);  
  Serial.print(" ...");
  for (int i=dim_brigthness; i>0; i = i - DIM_STEP) {
    dmx.write(chan + DMX_START, i); 
    dmx.update();
  }
#ifdef LED_FIX
  dmx.write(chan + DMX_START, 1);  // hopefully turn LED off
#else
  dmx.write(chan + DMX_START, 0);
#endif
  led_state[chan] = LOW;
  lcd_chan_state(chan, LOW);
  led_write();
  Serial.println("done");
}

void dim_up(int chan) {
  led_state[chan] = HIGH;
  led_write();
  Serial.print("DIM UP ");
  Serial.print(chan + 1);  
  Serial.print(" ...");
  for (int i=dim_brigthness; i<255; i = i + DIM_STEP) {
    dmx.write(chan + DMX_START, i);
    dmx.update();
  }
  dmx.write(chan + DMX_START, 255); 
  lcd_chan_state(chan, HIGH);
  Serial.println("done");
}

void lcd_chan_state(int chan, bool state) {
  lcd.setCursor(0, 0);
  if (state) {
    //         0123456789012345
    lcd.print("  Kanal x  AN   ");
  } else {
    lcd.print("  Kanal x AUS   ");
  }
  idleTime = millis();
  lcd.setCursor(8, 0);
  lcd.print(chan + 1);    
  lcd.setCursor(0, 1);
  lcd.print("                ");
  Serial.print("kanal an ");
  Serial.println(chan + 1);
}

void key_query() {
  byte currentKeyStates = led.readButton8();
//  Serial.println(currentKeyStates, BIN);
  currKeyState[0] = bitRead(currentKeyStates, KeyIn0);
  currKeyState[1] = bitRead(currentKeyStates, KeyIn1);
  currKeyState[2] = bitRead(currentKeyStates, KeyIn2);
  currKeyState[3] = bitRead(currentKeyStates, KeyIn3);

  for (int i = 0; i < NUM_KEYS; i++) {
    if ((prevKeyState[i] == HIGH) && (currKeyState[i] == LOW)) {
        keyPress(i);
    }
    else if ((prevKeyState[i] == LOW) && (currKeyState[i] == HIGH)) {
        keyRelease(i);
    }
    else if (currKeyState[i] == LOW) {
        KeyPressCount[i]++;
    }
    prevKeyState[i] = currKeyState[i];
  }  
}

void led_write() {
  uint8_t led_buf;
  uint8_t keys = led.read8() | 0xF0;

  led_buf = ((led_state[0] ^ 0x01) << LedOut0) | ((led_state[1] ^ 0x01) << LedOut1) | ((led_state[2] ^ 0x01) << LedOut2) | ((led_state[3] ^ 0x01) << LedOut3) ^ 0x0F;
  Serial.print("led_write()  buf ");
  Serial.println(led_buf, BIN);
  
  Serial.print("led_write() keys ");
  Serial.println(keys, BIN);
  
  led_buf = keys & led_buf;
  Serial.print("led_write()  new ");
  Serial.println(led_buf, BIN);
  led.write8(led_buf);
}

void led_toggle() {
  for (int i; i < NUM_LIGHTS; i++) {
    if(!light_state[i]) {
      led_state[i] = led_state[i] ^ 0x01;
    }    
  }
  led_write();
}

void dim_start() {
  for (int chan=0; chan<NUM_LIGHTS; chan++) {
    if(!light_state[chan]) {
      dim_up(chan);
    }
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  //         0123456789012345
  lcd.print("   Dimmer  AN   ");
  dim_running = HIGH;
  dim_time_left = dim_time;
  idleTime = millis();
}

void dim_stop() {
  ESPUI.updateControlValue(status, "Dimmer Stopped");
  for (int chan=0; chan<NUM_LIGHTS; chan++) {
    if(!light_state[chan]) {
      dim_down_dim(chan);
    }
  }
  lcd.clear();
  lcd.setCursor(0, 0);
  //         0123456789012345
  lcd.print("   Dimmer AUS   ");
  idleTime = millis();
  dim_running = LOW;
  dim_time_left = 0;
  dim_brigthness = DIM_BRIGTH_INIT;
}


void dim_lights() {
  Serial.print("DIM running ");
  Serial.println(dim_time_left);
  dim_time_left = dim_time_left - 1;
  if (dim_time_left <= 0) {
    dim_stop();
    Serial.println("DIM stopped ");
    idleTime = millis();
  } else {
    // calc dim stufe
    dim_brigthness = map(dim_time_left, 0, dim_time, 0, 255);
    DisplayTime(dim_time_left);
    for (int chan=0; chan<NUM_LIGHTS; chan++) {
      if(!light_state[chan]) {
        dmx.write(chan + DMX_START, dim_brigthness);
      }
    }
  }
}

void DisplayTime( long unsigned int secs ) {
  uint32_t h = secs / 3600;
  uint32_t rem = secs % 3600;
  uint32_t m = rem / 60;
  uint32_t s = rem % 60;
  char timeString[9] = "        ";

  snprintf(timeString, 9, "%02d:%02d:%02d", h, m, s);

  lcd.setCursor(4, 1);
  lcd.print(timeString);
  ESPUI.updateControlValue(status, timeString);
}

void switchExample(Control *sender, int value) {
  int chan;
  chan = sender->id - 3;
  Serial.print("Sender ID ");
  Serial.print(sender->id);
  switch (value) {
  case S_ACTIVE:
    Serial.println(" Active");
    if (sender->id == 2) {
      dim_start();
    } else {
      dim_up(chan);
      light_state[chan] = HIGH;
    }
    break;

  case S_INACTIVE:
    Serial.println(" Inactive");
    if (sender->id == 2) {
      dim_stop();
    } else {
      light_state[chan] = LOW;
      dim_down(chan);
    }
    break;
  }

}

// --------------------


void setup() {
  int error;
  
  Serial.begin(115200);
  Serial.println("");
  
  dmx.init();               // initialization for first 32 addresses by default
  //dmx.init(512)           // initialization for complete bus

  for (int chan=0; chan<NUM_LIGHTS; chan++) {
#ifdef LED_FIX
    dmx.write(chan + DMX_START, 1);
#else
    dmx.write(chan + DMX_START, 0);
#endif
    dmx.update();
  }
  
  delay(200);
  Wire.begin();  
  delay(1000);
  
  Wire.beginTransmission(I2C_LCD); // first PCF8574 (LCD)
  error = Wire.endTransmission();

  if (error == 0) {
    Serial.println("LCD found.");
    lcd.begin(16, 2); // initialize the lcd

    lcd.setBacklight(255);
    lcd.display();
    lcd.home();
    lcd.clear();
   
    lcd.setCursor(0, 0);
    lcd.print("VogelLichtDimmer");   

  } else {
    Serial.print("Error: ");
    Serial.print(error);
    Serial.println(": LCD NOT found.");
  }

  delay(1000);
  
  Wire.beginTransmission(I2C_LED); // second PCF8574 (Keys/LEDS)
  error = Wire.endTransmission();

  if (error == 0) {
    Serial.println("LED found.");
    led.begin();

    for (int i=0; i<NUM_LIGHTS; i++) {
      Serial.print("LED test ");
      Serial.println(i);
      led_state[i] = HIGH;
      led_write();
      delay(500);
      led_state[i] = LOW;
      led_write();
    }
    Serial.println("LED test done");
  } else {
    Serial.print("Error: ");
    Serial.print(error);
    Serial.println(": LED NOT found.");
  }

  // ESPUI.setVerbosity(Verbosity::VerboseJSON);
  WiFi.hostname(hostname);
    
  // try to connect to existing network
  WiFi.begin(ssid, password);
  Serial.print("\n\nTry to connect to existing network");

  {
    uint8_t timeout = 10;

    // Wait for connection, 5s timeout
    do {
      delay(500);
      Serial.print(".");
      timeout--;
    } while (timeout && WiFi.status() != WL_CONNECTED);

    // not connected -> create hotspot
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("\n\nCreating hotspot");

      WiFi.mode(WIFI_AP);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(ssid, password);

      timeout = 5;

      do {
        delay(500);
        Serial.print(".");
        timeout--;
      } while (timeout);
    }
  }

  dnsServer.start(DNS_PORT, "*", apIP);

  Serial.println("\n\nWiFi parameters:");
  Serial.print("Mode: ");
  Serial.println(WiFi.getMode() == WIFI_AP ? "Station" : "Client");
  Serial.print("IP address: ");
  Serial.println(WiFi.getMode() == WIFI_AP ? WiFi.softAPIP() : WiFi.localIP());

  status = ESPUI.addControl(ControlType::Label, "Status:", "Stop", ControlColor::Turquoise);
  switchDimmer = ESPUI.addControl(ControlType::Switcher, "Dimmer", "", ControlColor::Wetasphalt, Control::noParent, &switchExample);
  switchOne = ESPUI.addControl(ControlType::Switcher, "Channel 1", "", ControlColor::Alizarin, Control::noParent, &switchExample);
  switchTwo = ESPUI.addControl(ControlType::Switcher, "Channel 2", "", ControlColor::Peterriver, Control::noParent, &switchExample);
  switchThree = ESPUI.addControl(ControlType::Switcher, "Channel 3", "", ControlColor::Emerald, Control::noParent, &switchExample);
  switchFour = ESPUI.addControl(ControlType::Switcher, "Channel 4", "", ControlColor::Sunflower, Control::noParent, &switchExample);
  ESPUI.begin("ESPUI Control");
  Serial.println("starting app");
  lcd.clear();
  lcd.setCursor(0, 0);
  //         0123456789012345
  lcd.print("   Dimmer AUS   ");
}



void loop() {
  unsigned int currentMillis = millis();

  dnsServer.processNextRequest();

  // "screen saver"
  if (currentMillis - idleTime > 60000) {
    idleTime = currentMillis;
    if (!dim_running) {
      lcd.setCursor(0, 0);
      lcd.print("   Servus       ");
      lcd.setCursor(0, 1);
      lcd.print("    Schorsch!   ");
    }
  }  

  // update webpage controls
  if (currentMillis - oldTime > 2500) {
    oldTime = currentMillis;
    ESPUI.updateControlValue(switchDimmer, dim_running ? "1" : "0");
    ESPUI.updateControlValue(switchOne, light_state[0] ? "1" : "0");
    ESPUI.updateControlValue(switchTwo, light_state[1] ? "1" : "0");
    ESPUI.updateControlValue(switchThree, light_state[2] ? "1" : "0");
    ESPUI.updateControlValue(switchFour, light_state[3] ? "1" : "0");
  }  

  // key polling
  if (currentMillis - keyPrevMillis >= keySampleIntervalMs) {
      keyPrevMillis = currentMillis;
      key_query();
  }

  // LED blinking if dimmer running
  if(currentMillis - halfsecMillis >= 500){
    halfsecMillis = currentMillis;
    if (dim_running) {
      led_toggle();
    }
  }

  // DMS update loop
  if(currentMillis - secondsMillis >= 1000){
    secondsMillis = currentMillis;
    dmx.update();

    if (dim_running) {
      dim_lights();
    }

    if (WiFi.softAPgetStationNum() > 0) {
      lcd.setCursor(15, 0);
      lcd.print("#");
    }
  }
}
