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

class StateMachineTests : public testing::Test, public bst_platform {
public:
 protected:
    virtual void TearDown() {
        instance = nullptr;
    }

    virtual void SetUp() {
        instance = this;
        network_output_flag = false;
        state = BST_MODE_UNINITIALIZED;
        retry_advanced_connection = 0;
        next_connect_state = BST_STATE_NO_CONNECTION;
    }

    bool network_output_flag;
    prv_bst_error_state network_out_state;
    char network_out_data[512];

    int retry_advanced_connection;
    bst_state state;
    bst_connect_state next_connect_state;


public:
    void bst_network_output(const char *data, size_t data_len) override {
        network_output_flag = true;
        bst_udp_send_pkt_t* pkt = (bst_udp_send_pkt_t*)data;
        ASSERT_TRUE(check_send_header(pkt));
        network_out_state =(prv_bst_error_state) pkt->state_code;
        ASSERT_GE(data_len, sizeof(bst_udp_send_pkt_t));
        memcpy(network_out_data, pkt->data_wifi_list_and_log_msg, data_len);
    }
    bst_connect_state bst_get_connection_state() override {
        return next_connect_state;
    }
    void bst_connect_to_wifi(const char *ssid, const char *pwd, bst_state state) override {
        this->state = state;
        if (state == BST_MODE_BOOTSTRAPPED) {
            if (strcmp("wifi1", ssid) == 0 && strcmp("pwd", pwd) == 0)
                next_connect_state = BST_STATE_CONNECTED;
            else
                next_connect_state = BST_STATE_FAILED_CREDENTIALS_WRONG;
        } else if (state == BST_MODE_WAIT_FOR_BOOTSTRAP) {
            if (strcmp("bootstrap_ssid", ssid) == 0 && strcmp("bootstrap_key", pwd) == 0)
                next_connect_state = BST_STATE_CONNECTED;
            else
                next_connect_state = BST_STATE_FAILED_CREDENTIALS_WRONG;
        }
    }
    void bst_connect_advanced(const char *data) override {
        ASSERT_TRUE(data);
        size_t data_len = strlen(data);
        ASSERT_TRUE(memcmp(data,"test",data_len)==0);
         ++retry_advanced_connection;
    }

    void bst_request_wifi_network_list() {
        bst_wifi_network_list(NULL);
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

static void prv_generate_test_data(bst_udp_bootstrap_receive_pkt_t* p, bool correct) {
    const char hdr[] = BST_NETWORK_HEADER;
    memcpy((char*)p->hdr, hdr, sizeof(BST_NETWORK_HEADER));

    p->command_code = CMD_SET_DATA;

    if (correct) {
        int len = sizeof("wifi1\0pwd\0test");
        memcpy(p->bootstrap_data,"wifi1\0pwd\0test",len);
    } else {
        int len = sizeof("wifi1\0pwdwrong\0test");
        memcpy(p->bootstrap_data,"wifi1\0pwdwrong\0test",len);
    }

    prv_add_checksum_and_encrypt((bst_udp_send_pkt_t*)p);
}

static void prv_generate_test_bind(bst_udp_bind_receive_pkt_t* p) {
    const char hdr[] = BST_NETWORK_HEADER;
    memcpy((char*)p->hdr, hdr, sizeof(BST_NETWORK_HEADER));

    p->command_code = CMD_BIND;

    int len = sizeof("new_secret");
    memcpy(p->new_bind_key,"new_secret",len);
    p->new_bind_key_len = len;

    prv_add_checksum_and_encrypt((bst_udp_send_pkt_t*)p);
}

static void prv_generate_test_hello(bst_udp_hello_receive_pkt_t* p) {
    const char hdr[] = BST_NETWORK_HEADER;
    memcpy((char*)p->hdr, hdr, sizeof(BST_NETWORK_HEADER));

    p->command_code = CMD_HELLO;
    int len = sizeof("app_nonce");
    memcpy(p->app_nonce,"app_nonce",len);

    prv_add_checksum_and_encrypt((bst_udp_send_pkt_t*)p);
}

TEST_F(StateMachineTests, NoNetworkInputWithoutAnAppSession) {
    bst_connect_options o = default_options();
    bst_setup(o,NULL, 0, NULL, 0);

    useCurrentTimeOverwrite();

    ASSERT_EQ(BST_MODE_WAIT_FOR_BOOTSTRAP, prv_instance.state.state);
    // We should have been called by bst_connect_advanced() as of now
    ASSERT_EQ(BST_MODE_WAIT_FOR_BOOTSTRAP, this->state);

    bst_periodic();

    {
        bst_udp_bind_receive_pkt_t pkt;
        prv_generate_test_bind(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_bind_receive_pkt_t));
    }

    // Without an app session, initiated by a hello packet, no network response
    // should be generated.
    ASSERT_FALSE(prv_instance.flags.request_bind);
    bst_periodic();
    ASSERT_FALSE(network_output_flag);
}

TEST_F(StateMachineTests, Binding) {
    bst_connect_options o = default_options();
    bst_setup(o,NULL, 0, NULL, 0);

    useCurrentTimeOverwrite();

    ASSERT_EQ(BST_MODE_WAIT_FOR_BOOTSTRAP, this->state);

    {
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    ASSERT_EQ(0, *prv_instance.state.prv_device_nonce);
    ASSERT_GT(prv_instance.state.timeout_app_session, bst_get_system_time_ms());

    network_output_flag = false;
    for (uint8_t i = 0; i<5;++i) bst_periodic();
    ASSERT_EQ(STATE_OK, network_out_state);
    ASSERT_TRUE(network_output_flag);

    {
        bst_udp_bind_receive_pkt_t pkt;
        prv_generate_test_bind(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_bind_receive_pkt_t));
    }

    ASSERT_TRUE(prv_instance.flags.request_bind);
    ASSERT_GT(*prv_instance.state.prv_device_nonce, 0);
    bst_periodic();
    ASSERT_FALSE(prv_instance.flags.request_bind);

    // Without an app session, initiated by a hello packet, no network response
    // should be generated.
    bst_periodic();
    ASSERT_FALSE(network_output_flag);
}

TEST_F(StateMachineTests, DataViaInputAndTimeoutAndReconnect) {
    bst_connect_options o = default_options();
    bst_setup(o,NULL, 0, NULL, 0);

    useCurrentTimeOverwrite();

    { // Send hello packet now
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    bst_periodic();

    // Send data packet now
    {
        bst_udp_bootstrap_receive_pkt_t pkt;
        prv_generate_test_data(&pkt, true);
        bst_network_input((char*)&pkt,sizeof(bst_udp_bootstrap_receive_pkt_t));
    }

    // Test if input is parsed correctly
    ASSERT_STREQ("wifi1",prv_instance.ssid);
    ASSERT_STREQ("pwd",prv_instance.pwd);
    ASSERT_STREQ("test",prv_instance.additional);

    bst_periodic();

    // We simulate a failed connection now
    next_connect_state = BST_STATE_FAILED_SSID_NOT_FOUND;

    for (uint8_t i = 0; i<5;++i) bst_periodic();

    // Test if the state has changed to wait for bootstrap mode.
    ASSERT_EQ(BST_MODE_WAIT_FOR_BOOTSTRAP, this->state);
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());

    // The state should stay the same until the timeout happends
    bst_periodic();

    ASSERT_EQ(BST_MODE_WAIT_FOR_BOOTSTRAP, this->state);
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());

    // Test if timeout_connecting_state_ms works
    addTimeMsOverwrite(prv_instance.options.timeout_connecting_state_ms+5);

    bst_periodic();

    // Bootstrap mode should be active now, because retry is set to <= 1
    ASSERT_EQ(BST_MODE_BOOTSTRAPPED, this->state);
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());
}

TEST_F(StateMachineTests, AdvancedConnection) {
    bst_connect_options o = default_options();
    o.retry_connecting_to_destination_network = 2;
    bst_setup(o,NULL, 0, NULL, 0);

    useCurrentTimeOverwrite();

    { // Send hello packet now
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    bst_periodic();

    { // Send bootstrap packet
        bst_udp_bootstrap_receive_pkt_t pkt;
        prv_generate_test_data(&pkt, true);
        bst_network_input((char*)&pkt,sizeof(bst_udp_bootstrap_receive_pkt_t));
    }

    network_output_flag = false;
    bst_periodic();
    ASSERT_EQ(STATE_OK, network_out_state);
    ASSERT_TRUE(network_output_flag);

    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());
    ASSERT_GE(1, retry_advanced_connection);

    // Expect no further connect to advanced until timeout_connecting_state_ms is over
    for(unsigned i=0;i<5;++i) {
        bst_periodic();
    }

    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());
    ASSERT_GE(1, retry_advanced_connection);

    // Expect second try to connect to advanced
    addTimeMsOverwrite(o.timeout_connecting_state_ms+10);

    ASSERT_GE(2, retry_advanced_connection);
    next_connect_state = BST_STATE_CONNECTED_ADVANCED;

    bst_periodic();

    ASSERT_EQ(BST_STATE_CONNECTED_ADVANCED, bst_get_connection_state());
}

TEST_F(StateMachineTests, CredentialsWrong) {
    int len = sizeof("wifi1\0pwd_wrong\0test");
    char data[] = "wifi1\0pwd_wrong\0test";
    bst_setup({},data,len,NULL,0);

    ASSERT_EQ(STATE_ERROR_WIFI_CREDENTIALS_WRONG, bst_get_connection_state());

    bst_periodic();

    useCurrentTimeOverwrite();

    { // Send hello packet now
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    bst_periodic();

    {
        bst_udp_bootstrap_receive_pkt_t pkt;
        prv_generate_test_data(&pkt, false);
        bst_network_input((char*)&pkt,sizeof(bst_udp_bootstrap_receive_pkt_t));
    }

    bst_periodic();
    ASSERT_EQ(BST_STATE_FAILED_CREDENTIALS_WRONG, bst_get_connection_state());
    ASSERT_EQ(STATE_ERROR_WIFI_CREDENTIALS_WRONG, network_out_state);
    ASSERT_TRUE(network_output_flag);
    ASSERT_STREQ("WiFi Credentials wrong", network_out_data);
}
