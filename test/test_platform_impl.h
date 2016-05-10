/*******************************************************************************
 * Copyright (c) 2016  MSc. David Graeff <david.graeff@web.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include <stdint.h>
#include <time.h>

#include <chrono>

#include "boostrapWifi.h"

#include "prv_boostrapWifi.h"

class bst_platform {
protected:
    time_t overwrite_time = 0;
public: 
    static bst_platform* instance;

    bool check_send_header(bst_udp_send_pkt_t* pkt);

    bool check_receive_header(bst_udp_receive_pkt_t* pkt);

    // Outgoing network traffic for udp port 8711 to be broadcasted
    virtual void bst_network_output(const char* data, size_t data_len) = 0;

    // Return your current wifi connection state.
    virtual bst_connect_state bst_get_connection_state() = 0;

    // Close the udp socket on port 8711.
    // SSID and password are known, connect now. Return CONNECTING in bst_get_connection_state().
    // If the connection failed change the state you return in bst_connection_state() to
    // FAILED_CREDENTIALS_WRONG or FAILED_SSID_NOT_FOUND state.
    virtual void bst_connect_to_wifi(const char* ssid, const char* pwd) = 0;

    // If you need to bootstrap not only the wifi connection but for example also
    // need to connect to a server, you may set the **need_advanced_connection** option.
    // After a successful wifi connection this method will be called with the additional data the app provided.
    virtual void bst_connect_advanced(const char* data) = 0;

    // Switch to AP mode with the given access point ssid and password. Open a udp socket on port 8711.
    virtual void bst_discover_mode(const char* ap_ssid, const char* ap_pwd) = 0;

    // The app requests a list of wifi networks in range. Call bst_wifi_network_list(network_list_start) asynchronously if you gathered that data.
    virtual void bst_request_wifi_network_list() = 0;

    // Store the data blob with the given length. Provide this data to `bst_setup` on boot.
    virtual void bst_store_data(char* data, size_t data_len) = 0;

    virtual time_t bst_get_system_time_ms() {
        if (overwrite_time)
            return overwrite_time;

        auto duration = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    void useCurrentTimeOverwrite() {
        auto duration = std::chrono::system_clock::now().time_since_epoch();
        overwrite_time = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    void resetTimeOverwrite() {
        overwrite_time = 0;
    }
    void addTimeMsOverwrite(time_t offset) {
        overwrite_time += offset;
    }
};

