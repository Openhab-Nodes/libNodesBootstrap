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

#include <vector>

#include "bootstrapWifi.h"
#include "prv_bootstrapWifi.h"
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
        output_data.clear();
    }

    bool bst_request_wifi_network_list_flag;
    std::vector<char> output_data;

    // bst_platform interface
public:
    void bst_network_output(const char *data, size_t data_len) override {
        output_data = std::vector<char>(data, data+data_len);
    }
    bst_connect_state bst_get_connection_state() override {
        return BST_STATE_CONNECTED;
    }
    void bst_connect_to_wifi(const char *ssid, const char *pwd) override {
        (void)ssid;
        (void)pwd;
    }
    void bst_connect_advanced(const char *data) override {
        (void)data;
    }
    void bst_request_wifi_network_list() override {
        bst_request_wifi_network_list_flag = true;
    }
    void bst_connected_to_bootstrap_network() {
    }
    void bst_store_bootstrap_data(char *data, size_t data_len) {
        (void)data;
        (void)data_len;
    }
    void bst_store_crypto_secret(char *data, size_t data_len) {
        (void)data;
        (void)data_len;
    }
};

static void prv_generate_test_hello(bst_udp_hello_receive_pkt_t* p) {
    int len = sizeof("app_nonce");
    memcpy(p->app_nonce,"app_nonce",len);

    RequestWifiListTests::add_header_to_receive_pkt((bst_udp_receive_pkt_t*)p, CMD_HELLO);
    RequestWifiListTests::add_checksum_to_receive_pkt((bst_udp_receive_pkt_t*)p, sizeof(bst_udp_hello_receive_pkt_t));
}


TEST_F(RequestWifiListTests, ExternalConfirmationRequired) {
    bst_connect_options options = default_options();
    options.external_confirmation_mode = BST_CONFIRM_REQUIRED_FIRST_START;
    bst_setup(options, NULL, 0, NULL, 0);

     ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
     bst_periodic();
     ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, bst_get_state());

    { // Send hello packet now
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    ASSERT_FALSE(bst_request_wifi_network_list_flag);
    bst_periodic();
    ASSERT_TRUE(bst_request_wifi_network_list_flag);

    bst_wifi_network_list(nullptr);

    ASSERT_EQ((size_t)BST_NETWORK_PACKET_SIZE, output_data.size());

    bst_udp_send_pkt_t* pkt = (bst_udp_send_pkt_t*)output_data.data();
    ASSERT_TRUE(check_send_header_and_decrypt(pkt));
    ASSERT_EQ(STATE_OK, pkt->state_code);

    ASSERT_EQ(CONFIRM_REQUIRED, pkt->external_confirmation_state);

    /////////////////////////////////////////////////////////////
    output_data.clear();

    bst_confirm_bootstrap();
    bst_wifi_network_list(nullptr);

    ASSERT_EQ((size_t)BST_NETWORK_PACKET_SIZE, output_data.size());

    pkt = (bst_udp_send_pkt_t*)output_data.data();
    ASSERT_TRUE(check_send_header_and_decrypt(pkt));
    ASSERT_EQ(STATE_OK, pkt->state_code);

    ASSERT_EQ(CONFIRM_OK, pkt->external_confirmation_state);
}

TEST_F(RequestWifiListTests, RequestList) {
    bst_setup(default_options(), NULL, 0, NULL, 0);

     ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
     bst_periodic();
     ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, bst_get_state());

    { // Send hello packet now
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    ASSERT_FALSE(bst_request_wifi_network_list_flag);
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

    ASSERT_EQ((size_t)BST_NETWORK_PACKET_SIZE, output_data.size());

    //print_out_java_array("msg_encrypted_crc_key_app_secret", output_data.data(), output_data.size());
    //print_out_java_array("app_nonce", prv_instance.state.prv_app_nonce, BST_NONCE_SIZE);

    bst_udp_send_pkt_t* pkt = (bst_udp_send_pkt_t*)output_data.data();
    ASSERT_TRUE(check_send_header_and_decrypt(pkt));
    ASSERT_EQ(STATE_OK, pkt->state_code);

    ASSERT_EQ(0, memcmp(prv_instance.options.unique_device_id, pkt->uid, BST_UID_SIZE));

    ASSERT_EQ(2, pkt->wifi_list_entries);
    ASSERT_GE(18, pkt->wifi_list_size_in_bytes);
    ASSERT_EQ(CONFIRM_NOT_REQUIRED, pkt->external_confirmation_state);

    const char* p = pkt->data_wifi_list_and_log_msg;

    // If STATE_OK then the log field is for the device name.
    const char* log = p + pkt->wifi_list_size_in_bytes;
    ASSERT_STREQ(prv_instance.options.name, log);

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
}
