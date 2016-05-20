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
#include <string.h>

#include "test_platform_impl.h"

bst_platform* bst_platform::instance = nullptr;

bool bst_platform::check_send_header(bst_udp_send_pkt_t* pkt)
{
    const char hdr[] = "BSTwifi1";
    return (memcmp(pkt->hdr, hdr, 8) == 0);
}

bst_connect_options bst_platform::default_options() {
    bst_connect_options o;
    o.initial_crypto_secret = "app_secret";
    o.initial_crypto_secret_len = sizeof("app_secret");
    o.name = "testname";
    o.need_advanced_connection = false;
    o.unique_device_id = "ABCDEF";
    o.retry_connecting_to_destination_network = 0;
    o.retry_connecting_to_bootstrap_network = 0;
    o.timeout_connecting_state_ms = 10000;
    o.bootstrap_ssid = "bootstrap_ssid";
    o.bootstrap_key = "bootstrap_key";
    return o;
}

extern "C" {

void bst_network_output(const char* data, size_t data_len)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_network_output(data, data_len);
}

bst_connect_state bst_get_connection_state()
{
    if (bst_platform::instance)
        return bst_platform::instance->bst_get_connection_state();
    return BST_STATE_NO_CONNECTION;
}

void bst_connect_to_wifi(const char* ssid, const char* pwd, bst_state state)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_connect_to_wifi(ssid, pwd, state);
}

void bst_connect_advanced(const char* data)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_connect_advanced(data);
}

void bst_request_wifi_network_list()
{
    if (bst_platform::instance)
        bst_platform::instance->bst_request_wifi_network_list();
}

void bst_store_bootstrap_data(char* bst_data, size_t bst_data_len)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_store_bootstrap_data(bst_data, bst_data_len);
}

void bst_store_crypto_secret(char* bound_key, size_t bound_key_len)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_store_crypto_secret(bound_key, bound_key_len);
}

time_t bst_get_system_time_ms()
{
    if (bst_platform::instance)
        return bst_platform::instance->bst_get_system_time_ms();
    return 0;
}

uint64_t bst_get_random()
{
    if (bst_platform::instance)
        return bst_platform::instance->bst_get_random();
    return 1;
}

}

