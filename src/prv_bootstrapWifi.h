#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "bootstrapWifiConfig.h"
#include "bootstrapWifi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BST_NETWORK_HEADER_SIZE (sizeof(BST_NETWORK_HEADER)-1)

typedef enum
{
    STATE_OK,       // Send with the wifi list and no errors occurred
    STATE_HELLO,    // Used for unencrypted "hello" messages
    STATE_BOOTSTRAP_OK, // Used as confirmation message that bootstraping was succesful

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
        };
        // The bootstrap app establishes a session with its first
        // HELLO packet. An app session nonce is provided with such
        // a packet and this library generates a device nonce in response.
        // The device nonce is valid for bst_connect_options.timeout_nonce_ms.
        time_t time_nonce_valid;
    } state;

    // Delayed execution flags. network_input, bst_factory_reset and other
    // methods only set a flag and the actual execution is done in bst_periodic().
    struct {
        uint8_t request_wifi_list:1;
        uint8_t request_set_wifi:1;
        uint8_t request_bind:1;
        uint8_t request_factory_reset:1;
        uint8_t external_confirmation:1;
    } flags;
} instance_t;

typedef enum
{
    CMD_UNKNOWN,
    CMD_HELLO,
    CMD_SET_DATA,
    CMD_BIND
} prv_bst_cmd;

typedef enum
{
    CONFIRM_NOT_REQUIRED,
    CONFIRM_REQUIRED,
    CONFIRM_OK
} prv_bst_confirm_state;

typedef struct __attribute__((__packed__)) _bst_crc_value
{
    uint8_t crc[BST_CRC_SIZE];
} bst_crc_value;

typedef struct __attribute__((__packed__)) _bst_udp_receive_pkt
{
    char hdr[BST_NETWORK_HEADER_SIZE];
    bst_crc_value crc; // crc in network byte order
    uint8_t command_code;
} bst_udp_receive_pkt_t;

typedef struct __attribute__((__packed__)) _bst_udp_receive_hello_pkt
{
    char hdr[BST_NETWORK_HEADER_SIZE];
    bst_crc_value crc; // crc in network byte order
    uint8_t command_code; // == CMD_HELLO
    char app_nonce[BST_NONCE_SIZE];
} bst_udp_hello_receive_pkt_t;

typedef struct __attribute__((__packed__)) _bst_udp_receive_bind_pkt
{
    char hdr[BST_NETWORK_HEADER_SIZE];
    bst_crc_value crc; // crc in network byte order
    uint8_t command_code; // == CMD_BIND
    uint8_t new_bind_key_len;
    char new_bind_key[BST_BINDKEY_MAX_SIZE];
} bst_udp_bind_receive_pkt_t;

typedef struct __attribute__((__packed__)) _bst_udp_receive_bootstrap_pkt
{
    char hdr[BST_NETWORK_HEADER_SIZE];
    bst_crc_value crc; // crc in network byte order
    uint8_t command_code; // == CMD_SET_DATA
    // Format: ssid\0pwd\0additional_data
    char bootstrap_data[BST_STORAGE_RAM_SIZE];
} bst_udp_bootstrap_receive_pkt_t;

typedef struct __attribute__((__packed__)) _bst_udp_send_hello_pkt
{
    char hdr[BST_NETWORK_HEADER_SIZE];
    bst_crc_value crc; // crc in network byte order
    uint8_t state_code; // prv_bst_error_state
} bst_udp_send_hello_pkt_t;

typedef struct __attribute__((__packed__)) _bst_udp_send_pkt
{
    char hdr[BST_NETWORK_HEADER_SIZE];
    bst_crc_value crc; // crc in network byte order
    uint8_t state_code; // prv_bst_error_state
    char device_nonce[BST_NONCE_SIZE];
    char uid[BST_UID_SIZE];
    uint8_t external_confirmation_state; // one of prv_bst_confirm_state
    uint8_t wifi_list_size_in_bytes;
    uint8_t wifi_list_entries;
    // Store the wifi list and last (error) log message.
    // sizeof(bst_udp_send_pkt_t) == BST_NETWORK_PACKET_SIZE should be true
    char data_wifi_list_and_log_msg[BST_NETWORK_PACKET_SIZE
            -BST_NETWORK_HEADER_SIZE-sizeof(bst_crc_value)
            -sizeof(uint8_t)-BST_NONCE_SIZE-BST_UID_SIZE-3];
} bst_udp_send_pkt_t;

extern instance_t prv_instance;

bst_crc_value bst_crc16(const unsigned char *pData, uint16_t size);

// Make some methods only available on the test suite, otherwise they are static inlined.
#ifdef BST_TEST_SUITE
bool prv_check_header_and_decrypt(bst_udp_receive_pkt_t* pkt, size_t pkt_len);
void prv_add_header(bst_udp_send_pkt_t* pkt);
void prv_add_checksum_and_encrypt(bst_udp_send_pkt_t* pkt, size_t pkt_len);
bool prv_crc16_is_valid(bst_udp_receive_pkt_t* pkt, size_t pkt_len);
#endif

#ifdef __cplusplus
}
#endif
