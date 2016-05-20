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
        ASSERT_EQ(STATE_OK, pkt->state_code);

        ASSERT_EQ(2, pkt->wifi_list_entries);
        ASSERT_GE(18, pkt->wifi_list_size_in_bytes);

        const char* p = pkt->data_wifi_list_and_log_msg;

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
        return BST_STATE_NO_CONNECTION;
    }
    void bst_connect_to_wifi(const char *ssid, const char *pwd, bst_state state) override {
        (void)ssid;
        (void)pwd;
        (void)state;
    }
    void bst_connect_advanced(const char *data) override {
        (void)data;
    }
    void bst_request_wifi_network_list() override {
        bst_request_wifi_network_list_flag = true;
    }
    void bst_store_bootstrap_data(char *data, size_t data_len) {
        (void)data;
        (void)data_len;
    }
    void bst_store_crypto_secret(char *data, size_t data_len) {
        (void)data;
        (void)data_len;
    }
    time_t bst_get_system_time_ms() override {
        return 0;
    }
};

static void prv_generate_test_hello(bst_udp_hello_receive_pkt_t* p) {
    const char hdr[] = BST_NETWORK_HEADER;
    memcpy((char*)p->hdr, hdr, sizeof(BST_NETWORK_HEADER));

    p->command_code = CMD_HELLO;
    int len = sizeof("app_nonce");
    memcpy(p->app_nonce,"app_nonce",len);

    prv_add_checksum_and_encrypt((bst_udp_send_pkt_t*)p);
}

TEST_F(RequestWifiListTests, RequestList) {
    bst_setup({}, NULL, 0, NULL, 0);

    { // Send hello packet now
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

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
