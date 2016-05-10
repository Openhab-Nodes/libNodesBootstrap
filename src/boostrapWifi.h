#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BST_DEBUG
#include <stdio.h>
void bst_printf(const char * format, ...);
#define BST_DBG(...) bst_printf(__VA_ARGS__)
#else
#define BST_DBG(...)
#endif

typedef enum {
    BST_DISCOVER_MODE,            ///< no wifi data
    BST_FAILED_SSID_NOT_FOUND,    ///< wifi credentials, but not in range
    BST_FAILED_CREDENTIALS_WRONG, ///< wifi pwd does not work
    BST_FAILED_ADVANCED,          ///< wifi works but the bootstrap information
                                  ///  for the advanced connection is not correct
    BST_CONNECTED = 10,           ///< Connected to wifi
    BST_CONNECTED_ADVANCED,       ///< Only if applicable: Advanced connection established
    BST_CONNECTING,               ///< Currently connecting

} bst_connect_state;

typedef struct _bst_connect_options_
{
    /// Device name. This will be part of the access point name.
    const char* name;

    /// Unique id of the device as ascii c-string (usually the MAC address)
    const char* unique_device_id;

    /// The app will use this as password for the wifi access point of this device
    /// if this device has not been bootstraped before. During bootstraping the app will
    /// replace the secret with another one. This is necessary, because the device will
    /// go into bootstrap mode if it loses the wifi connection and only the initial
    /// app should be able to change wifi credentials. If bst_factory_reset() is called
    /// the secret is reseted.
    const char* factory_app_secret;

    /// If the connection can not be established in the given time in ms,
    /// the state changes to BST_DISCOVER_MODE. A reconnection attempt
    /// will be made every "interval_try_again_ms". Usually this mechanism should
    /// never kick in because the wifi stack should connect or fail with
    /// BST_FAILED_SSID_NOT_FOUND or BST_FAILED_CREDENTIALS_WRONG before.
    int timeout_connecting_state_ms;

    /// If ssid and password are known but the connection cannot be established or is lost
    /// (BST_FAILED_SSID_NOT_FOUND, BST_DISCOVER_MODE), the library will try again
    /// by calling `bst_connect_to_wifi` periodically with the given interval in ms.
    /// If **need_advanced_connection** is set and the advanced condition is not met
    /// (BST_CONNECTED instead of BST_CONNECTED_ADVANCED) the method `bst_connect_advanced`
    /// will be called instead.
    int interval_try_again_ms;

    /// If this is set to true, the connection is only considered established if you
    /// return BST_CONNECTED_ADVANCED in bst_connection_state().
    /// This is useful if you additionally need a specific server connection for succeding
    /// the bootstrap process, for example.
    bool need_advanced_connection;

    /// If the library cannot establish the advanced connection (BST_CONNECTED_ADVANCED)
    /// after this many attempts or if BST_FAILED_ADVANCED is returned by bst_connection_state(),
    /// the wifi connection will be droped and the state will transit to BST_DISCOVER_MODE.
    uint8_t retry_advanced_connection;
} bst_connect_options;

typedef struct bst_wifi_list_entry {
    const char* ssid;
    uint8_t strength_percent;
    uint8_t encryption_mode; // 0 not encrypted, 1: WEP, 2: WPA
    struct bst_wifi_list_entry* next;
} bst_wifi_list_entry_t;

/**
 * @brief Boostrap setup routine
 * @param options Configure the boostrap module
 * @param stored_data The data you have stored from the bst_store_data callback or NULL.
 * @param stored_data_len The data length or 0.
 */
void bst_setup(bst_connect_options options, const char* stored_data, size_t stored_data_len);

/**
 * @brief Call this periodically. The internal state machine and timeouts are managed in here.
 */
void bst_periodic();

/**
 * @brief Forward udp traffic from any udp client of port 8711 to this method.
 * This method will not result in any method callback but will only setup some flags
 * and copy data. The real work will be done in bst_periodic(). This method can be called
 * from another thread.
 *
 * @param data The udp payload
 * @param len The payload length
 */
void bst_network_input(const char* data, size_t len);

/**
 * @brief Call this with neighbour wireless networks as a response for a bst_request_wifi_network_list() call.
 *
 * Usually you call this as a response due to a former request from bst_request_wifi_network_list().
 * Send a list of wifis in range to udp port 8711 via bst_network_output().
 * @param list The list of networks. This can be freed after the method returns.
 */
void bst_wifi_network_list(bst_wifi_list_entry_t* list);

/**
 * @brief Remove all bootstrap data (wifi credentials + additional data) and
 * start bootstraping process.
 */
void bst_factory_reset();

///// Implement the following methods /////

/**
 * @brief Outgoing network traffic for udp port 8711 to be broadcasted
 * @param data Payload data. Will always be less than the maximum udp packet size and the common default MTU of 1400.
 * @param data_len Payload length
 */
void bst_network_output(const char* data, size_t data_len);

/**
 * @brief bst_get_connection_state
 * @return Return your current wifi connection state.
 */
bst_connect_state bst_get_connection_state();

/// Close the udp socket on port 8711.
/// SSID and password are known, connect now. Return CONNECTING in bst_get_connection_state().
/// If the connection failed change the state you return in bst_connection_state() to
/// FAILED_CREDENTIALS_WRONG or FAILED_SSID_NOT_FOUND state.
/**
 * @brief bst_connect_to_wifi
 * Either the library connects to the bootstrap wireless access point or to the destination wifi.
 * If a connection to the bootstrap network has been established, the udp socket on port 8711 will be opened
 * and is waiting for commands.
 * If the connection failed change the state you return in bst_connection_state() to
 * FAILED_CREDENTIALS_WRONG or FAILED_SSID_NOT_FOUND state.
 * @param ssid
 * @param pwd
 */
void bst_connect_to_wifi(const char* ssid, const char* pwd);

/// If you need to bootstrap not only the wifi connection but for example also
/// need to connect to a server, you may set the **need_advanced_connection** option.
/// After a successful wifi connection this method will be called with the additional data the app provided.
void bst_connect_advanced(const char* data);

/**
 * @brief bst_request_wifi_network_list
 * The app requests a list of wifi networks in range.
 * Call bst_wifi_network_list(network_list_start) asynchronously if you gathered that data.
 */
void bst_request_wifi_network_list();

/**
 * @brief Store the data blob with the given length.
 * Provide this data to `bst_setup` on boot. This is called when new wifi credentials
 * arrive.
 * @param data
 * @param data_len
 */
void bst_store_data(char* data, size_t data_len);

/**
 * @brief Set error message.
 * The message is not copyied, keep the
 * memory at least as long until you call the method again.
 * @param mesg
 */
void bst_set_error_message(const char* mesg);

/**
 * @brief Return the ms system timer. This is used to measure timeouts.
 * @return
 */
time_t bst_get_system_time_ms();

#ifdef __cplusplus
}
#endif
