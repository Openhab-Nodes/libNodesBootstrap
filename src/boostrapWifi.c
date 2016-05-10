#include "boostrapWifi.h"
#include "prv_boostrapWifi.h"
#include <string.h>

const char* ERR_FAILED_ADVANCED = "Failed to connect to advanced";
const char* ERR_FAILED_WIFI_CRED = "WiFi Credentials wrong";
const char* ERR_FAILED_WIFI_NOT_FOUND = "WiFi not found";

instance_t prv_instance;

static inline uint16_t prv_next_session_id() {
    prv_instance.state.prv_session_id = bst_get_system_time_ms() % 65000 + 135;
    return prv_instance.state.prv_session_id;
}

static inline uint16_t ntohs(uint16_t i) {
  char* d = (char*)&i;
  return (d[1] << 8) | (d[0]);
}

static inline uint16_t htons(uint16_t i) {
  uint16_t v;
  char* d = (char*)&v;
  d[0] = i & 255;
  d[1] = (i >> 8) & 255;
  return v;
}

static inline bool prv_check_header(bst_udp_receive_pkt_t* pkt)
{
    const char hdr[] = "BSTwifi1";
    if (memcmp(pkt->hdr, hdr, 8) != 0) {
      BST_DBG("Header wrong\n");
      return false;
    }

    if (pkt->command_code != CMD_HELLO &&
      (prv_instance.state.prv_session_id==0 || ntohs(pkt->session_id) !=  prv_instance.state.prv_session_id)) {
      BST_DBG("Session expect %d, got %d\n", prv_instance.state.prv_session_id, ntohs(pkt->session_id));
      return false;
    }

    return true;
}

static inline void prv_add_header(bst_udp_send_pkt_t* pkt, uint8_t response_code)
{
    const char hdr[] = "BSTwifi1";
    memcpy((char*)pkt->hdr, hdr, 8);
    pkt->response_code = response_code;
}

/// Determine ssid, pwd, additional and ap_mode_pwd pointers
static void prv_assign_data(const char* stored_data, size_t stored_data_len, int data_fields)
{
    char** pointers[4] = {&prv_instance.ssid, &prv_instance.pwd, &prv_instance.additional, &prv_instance.ap_mode_pwd};
    uint8_t set_NULL  = 1+2+4; // =0b00000111; // if no data for a field, set it to NULL except ap_mode_pwd which is set to '\0'

    // data len = max(input len, sizeof prv_instance.storage - 4)
    if (stored_data_len > sizeof(prv_instance.storage) - 4)
        stored_data_len = sizeof(prv_instance.storage) - 4;


    // Move all fields that are not overwritten this time to the correct position
    const char* c = data_fields>=4 ? 0 : *pointers[data_fields];
    size_t bkpLen = 0;
    if (c) {
        for (int i=data_fields; i < 4; ++i) {
            if (!*pointers[i]) continue;
            bkpLen += strlen(*pointers[i])+1;
        }

        memmove(prv_instance.storage+stored_data_len,c, sizeof(prv_instance.storage)-stored_data_len);
    }

    // Copy new data
    memcpy(prv_instance.storage, stored_data, stored_data_len);
    prv_instance.storage_len = stored_data_len + bkpLen;

    // Set rest to 0
    memset(prv_instance.storage+prv_instance.storage_len, 0, sizeof(prv_instance.storage)-prv_instance.storage_len);

    char* dataP = prv_instance.storage;

    for (int i=0; i < 4; ++i) {
        if (!*dataP && (set_NULL & (1 << i))) {
            *pointers[i] = 0;
        } else {
            *pointers[i] = dataP;
        }

        dataP += strlen(dataP)+1;
    }
}

void prv_determine_ap_ssid_name() {
  size_t len;
  char* p = prv_instance.ap_mode_ssid;
  if (prv_instance.ap_mode_pwd &&
    memcmp(prv_instance.ap_mode_pwd, prv_instance.options.factory_app_secret, strlen(prv_instance.options.factory_app_secret))!=0) {
    memcpy(p, "BSTB_", 5); p += 5;
  } else {
    memcpy(p, "BSTF_", 5); p += 5;
  }
  len = prv_instance.options.name ? strlen(prv_instance.options.name) : 0;
  memcpy(p, prv_instance.options.name, len); p += len;
  *p = '_'; ++p;
  len = prv_instance.options.unique_device_id ? strlen(prv_instance.options.unique_device_id) : 0;
  memcpy(p, prv_instance.options.unique_device_id, len); p += len;
  *p = 0;
}

void bst_setup(bst_connect_options options, const char* stored_data, size_t stored_data_len)
{
    // Clear prv_instance and assign options
    memset(&prv_instance, 0, sizeof(instance_t));
    prv_instance.options = options;

    prv_assign_data(stored_data, stored_data_len, 4);

    if (strlen(prv_instance.ap_mode_pwd)==0 && prv_instance.options.factory_app_secret)
        memcpy(prv_instance.ap_mode_pwd, prv_instance.options.factory_app_secret, strlen(prv_instance.options.factory_app_secret)+1);

    prv_determine_ap_ssid_name();

    if (prv_instance.ssid)
        bst_connect_to_wifi(prv_instance.ssid, prv_instance.pwd);
    else
        bst_discover_mode(prv_instance.ap_mode_ssid, prv_instance.ap_mode_pwd);
}

void bst_periodic()
{
    if (prv_instance.flags.request_wifi_list) {
        prv_instance.flags.request_wifi_list = false;
        BST_DBG("request_wifi_list\n");
        bst_request_wifi_network_list();
    }

    if (prv_instance.flags.request_factory_reset) {
        prv_instance.flags.request_factory_reset = false;
        BST_DBG("request_factory_reset\n");
        bst_factory_reset();
    }

    if (prv_instance.flags.request_log) {
        prv_instance.flags.request_log = false;
        BST_DBG("request_log %s\n", prv_instance.state.error_log_msg);
        if (prv_instance.state.error_log_msg) {
            size_t len = strlen(prv_instance.state.error_log_msg)+1;
            char mem[bst_udp_send_pkt_t_len+len];
            bst_udp_send_pkt_t* p = (bst_udp_send_pkt_t*)mem;

            prv_add_header(p, prv_instance.state.last_error_code);
            memcpy(p->data, prv_instance.state.error_log_msg, len);

            bst_network_output((char*)p,bst_udp_send_pkt_t_len+len);
        }
    }

    if (prv_instance.flags.request_access_point) {
        prv_instance.flags.request_access_point = false;
        bst_discover_mode(prv_instance.ap_mode_ssid, prv_instance.ap_mode_pwd);
      }

    if (prv_instance.flags.request_bind) {
        prv_instance.flags.request_bind = false;
        prv_instance.state.last_error_code = RSP_ERROR_UNSPECIFIED;

        BST_DBG("CMD_BIND %s %s\n", prv_instance.ap_mode_ssid, prv_instance.ap_mode_pwd);

        prv_determine_ap_ssid_name();

        // Store new ap mode pwd
        bst_store_data(prv_instance.storage, prv_instance.storage_len);

        // Send response to client
        bst_udp_send_pkt_t p;
        prv_add_header(&p, RSP_BINDING_ACCEPTED);
        bst_network_output((char*)&p,bst_udp_send_pkt_t_len);

        prv_instance.flags.request_access_point = true;
        return;
    }

    if (prv_instance.flags.request_set_wifi) {
        // Reset connection state
        prv_instance.flags.request_set_wifi = false;
        memset(&(prv_instance.state), 0, sizeof(prv_instance.state));

        // Store wifi data
        bst_store_data(prv_instance.storage, prv_instance.storage_len);

        // Send response to client
        bst_udp_send_pkt_t p;
        prv_add_header(&p, RSP_DATA_ACCEPTED);
        bst_network_output((char*)&p,bst_udp_send_pkt_t_len);

        bst_connect_to_wifi(prv_instance.ssid, prv_instance.pwd);
        return;
    }

    time_t currentTime = bst_get_system_time_ms();
    bst_connect_state currentState = bst_get_connection_state();
    switch (currentState)
    {
        case BST_FAILED_SSID_NOT_FOUND:
            prv_instance.state.last = currentState;
            if (!prv_instance.state.error_log_msg)
                prv_instance.state.error_log_msg = ERR_FAILED_WIFI_NOT_FOUND;
            bst_discover_mode(prv_instance.ap_mode_ssid, prv_instance.ap_mode_pwd);
            break;
        case BST_FAILED_CREDENTIALS_WRONG:
            prv_instance.state.last = currentState;
            if (!prv_instance.state.error_log_msg)
                prv_instance.state.error_log_msg = ERR_FAILED_WIFI_CRED;
            prv_instance.state.last = BST_FAILED_CREDENTIALS_WRONG;
            bst_discover_mode(prv_instance.ap_mode_ssid, prv_instance.ap_mode_pwd);
            break;
        case BST_CONNECTING:
            if (prv_instance.state.last != BST_CONNECTING) {
                prv_instance.state.timeout_connecting = prv_instance.options.timeout_connecting_state_ms + currentTime;
                prv_instance.state.last = currentState;
            } else {
                if (currentTime > prv_instance.state.timeout_connecting) {
                    prv_instance.state.last = BST_FAILED_SSID_NOT_FOUND;
                    prv_instance.state.last_error_code = RSP_ERROR_WIFI_NOT_FOUND;
                    bst_discover_mode(prv_instance.ap_mode_ssid, prv_instance.ap_mode_pwd);
                    break;
                }
            }
            break;
        case BST_FAILED_ADVANCED:
            if (!prv_instance.state.error_log_msg) {
                prv_instance.state.error_log_msg = ERR_FAILED_ADVANCED;
            }
            prv_instance.state.last_error_code = RSP_ERROR_ADVANCED;
        case BST_CONNECTED:
            prv_instance.state.last_error_code = RSP_ERROR_UNSPECIFIED;

            if (prv_instance.state.last != BST_CONNECTED) {
                prv_instance.state.timeout_connecting = 0;
                prv_instance.state.last = currentState;
            }

            if (!prv_instance.options.need_advanced_connection) {
                // Done
                prv_instance.state.error_log_msg = NULL;
                break;
            }
            // The advanced connection failed, go back to discover mode after n attempts
            if (prv_instance.state.count_connection_attempts > prv_instance.options.retry_advanced_connection) {
                prv_instance.state.count_connection_attempts = 0;
                prv_instance.state.error_log_msg = ERR_FAILED_ADVANCED;
                bst_discover_mode(prv_instance.ap_mode_ssid, prv_instance.ap_mode_pwd);
                break;
            }

            if (currentTime > prv_instance.state.timeout_connecting) {
                ++prv_instance.state.count_connection_attempts;
                prv_instance.state.timeout_connecting = prv_instance.options.timeout_connecting_state_ms + currentTime;
                bst_connect_advanced(prv_instance.additional);
            }
            break;
        case BST_CONNECTED_ADVANCED:
            prv_instance.state.error_log_msg = NULL;
            prv_instance.state.last = currentState;
            break;
        case BST_DISCOVER_MODE:
            // If we have wifi credentials, try to reconnect every now and then.
            if (!prv_instance.ssid)
                break;

            // But do not do that if we received an app command within the last 60 seconds
            if (prv_instance.state.time_last_access + 60000 > currentTime)
                break;

            if (prv_instance.state.last != BST_DISCOVER_MODE) {
                prv_instance.state.timeout_connecting = prv_instance.options.interval_try_again_ms + currentTime;
                prv_instance.state.last = currentState;
                break;
            }

            if (currentTime > prv_instance.state.timeout_connecting) {
                bst_udp_send_pkt_t p;
                prv_add_header(&p, RSP_DATA_ACCEPTED);
                bst_network_output((char*)&p,bst_udp_send_pkt_t_len);

                bst_connect_to_wifi(prv_instance.ssid, prv_instance.pwd);
                break;
            }

        default:
            break;
    }
}

void bst_network_input(const char* data, size_t len)
{
    bst_udp_receive_pkt_t* pkt = (bst_udp_receive_pkt_t*)data;
    if (len < bst_udp_receive_pkt_t_len)
        return;
    len = len - bst_udp_receive_pkt_t_len;

    if (!prv_check_header(pkt)) {
      return;
    }

    prv_instance.state.time_last_access = bst_get_system_time_ms();

    switch(pkt->command_code) {
        case CMD_HELLO: {
            bst_udp_send_welcome_t p;
            prv_add_header((bst_udp_send_pkt_t*)&p, RSP_WELCOME_MESSAGE);
            prv_instance.state.prv_session_id = prv_next_session_id();
            BST_DBG("New session: %d\n", prv_instance.state.prv_session_id);
            p.session_id = htons(prv_instance.state.prv_session_id);
            bst_network_output((char*)&p,sizeof(bst_udp_send_welcome_t));
            break;
          }
        case CMD_RESET_FACTORY:
            prv_instance.flags.request_factory_reset = true;
            break;
        case CMD_BIND:
            if (len <= 0) {
                bst_udp_send_pkt_t p;
                prv_add_header(&p, RSP_ERROR_BINDING);
                bst_network_output((char*)&p,bst_udp_send_pkt_t_len);
                return;
            }
            if (prv_instance.flags.request_bind)
                break;
            prv_instance.flags.request_bind = true;
            // Copy new access point secret and make sure there is a trailing 0.
            // We can use memcpy here, because ap_mode_pwd is the last field in storage
            memcpy(prv_instance.ap_mode_pwd, pkt->data, len);
            prv_instance.ap_mode_pwd[len] = 0;

            break;
        case CMD_SET_DATA: {
            if (len < 3) {
                bst_udp_send_pkt_t p;
                prv_add_header(&p, RSP_ERROR_BOOTSTRAP_DATA);
                bst_network_output((char*)&p,bst_udp_send_pkt_t_len);
                return;
            }

            if (prv_instance.flags.request_set_wifi)
                break;

            BST_DBG("net: CMD_SET_DATA");
            prv_assign_data(pkt->data, len, 3);

            prv_instance.flags.request_set_wifi = true;
            break;
        }

        case CMD_REQUEST_WIFI:
            prv_instance.flags.request_wifi_list = true;
            break;

        case CMD_REQUEST_ERROR_LOG:
            prv_instance.flags.request_log = true;
            break;
        default:
            BST_DBG("net: UNKNOWN cmd %d", pkt->command_code);
            break;
    }
}

void bst_wifi_network_list(bst_wifi_list_entry_t* list)
{
    // Create buffer that looks like this:
    // 0: list size
    // 1: strength of first wifi
    // 2..x: ssid of first wifi with training 0.
    // x+1: strength of second wifi
    // x+2..y: ssid of second wifi with training 0.
    // ...

    uint8_t c = 0;
    int len = 1; // the first byte is the size of the list
    bst_wifi_list_entry_t* it = list;
    while (it) {
        len += strlen(it->ssid) + 1 + 1 + 1; // trailing 0 + wifi strength in percent + enc mode
        it = it->next;
        ++c;
    }

    char buffer[bst_udp_send_pkt_t_len+len];
    memset(buffer, 0, bst_udp_send_pkt_t_len+len);

    bst_udp_send_pkt_t* p = (bst_udp_send_pkt_t*)buffer;
    prv_add_header(p, RSP_WIFI_LIST);
    char* bufferP = buffer + bst_udp_send_pkt_t_len;

    bufferP[0] = c;
    ++bufferP;

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

    bst_network_output(buffer,bst_udp_send_pkt_t_len+len);
}


void bst_set_error_message(const char* mesg)
{
    prv_instance.state.error_log_msg = mesg;
}

void bst_factory_reset()
{
    bst_connect_options o = prv_instance.options;
    memset(&prv_instance, 0, sizeof(instance_t));

    bst_store_data("\0\0\0\0", 4);
    bst_setup(o,NULL,0);
}

#ifndef BST_NO_DEFAULT_PLATFORM
void __attribute__((weak)) bst_network_output(const char* data, size_t data_len)
{
    (void)data;
    (void)data_len;
}

bst_connect_state __attribute__((weak)) bst_get_connection_state()
{
    return BST_DISCOVER_MODE;
}

void __attribute__((weak)) bst_connect_to_wifi(const char* ssid, const char* pwd)
{
    (void)ssid;
    (void)pwd;
}

void __attribute__((weak)) bst_connect_advanced(const char* data)
{
    (void)data;
}

void __attribute__((weak)) bst_request_wifi_network_list()
{
}

void __attribute__((weak)) bst_store_data(char* data, size_t data_len)
{
    (void)data;
    (void)data_len;
}

time_t __attribute__((weak)) bst_get_system_time_ms()
{
    return 0;
}
#endif
