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
        m_state = BST_MODE_UNINITIALIZED;
    }

    const char* m_ssid;
    const char* m_pwd;
    bst_state m_state;

    // bst_platform interface
public:
    void bst_network_output(const char *data, size_t data_len) override {
        (void)data;
        (void)data_len;
    }
    bst_connect_state bst_get_connection_state() override {
        return BST_STATE_NO_CONNECTION;
    }
    void bst_connect_to_wifi(const char *ssid, const char *pwd, bst_state state) override {
        this->m_ssid = ssid;
        this->m_pwd = pwd;
        this->m_state = state;
    }
    void bst_connect_advanced(const char *data) override {
        (void)data;
    }

    void bst_request_wifi_network_list() {
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

TEST_F(SetupTests, CRC16) {
    typedef struct __attribute__((__packed__)) {
        unsigned char data[9];
        unsigned char chk[2];
    } data_with_chk;

    data_with_chk data = {{'1','2','3','4','5','6','7','8','9'}, {0x29, 0xB1}};
    ASSERT_EQ(0x29B1, bst_crc16((unsigned char*)&data,9));
    ASSERT_EQ(0, bst_crc16((unsigned char*)&data,11));

    uint16_t crc16 = 0x29B1;

    char* crc = (char*)&(crc16);
    // Store crc16 in network byte order
    crc[0] = crc16 & 0xff;
    crc[1] = (crc16>>8) & 0xff;

    // convert network to host byte order
    crc16 = crc[0] + (crc[1]<<8);
    ASSERT_EQ(0x29B1, crc16);
}

TEST_F(SetupTests, EncryptDecrypt) {
    bst_udp_send_pkt_t p;

    prv_add_header(&p);
    memcpy(p.device_nonce,"test",5);
    prv_add_checksum_and_encrypt(&p);

    bst_udp_hello_receive_pkt_t* pkt = (bst_udp_hello_receive_pkt_t*)&p;
    ASSERT_TRUE(prv_check_header_and_decrypt((bst_udp_receive_pkt_t*)pkt,sizeof(bst_udp_send_pkt_t)));

    // safety \0
    pkt->app_nonce[BST_NONCE_SIZE-1] = 0;
    ASSERT_STREQ("test", pkt->app_nonce);
}

TEST_F(SetupTests, EmptyOptions) {
    bst_setup({},NULL,0,NULL,0);
}



TEST_F(SetupTests, NormalOptions) {
    bst_connect_options o = default_options();
    bst_setup(o,NULL, 0, NULL, 0);

    ASSERT_EQ(BST_MODE_WAIT_FOR_BOOTSTRAP, prv_instance.state.state);
    ASSERT_STREQ("app_secret", prv_instance.crypto_secret);

    ASSERT_EQ(BST_MODE_WAIT_FOR_BOOTSTRAP, m_state);
    ASSERT_STREQ(m_pwd, prv_instance.options.bootstrap_key);
    ASSERT_STREQ(m_ssid, prv_instance.options.bootstrap_ssid);
}

TEST_F(SetupTests, OptionsWithStoredBoundKey) {
    bst_connect_options o = default_options();
    bst_setup(o, NULL, 0, "new_bound_key", sizeof("new_bound_key"));

    ASSERT_STREQ("new_bound_key", prv_instance.crypto_secret);
}

TEST_F(SetupTests, OptionsWithStoredData) {
    bst_connect_options o = default_options();

    char data[512] = {0};
    char* p = data;
    memcpy(p,"WLAN_SSID_NAME", sizeof("WLAN_SSID_NAME")); p+= sizeof("WLAN_SSID_NAME");
    ASSERT_EQ(sizeof("WLAN_SSID_NAME"), p - data);

    // Test with only ssid set
    bst_setup(o, data, p - data, NULL, 0);
    ASSERT_EQ(sizeof("WLAN_SSID_NAME"),prv_instance.storage_len);

    ASSERT_STREQ("WLAN_SSID_NAME", prv_instance.ssid);
    ASSERT_EQ(BST_MODE_BOOTSTRAPPED, prv_instance.state.state);
    ASSERT_STREQ("WLAN_SSID_NAME", this->m_ssid);
    ASSERT_FALSE(this->m_pwd);
    ASSERT_FALSE(prv_instance.pwd);
    ASSERT_FALSE(prv_instance.additional);

    // Test with ssid and pwd set
    memcpy(p,"WLAN_PWD", sizeof("WLAN_PWD")); p+= sizeof("WLAN_PWD");
    ASSERT_EQ(sizeof("WLAN_SSID_NAME")+sizeof("WLAN_PWD"), p - data);

    bst_setup(o,data, p - data, NULL, 0);
    ASSERT_EQ(sizeof("WLAN_SSID_NAME")+sizeof("WLAN_PWD"),prv_instance.storage_len);

    ASSERT_STREQ("WLAN_PWD", prv_instance.pwd);
    ASSERT_STREQ("WLAN_PWD", this->m_pwd);

    // Test with ssid and pwd set and adv data
    const char* adv_data = "coap://192.168.0.117;security_secret";
    int additional_data_len = strlen(adv_data)+1;
    memcpy(p, adv_data, additional_data_len); p+= additional_data_len;

    bst_setup(o,data, p - data, NULL, 0);
    ASSERT_EQ(sizeof("WLAN_SSID_NAME")+sizeof("WLAN_PWD")+additional_data_len, prv_instance.storage_len);
    ASSERT_STREQ(adv_data, prv_instance.additional);
}
