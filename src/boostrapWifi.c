#include "boostrapWifi.h"
#include "prv_boostrapWifi.h"
#include <string.h>

#ifndef BST_NO_ERROR_MESSAGES
    const char* ERR_FAILED_ADVANCED = "Failed to connect to advanced";
    const char* ERR_FAILED_WIFI_CRED = "WiFi Credentials wrong";
    const char* ERR_FAILED_WIFI_NOT_FOUND = "WiFi not found";
#else
    const char* ERR_FAILED_ADVANCED = 0;
    const char* ERR_FAILED_WIFI_CRED = 0;
    const char* ERR_FAILED_WIFI_NOT_FOUND = 0;
#endif

#ifdef BST_TEST_SUITE
#define STATIC_INLINE
#else
#define STATIC_INLINE static inline
#endif

instance_t prv_instance;

static void prv_enter_wait_for_bootstrap_mode(prv_bst_error_state last_error_code, const char* last_error_message);
static void prv_enter_bootstrapped_mode();

#define CRC16 0x1021 // ("CRC-16/CCITT-FALSE")
//#define CRC16 0x8005 // ("CRC-16")

/// CRC-16/CCITT-FALSE
/// width=16 poly=0x1021 init=0xffff refin=false refout=false xorout=0x0000 check=0x29b1 name="CRC-16/CCITT-FALSE"
uint16_t __attribute__((weak)) bst_crc16(const unsigned char *pData, uint16_t size)
{
    uint16_t wCrc = 0xffff;

    while(size) {
        wCrc ^= *pData++ << 8;
        for (uint8_t i=0; i < 8; i++)
            wCrc = (wCrc & 0x8000) ? ((wCrc << 1) ^ 0x1021) : (wCrc << 1);
        --size;
    }
    return wCrc;
}

static inline void prv_enter_and_keep_app_session() {
    if (!prv_instance.state.timeout_app_session)
        return;

    time_t currentTime = bst_get_system_time_ms();

    // If app session timed out, remove all nonces and reset timeouts.
    if (currentTime > prv_instance.state.timeout_app_session) {
        prv_instance.state.timeout_app_session = 0;
        prv_instance.state.time_nonce_valid = 0;
        memset(prv_instance.state.prv_app_nonce,0,BST_NONCE_SIZE);
        memset(prv_instance.state.prv_device_nonce,0,BST_NONCE_SIZE);
        return;
    }

    // Renew device nonce every timeout_nonce_ms.
    if (currentTime <= prv_instance.state.time_nonce_valid) {
        prv_instance.state.time_nonce_valid = prv_instance.options.timeout_nonce_ms + currentTime;
        uint64_t* p = (uint64_t*)prv_instance.state.prv_device_nonce;
        for (unsigned i=0;i<BST_NONCE_SIZE/8;++i) {
             p[i] = bst_get_random();
        }
        return;
    }
}

STATIC_INLINE bool prv_check_header_and_decrypt(bst_udp_receive_pkt_t* pkt, size_t pkt_len)
{
    const char hdr[] = BST_NETWORK_HEADER;
    if (memcmp(pkt->hdr, hdr, sizeof(BST_NETWORK_HEADER)) != 0) {
      BST_DBG("Header wrong\n");
      return false;
    }

    // TODO decrypt

    // Check crc16
    char* crc = (char*)&(pkt->crc16); // convert network to host byte order
    pkt->crc16 = crc[0] | crc[1]<<8;
    return bst_crc16((uint8_t*)pkt,pkt_len) == 0;
}

STATIC_INLINE void prv_add_checksum_and_encrypt(bst_udp_send_pkt_t* pkt)
{
    char* crc = (char*)&(pkt->crc16);
    uint16_t crc16 = bst_crc16((unsigned char*)pkt, sizeof(bst_udp_send_pkt_t));
    // Store crc16 in network byte order
    crc[0] = crc16 & 0xff;
    crc[1] = (crc16>>8) & 0xff;

    // TODO encrypt
}

STATIC_INLINE void prv_add_header(bst_udp_send_pkt_t* pkt)
{
    const char hdr[] = BST_NETWORK_HEADER;
    memcpy((char*)pkt->hdr, hdr, sizeof(BST_NETWORK_HEADER));
    pkt->state_code = prv_instance.state.last_error;
    memcpy(pkt->device_nonce, prv_instance.state.prv_device_nonce, BST_NONCE_SIZE);
    pkt->wifi_list_size_in_bytes = 0;
    pkt->wifi_list_entries = 0;
}

/// Determine ssid, pwd, additional and ap_mode_pwd pointers
static void prv_assign_data(const char* stored_data, size_t stored_data_len)
{
    char** pointers[] = {&prv_instance.ssid, &prv_instance.pwd, &prv_instance.additional};

    // data len = max(input len, sizeof prv_instance.storage - 4)
    if (stored_data_len > BST_STORAGE_RAM_SIZE-3)
        stored_data_len = BST_STORAGE_RAM_SIZE-3;

    // Copy new data
    memcpy(prv_instance.storage, stored_data, stored_data_len);

    // Set rest to 0
    memset(prv_instance.storage+stored_data_len, 0, BST_STORAGE_RAM_SIZE-stored_data_len);

    char* dataP = prv_instance.storage;

    for (int i=0; i < 3; ++i) {
        if (!*dataP) {
            *pointers[i] = 0;
        } else {
            *pointers[i] = dataP;
            dataP += strlen(dataP);
        }

        ++dataP;
    }

    prv_instance.storage_len = stored_data_len;
}

void bst_setup(bst_connect_options options, const char* bst_data, size_t bst_data_len, const char *bound_key, size_t bound_key_len)
{
    // Clear prv_instance and assign options
    memset(&prv_instance, 0, sizeof(instance_t));
    prv_instance.options = options;

    prv_assign_data(bst_data, bst_data_len);

    if (bound_key_len > BST_BINDKEY_MAX_SIZE)
        bound_key_len = BST_BINDKEY_MAX_SIZE;

    if (bound_key_len) {
        memcpy(prv_instance.crypto_secret, bound_key, bound_key_len);
        prv_instance.crypto_secret_len = bound_key_len;
    } else {
        memcpy(prv_instance.crypto_secret, prv_instance.options.initial_crypto_secret, prv_instance.options.initial_crypto_secret_len);
        prv_instance.crypto_secret_len = prv_instance.options.initial_crypto_secret_len;
    }

    if (prv_instance.ssid)
        prv_enter_bootstrapped_mode();
    else
        prv_enter_wait_for_bootstrap_mode(STATE_OK, NULL);
}


/**
 * Try to connect to the wireless network (prv_instance.options.bootstrap_ssid) every
 * timeout_connecting_state_ms.
 */
static void prv_enter_wait_for_bootstrap_mode(prv_bst_error_state last_error_code, const char* last_error_message)
{
    // Reset connection+flags state
    memset(&(prv_instance.flags), 0, sizeof(prv_instance.flags));
    memset(&(prv_instance.state), 0, sizeof(prv_instance.state));
    prv_instance.state.error_log_msg = last_error_message;
    prv_instance.state.last_error = last_error_code;
    prv_instance.state.state = BST_MODE_WAIT_FOR_BOOTSTRAP;
    prv_instance.state.timeout_connecting_bootstrap_app = bst_get_system_time_ms() + prv_instance.options.timeout_connecting_state_ms;

    bst_connect_to_wifi(prv_instance.options.bootstrap_ssid, prv_instance.options.bootstrap_key, BST_MODE_WAIT_FOR_BOOTSTRAP);
}

static void prv_enter_bootstrapped_mode()
{
    // Reset connection+flags state
    memset(&(prv_instance.flags), 0, sizeof(prv_instance.flags));
    memset(&(prv_instance.state), 0, sizeof(prv_instance.state));
    prv_instance.state.state = BST_MODE_BOOTSTRAPPED;
    prv_instance.state.timeout_connecting_destination = bst_get_system_time_ms() + prv_instance.options.timeout_connecting_state_ms;

    bst_connect_to_wifi(prv_instance.ssid, prv_instance.pwd, BST_MODE_BOOTSTRAPPED);
}

void bst_periodic()
{
    if (prv_instance.flags.request_factory_reset) {
        prv_instance.flags.request_factory_reset = false;
        BST_DBG("request_factory_reset\n");
        bst_connect_options o = prv_instance.options;
        memset(&prv_instance, 0, sizeof(instance_t));

        bst_store_bootstrap_data(NULL, 0);
        bst_store_crypto_secret(NULL, 0);
        bst_setup(o,NULL,0,NULL,0);
        return;
    }

    if (prv_instance.flags.request_bind) {
        prv_instance.flags.request_bind = false;
        prv_instance.state.last_error = STATE_OK;

        BST_DBG("CMD_BIND %s %s\n", prv_instance.crypto_secret, prv_instance.options.bootstrap_ssid);

        bst_store_crypto_secret(prv_instance.crypto_secret, prv_instance.crypto_secret_len);

        // Send response to client
        bst_request_wifi_network_list();
        return;
    }

    if (prv_instance.flags.request_wifi_list) {
        prv_instance.flags.request_wifi_list = false;
        BST_DBG("request_wifi_list\n");
        bst_request_wifi_network_list();
    }

    if (prv_instance.flags.request_set_wifi) {
        bst_store_bootstrap_data(prv_instance.storage, prv_instance.storage_len);
        prv_enter_bootstrapped_mode();
        return;
    }

    time_t currentTime = bst_get_system_time_ms();
    bst_connect_state currentConnectionState = bst_get_connection_state();

    if (prv_instance.state.state == BST_MODE_WAIT_FOR_BOOTSTRAP) {
        if (currentConnectionState == BST_STATE_CONNECTED) {
            // We are in wait-for-bootstrap mode and connected to the app. Refresh session data.
            prv_enter_and_keep_app_session();
        } else {
            // We are not connected and in wait-for-bootstrap mode.
            // Check if it is time to start a new connection attempt.
            if (prv_instance.state.timeout_connecting_bootstrap_app <= currentTime) {
                prv_instance.state.timeout_connecting_bootstrap_app = currentTime + prv_instance.options.timeout_connecting_state_ms;
                // If we are already bootstraped (ssid is known) try to connect to the destination network.
                if (prv_instance.ssid && ++prv_instance.state.count_connection_attempts >= prv_instance.options.retry_connecting_to_bootstrap_network) {
                    prv_enter_bootstrapped_mode();
                } else {
                    // We are not bootstrapped so far. Try to connect to a bootstrap network.
                    bst_connect_to_wifi(prv_instance.options.bootstrap_ssid, prv_instance.options.bootstrap_key, BST_MODE_WAIT_FOR_BOOTSTRAP);
                }
            }
        }
    } else { // == BST_BOOTSTRAPPED_MODE
        // We are in bootstrapped mode. Check the connection to the destination network.
        switch (currentConnectionState)
        {
            case BST_STATE_FAILED_SSID_NOT_FOUND:
                prv_enter_wait_for_bootstrap_mode(STATE_ERROR_WIFI_NOT_FOUND,
                                                  prv_instance.state.error_log_msg?prv_instance.state.error_log_msg:ERR_FAILED_WIFI_NOT_FOUND);
                break;
            case BST_STATE_FAILED_CREDENTIALS_WRONG:
                prv_enter_wait_for_bootstrap_mode(STATE_ERROR_WIFI_CREDENTIALS_WRONG,
                                                  prv_instance.state.error_log_msg?prv_instance.state.error_log_msg:ERR_FAILED_WIFI_CRED);
                break;
            case BST_STATE_FAILED_ADVANCED:
                prv_enter_wait_for_bootstrap_mode(STATE_ERROR_ADVANCED,
                                                  prv_instance.state.error_log_msg?prv_instance.state.error_log_msg:ERR_FAILED_ADVANCED);
                break;
            case BST_STATE_CONNECTED:
                if (!prv_instance.options.need_advanced_connection) {
                    prv_instance.state.error_log_msg = NULL;
                    prv_instance.state.last_error = STATE_OK;
                    break;
                }

                // The advanced connection failed, go back to discover mode after n attempts
                if (prv_instance.state.count_connection_attempts > prv_instance.options.retry_connecting_to_destination_network) {
                    prv_enter_wait_for_bootstrap_mode(STATE_ERROR_ADVANCED,
                                                      prv_instance.state.error_log_msg?prv_instance.state.error_log_msg:ERR_FAILED_ADVANCED);
                    break;
                }

                if (currentTime > prv_instance.state.timeout_connecting_advanced) {
                    ++prv_instance.state.count_connection_attempts;
                    prv_instance.state.timeout_connecting_advanced = prv_instance.options.timeout_connecting_state_ms + currentTime;
                    bst_connect_advanced(prv_instance.additional);
                }
                break;
            case BST_STATE_CONNECTED_ADVANCED:
                prv_instance.state.error_log_msg = NULL;
                prv_instance.state.last_error = STATE_OK;
                break;

            case BST_STATE_CONNECTING:
            default:
                if (currentTime > prv_instance.state.timeout_connecting_destination) {
                    prv_instance.state.timeout_connecting_destination = prv_instance.options.timeout_connecting_state_ms + currentTime;
                    if (++prv_instance.state.count_connection_attempts >= prv_instance.options.retry_connecting_to_destination_network) {
                        prv_enter_wait_for_bootstrap_mode(STATE_ERROR_WIFI_NOT_FOUND,
                                                          prv_instance.state.error_log_msg?prv_instance.state.error_log_msg:ERR_FAILED_WIFI_NOT_FOUND);
                    } else {
                        bst_connect_to_wifi(prv_instance.ssid, prv_instance.pwd, BST_MODE_BOOTSTRAPPED);
                    }
                }
                break;
        }
    }
}

void bst_network_input(const char* data, size_t len)
{
    if (prv_instance.state.state!=BST_MODE_WAIT_FOR_BOOTSTRAP || len < sizeof(bst_udp_receive_pkt_t))
        return;

    bst_udp_receive_pkt_t* pkt = (bst_udp_receive_pkt_t*)data;
    if (!prv_check_header_and_decrypt(pkt, len)) {
      return;
    }

    time_t current_time = bst_get_system_time_ms();

    switch(pkt->command_code) {
        case CMD_HELLO: {
            bst_udp_hello_receive_pkt_t* pkt_hello = (bst_udp_hello_receive_pkt_t*)data;
            if (len < sizeof(bst_udp_hello_receive_pkt_t))
                return;

            // If there is a valid app session on going, check if the HELLO message sends
            // the same app nonce. Reset the session timeout in this case.
            if (current_time <= prv_instance.state.timeout_app_session) {
                if (memcmp(prv_instance.state.prv_app_nonce ,pkt_hello->app_nonce, BST_NONCE_SIZE)==0) {
                    prv_instance.state.timeout_app_session = prv_instance.options.timeout_nonce_ms + current_time;
                    prv_instance.flags.request_wifi_list = true;
                }
            } else { // No session? Start a new one
                prv_instance.state.timeout_app_session = prv_instance.options.timeout_nonce_ms + current_time;
                prv_instance.state.time_nonce_valid = 0;
                prv_enter_and_keep_app_session();
                prv_instance.flags.request_wifi_list = true;
            }
            break;
        }
        case CMD_BIND: {
            // Exit if there is no app session opened
            if (current_time > prv_instance.state.timeout_app_session) {
                return;
            }
            bst_udp_bind_receive_pkt_t* pkt_bind = (bst_udp_bind_receive_pkt_t*)data;
            if (len < sizeof(bst_udp_bind_receive_pkt_t))
                return;

            if (prv_instance.flags.request_bind)
                break;

            memcpy(prv_instance.crypto_secret, pkt_bind->new_bind_key, pkt_bind->new_bind_key_len);
            prv_instance.crypto_secret_len = pkt_bind->new_bind_key_len;
            prv_instance.flags.request_bind = true;

            break;
        }
        case CMD_SET_DATA: {
            // Exit if there is no app session opened
            if (current_time > prv_instance.state.timeout_app_session) {
                return;
            }

            bst_udp_bootstrap_receive_pkt_t* pkt_bst_data = (bst_udp_bootstrap_receive_pkt_t*)data;
            if (len < sizeof(bst_udp_bootstrap_receive_pkt_t))
                return;

            if (prv_instance.flags.request_set_wifi)
                break;

            BST_DBG("net: CMD_SET_DATA");
            prv_assign_data(pkt_bst_data->bootstrap_data, len-sizeof(bst_udp_receive_pkt_t));

            prv_instance.flags.request_set_wifi = true;
            break;
        }

        default:
            BST_DBG("net: UNKNOWN cmd %d", pkt->command_code);
            break;
    }
}

void bst_wifi_network_list(bst_wifi_list_entry_t* list)
{
    if (prv_instance.state.state != BST_MODE_WAIT_FOR_BOOTSTRAP)
        return;

    // Create buffer that looks like this:
    // 0: list size
    // 1: strength of first wifi
    // 2..x: ssid of first wifi with training 0.
    // x+1: strength of second wifi
    // x+2..y: ssid of second wifi with training 0.
    // ...

    uint8_t wifi_list_entries = 0;
    uint8_t wifi_list_size_in_bytes = 0;
    bst_wifi_list_entry_t* it = list;
    while (it) {
        wifi_list_size_in_bytes += strlen(it->ssid) + 1 + 1 + 1; // trailing 0 + wifi strength in percent + enc mode
        it = it->next;
        ++wifi_list_entries;
    }

    size_t log_message_len = prv_instance.state.error_log_msg ? strlen(prv_instance.state.error_log_msg)+1 : 0;

    size_t total_len = sizeof(bst_udp_send_pkt_t)-BST_STORAGE_RAM_SIZE+wifi_list_size_in_bytes+log_message_len;

    char buffer[total_len];
    memset(buffer, 0, total_len);

    bst_udp_send_pkt_t* p = (bst_udp_send_pkt_t*)buffer;
    prv_add_header(p);
    p->wifi_list_entries = wifi_list_entries;
    p->wifi_list_size_in_bytes = wifi_list_size_in_bytes;
    char* bufferP = p->data_wifi_list_and_log_msg;

    it = list;
    while (it) {

        // strength +  enc mode
        bufferP[0] = it->strength_percent;
        bufferP[1] = it->encryption_mode;
        bufferP += 2;

        // ssid
        size_t ssid_len = strlen(it->ssid);
        memcpy(bufferP, it->ssid, ssid_len);
        bufferP += ssid_len;

        // trailing 0
        bufferP[0] = 0;
        ++bufferP;

        it = it->next;
    }

    BST_DBG("request_log %s\n", prv_instance.state.error_log_msg);
    if (prv_instance.state.error_log_msg) {
        memcpy(bufferP, prv_instance.state.error_log_msg, log_message_len);
    }

    bst_network_output(buffer, total_len);
}


void bst_set_error_message(const char* mesg)
{
    prv_instance.state.error_log_msg = mesg;
}

void bst_factory_reset()
{
    prv_instance.flags.request_factory_reset = true;
}
