# libBootstrapWifi [![Build Status](https://travis-ci.org/Openhab-Nodes/libBootstrapWifi.svg?branch=master)](https://travis-ci.org/Openhab-Nodes/libBootstrapWifi)

If you program and run your devices initially, they need their first bootstraping information
like the WiFi SSID, the WiFi KEY and probably other information.

This library is to be used together with the [android app](https://github.com/Openhab-Nodes/libBootstrapWifiApp).

__How it works:__ 
[State machine](doc/states.png)
Your device will open an access point with a name like "**BST-somename-AFCDB9**" in the **Discover mode** state
(after boot and if no bootstrap information is available so far). The app connects to such a ssid and if that works, it will request a list of wifi networks in range from the device. The app user enters the credentials and further bootstrap data is gathered and transmitted eventually. The library interprets the data and calls you back with `bst_connect_to_wifi`. If advanced information is provided, `bst_connect_advanced` is called to further bootstrap your device. If all went well, on next boot all necessary information is available.

## Usage
* In your initial setup call `bst_setup(options, stored_data, stored_data_len, preshared_secret)`.
* Call `bst_periodic()` in your main loop.
* Forward udp traffic from port 8711 to `bst_network_input(data, data_len)`.
* Broadcast outgoing data of `bst_network_output` on udp port 8711.
* If `bst_request_wifi_network_list` is called, prepare a list of all known wifi networks in range and call asynchronously the method `bst_wifi_network_list(network_list_start)`.

You have to implement the following methods:
* `bst_network_output(data, data_len)`: Outgoing network traffic for udp port 8711 to be broadcasted.
* `bst_get_connection_state(): bst_state`: Return your current wifi connection state.
* `bst_connect_to_wifi(ssid, password)`: SSID and password are known, connect now. Return CONNECTING as current state. If the connection failed change the state you return in bst_connection_state() to DISCONNECTED_CREDENTIALS_WRONG or any other disconnected failure state.
* `bst_connect_advanced(data, data_len)`: If you need to bootstrap not only the wifi connection but for example also need to connect to a server, you may set the **need_advanced_connection** option. After a successful wifi connection this method will be called with the additional data the app provided.
* `bst_discover_mode(ssid, password)`: Switch to AP mode with the given ssid as name and wpa2 encrypted with the given password credential. The app will look for ssids that follow a specific pattern so the exact name is required.
* `bst_request_wifi_network_list()`: The app requests a list of wifi networks in range. Call bst_wifi_network_list(network_list_start) asynchronously if you gathered that data.
* `bst_store_data(data, data_len)`: Store the data blob with the given length. Provide this data to `bst_setup` on boot.
Options:
* char* name: Device name. This will be part of the access point name.
* int timeout_connecting_state_ms: If the connection can not be established in the given time in ms, the state changes to DISCONNECTED again.
* bool need_advanced_connection: If that is set to true, the connection is only seen as established if you return CONNECTED_ADVANCED in bst_connection_state(). This is useful if you need for example a specific server connection.
* int interval_try_again_ms: If ssid and password are known but the connection cannot be established or is lost (DISCONNECED_SSID_NOT_FOUND or DISCONNECTED_SSID_LOST), the library will try again by calling `bst_connect_to_wifi` in the given interval in ms. If **need_advanced_connection** is set and the advanced condition is not met (CONNECTED instead of CONNECTED_ADVANCED) the method `bst_connect_advanced` will be called instead.

## License
This code is provided under the MIT license.
