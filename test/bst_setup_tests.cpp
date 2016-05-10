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

class SetupTests : public testing::Test, public bst_platform {
public:
 protected:
    virtual void TearDown() {
        instance = nullptr;
    }

    virtual void SetUp() {
        instance = this;
         m_bst_discover_mode = false;
         m_bst_wifi_mode = false;
    }

    bool m_bst_discover_mode;
    bool m_bst_wifi_mode;
    const char* m_ssid;
    const char* m_pwd;


    // bst_platform interface
public:
    void bst_network_output(const char *data, size_t data_len) override {
        (void)data;
        (void)data_len;
    }
    bst_connect_state bst_get_connection_state() override {
        return BST_DISCOVER_MODE;
    }
    void bst_connect_to_wifi(const char *ssid, const char *pwd) override {
        m_bst_wifi_mode = true;
        this->m_ssid = ssid;
        this->m_pwd = pwd;
    }
    void bst_connect_advanced(const char *data) override {
        (void)data;
    }
    void bst_discover_mode(const char *ap_ssid, const char *ap_pwd) override {
        m_bst_discover_mode = true;
        this->m_ssid = ap_ssid;
        this->m_pwd = ap_pwd;
    }
    void bst_request_wifi_network_list() {
    }
    void bst_store_data(char *data, size_t data_len) {
        (void)data;
        (void)data_len;
    }
};

TEST_F(SetupTests, EmptyOptions) {
    bst_setup({},NULL,0);
}

TEST_F(SetupTests, NormalOptions) {
    bst_connect_options o;
    o.factory_app_secret = "app_secret";
    o.interval_try_again_ms = 5 * 60 * 1000; // every 5 min
    o.name = "testname";
    o.need_advanced_connection = false;
    o.unique_device_id = "ABCDEF";
    o.retry_advanced_connection = 0;
    o.timeout_connecting_state_ms = 15000; // 15sec
    bst_setup(o,NULL, 0);

    ASSERT_EQ(BST_DISCOVER_MODE, prv_instance.state.last);
    ASSERT_STREQ("app_secret", prv_instance.ap_mode_pwd);
    ASSERT_STREQ("BST_testname_ABCDEF", prv_instance.ap_mode_ssid);

    ASSERT_TRUE(m_bst_discover_mode);
    ASSERT_STREQ(m_pwd, prv_instance.ap_mode_pwd);
    ASSERT_STREQ(m_ssid, prv_instance.ap_mode_ssid);
}

TEST_F(SetupTests, OptionsWithStoredData) {
    bst_connect_options o;
    o.factory_app_secret = "app_secret";
    o.interval_try_again_ms = 5 * 60 * 1000; // every 5 min
    o.name = "testname";
    o.need_advanced_connection = false;
    o.unique_device_id = "ABCDEF";
    o.retry_advanced_connection = 0;
    o.timeout_connecting_state_ms = 15000; // 15sec

    char data[512] = {0};
    char* p = data;
    memcpy(p,"WLAN_SSID_NAME", sizeof("WLAN_SSID_NAME")); p+= sizeof("WLAN_SSID_NAME");
    ASSERT_EQ(sizeof("WLAN_SSID_NAME"), p - data);

    // Test with only ssid set
    bst_setup(o,data, p - data);
    ASSERT_EQ(sizeof("WLAN_SSID_NAME"),prv_instance.storage_len);

    ASSERT_STREQ(o.factory_app_secret, prv_instance.ap_mode_pwd);

    ASSERT_STREQ("WLAN_SSID_NAME", prv_instance.ssid);
    ASSERT_TRUE(this->m_bst_wifi_mode);
    ASSERT_STREQ("WLAN_SSID_NAME", this->m_ssid);
    ASSERT_FALSE(this->m_pwd);
    ASSERT_FALSE(prv_instance.pwd);
    ASSERT_FALSE(prv_instance.additional);

    // Test with ssid and pwd set
    memcpy(p,"WLAN_PWD", sizeof("WLAN_PWD")); p+= sizeof("WLAN_PWD");
    ASSERT_EQ(sizeof("WLAN_SSID_NAME")+sizeof("WLAN_PWD"), p - data);

    bst_setup(o,data, p - data);
    ASSERT_EQ(sizeof("WLAN_SSID_NAME")+sizeof("WLAN_PWD"),prv_instance.storage_len);

    ASSERT_STREQ(o.factory_app_secret, prv_instance.ap_mode_pwd);

    ASSERT_STREQ("WLAN_PWD", prv_instance.pwd);
    ASSERT_STREQ("WLAN_PWD", this->m_pwd);

    // Test with ssid and pwd set and adv data
    const char* adv_data = "coap://192.168.0.117;security_secret";
    int additional_data_len = strlen(adv_data)+1;
    memcpy(p, adv_data, additional_data_len); p+= additional_data_len;

    bst_setup(o,data, p - data);
    ASSERT_EQ(sizeof("WLAN_SSID_NAME")+sizeof("WLAN_PWD")+additional_data_len, prv_instance.storage_len);
    ASSERT_STREQ(adv_data, prv_instance.additional);
}
