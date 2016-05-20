#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "boostrapWifiConfig.h"
#include "boostrapWifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    STATE_OK,

    STATE_ERROR_UNSPECIFIED,
    STATE_ERROR_BINDING,
    STATE_ERROR_BOOTSTRAP_DATA,
    STATE_ERROR_WIFI_LIST,
    STATE_ERROR_WIFI_NOT_FOUND,
    STATE_ERROR_WIFI_CREDENTIALS_WRONG,
    STATE_ERROR_ADVANCED
} prv_bst_error_state;

typedef struct _instance_ {
    /// User options which are assigned in bst_setup()
    /// and will also survive a factory reset.
    bst_connect_options options;

    /// Storage for below pointers
    char storage[BST_STORAGE_RAM_SIZE];
    uint16_t storage_len;

    /// Pointers to ssid, pwd, additional bootstrap data and
    /// the discovery mode access point password.
    /// They point to a cstring within "storage".
    char* ssid;
    char* pwd;
    char* additional;

    /// Initially bound_key is set to options.initial_bound_key. This key
    /// is commonly known and will be used by an app to encrypt/decrypt traffic.
    /// An app will usually try to bind a device and exchange this key by
    /// an app instance specific one. If the app instance is lost, the device
    /// has to be factory reseted to enable bootstrapping again.
    char crypto_secret[BST_BINDKEY_MAX_SIZE];
    uint8_t crypto_secret_len;

    struct {
        const char* error_log_msg;
        char prv_app_nonce[BST_NONCE_SIZE];
        char prv_device_nonce[BST_NONCE_SIZE];
        uint8_t count_connection_attempts;
        bst_state state;
        prv_bst_error_state last_error;

        // Only one of those timeouts is used at a time
        union {
            time_t timeout_connecting_advanced;
            time_t timeout_connecting_destination;
            time_t timeout_connecting_bootstrap_app;
            time_t timeout_app_session;
        };
        time_t time_nonce_valid;
    } state;

    // Delayed execution flags. network_input, bst_factory_reset and other
    // methods only set a flag and the actual execution is done in bst_periodic().
    struct {
        uint8_t request_wifi_list:1;
        uint8_t request_set_wifi:1;
        uint8_t request_bind:1;
        uint8_t request_factory_reset:1;
    } flags;
} instance_t;

typedef enum
{
    CMD_UNKNOWN,
    CMD_HELLO,
    CMD_SET_DATA,
    CMD_BIND
} prv_bst_cmd;

typedef struct __attribute__((__packed__)) _bst_udp_receive_pkt
{
    char hdr[sizeof(BST_NETWORK_HEADER)];
    uint8_t command_code;
    uint16_t crc16; // crc in network byte order
} bst_udp_receive_pkt_t;

typedef struct __attribute__((__packed__)) _bst_udp_receive_hello_pkt
{
    char hdr[sizeof(BST_NETWORK_HEADER)];
    uint8_t command_code; // == CMD_HELLO
    uint16_t crc16; // crc in network byte order
    char app_nonce[BST_NONCE_SIZE];
} bst_udp_hello_receive_pkt_t;

typedef struct __attribute__((__packed__)) _bst_udp_receive_bind_pkt
{
    char hdr[sizeof(BST_NETWORK_HEADER)];
    uint8_t command_code; // == CMD_BIND
    uint16_t crc16; // crc in network byte order
    uint8_t new_bind_key_len;
    char new_bind_key[BST_BINDKEY_MAX_SIZE];
} bst_udp_bind_receive_pkt_t;

typedef struct __attribute__((__packed__)) _bst_udp_receive_bootstrap_pkt
{
    char hdr[sizeof(BST_NETWORK_HEADER)];
    uint8_t command_code; // == CMD_SET_DATA
    uint16_t crc16; // crc in network byte order
    char bootstrap_data[BST_STORAGE_RAM_SIZE];
} bst_udp_bootstrap_receive_pkt_t;


typedef struct __attribute__((__packed__)) _bst_udp_send_pkt
{
    char hdr[sizeof(BST_NETWORK_HEADER)];
    uint8_t state_code; // prv_bst_error_state
    uint16_t crc16; // crc in network byte order
    char device_nonce[BST_NONCE_SIZE];
    uint8_t wifi_list_size_in_bytes;
    uint8_t wifi_list_entries;
    // Store the wifi list and last (error) log message.
    // sizeof(bst_udp_send_pkt_t) == BST_NETWORK_PACKET_SIZE should be true
    char data_wifi_list_and_log_msg[BST_NETWORK_PACKET_SIZE-2-BST_NONCE_SIZE-sizeof(BST_NETWORK_HEADER)];
} bst_udp_send_pkt_t;

extern instance_t prv_instance;

uint16_t bst_crc16(const unsigned char *pData, uint16_t size);

// Make some methods only available on the test suite, otherwise they are static inlined.
#ifdef BST_TEST_SUITE
bool prv_check_header_and_decrypt(bst_udp_receive_pkt_t* pkt, size_t pkt_len);
void prv_add_header(bst_udp_send_pkt_t* pkt);
void prv_add_checksum_and_encrypt(bst_udp_send_pkt_t* pkt);
#endif

#ifdef __cplusplus
}
#endif
