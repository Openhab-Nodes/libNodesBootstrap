#include "bootstrapWifi.h"
#include "prv_bootstrapWifi.h"
#include "spritz.h"
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
bst_crc_value bst_crc16(const unsigned char *pData, uint16_t size)
{
    uint16_t wCrc = 0xffff;

    while(size) {
        wCrc ^= *pData++ << 8;
        for (uint8_t i=0; i < 8; i++)
            wCrc = (wCrc & 0x8000) ? ((wCrc << 1) ^ CRC16) : (wCrc << 1);
        --size;
    }

    bst_crc_value v;
    v.crc[1] = wCrc & 0xff;
    v.crc[0] = (wCrc>>8) & 0xff;
    return v;
}

STATIC_INLINE bool prv_crc16_is_valid(bst_udp_receive_pkt_t* pkt, size_t pkt_len) {
    // Do not take the header, command and crc field into account for crc calculation.
    const size_t offset = sizeof(bst_udp_receive_pkt_t);
    bst_crc_value v = bst_crc16(((unsigned char*)pkt)+offset, pkt_len-offset);
    return memcmp(&(v.crc),&(pkt->crc),sizeof(bst_crc_value)) == 0;
}

static bool prv_is_app_session_valid() {
    bool valid = prv_instance.state.time_nonce_valid >= bst_get_system_time_ms();
    if (!valid) {
        prv_instance.state.time_nonce_valid = 0;
        memset(prv_instance.state.prv_app_nonce,0,BST_NONCE_SIZE);
        memset(prv_instance.state.prv_device_nonce,0,BST_NONCE_SIZE);
    }
    return valid;
}

/**
 * @brief Called if we receive a HELLO packet from a bootstrap app.
 * This either starts a new app session if no one is active at the time and uses the given app_nonce
 * or compares the given app_nonce to the internally stored one. If it is the
 * same app communicating with us, renew the device nonce and return true.
 * For security reasons we do nothing and return false if there is an app session
 * ongoing but the "new" app_none differs from the stored one. In that case a bootstrap
 * app has to wait for the session timeout (time_nonce_valid).
 * @param app_nonce
 */
static inline bool prv_enter_and_keep_app_session(const char* app_nonce) {
    time_t current_time = bst_get_system_time_ms();
    bool valid;

    // Start a new session with a new device nonce.
    if (!prv_instance.state.time_nonce_valid || prv_instance.state.time_nonce_valid <= current_time)
    {
        memcpy(prv_instance.state.prv_app_nonce, app_nonce, BST_NONCE_SIZE);
        valid = true;
    } else
        valid = memcmp(prv_instance.state.prv_app_nonce, app_nonce, BST_NONCE_SIZE) == 0;

    if (valid)
    {
        // Renew device nonce on every call to this method.
        prv_instance.state.time_nonce_valid = prv_instance.options.timeout_nonce_ms + current_time;
        uint64_t* p = (uint64_t*)prv_instance.state.prv_device_nonce;
        for (unsigned i=0;i<BST_NONCE_SIZE/8;++i) {
             p[i] = bst_get_random();
        }
    }

    return valid;
}

/**
 * @brief Return true if the header equals BST_NETWORK_HEADER and the crc value
 * is correct after decryption with the prv_instance.crypto_secret
 * and the device nonce (prv_instance.state.prv_device_nonce).
 * @param pkt The packet to decrypt and check.
 * @param pkt_len The packet length.
 * @return
 */
STATIC_INLINE bool prv_check_header_and_decrypt(bst_udp_receive_pkt_t* pkt, size_t pkt_len)
{
    const char hdr[] = BST_NETWORK_HEADER;
    if (memcmp(pkt->hdr, hdr, BST_NETWORK_HEADER_SIZE) != 0) {
      BST_DBG("Header wrong\n");
      return false;
    }

    // decrypt. HELLO packets are not encrypted
    if (pkt->command_code != CMD_HELLO)
    {
        const size_t offset = sizeof(bst_udp_receive_pkt_t);

        unsigned char* out_in = (unsigned char*)pkt+offset;
        spritz_decrypt(out_in,out_in,pkt_len-offset,
                       (unsigned char*)prv_instance.state.prv_device_nonce,BST_NONCE_SIZE,
                       (unsigned char*)prv_instance.crypto_secret,prv_instance.crypto_secret_len);
    }

    // Check crc16
    return prv_crc16_is_valid(pkt, pkt_len);
}

/**
 * @brief Compute a checksum for the content and encrypt it with the prv_instance.crypto_secret
 * and the app nonce (prv_instance.state.prv_app_nonce).
 * We compute the checksum and encrypt the content only and skip the header and the command field.
 * @param pkt The packet to encrypt.
 * @param pkt_len The packet length.
 */
STATIC_INLINE void prv_add_checksum_and_encrypt(bst_udp_send_pkt_t* pkt, size_t pkt_len)
{
    // bst_udp_receive_pkt_t consists only of the header, the command and the crc field.
    // We therefor use its size for the offset. bst_udp_send_pkt_t uses the same structure.
    const size_t offset = sizeof(bst_udp_receive_pkt_t);

    pkt_len -= offset;

    pkt->crc = bst_crc16((unsigned char*)pkt+offset, pkt_len);

    // encrypt
    unsigned char* out_in = (unsigned char*)pkt+offset;
    spritz_encrypt(out_in,out_in, pkt_len,
                   (unsigned char*)prv_instance.state.prv_app_nonce,BST_NONCE_SIZE,
                   (unsigned char*)prv_instance.crypto_secret,prv_instance.crypto_secret_len);
}

/**
 * @brief Add the BST_NETWORK_HEADER header and device nonce to the given packet.
 * You have to call prv_add_checksum_and_encrypt() after adding the content to the packet.
 * @param pkt The packet.
 */
STATIC_INLINE void prv_add_header(bst_udp_send_pkt_t* pkt)
{
    const char hdr[] = BST_NETWORK_HEADER;
    memcpy((char*)pkt->hdr, hdr, sizeof(BST_NETWORK_HEADER)-1);
    pkt->state_code = prv_instance.state.last_error;
    memcpy(pkt->uid, prv_instance.options.unique_device_id, BST_UID_SIZE);
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
    } else if (prv_instance.options.initial_crypto_secret) {
        memcpy(prv_instance.crypto_secret, prv_instance.options.initial_crypto_secret, prv_instance.options.initial_crypto_secret_len);
        prv_instance.crypto_secret_len = prv_instance.options.initial_crypto_secret_len;
    }

    if (prv_instance.ssid) {
        // External confirmation is only required the first time. We are
        // bootstrapped already, so disable external confirmation.
        if (prv_instance.options.external_confirmation_mode == BST_CONFIRM_REQUIRED_FIRST_START)
            prv_instance.options.external_confirmation_mode = BST_CONFIRM_NOT_REQUIRED;
        // Go into bootstrapped mode and connect to the destination network.
        prv_enter_bootstrapped_mode();
    } else if (prv_instance.options.bootstrap_ssid)
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
    prv_instance.state.state = BST_MODE_CONNECTING_TO_BOOTSTRAP;
    prv_instance.state.timeout_connecting_bootstrap_app = bst_get_system_time_ms() + prv_instance.options.timeout_connecting_state_ms;

    bst_connect_to_wifi(prv_instance.options.bootstrap_ssid, prv_instance.options.bootstrap_key);
}

/**
 * Sends an unencrypted message to the app with just a state byte.
 * @param state
 */
static void prv_send_message(prv_bst_error_state state) {
    bst_udp_send_hello_pkt_t p;
    const char hdr[] = BST_NETWORK_HEADER;
    memcpy((char*)p.hdr, hdr, sizeof(BST_NETWORK_HEADER)-1);
    p.state_code = state;
    bst_network_output((const char*)&p, sizeof(bst_udp_send_hello_pkt_t));
}

/**
 * Try to connect to the destination network with the help of the bootstrap data.
 * A timeout of timeout_connecting_state_ms will cancel the attempt and reenter
 * bootstrap mode instead.
 */
static void prv_enter_bootstrapped_mode()
{
    // Reset connection+flags state
    memset(&(prv_instance.flags), 0, sizeof(prv_instance.flags));
    memset(&(prv_instance.state), 0, sizeof(prv_instance.state));
    prv_instance.state.state = BST_MODE_CONNECTING_TO_DEST;
    prv_instance.state.timeout_connecting_destination = bst_get_system_time_ms() + prv_instance.options.timeout_connecting_state_ms;

    bst_connect_to_wifi(prv_instance.ssid, prv_instance.pwd);
}

void bst_periodic()
{
    if (!prv_instance.options.bootstrap_ssid || !prv_instance.options.initial_crypto_secret)
        return;

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
        return;
    }

    if (prv_instance.flags.request_set_wifi) {
        prv_send_message(STATE_BOOTSTRAP_OK);
        bst_store_bootstrap_data(prv_instance.storage, prv_instance.storage_len);
        prv_enter_bootstrapped_mode();
        return;
    }

    time_t currentTime = bst_get_system_time_ms();
    bst_connect_state currentConnectionState = bst_get_connection_state();

    switch (prv_instance.state.state) {
    case BST_MODE_CONNECTING_TO_BOOTSTRAP:
        if (currentConnectionState == BST_STATE_CONNECTED ||
                currentConnectionState == BST_STATE_CONNECTED_ADVANCED) {
            prv_instance.state.state = BST_MODE_WAITING_FOR_DATA;
            // Notify the user that we have a bootstrap connection now.
            bst_connected_to_bootstrap_network();
            // Send HELLO message to notify the bootstrap app that we are online and ready
            // This is not necessary because the app will detect the device anyway
            // by periodically requesting neighbour wifi lists from but will fasten up things.
            prv_send_message(STATE_HELLO);
            // No break here, we go straight to the next switch state
        } else {
            // Check if it is time to start a new connection attempt.
            if (prv_instance.state.timeout_connecting_bootstrap_app > currentTime)
                break;
            prv_instance.state.timeout_connecting_bootstrap_app = currentTime + prv_instance.options.timeout_connecting_state_ms;

            // We are not connected and in wait-for-bootstrap mode.
            if (!prv_instance.ssid ||
                    ++prv_instance.state.count_connection_attempts <= prv_instance.options.retry_connecting_to_bootstrap_network)
            { // We are not bootstrapped so far. Try to connect to a bootstrap network.
                bst_connect_to_wifi(prv_instance.options.bootstrap_ssid,
                                    prv_instance.options.bootstrap_key);
            } else {
                // If we are already bootstrapped (ssid is known)
                // and we tried count_connection_attempts times to reach the
                // bootstrap network, try to connect to the destination network instead.
                prv_enter_bootstrapped_mode();
            }
            break;
        }
    case BST_MODE_WAITING_FOR_DATA:
        // We lost the connection, change the internal state accordingly.
        if (currentConnectionState != BST_STATE_CONNECTED) {
            prv_instance.state.state = BST_MODE_CONNECTING_TO_BOOTSTRAP;
            break;
        }

        // Check if it is time to timeout waiting for data.
        if (prv_instance.state.timeout_connecting_bootstrap_app > currentTime)
            break;
        prv_instance.state.timeout_connecting_bootstrap_app = currentTime + prv_instance.options.timeout_connecting_state_ms;

        // We are connected to the app which should bootstrap us
        // but no bootstrap session is ongoing. The app should not
        // be able to keep the device from connecting to the already
        // stored destination SSID. Therefore we enter the bootstrapped
        // mode now.
        if (prv_instance.ssid && !prv_is_app_session_valid())
            prv_enter_bootstrapped_mode();

        break;
    case BST_MODE_CONNECTING_TO_DEST:
        if (currentConnectionState == BST_STATE_CONNECTED ||
                currentConnectionState == BST_STATE_CONNECTED_ADVANCED) {
            prv_instance.state.state = BST_MODE_DESTINATION_CONNECTED;

            if (prv_instance.options.need_advanced_connection) {
                // Start an advanced connection immediatelly after the wireless
                // connection has been established, without waiting for a timeout.
                prv_instance.state.timeout_connecting_advanced = 0;
                prv_instance.state.count_connection_attempts = 0;
                prv_instance.state.error_log_msg = NULL;
                prv_instance.state.last_error = STATE_OK;
            }
            // No break here, we go straight to the next switch state
        } else if (currentTime > prv_instance.state.timeout_connecting_destination) {
            prv_instance.state.timeout_connecting_destination = prv_instance.options.timeout_connecting_state_ms + currentTime;
            if (++prv_instance.state.count_connection_attempts >= prv_instance.options.retry_connecting_to_destination_network) {
                prv_enter_wait_for_bootstrap_mode(STATE_ERROR_WIFI_NOT_FOUND,
                                                  prv_instance.state.error_log_msg?prv_instance.state.error_log_msg:ERR_FAILED_WIFI_NOT_FOUND);
            } else {
                bst_connect_to_wifi(prv_instance.ssid, prv_instance.pwd);
            }
            break;
        }
    case BST_MODE_DESTINATION_CONNECTED:
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
                if (prv_instance.options.need_advanced_connection) {
                    // The advanced connection failed, go back to discover mode after n attempts
                    if (prv_instance.state.count_connection_attempts > prv_instance.options.retry_connecting_to_destination_network) {
                        prv_enter_wait_for_bootstrap_mode(STATE_ERROR_ADVANCED,
                                                          prv_instance.state.error_log_msg?prv_instance.state.error_log_msg:ERR_FAILED_ADVANCED);
                        break;
                    }

                    if (currentTime >= prv_instance.state.timeout_connecting_advanced) {
                        ++prv_instance.state.count_connection_attempts;
                        prv_instance.state.timeout_connecting_advanced = prv_instance.options.timeout_connecting_state_ms + currentTime;
                        bst_connect_advanced(prv_instance.additional);
                    }
                }
                break;
            case BST_STATE_CONNECTED_ADVANCED:
                prv_instance.state.error_log_msg = NULL;
                prv_instance.state.last_error = STATE_OK;
                break;

            case BST_STATE_CONNECTING:
            case BST_STATE_NO_CONNECTION:
            default:
                // We lost the connection, change the internal state accordingly.
                prv_instance.state.state = BST_MODE_CONNECTING_TO_DEST;
                break;
        } // end switch(currentConnectionState)
        break;
    default:
        break;
    } // end switch(prv_instance.state.state)
}

void bst_network_input(const char* data, size_t len)
{
    if (prv_instance.state.state!=BST_MODE_WAITING_FOR_DATA ||
            len < sizeof(bst_udp_receive_pkt_t)) {
        BST_DBG("net: too short\n");
        return;
    }

    bst_udp_receive_pkt_t* pkt = (bst_udp_receive_pkt_t*)data;
    if (!prv_check_header_and_decrypt(pkt, len)) {
        BST_DBG("net: crc wrong\n");
        #ifdef BST_DEBUG
        const size_t offset = sizeof(bst_udp_receive_pkt_t);
        bst_crc_value crc = bst_crc16(((unsigned char*)pkt)+offset, len-offset);
        BST_DBG("net: crc wrong. len(%d), offset(%d), comp(%d.%d), given(%d.%d)\n",
                len-offset, offset,
                crc.crc[0], crc.crc[1], pkt->crc.crc[0], pkt->crc.crc[1]);
        for (int i=0;i<len;++i)
            BST_DBG("%c(%d)", data[i], (unsigned)data[i]);
        BST_DBG("\n");
        #endif
      return;
    }

    switch(pkt->command_code) {
        case CMD_HELLO: {
            bst_udp_hello_receive_pkt_t* pkt_hello = (bst_udp_hello_receive_pkt_t*)data;
            if (len < sizeof(bst_udp_hello_receive_pkt_t)) {
                BST_DBG("net: hello too short\n");
                return;
            }

            // To protect from DOS we do not accept rapidly changing app_nonces.
            // Keep your app session for at least 1min.
            if (prv_enter_and_keep_app_session(pkt_hello->app_nonce)) {
                // A new session is opened or the current session is renewed (new device nonce).
                // Send the wifi list as response to the app now.
                prv_instance.flags.request_wifi_list = true;
            } else {
                BST_DBG("net: hello. no app session\n");
            }
            break;
        }
        case CMD_BIND: {
            // Exit if there is no app session opened
            if (!prv_is_app_session_valid()) {
                BST_DBG("net: no app session\n");
                return;
            }
            bst_udp_bind_receive_pkt_t* pkt_bind = (bst_udp_bind_receive_pkt_t*)data;
            if (len < sizeof(bst_udp_bind_receive_pkt_t)) {
                BST_DBG("net: bind too short\n");
                return;
            }

            if (prv_instance.flags.request_bind)
                break;

            memcpy(prv_instance.crypto_secret, pkt_bind->new_bind_key, pkt_bind->new_bind_key_len);
            prv_instance.crypto_secret_len = pkt_bind->new_bind_key_len;
            prv_instance.flags.request_bind = true;

            break;
        }
        case CMD_SET_DATA: {
            // Exit if there is no app session opened
            if (!prv_is_app_session_valid()) {
                BST_DBG("net: no app session\n");
                return;
            }

            bst_udp_bootstrap_receive_pkt_t* pkt_bst_data = (bst_udp_bootstrap_receive_pkt_t*)data;
            if (len < sizeof(bst_udp_bootstrap_receive_pkt_t)) {
                BST_DBG("net: setdata too short\n");
                return;
            }

            if (prv_instance.options.external_confirmation_mode != BST_CONFIRM_NOT_REQUIRED &&
                    !prv_instance.flags.external_confirmation) {
                BST_DBG("net: setdata confirmation missing\n");
                return;
            }

            if (prv_instance.flags.request_set_wifi)
                break;

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
    if (prv_instance.state.state != BST_MODE_WAITING_FOR_DATA)
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

    // If no error (state == STATE_OK) use the log field for the device name.
    size_t log_message_len = prv_instance.state.last_error != STATE_OK && prv_instance.state.error_log_msg ?
                strlen(prv_instance.state.error_log_msg)+1 : strlen(prv_instance.options.name);

    // We always send a fixed size packet to not reveal anything about nearby networks.
    // The downside: We may not cover all available networks with this packet.
    bst_udp_send_pkt_t p;
    memset(&p, 0, sizeof(bst_udp_send_pkt_t));
    prv_add_header(&p);
    char* bufferP = p.data_wifi_list_and_log_msg;
    char* endP = bufferP + sizeof(p.data_wifi_list_and_log_msg);

    it = list;
    while (it) {
        size_t ssid_len = strlen(it->ssid);
        if (bufferP+ssid_len+3>endP)
            break;

        if (memcmp(it->ssid, prv_instance.options.bootstrap_ssid, ssid_len)==0) {
            it = it->next;
            continue;
        }

        ++wifi_list_entries;
        wifi_list_size_in_bytes += ssid_len + 2 + 1; // trailing 0 + wifi strength in percent + enc mode

        // strength +  enc mode
        bufferP[0] = it->strength_percent;
        bufferP[1] = it->encryption_mode;
        bufferP += 2;

        // ssid
        memcpy(bufferP, it->ssid, ssid_len);
        bufferP += ssid_len;

        // trailing 0
        bufferP[0] = 0;
        ++bufferP;

        it = it->next;
    }

    if (prv_instance.options.external_confirmation_mode == BST_CONFIRM_NOT_REQUIRED)
        p.external_confirmation_state = CONFIRM_NOT_REQUIRED;
    else
        p.external_confirmation_state =
                prv_instance.flags.external_confirmation ? CONFIRM_OK : CONFIRM_REQUIRED;

    p.wifi_list_entries = wifi_list_entries;
    p.wifi_list_size_in_bytes = wifi_list_size_in_bytes;

    if (bufferP+log_message_len<endP)
    {
        if (prv_instance.state.last_error != STATE_OK && prv_instance.state.error_log_msg) {
            BST_DBG("log %s\n", prv_instance.state.error_log_msg);
            memcpy(bufferP, prv_instance.state.error_log_msg, log_message_len);
        } else {
            memcpy(bufferP, prv_instance.options.name, log_message_len);
        }
    }

    prv_add_checksum_and_encrypt(&p, sizeof(bst_udp_send_pkt_t));
    bst_network_output((const char*)&p, sizeof(bst_udp_send_pkt_t));
}


void bst_set_error_message(const char* mesg)
{
    prv_instance.state.error_log_msg = mesg;
}

void bst_factory_reset()
{
    prv_instance.flags.request_factory_reset = true;
}

bst_state bst_get_state()
{
    return prv_instance.state.state;
}

void bst_confirm_bootstrap()
{
    prv_instance.flags.external_confirmation = 1;
}
