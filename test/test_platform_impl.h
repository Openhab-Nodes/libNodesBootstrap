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

#include <iostream>
#include <exception>
#include <chrono>

#include "bootstrapWifi.h"

#include "prv_bootstrapWifi.h"

class bst_platform {
protected:
    time_t overwrite_time = 0;
public: 
    static bst_platform* instance;

    /**
     * @brief Checks a send packet for its correct header.
     * @param pkt The send packet.
     * @return
     */
    static bool check_send_header_and_decrypt(bst_udp_send_pkt_t* pkt);

    /**
     * @brief Return a bst_connect_options object with some
     * default options for test cases.
     * @return
     */
    static bst_connect_options default_options();

    /**
     * @brief Add the BST_NETWORK_HEADER header and device nonce to the given packet.
     * You have to call prv_add_checksum_and_encrypt() after adding the content to the packet.
     * @param pkt The packet.
     */
    static void add_header_to_receive_pkt(bst_udp_receive_pkt_t* pkt, prv_bst_cmd cmd);

    static void add_checksum_to_receive_pkt(bst_udp_receive_pkt_t* pkt, size_t pkt_len);

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

    /**
     * You are called back by this method once, if a connection
     * to the bootstrap wifi network could be established.
     */
    virtual void bst_connected_to_bootstrap_network() = 0;

    // The app requests a list of wifi networks in range. Call bst_wifi_network_list(network_list_start) asynchronously if you gathered that data.
    virtual void bst_request_wifi_network_list() = 0;

    // Store the data blob with the given length. Provide this data to `bst_setup` on boot.
    virtual void bst_store_bootstrap_data(char* bst_data, size_t bst_data_len) = 0;

    // Store the data blob with the given length. Provide this data to `bst_setup` on boot.
    virtual void bst_store_crypto_secret(char* bound_key, size_t bound_key_len) = 0;

    /**
     * @brief bst_get_system_time_ms
     * @return Return either the unix time in ms or a spefic value if useCurrentTimeOverwrite()
     * and addTimeMsOverwrite() are used.
     */
    virtual time_t bst_get_system_time_ms() {
        if (overwrite_time)
            return overwrite_time;

        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        return std::chrono::system_clock::to_time_t(now_ms);
    }

    /**
     * @brief bst_get_random
     * @return Return a random 64bit value. This implementation just returns the current
     * unix time in ms.
     */
    virtual uint64_t bst_get_random()
    {
        return        (uint64_t)'a' | ((uint64_t)'b'<<8) |
                ((uint64_t)'c'<<16) | ((uint64_t)'d'<<24) |
                ((uint64_t)'e'<<32) | ((uint64_t)'f'<<40) |
                ((uint64_t)'g'<<48) | ((uint64_t)'h'<<56);
    }

    void useCurrentTimeOverwrite() {
        auto now = std::chrono::system_clock::now();
        auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
        overwrite_time = std::chrono::system_clock::to_time_t(now_ms);

        if (overwrite_time<0)
            throw new std::exception();
    }

    void resetTimeOverwrite() {
        overwrite_time = 0;
    }
    void addTimeMsOverwrite(time_t offset) {
        overwrite_time += offset;
    }

    template<class T>
    static void print_out_java_array(const char* var_name, T* data, size_t data_len) {
        printf("byte %s[] = {", var_name);
        for (unsigned i=0;i<data_len;++i) {
            char v = (unsigned char)*(data+i);
            if (v == '\\')
                printf("'\\\\',");
            else if (v>=32 && v<=126) {
                printf("'%c',", (char)v);
            } else
                printf("%d,", v);
        }
        printf("};\n");
        fflush(stdout);
    }
};

