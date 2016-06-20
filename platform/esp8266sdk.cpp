#ifdef ESP8266

#include "../src/bootstrapWifi.h"
#include <string.h>
#include <stdarg.h>

extern "C" {
  #include <stdlib.h>
  #include "ets_sys.h"
  #include "osapi.h"
  #include "gpio.h"
  #include "os_type.h"
  #include "user_interface.h"
}

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FS.h>

WiFiUDP udpIPv4;
IPAddress multiIP = { 239,0,0,57 };
IPAddress broadcastIP = { 255,255,255,255 };


void bst_printf(const char * format, ...)
{
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
}

uint64_t bst_get_random() {
    return rand();
}

time_t bst_get_system_time_ms() {
  return system_get_time()/1000;
}

void bst_connected_to_bootstrap_network()
{
  if (udpIPv4.begin(8711)) {
    BST_DBG("udp start\n");
  }
  //if (!udpIPv4.beginMulticast(WiFi.softAPIP(), multiIP, 8711))
  //  Serial.println("udpIPV4 bind failed!");
  broadcastIP = ~WiFi.subnetMask() | WiFi.localIP();
}

void bst_network_output(const char *data, size_t data_len) {
  udpIPv4.beginPacket(broadcastIP, 8711);
  udpIPv4.write(data, data_len);
  udpIPv4.endPacket();
}

long last_rssi_time = 0;

bst_connect_state bst_get_connection_state() {
  switch(wifi_station_get_connect_status()) {
      case STATION_GOT_IP:
        // Workaround: esp8266 sdk does not report if the connection to a network is lost.
        // Measure the RSSI periodically therefore.
        if (last_rssi_time + 5000 < bst_get_system_time_ms()) {
          last_rssi_time = bst_get_system_time_ms();
          if (wifi_station_get_rssi()>10) {
            WiFi.disconnect();
            BST_DBG("force disconnect");
          }
        }
        return BST_STATE_CONNECTED;
      case STATION_CONNECT_FAIL:
      case STATION_NO_AP_FOUND:
        return BST_STATE_FAILED_SSID_NOT_FOUND;

      case STATION_WRONG_PASSWORD:
        WiFi.disconnect();
        return BST_STATE_FAILED_CREDENTIALS_WRONG;

      case STATION_IDLE:
      default:
          return BST_STATE_NO_CONNECTION;
  };
}


void bst_connect_to_wifi(const char* ssid, const char* passphrase) {
  if (bst_get_state() == BST_MODE_CONNECTING_TO_DEST) {
    udpIPv4.stop();
  }

  static int static_counter = 0;
  BST_DBG("connect %s %s (%d)\n", ssid, passphrase, static_counter);

  struct station_config conf;
  strcpy(reinterpret_cast<char*>(conf.ssid), ssid);
  strcpy(reinterpret_cast<char*>(conf.password), passphrase);
  conf.bssid_set = 0;
  ETS_UART_INTR_DISABLE();
  wifi_set_opmode_current(WIFI_OFF);
  ETS_UART_INTR_ENABLE();
  delay(100);

  ETS_UART_INTR_DISABLE();
  wifi_set_opmode_current(WIFI_STA);
  wifi_station_set_config_current(&conf);
  wifi_station_connect();
  ETS_UART_INTR_ENABLE();
  wifi_station_dhcpc_start();

  Serial.println(wifi_station_get_connect_status());
}

void bst_loop_esp8266() {
    int cb = udpIPv4.parsePacket();
    if (cb) {
      char packetBuffer[cb];
      cb = udpIPv4.read(packetBuffer, cb);
      BST_DBG("net: loop in %d %d\n", cb, packetBuffer[10]);
      bst_network_input(packetBuffer, cb);
    }
    bst_periodic();
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
        int dBm = it->rssi;
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
                break;
            case AUTH_WEP:
                entries[i].encryption_mode = 1;
                break;
            case AUTH_WPA_PSK:
                entries[i].encryption_mode = 2;
                break;
            case AUTH_WPA2_PSK:
                entries[i].encryption_mode = 2;
                break;
            case AUTH_WPA_WPA2_PSK:
                entries[i].encryption_mode = 2;
                break;
            default:
                entries[i].encryption_mode = 255;
                break;
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

void bst_setup_esp8266(bst_connect_options& o)
{
      if (!SPIFFS.begin())
      {
        BST_DBG("Failed to mount file system\n");
        return;
      }

      File configFile = SPIFFS.open("/bst_data.txt", "r");
      size_t bst_data_len = configFile ? configFile.available() : 0;
      char bst_data[bst_data_len];
      if (configFile) {
        bst_data_len = configFile.readBytes(bst_data, bst_data_len);
        configFile.close();
      }

      configFile = SPIFFS.open("/bst_crypto.txt", "r");
      size_t bst_crypto_len = configFile ? configFile.available() : 0;
      char bst_crypto[bst_crypto_len];
      if (configFile) {
        bst_crypto_len = configFile.readBytes(bst_crypto, bst_crypto_len);
        configFile.close();
      }

      bst_setup(o, bst_data, bst_data_len, bst_crypto, bst_crypto_len);
}

void bst_store_bootstrap_data(char* bst_data, size_t bst_data_len) {
    File configFile = SPIFFS.open("/bst_data.txt", "w");
    if (!configFile)
    {
      BST_DBG("Failed to write bootstrap data\n");
      return;
    }

    BST_DBG("bst_store_bootstrap_data\n");
    configFile.write((uint8_t *)bst_data, bst_data_len);
    configFile.close();
}

void bst_store_crypto_secret(char* secret, size_t secret_len) {
    File configFile = SPIFFS.open("/bst_crypto.txt", "w");
    if (!configFile)
    {
      BST_DBG("Failed to write bootstrap data\n");
      return;
    }

    BST_DBG("bst_store_crypto_secret\n");
    configFile.write((uint8_t *)secret, secret_len);
    configFile.close();
}

#endif
