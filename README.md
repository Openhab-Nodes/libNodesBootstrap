# libBootstrapWifi [![Build Status](https://travis-ci.org/Openhab-Nodes/libBootstrapWifi.svg?branch=master)](https://travis-ci.org/Openhab-Nodes/libBootstrapWifi)

If you deploy your program to your embedded devices, like the esp8266, those devices need their first bootstrapping information like the WiFi SSID, WiFi KEY and probably other information.

This library is to be used together with the [android app](https://github.com/Openhab-Nodes/libBootstrapWifiApp).

[Overview](doc/overview.png)

Features:
* Bootstrap device with WiFi network credentials
* Bootstrap any additional data (like a server URL)
* Stores received data permanently and loads those on start (depending on the platform implementation)
* Runtime configurable timings, retry values and encryption key up to 32 Bytes.
* Factory reset method to erase all bootstrap data and start over.
* Reconnect if destination network is lost. Look for known bootstrap app instead if destination is gone for a while.

Integrity/Security features:
* Spritz encrypted traffic (RC4 related, improved stream cypher)
* CRC16 for data packet integrity
* Device will be bound to a bootstrap app on first use to prevent 3rd party app users to reprogram the device. Unbind a device by factory resetting a device.
* Transmission is secured against replay attacks (64-Bit Nonce values).

Code features:
* The internal state machine, all API methods, the encryption, CRC have test cases with 100% of the code covered.
* Simple interface to be implemented for new platforms (supported right now: esp8266)
* Zero overhead callbacks via to-be-implemented methods.
* Simple protocol with only 5 different data packets (HELLO, WIFILIST, BIND, BOOTSTRAP, STATUS)
* No external dependencies.
* Compiled code for the esp8266 for example is only 4KB ROM, 2KB RAM, no dynamic memory usage.

## Minimal example (esp8266)
```
#include <ESP8266WiFi.h>
#include "bootstrapWifi.h"

// The esp8266 platform implementation provides these two methods:
void bst_setup_esp8266(bst_connect_options& o);
void bst_loop_esp8266();

// Global object with the options for the bst library
bst_connect_options options;

void setup() {
  WiFi.hostname(_chipID);
  WiFi.persistent(false);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);

  options.name = "testname";
  options.unique_device_id = getChipID();
  options.initial_crypto_secret = "app_secret";
  options.initial_crypto_secret_len = sizeof("app_secret")-1;
  options.bootstrap_ssid = "Bootstrap_BST_v1";
  options.bootstrap_key = "bootstrap_key";
  options.timeout_connecting_state_ms = 10000; // 10 s
  options.timeout_nonce_ms = 120000;           // 2 min
  options.need_advanced_connection = false;    // no additional server connection required
  bst_setup_esp8266(options);
}

void loop() {
  bst_loop_esp8266();
  if (bst_get_state() == BST_MODE_DESTINATION_CONNECTED) {
    // do stuff only if connected ...
  }
}

void bst_connect_advanced(const char *data) {}
```

For a more complete example for the NodeMCU with button and led support, please
have a look at the **examples/platformio_esp8266sdk** directory.

## Usage
* In your initial setup routine call `bst_setup(options, stored_data, stored_data_len, preshared_secret, preshared_secret_len)` or if available the platform specific method for example `bst_setup_esp8266(options)`.
* Call `bst_periodic()` or if available the platform specific method for example `bst_loop_esp8266()` in your main loop.
* `bst_connect_advanced(data, data_len)`: If you need to bootstrap not only the wifi connection but for example also need to connect to a server, you may set the **need_advanced_connection** option. After a successful wifi connection this method will be called with the additional data the app provided.

### Platform implementation
* Forward UDP traffic from port 8711 to `bst_network_input(data, data_len)`.
* Broadcast outgoing data of `bst_network_output` on udp port 8711.
* If `bst_request_wifi_network_list` is called, prepare a list of all known wifi networks in range and call asynchronously the method `bst_wifi_network_list(network_list_start)`.
* `bst_get_connection_state(): bst_state`: Return your current wifi connection state.
* `bst_connect_to_wifi(ssid, password)`: SSID and password are known, connect now. Return CONNECTING as current state. If the connection failed change the state you return in bst_connection_state() to DISCONNECTED_CREDENTIALS_WRONG or any other disconnected failure state.
* `bst_store_bootstrap_data(data, data_len)`: Store the data blob with the given length. Provide this data to `bst_setup` on boot.
* `bst_store_crypto_secret(data, data_len)`: Store the data blob with the given length. Provide this data to `bst_setup` on boot.

### Options
* `char* name`: Device name. This will be part of the access point name.
* `char* unique_device_id`: Unique device id.
* `char* initial_crypto_secret`. Sets up the initial crypto key, together with initial_crypto_secret_len.
* `char* bootstrap_ssid` and `bootstrap_key`: The bootstrap app ssid and password.
* `int timeout_connecting_state_ms`: If the connection can not be established in the given time in ms, the state changes to DISCONNECTED again.
* `int timeout_nonce_ms`: The time in ms before a valid device nonce will be renewed.
* `bool need_advanced_connection`: If that is set to true, the connection is only seen as established if you return CONNECTED_ADVANCED in bst_connection_state(). This is useful if you need for example a specific server connection.
* `uint8_t external_confirmation_mode`: Either BST_CONFIRM_NOT_REQUIRED or BST_CONFIRM_REQUIRED_FIRST_START or BST_CONFIRM_ALWAYS_REQUIRED. If set to one of the later ones, you need to confirm a bootstrap request with a physical action (e.g. a button press).
* `int retry_connecting_to_bootstrap_network`/`retry_connecting_to_destination_network`: If ssid and password are known but the connection cannot be established or is lost (DISCONNECED_SSID_NOT_FOUND or DISCONNECTED_SSID_LOST), the library will try again by calling `bst_connect_to_wifi` in the given interval in ms. If **need_advanced_connection** is set and the advanced condition is not met (CONNECTED instead of CONNECTED_ADVANCED) the method `bst_connect_advanced` will be called instead.

## How it works:
[State machine](doc/bst_lib_state_diagram.png)

[Input State machine](doc/bst_lib_state_diagram_input.png)

__Connect to the app:__
Your device will try to read already stored bootstrap information on start. It will fail doing this on the first start. It will enter the bootstrap mode and will look for the "**Bootstrap_BST_v1**" SSID. If a connection is established, it will send an unencrypted HELLO packet to the app. The HELLO packet is not necessary and does not contain any relevant information but is just a hint to the app that a device is in bootstrap mode. A UDP socket is created and listens on port 8711.

If the device could connect to the destination network (with already stored bootstrap information), but will loose the connection later on, it will also enter the bootstrap mode.

__Request the list of neighbour wifis:__
The app sends unencrypted DETECT packets via broadcast. It does so periodically but also if it receives a HELLO packet. A DETECT packet contains the app nonce. Together with the known secret key, the device will encrypt its response to the DETECT packet and sends the encrypted WIFILIST packet.

__The app binds devices:__
The app receives a WIFILIST packet and tries to decrypt the packet with the known secret key and additionally with an app specific key. If the generic secret works, it deduces that the device is not bound. The app sends a BIND packet with the app specific key inside. The device stores the new secret and confirms with a WIFILIST packet, already encrypted with the new secret.

__Wifilist/Bootstrap:__
The WIFILIST packet consists of all neighbour wifi networks. The user selects one in the app and enters the wifi password, which is tested and then send to the device via the BOOTSTRAP packet. The device responses with a STATUS packet and calls `bst_setup()` with the new destination wifi network information.

__Advanced connection:__
If advanced information is provided and the option need_advanced_connection is set, `bst_connect_advanced` is called to further bootstrap your device. If all went well, on next boot all necessary information is available again and used directly.

__App session:__
The bind mechanism already make sure that only one app can effectively access a device. The library also prevents rapidly changing app_nonce values. It creates a so called "app session" and only accepts bind and bootstrap commands during this session time. The session timeout is reseted on every incoming packet that origins from the current app. If the app changes its app_nonce value during a session, no further command is accepted and the app has to wait for the old session to timeout. This procedure assures that an app cannot keep a device in bootstrap mode forever without interacting with it.

## License
This code is provided under the MIT license.
