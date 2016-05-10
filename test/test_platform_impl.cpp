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

bool bst_platform::check_receive_header(bst_udp_receive_pkt_t* pkt)
{
    const char hdr[] = "BSTwifi1";
    return (memcmp(pkt->hdr, hdr, 8) == 0);
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
    return BST_DISCOVER_MODE;
}

void bst_connect_to_wifi(const char* ssid, const char* pwd)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_connect_to_wifi(ssid, pwd);
}

void bst_connect_advanced(const char* data)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_connect_advanced(data);
}

void bst_discover_mode(const char* ap_ssid, const char* ap_pwd)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_discover_mode(ap_ssid, ap_pwd);
}

void bst_request_wifi_network_list()
{
    if (bst_platform::instance)
        bst_platform::instance->bst_request_wifi_network_list();
}

void bst_store_data(char* data, size_t data_len)
{
    if (bst_platform::instance)
        bst_platform::instance->bst_store_data(data, data_len);
}

time_t bst_get_system_time_ms()
{
    if (bst_platform::instance)
        return bst_platform::instance->bst_get_system_time_ms();
    return 0;
}

}

