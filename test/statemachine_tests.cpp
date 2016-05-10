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
        bst_network_output_flag = false;
        bst_connect_to_wifi_flag= false;
        bst_discover_mode_flag = false;
        retry_advanced_connection = 0;
        connect_will_succeed = false;
        connect_wrong_cred_flag = false;
        connect_adv_will_succeed = 0;
    }

    bool bst_network_output_flag;
    bool bst_connect_to_wifi_flag;
    bool bst_discover_mode_flag;
    int retry_advanced_connection;
    bool connect_will_succeed;
    bool connect_adv_will_succeed;
    bool connect_wrong_cred_flag;

    prv_bst_response expect_rsp_code;
    char network_out_data[512];

public:
    void bst_network_output(const char *data, size_t data_len) override {
        bst_network_output_flag = true;
        bst_udp_send_pkt_t* pkt = (bst_udp_send_pkt_t*)data;
        ASSERT_TRUE(check_send_header(pkt));
        ASSERT_EQ(expect_rsp_code, pkt->response_code);
        ASSERT_GE(data_len, bst_udp_send_pkt_t_len);
        memcpy(network_out_data, pkt->data, data_len);
    }
    bst_connect_state bst_get_connection_state() override {
        if (connect_wrong_cred_flag)
            return BST_FAILED_CREDENTIALS_WRONG;
        if (connect_will_succeed) {
            if (connect_adv_will_succeed && retry_advanced_connection)
                return BST_CONNECTED_ADVANCED;
            else
                return BST_CONNECTED;
        }
        if (bst_connect_to_wifi_flag)
            return BST_CONNECTING;
        return BST_DISCOVER_MODE;
    }
    void bst_connect_to_wifi(const char *ssid, const char *pwd) override {
        bst_connect_to_wifi_flag = true;
        ASSERT_STREQ("wifi1", ssid);
        ASSERT_STREQ("pwd", pwd);
    }
    void bst_connect_advanced(const char *data) override {
        ASSERT_TRUE(data);
        size_t data_len = strlen(data);
        ASSERT_TRUE(memcmp(data,"test",data_len)==0);
         ++retry_advanced_connection;
    }
    void bst_discover_mode(const char *ap_ssid, const char *ap_pwd) override {
        (void)ap_ssid;
        (void)ap_pwd;
        bst_connect_to_wifi_flag = false;
        bst_discover_mode_flag = true;
    }
    void bst_request_wifi_network_list() {
    }
    void bst_store_data(char *data, size_t data_len) {
        (void)data;
        (void)data_len;
    }
};

static int prv_generate_test_data(char* mem) {
    int len = sizeof("wifi1\0pwd\0test");
    bst_udp_receive_pkt_t* p = (bst_udp_receive_pkt_t*)mem;
    memcpy((char*)&(p->hdr[0]),"BSTwifi1",sizeof("BSTwifi1"));
    p->command_code = CMD_SET_DATA;
    p->session_id = 10;
    memcpy(p->data,"wifi1\0pwd\0test",len);
    return len;
}

static int prv_generate_test_bind_data(char* mem) {
    int len = sizeof("new_secret");
    bst_udp_receive_pkt_t* p = (bst_udp_receive_pkt_t*)mem;
    memcpy((char*)&(p->hdr[0]),"BSTwifi1",sizeof("BSTwifi1"));
    p->command_code = CMD_BIND;
    p->session_id = 10;
    memcpy(p->data,"new_secret",len);
    return len;
}

TEST_F(StateMachineTests, DataViaInputAndTimeoutAndReconnect) {
    bst_connect_options o;
    o.factory_app_secret = "app_secret";
    o.interval_try_again_ms = 500; // every 500ms
    o.name = "testname";
    o.need_advanced_connection = false;
    o.unique_device_id = "ABCDEF";
    o.retry_advanced_connection = 0;
    o.timeout_connecting_state_ms = 200; // 200ms
    bst_setup(o,NULL, 0);

    for (uint8_t i = 0; i<5;++i) {
        ASSERT_EQ(BST_DISCOVER_MODE, prv_instance.state.last);
        bst_periodic();
    }
    ASSERT_EQ(BST_DISCOVER_MODE, prv_instance.state.last);

    char mem[bst_udp_receive_pkt_t_len+20] = {0};
    int len = prv_generate_test_data(mem);
    bst_network_input(mem,bst_udp_receive_pkt_t_len+len);

    expect_rsp_code = RSP_DATA_ACCEPTED;

    bst_periodic();
    ASSERT_TRUE(bst_connect_to_wifi_flag);
    ASSERT_TRUE(bst_network_output_flag);
    bst_network_output_flag = false;

    ASSERT_EQ(BST_CONNECTING, bst_get_connection_state());

    for (uint8_t i = 0; i<5;++i) {
        bst_periodic();
        ASSERT_EQ(BST_CONNECTING, prv_instance.state.last);
        ASSERT_GT(prv_instance.state.timeout_connecting, bst_get_system_time_ms());
    }

    // Test if input is parsed correctly
    ASSERT_STREQ("wifi1",prv_instance.ssid);
    ASSERT_STREQ("pwd",prv_instance.pwd);
    ASSERT_STREQ("test",prv_instance.additional);

    // Test if timeout_connecting_state_ms works
    usleep(1000*210);
    bst_periodic();
    ASSERT_EQ(BST_FAILED_SSID_NOT_FOUND, prv_instance.state.last);
    ASSERT_EQ(BST_DISCOVER_MODE, bst_get_connection_state());
    ASSERT_TRUE(bst_discover_mode_flag);

    // Test if interval_try_again_ms works
    bst_periodic();
    usleep(1000*510);
    bst_periodic();
    ASSERT_EQ(BST_CONNECTING, bst_get_connection_state());
    ASSERT_TRUE(bst_connect_to_wifi_flag);
    ASSERT_TRUE(bst_network_output_flag);

    connect_will_succeed = true;
    ASSERT_EQ(BST_CONNECTED, bst_get_connection_state());
    bst_periodic();
    ASSERT_EQ(BST_CONNECTED, prv_instance.state.last);
}

TEST_F(StateMachineTests, AdvancedConnection) {
    bst_connect_options o;
    o.factory_app_secret = "app_secret";
    o.interval_try_again_ms = 500; // every 500ms
    o.name = "testname";
    o.need_advanced_connection = true;
    o.unique_device_id = "ABCDEF";
    o.retry_advanced_connection = 3;
    o.timeout_connecting_state_ms = 200; // 200ms
    bst_setup(o,NULL, 0);

    char mem[bst_udp_receive_pkt_t_len+20] = {0};
    int len = prv_generate_test_data(mem);

    useCurrentTimeOverwrite();

    // Expect change to CONNECTING
    expect_rsp_code = RSP_DATA_ACCEPTED;
    bst_network_input(mem,bst_udp_receive_pkt_t_len+len);
    ASSERT_STREQ(o.factory_app_secret, prv_instance.ap_mode_pwd);
    bst_periodic();
    ASSERT_EQ(BST_DISCOVER_MODE, prv_instance.state.last);
    bst_periodic();
    ASSERT_TRUE(bst_connect_to_wifi_flag);
    ASSERT_TRUE(bst_network_output_flag);
    ASSERT_EQ(BST_CONNECTING, prv_instance.state.last);

    // Expect change to CONNECTED and at least one try to connect to advanced
    connect_will_succeed = true;
    bst_periodic();
    ASSERT_EQ(BST_CONNECTED, prv_instance.state.last);
    ASSERT_EQ(1, retry_advanced_connection);

    // Expect no further connect to advanced until timeout_connecting_state_ms is over
    for(unsigned i=0;i<5;++i) {
        bst_periodic();
    }

    // Expect second try to connect to advanced
    addTimeMsOverwrite(o.timeout_connecting_state_ms+10);

    bst_periodic();
    ASSERT_EQ(BST_CONNECTED, prv_instance.state.last);
    ASSERT_EQ(2, retry_advanced_connection);

    connect_adv_will_succeed = true;
    // Expect third try to connect to advanced
    addTimeMsOverwrite(o.timeout_connecting_state_ms+10);
    bst_periodic();
    ASSERT_EQ(BST_CONNECTED_ADVANCED, prv_instance.state.last);
}

TEST_F(StateMachineTests, ChangeSecret) {
    bst_setup({},NULL,0);
    ASSERT_STREQ("", prv_instance.ap_mode_pwd);

    useCurrentTimeOverwrite();

    expect_rsp_code = RSP_BINDING_ACCEPTED;

    char mem[bst_udp_receive_pkt_t_len+20] = {0};
    int len = prv_generate_test_bind_data(mem);
    bst_network_input(mem,bst_udp_receive_pkt_t_len+len);
    ASSERT_STREQ("new_secret", prv_instance.ap_mode_pwd);
    bst_periodic();
}

TEST_F(StateMachineTests, CredentialsWrong) {
    bst_setup({},NULL,0);

    useCurrentTimeOverwrite();

    expect_rsp_code = RSP_DATA_ACCEPTED;
    char mem[bst_udp_receive_pkt_t_len+20] = {0};
    int len = prv_generate_test_data(mem);
    bst_network_input(mem,bst_udp_receive_pkt_t_len+len);
    ASSERT_STREQ("", prv_instance.ap_mode_pwd);

    bst_periodic();
    bst_periodic();
    ASSERT_EQ(BST_CONNECTING, prv_instance.state.last);

    bst_discover_mode_flag = false;
    connect_wrong_cred_flag = true;
    bst_periodic();
    ASSERT_EQ(BST_FAILED_CREDENTIALS_WRONG, prv_instance.state.last);

    connect_wrong_cred_flag = false;
    bst_periodic();
    ASSERT_EQ(BST_DISCOVER_MODE, prv_instance.state.last);
    ASSERT_TRUE(bst_discover_mode_flag);

    // request error msg
    bst_udp_receive_pkt_t p = {0};
    memcpy((char*)&(p.hdr[0]),"BSTwifi1",sizeof("BSTwifi1"));
    p.command_code = CMD_REQUEST_ERROR_LOG;
    bst_network_input((char*)&p,bst_udp_receive_pkt_t_len);

    expect_rsp_code = RSP_ERROR_UNSPECIFIED;
    bst_periodic();
    ASSERT_TRUE(bst_network_output_flag);
    ASSERT_STREQ("WiFi Credentials wrong", network_out_data);
}
