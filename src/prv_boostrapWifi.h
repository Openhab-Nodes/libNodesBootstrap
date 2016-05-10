#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "boostrapWifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _instance_ {
    /// User options which are assigned in bst_setup()
    /// and will also survive a factory reset.
    bst_connect_options options;

    /// Storage for below pointers
    char storage[512];
    uint16_t storage_len;

    /// Pointers to ssid, pwd, additional bootstrap data and
    /// the discovery mode access point password.
    /// They point to a cstring within "storage".
    char* ssid;
    char* pwd;
    char* additional;
    char* ap_mode_pwd;

    char ap_mode_ssid[30];

    struct {
        const char* error_log_msg;
        uint16_t prv_session_id;
        uint8_t last_error_code;
        uint8_t count_connection_attempts;
        time_t timeout_connecting;
        time_t timeout_welcomeMessage;
        time_t time_last_access;
        bst_connect_state last;
    } state;

    // Delayed execution flags. network_input, bst_factory_reset and other
    // methods only set a flag and the actual execution is done in bst_periodic().
    struct {
        uint8_t request_wifi_list:1;
        uint8_t request_log:1;
        uint8_t request_set_wifi:1;
        uint8_t request_factory_reset:1;
        uint8_t request_bind:1;
        uint8_t request_access_point:1;
    } flags;
} instance_t;

typedef enum
{
    CMD_UNKNOWN,
    CMD_HELLO,
    CMD_SET_DATA,
    CMD_RESET_FACTORY,
    CMD_REQUEST_WIFI,
    CMD_REQUEST_ERROR_LOG,
    CMD_BIND
} prv_bst_cmd;

typedef enum
{
    RSP_ERROR_UNSPECIFIED,
    RSP_ERROR_BINDING,
    RSP_ERROR_BOOTSTRAP_DATA,
    RSP_ERROR_WIFI_LIST,
    RSP_ERROR_WIFI_NOT_FOUND,
    RSP_ERROR_WIFI_PWD_WRONG,
    RSP_ERROR_ADVANCED,

    RSP_WIFI_LIST = 50,
    RSP_BINDING_ACCEPTED,
    RSP_DATA_ACCEPTED,
    RSP_WELCOME_MESSAGE
} prv_bst_response;

// C11 allows flexible arrays, c++11 not
// C++ is used for tests and applications only, not for the internal handling
// of the library, therefore we use an array of size 1 in c++ instead of a flexible array
// although this will require one useless additional byte for each allocation.
#ifdef __cplusplus
#define FLEX_ARRAY 1
#else
#define FLEX_ARRAY
#endif

typedef struct __attribute__((__packed__)) _bst_udp_receive_pkt
{
    const char hdr[8]; // = BSTwifi1
    uint16_t session_id;
    uint8_t command_code; // prv_bst_cmd
    char data[FLEX_ARRAY];
} bst_udp_receive_pkt_t;
#define bst_udp_receive_pkt_t_len (sizeof(bst_udp_receive_pkt_t)-FLEX_ARRAY-0)

typedef struct __attribute__((__packed__)) _bst_udp_send_pkt
{
    char hdr[8]; // = BSTwifi1
    uint8_t response_code; // prv_bst_response
    char data[FLEX_ARRAY];
} bst_udp_send_pkt_t;
#define bst_udp_send_pkt_t_len (sizeof(bst_udp_send_pkt_t)-FLEX_ARRAY-0)

// Specialiced response structures
typedef struct __attribute__((__packed__)) _bst_udp_send_welcome
{
    char hdr[8]; // = BSTwifi1
    uint8_t response_code; // prv_bst_response
    uint16_t session_id;
} bst_udp_send_welcome_t;

extern instance_t prv_instance;

#ifdef __cplusplus
}
#endif
