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

#include "bootstrapWifi.h"
#include "prv_bootstrapWifi.h"
#include "test_platform_impl.h"

class StateMachineTests : public testing::Test, public bst_platform {
public:
 protected:
    virtual void TearDown() {
        instance = nullptr;
    }

    virtual void SetUp() {
        instance = this;
        network_output_flag = NET_OUT_UNDEFINED;
        network_out_state = STATE_ERROR_UNSPECIFIED;
        m_state = BST_MODE_UNINITIALIZED;
        retry_advanced_connection = 0;
        next_connect_state = BST_STATE_NO_CONNECTION;
        connected_confirmed = false;

        bst_connect_options o = default_options();
        bst_setup(o,NULL, 0, NULL, 0);
        useCurrentTimeOverwrite();
    }

    enum {
        NET_OUT_UNDEFINED,
        NET_OUT_WIFI_LIST,
        NET_OUT_HELLO,
        NET_OUT_BOOTSTRAP_OK
    } network_output_flag;
    prv_bst_error_state network_out_state;
    char network_out_data[512];

    bool connected_confirmed;
    int retry_advanced_connection;
    bst_state m_state;
    bst_connect_state next_connect_state;


public:
    void bst_network_output(const char *data, size_t data_len) override {
        bst_udp_send_pkt_t* pkt = (bst_udp_send_pkt_t*)data;
        if (pkt->state_code == STATE_HELLO) {
            ASSERT_EQ(data_len, sizeof(bst_udp_send_hello_pkt_t));
            network_output_flag = NET_OUT_HELLO;
            return;
        }
        if (pkt->state_code == STATE_BOOTSTRAP_OK) {
            ASSERT_EQ(data_len, sizeof(bst_udp_send_hello_pkt_t));
            network_output_flag = NET_OUT_BOOTSTRAP_OK;
            return;
        }
        network_output_flag = NET_OUT_WIFI_LIST;
        ASSERT_TRUE(check_send_header_and_decrypt(pkt));
        network_out_state =(prv_bst_error_state) pkt->state_code;
        ASSERT_EQ(data_len, sizeof(bst_udp_send_pkt_t));
        memcpy(network_out_data, pkt->data_wifi_list_and_log_msg, data_len);
    }
    bst_connect_state bst_get_connection_state() override {
        return next_connect_state;
    }
    void bst_connect_to_wifi(const char *ssid, const char *pwd) override {
        this->m_state = bst_get_state();
        if (m_state == BST_MODE_CONNECTING_TO_DEST) {
            if (strcmp("wifi1", ssid) == 0 && strcmp("pwd", pwd) == 0)
                next_connect_state = BST_STATE_CONNECTED;
            else
                next_connect_state = BST_STATE_FAILED_CREDENTIALS_WRONG;
        } else if (m_state == BST_MODE_CONNECTING_TO_BOOTSTRAP) {
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

    void bst_request_wifi_network_list() override {
        bst_wifi_network_list(NULL);
    }

    void bst_connected_to_bootstrap_network() {
        connected_confirmed = true;
    }

    void bst_store_bootstrap_data(char *data, size_t data_len) override {
        (void)data;
        (void)data_len;
    }
    void bst_store_crypto_secret(char *data, size_t data_len) override {
        (void)data;
        (void)data_len;
    }
};

static void prv_generate_test_data(bst_udp_bootstrap_receive_pkt_t* p, bool correct) {
    const size_t pkt_len = sizeof(bst_udp_bootstrap_receive_pkt_t);
    bst_platform::add_header_to_receive_pkt((bst_udp_receive_pkt_t*)p, CMD_SET_DATA);

    if (correct) {
        int len = sizeof("wifi1\0pwd\0test");
        memcpy(p->bootstrap_data,"wifi1\0pwd\0test",len);
    } else {
        int len = sizeof("wifi1\0pwdwrong\0test");
        memcpy(p->bootstrap_data,"wifi1\0pwdwrong\0test",len);
    }

    bst_platform::add_checksum_to_receive_pkt((bst_udp_receive_pkt_t*)p, pkt_len);
}

static void prv_generate_test_bind(bst_udp_bind_receive_pkt_t* p) {
    const size_t pkt_len = sizeof(bst_udp_bind_receive_pkt_t);
    bst_platform::add_header_to_receive_pkt((bst_udp_receive_pkt_t*)p, CMD_BIND);

    int len = sizeof("new_secret");
    memcpy(p->new_bind_key,"new_secret",len);
    p->new_bind_key_len = len;

    bst_platform::add_checksum_to_receive_pkt((bst_udp_receive_pkt_t*)p, pkt_len);
}

static void prv_generate_test_hello(bst_udp_hello_receive_pkt_t* p) {
    const size_t pkt_len = sizeof(bst_udp_hello_receive_pkt_t);
    bst_platform::add_header_to_receive_pkt((bst_udp_receive_pkt_t*)p, CMD_HELLO);

    int len = sizeof("app_nonce");
    memcpy(p->app_nonce,"app_nonce",len);

    bst_platform::add_checksum_to_receive_pkt((bst_udp_receive_pkt_t*)p, pkt_len);
}

TEST_F(StateMachineTests, ConnectPeriodicallyToBootstrap) {
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
    bst_periodic();
    ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, bst_get_state());

    // "Disconnect" from bootstrap
    this->next_connect_state = BST_STATE_NO_CONNECTION;
    bst_periodic();
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());

    prv_instance.options.bootstrap_key = "wrong";

    for (int i=0;i<10;++i) {
        this->next_connect_state = BST_STATE_NO_CONNECTION;
        this->m_state = BST_MODE_UNINITIALIZED;

        addTimeMsOverwrite(prv_instance.options.timeout_connecting_state_ms+10);

        bst_periodic();
        ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, m_state);
        ASSERT_EQ(BST_STATE_FAILED_CREDENTIALS_WRONG, next_connect_state);
    }
}

TEST_F(StateMachineTests, NoNetworkInputWithoutAnAppSession) {
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
    bst_periodic();
    ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, bst_get_state());
    ASSERT_TRUE(this->connected_confirmed);
    ASSERT_EQ(NET_OUT_HELLO, network_output_flag);
    network_output_flag = NET_OUT_UNDEFINED;

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
    ASSERT_EQ(NET_OUT_UNDEFINED, network_output_flag);
}

TEST_F(StateMachineTests, Binding) {
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
    bst_periodic();
    ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, bst_get_state());

    {
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    // Check if the device nonce has been applied correctly from bst_get_random().
    uint64_t nonce = *((uint64_t*)prv_instance.state.prv_device_nonce);
    ASSERT_EQ(bst_get_random(), nonce);

    // Check if the nonce is valid
    ASSERT_GE(prv_instance.state.time_nonce_valid, bst_get_system_time_ms());

    network_output_flag = NET_OUT_UNDEFINED;
    for (uint8_t i = 0; i<5;++i) bst_periodic();
    ASSERT_EQ(STATE_OK, network_out_state);
    ASSERT_EQ(NET_OUT_WIFI_LIST, network_output_flag);
    network_output_flag = NET_OUT_UNDEFINED;

    {
        bst_udp_bind_receive_pkt_t pkt;
        prv_generate_test_bind(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_bind_receive_pkt_t));
    }

    ASSERT_TRUE(prv_instance.flags.request_bind);
    bst_periodic();
    ASSERT_EQ(NET_OUT_WIFI_LIST, network_output_flag);
    ASSERT_FALSE(prv_instance.flags.request_bind);
    ASSERT_STREQ(prv_instance.crypto_secret, "new_secret");
}

TEST_F(StateMachineTests, DataViaInputAndTimeoutAndReconnect) {
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
    bst_periodic();
    ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, bst_get_state());
    ASSERT_EQ(NET_OUT_HELLO, network_output_flag);


    { // Send hello packet now
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    bst_periodic();

    // Send bootstrap packet now
    {
        bst_udp_bootstrap_receive_pkt_t pkt;
        prv_generate_test_data(&pkt, true);
        bst_network_input((char*)&pkt,sizeof(bst_udp_bootstrap_receive_pkt_t));
    }

    // Test if input is parsed correctly
    ASSERT_STREQ("wifi1",prv_instance.ssid);
    ASSERT_STREQ("pwd",prv_instance.pwd);
    ASSERT_STREQ("test",prv_instance.additional);

    // In the next periodic run, we shift from bootstrapping mode
    // to BST_MODE_CONNECTING_TO_DEST and establish a connection.
    next_connect_state = BST_STATE_NO_CONNECTION;
    bst_periodic();
    ASSERT_EQ(NET_OUT_BOOTSTRAP_OK, network_output_flag);
    ASSERT_EQ(BST_STATE_CONNECTED, next_connect_state);
    ASSERT_EQ(BST_MODE_CONNECTING_TO_DEST, bst_get_state());

    // In the next run the internal state will update accordingly.
    bst_periodic();
    ASSERT_EQ(BST_MODE_DESTINATION_CONNECTED, bst_get_state());

    // We simulate a failed connection now
    next_connect_state = BST_STATE_FAILED_SSID_NOT_FOUND;

    for (uint8_t i = 0; i<5;++i)
        bst_periodic();

    // Test if the state has changed to wait for bootstrap mode.
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, this->m_state);
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());

    // The state should stay the same until the timeout happends
    bst_periodic();

    ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, prv_instance.state.state);
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());

    // Test if timeout_connecting_state_ms works
    addTimeMsOverwrite(prv_instance.options.timeout_connecting_state_ms+5);

    bst_periodic();

    ASSERT_EQ(BST_MODE_CONNECTING_TO_DEST, prv_instance.state.state);
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());

    bst_periodic();

    // Bootstrap mode should be active now, because retry is set to <= 1
    ASSERT_EQ(BST_MODE_DESTINATION_CONNECTED, prv_instance.state.state);
}

TEST_F(StateMachineTests, AdvancedConnection) {
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
    bst_periodic();
    ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, bst_get_state());

    prv_instance.options.retry_connecting_to_destination_network = 2;
    prv_instance.options.need_advanced_connection = true;

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

    ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, prv_instance.state.state);
    bst_periodic();
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());
    ASSERT_EQ(BST_MODE_CONNECTING_TO_DEST, prv_instance.state.state);
    ASSERT_EQ(0, retry_advanced_connection);

    // Expect no further connect to advanced until timeout_connecting_state_ms is over
    for(unsigned i=0;i<5;++i) {
        bst_periodic();
    }

    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());
    ASSERT_GE(1, retry_advanced_connection);

    // Expect second try to connect to advanced
    addTimeMsOverwrite(prv_instance.options.timeout_connecting_state_ms+10);

    ASSERT_GE(2, retry_advanced_connection);
    next_connect_state = BST_STATE_CONNECTED_ADVANCED;

    bst_periodic();

    ASSERT_EQ(BST_STATE_CONNECTED_ADVANCED, bst_get_connection_state());
}


TEST_F(StateMachineTests, InitialCredentialsWrong) {
    next_connect_state = BST_STATE_NO_CONNECTION;
    int len = sizeof("wifi1\0pwd_wrong\0test");
    char data[] = "wifi1\0pwd_wrong\0test";

    ASSERT_EQ(BST_STATE_NO_CONNECTION, bst_get_connection_state());
    bst_setup(default_options(),data,len,NULL,0);

    // bst_setup will trigger a connection attempt, which will fail
    ASSERT_EQ(BST_MODE_CONNECTING_TO_DEST, bst_get_state());
    ASSERT_EQ(BST_STATE_FAILED_CREDENTIALS_WRONG, bst_get_connection_state());

    // The state will fallback to wait for bootstrap in the next run.
    bst_periodic();
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());
}

TEST_F(StateMachineTests, SendCredentialsWrong) {
    ASSERT_EQ(BST_MODE_CONNECTING_TO_BOOTSTRAP, bst_get_state());
    bst_periodic();
    ASSERT_EQ(BST_MODE_WAITING_FOR_DATA, bst_get_state());

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

    // Test if input is parsed correctly
    ASSERT_STREQ("wifi1",prv_instance.ssid);
    ASSERT_STREQ("pwdwrong",prv_instance.pwd);
    ASSERT_STREQ("test",prv_instance.additional);

    // Try to connect in the next run
    bst_periodic();
    ASSERT_EQ(BST_STATE_FAILED_CREDENTIALS_WRONG, bst_get_connection_state());

    for (int i=0;i<5;++i) bst_periodic();
    ASSERT_EQ(BST_STATE_CONNECTED, bst_get_connection_state());

    ASSERT_EQ(0, network_out_state);

    { // Send hello packet now
        bst_udp_hello_receive_pkt_t pkt;
        prv_generate_test_hello(&pkt);
        bst_network_input((char*)&pkt,sizeof(bst_udp_hello_receive_pkt_t));
    }

    ASSERT_EQ(0, network_out_state);
    bst_periodic();

    ASSERT_EQ(STATE_ERROR_WIFI_CREDENTIALS_WRONG, network_out_state);
    ASSERT_EQ(NET_OUT_WIFI_LIST, network_output_flag);
    ASSERT_STREQ("WiFi Credentials wrong", network_out_data);
}
