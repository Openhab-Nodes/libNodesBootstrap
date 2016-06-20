// Example for the esp8266
// -----------------------
// This example shows you how to include the bootstrap functionality
// into your software. You need a platform implementation for your
// hardware (there is one provided for the esp8266 soc with wifi support).
// We use the Arduino/esp8266 sdk combination for this example on a
// NodeMCU v1 design with a button which is used for confirming a bootstrap [optional]
// (short press) and factory resetting (long press) and we use the status led.
// The led can be in one of these states:
// * Off: Bootstrap successful and connected to the destination wifi.
// * Rapid blinking with increasing frequency: Factory reset button is pressed.
//        If flashing stops and you release the button, a factory reset will
//        be performed.
// * Blinking (200ms period): Seaching the bootstrap app.
// * Blinking (500ms period): Waiting for bootstrap data from the app.
// * Blinking (700ms period): Trying to connect to the destination network.

#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "bootstrapWifi.h"

extern "C" {
  #include <stdlib.h>
  #include "ets_sys.h"
  #include "osapi.h"
  #include "gpio.h"
  #include "os_type.h"
  #include "user_interface.h"
}

// The esp8266 platform implementation provides these two methods:
void bst_setup_esp8266(bst_connect_options& o);
void bst_loop_esp8266();

// We need a setup struture for bst.
bst_connect_options o;
static char _chipID[10];

// Button and Led pins
const int buttonPin = 0;
const int ledPin = LED_BUILTIN;

void setup(void)
{
    // Initialize the serial port
    Serial.begin(115200);
    Serial.print("ChipID:");
    sprintf(_chipID, "%06x", ESP.getChipId());
    Serial.println(_chipID);

    // Setup wifi system (max power, no sleep, no auto reconnect, no auto connect)
    WiFi.hostname(_chipID);
    WiFi.persistent(false);
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    wifi_set_sleep_type((sleep_type_t) WIFI_NONE_SLEEP);
    const uint8_t dBm = 20.5;
    system_phy_set_max_tpw((dBm*4.0f));

    // Firmware update via arduinoOTA protocol.
    ArduinoOTA.setHostname(_chipID);
    ArduinoOTA.begin();

    pinMode(buttonPin, INPUT);
    pinMode(ledPin, OUTPUT);

    // Setup the bst library
    o.name = "testname";
    o.unique_device_id = _chipID;
    o.initial_crypto_secret = "app_secret";
    o.initial_crypto_secret_len = sizeof("app_secret")-1;
    o.bootstrap_ssid = "Bootstrap_BST_v1";
    o.bootstrap_key = "bootstrap_key";
    o.timeout_connecting_state_ms = 10000; // 10s
    o.timeout_nonce_ms = 120000; // 2m
    o.need_advanced_connection = false;
    bst_setup_esp8266(o);
}

void handleLEDandButton() {
  unsigned long currentMillis = millis();
  static unsigned long previousMillis = 0;
  static unsigned long previousMillisButton = 0;
  static uint8_t ledState = 0;

  uint8_t btn = !digitalRead(buttonPin);
  bst_state state = bst_get_state();

  if (!previousMillisButton && btn) {
    previousMillisButton = currentMillis;
  } else if (previousMillisButton && !btn) {
    previousMillisButton = currentMillis - previousMillisButton;
    if (previousMillisButton > 3700) {
      if (state == BST_MODE_DESTINATION_CONNECTED) {
        BST_DBG("reset\n");
        bst_factory_reset();
        //ESP.restart();
        return;
      }
    } else if (previousMillisButton > 100) {
      BST_DBG("confirm\n");
      bst_confirm_bootstrap();
    }
    previousMillisButton = 0;
  }

    int interval;
    if (!btn) {
      switch(state) {
        case BST_MODE_CONNECTING_TO_BOOTSTRAP:
          interval = 200;
          break;
        case BST_MODE_WAITING_FOR_DATA:
          interval = 500;
          break;
        case BST_MODE_CONNECTING_TO_DEST:
          interval = 700;
          break;
        case BST_MODE_DESTINATION_CONNECTED:
        default:
          interval = 0;
          digitalWrite(ledPin, 1);
          break;
      }
    } else {
      interval =  200 - ((currentMillis - previousMillisButton) * 200 / 3800);
    }

  if(interval && currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    ledState = ledState == LOW ? HIGH : LOW;
    digitalWrite(ledPin, ledState);
  }
}

void loop() {
  ArduinoOTA.handle();
  handleLEDandButton();
  bst_loop_esp8266();
}

void bst_connect_advanced(const char *data) {
  Serial.print("Openhab:");
  Serial.println(data);
}
