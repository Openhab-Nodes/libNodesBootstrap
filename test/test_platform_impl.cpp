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
#include "spritz.h"

#include "test_platform_impl.h"

bst_platform* bst_platform::instance = nullptr;

bool bst_platform::check_send_header_and_decrypt(bst_udp_send_pkt_t* pkt)
{
    const char hdr[] = BST_NETWORK_HEADER;
    if (memcmp(pkt->hdr, hdr, BST_NETWORK_HEADER_SIZE) != 0)
        return false;

    // decrypt
    const size_t offset = sizeof(bst_udp_receive_pkt_t);
    const size_t pkt_len= sizeof(bst_udp_send_pkt_t)-offset;
    unsigned char* out_in = (unsigned char*)pkt+offset;

    spritz_decrypt(out_in,out_in,pkt_len,
                   (unsigned char*)prv_instance.state.prv_app_nonce,BST_NONCE_SIZE,
                   (unsigned char*)prv_instance.crypto_secret,prv_instance.crypto_secret_len);

    // Check crc16
    bst_crc_value v = bst_crc16(out_in, pkt_len);
    return memcmp(&(v.crc),&(pkt->crc),sizeof(bst_crc_value)) == 0;
}

bst_connect_options bst_platform::default_options() {
    bst_connect_options o;
    // The secret for testing is "app_secret\0" with the trailing 0 byte!
    // Secrets may contain any character not only ASCII.
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
    o.external_confirmation_mode = BST_CONFIRM_NOT_REQUIRED;
    return o;
}

void bst_platform::add_header_to_receive_pkt(bst_udp_receive_pkt_t *pkt, prv_bst_cmd cmd)
{
    const char hdr[] = BST_NETWORK_HEADER;
    memcpy((char*)pkt->hdr, hdr, sizeof(BST_NETWORK_HEADER)-1);
    pkt->command_code = (uint8_t)cmd;
}

void bst_platform::add_checksum_to_receive_pkt(bst_udp_receive_pkt_t *pkt, size_t pkt_len)
{
    const size_t offset = sizeof(bst_udp_receive_pkt_t);
    pkt_len -= offset;
    unsigned char* crc_enc_start = (unsigned char*)pkt+offset;

    pkt->crc = bst_crc16(crc_enc_start, pkt_len);

    // encrypt
    if (pkt->command_code != CMD_HELLO) {
        spritz_encrypt(crc_enc_start, crc_enc_start, pkt_len,
                       (unsigned char*)prv_instance.state.prv_device_nonce,BST_NONCE_SIZE,
                       (unsigned char*)prv_instance.crypto_secret,prv_instance.crypto_secret_len);
    }
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

void bst_connected_to_bootstrap_network()
{
    if (bst_platform::instance)
        bst_platform::instance->bst_connected_to_bootstrap_network();
}


}

