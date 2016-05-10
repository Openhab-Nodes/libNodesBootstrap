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

#include <gtest/gtest.h>

#include <stdint.h>
#include <stdio.h>

#include "boostrapWifi.h"
#include "prv_boostrapWifi.h"
#include "test_platform_impl.h"

class RequestWifiListTests : public testing::Test, public bst_platform {
public:
 protected:
    virtual void TearDown() {
        instance = nullptr;
    }

    virtual void SetUp() {
        instance = this;
        bst_request_wifi_network_list_flag = false;
        bst_network_output_flag = false;
    }

    bool bst_request_wifi_network_list_flag;
    bool bst_network_output_flag;

    // bst_platform interface
public:
    void bst_network_output(const char *data, size_t data_len) override {
        bst_network_output_flag = true;
        bst_udp_send_pkt_t* pkt = (bst_udp_send_pkt_t*)data;
        ASSERT_TRUE(check_send_header(pkt));
        ASSERT_EQ(RSP_WIFI_LIST, pkt->response_code);

        const char* p = data + bst_udp_send_pkt_t_len;

        // First byte is the number of the entries
        ASSERT_EQ(2, (uint8_t)*p);
        ++p;

        // strength
        ASSERT_EQ(100, (uint8_t)*p);
        ++p;

        // enc mode
        ASSERT_EQ(2, (uint8_t)*p);
        ++p;

        ASSERT_STREQ("wifi1", p);
        p += 6;

        // strength
        ASSERT_EQ(50, (uint8_t)*p);
        ++p;

        // enc mode
        ASSERT_EQ(2, (uint8_t)*p);
        ++p;

        ASSERT_STREQ("wifi2", p);

        (void)data_len;
    }
    bst_connect_state bst_get_connection_state() override {
        return BST_DISCOVER_MODE;
    }
    void bst_connect_to_wifi(const char *ssid, const char *pwd) override {
        (void)ssid;
        (void)pwd;
    }
    void bst_connect_advanced(const char *data) override {
        (void)data;
    }
    void bst_discover_mode(const char *ap_ssid, const char *ap_pwd) override {
        (void)ap_ssid;
        (void)ap_pwd;
    }
    void bst_request_wifi_network_list() override {
        bst_request_wifi_network_list_flag = true;
    }
    void bst_store_data(char *data, size_t data_len) {
        (void)data;
        (void)data_len;
    }
    time_t bst_get_system_time_ms() override {
        return 0;
    }
};

TEST_F(RequestWifiListTests, RequestList) {
    bst_setup({},NULL,0);

    char mem[bst_udp_receive_pkt_t_len] = {0};
    bst_udp_receive_pkt_t* p = (bst_udp_receive_pkt_t*)mem;
    memcpy((char*)&(p->hdr[0]),"BSTwifi1",sizeof("BSTwifi1"));
    p->command_code = CMD_REQUEST_WIFI;
    p->session_id = 10;

    bst_network_input((char*)p,bst_udp_receive_pkt_t_len);
    bst_periodic();

    ASSERT_TRUE(bst_request_wifi_network_list_flag);

    bst_wifi_list_entry_t p1;
    p1.ssid = "wifi1";
    p1.strength_percent = 100;
    p1.encryption_mode = 2;
    bst_wifi_list_entry_t p2;
    p2.ssid = "wifi2";
    p2.strength_percent = 50;
    p2.encryption_mode = 2;
    p2.next = nullptr;
    p1.next = &p2;

    bst_wifi_network_list(&p1);
    ASSERT_TRUE(bst_network_output_flag);
}
