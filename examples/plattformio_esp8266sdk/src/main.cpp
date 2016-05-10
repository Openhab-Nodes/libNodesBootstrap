#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <FS.h>
#include "boostrapWifi.h"

extern "C" {
  #include <stdlib.h>
  #include "ets_sys.h"
  #include "osapi.h"
  #include "gpio.h"
  #include "os_type.h"
  #include "user_interface.h"
}

bst_connect_options o;
WiFiUDP udpIPv4;
IPAddress multiIP = { 239,0,0,57 };
IPAddress broadcastIP = { 255,255,255,255 };
static char _chipID[10];

void setup(void)
{
    Serial.begin(115200);
    Serial.print("ChipID:");
    sprintf(_chipID, "%06x", ESP.getChipId());
    Serial.println(_chipID);

    o.factory_app_secret = "app_secret";
    o.interval_try_again_ms = 500; // every 500ms
    o.name = "testname";
    o.need_advanced_connection = false;
    o.unique_device_id = _chipID;
    o.retry_advanced_connection = 0;
    o.timeout_connecting_state_ms = 200; // 200ms

    if (!SPIFFS.begin())
    {
      Serial.println("Failed to mount file system");
      return;
    }

    File configFile = SPIFFS.open("/bst_setup.txt", "r");

    if (configFile) {
        size_t len = configFile.available();
        Serial.write(len);

        char data[len];
        len = configFile.readBytes(data, len);
        // Replace \n by \0
        for (int i=0;i<len;++i)
          if (data[i]=='\n')
            data[i] = '\0';
        bst_setup(o, data, len);
        configFile.close();
    } else {
        bst_setup(o, NULL, 0);
    }

    WiFi.hostname(_chipID);
    ArduinoOTA.setHostname(_chipID);
    ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();

  int cb;

  cb = udpIPv4.parsePacket();
  if (cb) {
    char packetBuffer[cb];
    cb = udpIPv4.read(packetBuffer, cb);
    BST_DBG("net: loop in %d %d\n", cb, packetBuffer[10]);
    bst_network_input(packetBuffer, cb);
  }

  bst_periodic();
}

extern "C" {

void bst_printf(const char * format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

time_t bst_get_system_time_ms() {
  return system_get_time()/1000;
}

void bst_network_output(const char *data, size_t data_len) {
  printf("out %d=%s ", data_len, data);
  Serial.println(multiIP);

  udpIPv4.beginPacketMulticast(multiIP, 8711, WiFi.softAPIP());
  udpIPv4.write(data, data_len);
  udpIPv4.endPacket();

  udpIPv4.beginPacket(broadcastIP, 8711);
  udpIPv4.write(data, data_len);
  udpIPv4.endPacket();
}

bst_connect_state bst_get_connection_state() {
  switch(WiFi.status()) {
      case WL_CONNECTED:
        return BST_CONNECTED;

      case WL_CONNECTION_LOST:
      case WL_NO_SSID_AVAIL:
        return BST_FAILED_SSID_NOT_FOUND;

      case WL_CONNECT_FAILED:
        return BST_FAILED_CREDENTIALS_WRONG;

      case WL_IDLE_STATUS:
      case WL_DISCONNECTED:
      default:
          return BST_DISCOVER_MODE;
  };
}

void bst_connect_to_wifi(const char *ssid, const char *pwd) {
    udpIPv4.stop();
    WiFi.begin(ssid, pwd);
    WiFi.enableSTA(true);
    Serial.print("bst_connect_to_wifi:");
    Serial.println(ssid);
}

void bst_connect_advanced(const char *data) {
  Serial.print("Openhab:");
  Serial.println(data);
}

void bst_discover_mode(const char *ap_ssid, const char *ap_pwd) {
    WiFi.enableAP(false);
    WiFi.softAP(ap_ssid, ap_pwd);
    WiFi.enableAP(true);
    Serial.print("bst_discover_mode: ");
    Serial.print(ap_ssid);
    Serial.print(" pwd: ");
    Serial.println(ap_pwd);

    if (!udpIPv4.beginMulticast(WiFi.softAPIP(), multiIP, 8711))
      Serial.println("udpIPV4 bind failed!");
}

void prv_scanDone(void* result, STATUS status) {
    if(status != OK) {
        return;
    }

    int len = 0;
    bss_info* head = reinterpret_cast<bss_info*>(result);

    for(bss_info* it = head; it; it = STAILQ_NEXT(it, next), ++len) ;

    if(len == 0) {
        return;
    }

    bst_wifi_list_entry entries[len];
    memset(entries,0, sizeof(bst_wifi_list_entry)*len);
    bss_info* it = head;
    for (int i = 0; i < len; ++i) {
        if (i < len - 1)
          entries[i].next = &(entries[i+1]);
        entries[i].ssid = (char*)it->ssid;
        int dBm = 0;
        int quality;
        if(dBm <= -100)
            quality = 0;
        else if(dBm >= -50)
            quality = 100;
        else
            quality = 2 * (dBm + 100);
        entries[i].strength_percent = quality;
        switch(it->authmode) {
            case AUTH_OPEN:
                entries[i].encryption_mode = 0;
            case AUTH_WEP:
                entries[i].encryption_mode = 1;
            case AUTH_WPA_PSK:
                entries[i].encryption_mode = 2;
            case AUTH_WPA2_PSK:
                entries[i].encryption_mode = 2;
            case AUTH_WPA_WPA2_PSK:
                entries[i].encryption_mode = 2;
            default:
                entries[i].encryption_mode = 255;
        }

        it = STAILQ_NEXT(it, next);
    }

    bst_wifi_network_list(entries);
}

void bst_request_wifi_network_list() {
  struct scan_config config;
    config.ssid = 0;
    config.bssid = 0;
    config.channel = 0;
    config.show_hidden = false;
    wifi_station_scan(&config, prv_scanDone);
}

void bst_store_data(char *data, size_t data_len) {
    printf("bst_store_data\n");
    // Open config file for writing.
    File configFile = SPIFFS.open("/bst_setup.txt", "w");
    if (!configFile)
    {
      Serial.println("Failed to write bootstrap data");
      return;
    }

    // Replace \0 by \n
    for (int i=0;i<data_len;++i)
      if (data[i]=='\0')
        data[i] = '\n';

    configFile.write((uint8_t *)data, data_len);
    configFile.close();
}
}
