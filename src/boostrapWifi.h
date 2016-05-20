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
    BST_STATE_NO_CONNECTION,

    BST_STATE_FAILED_SSID_NOT_FOUND,    ///< wifi credentials, but not in range
    BST_STATE_FAILED_CREDENTIALS_WRONG, ///< wifi pwd does not work
    BST_STATE_FAILED_ADVANCED,          ///< wifi works but the bootstrap information
                                  ///  for the advanced connection is not correct
    BST_STATE_CONNECTED = 10,           ///< Connected to wifi
    BST_STATE_CONNECTED_ADVANCED,       ///< Only if applicable: Advanced connection established
    BST_STATE_CONNECTING,               ///< Currently connecting

} bst_connect_state;

typedef enum {
    BST_MODE_UNINITIALIZED,       ///< Before calling bst_setup()
    BST_MODE_WAIT_FOR_BOOTSTRAP,  ///< Wait for bootstrap data
    BST_MODE_BOOTSTRAPPED,        ///< Bootstrapping done

} bst_state;

typedef struct _bst_connect_options_
{
    /// Device name.
    const char* name;

    /// Unique id of the device as ascii c-string (usually the MAC address in hex)
    const char* unique_device_id;

    /// A factory reseted device will use the initial bound key, instead of an
    /// app provided one.
    const char* initial_crypto_secret;
    uint8_t initial_crypto_secret_len;

    const char* bootstrap_ssid;
    const char* bootstrap_key;

    /// If a connection can not be established in the given time in ms,
    /// a reconnection attempt will be made periodically with the given interval in ms.
    ///
    /// This will happen in BST_WAIT_FOR_BOOTSTRAP_MODE as well as in the
    /// BST_BOOTSTRAPPED_MODE mode.
    ///
    /// If **need_advanced_connection** is set and the advanced condition is not met
    /// (BST_CONNECTED instead of BST_CONNECTED_ADVANCED) the method `bst_connect_advanced`
    /// will be called instead.
    ///
    /// A usuall value would be 10s.
    int timeout_connecting_state_ms;

    /// For security reasons, the nonce for encryption should change every now
    /// and then but should be valid for at least 60 sec. The nonce will be always changed
    /// if BST_WAIT_FOR_BOOTSTRAP_MODE is entered.
    int timeout_nonce_ms;

    /// If this is set to true, the connection is only considered established if you
    /// return BST_CONNECTED_ADVANCED in bst_connection_state().
    /// This is useful if you additionally need a specific server connection for succeding
    /// the bootstrap process, for example.
    bool need_advanced_connection;

    /// If the library cannot establish a connection (BST_CONNECTED or BST_CONNECTED_ADVANCED
    /// if required) after this many attempts or if BST_FAILED_* is returned by bst_connection_state(),
    /// the wifi connection will be dropped and the state will transit to BST_WAIT_FOR_BOOTSTRAP_MODE.
    ///
    /// Usually this should be set to ~7. timeout_connecting_state_ms*retry_connecting is the time
    /// for the library to detect if the bootstrapped network reappeared.
    uint8_t retry_connecting_to_bootstrap_network;
    uint8_t retry_connecting_to_destination_network;
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
void bst_setup(bst_connect_options options, const char* bst_data, size_t bst_data_len, const char* bound_key, size_t bound_key_len);

/**
 * @brief Call this periodically. The internal state machine and timeouts are managed in here.
 * No outgoing network traffic will be generated in here. Incoming network commands are always
 * processed in this method.
 */
void bst_periodic();

/**
 * @brief Forward udp traffic from any udp client of port 8711 to this method.
 * This method will not result in any method callback but will only setup some flags
 * and copy data. The real work will be done in bst_periodic(). This method can be called
 * from another thread.
 *
 * Not reentrance safe.
 *
 * @param data The udp payload
 * @param len The payload length
 */
void bst_network_input(const char* data, size_t len);

/**
 * @brief Call this with neighbour wireless networks as a response for a bst_request_wifi_network_list() call.
 *
 * You should only call this as a response due to a former request from bst_request_wifi_network_list().
 * Will send a list of wifis in range to udp port 8711 via bst_network_output().
 * @param list The list of networks. This can be freed after the method returns. This can be NULL.
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

/**
 * @brief bst_connect_to_wifi
 * Either the library connects to the bootstrap wireless access point or to the destination wifi.
 * If a connection to the bootstrap network has been established, an udp socket on port 8711 have to be opened.
 * If the connection failed change the state you return in bst_connection_state() to
 * FAILED_CREDENTIALS_WRONG or FAILED_SSID_NOT_FOUND state.
 * @param ssid
 * @param pwd
 */
void bst_connect_to_wifi(const char* ssid, const char* pwd, bst_state state);

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
 * @brief Store the data blobs with the given lengths.
 * Provide this data to `bst_setup` on boot. This is called when new wifi credentials arrive.
 * @param data
 * @param data_len
 */
void bst_store_bootstrap_data(char* bst_data, size_t bst_data_len);

/**
 * @brief Store the data blobs with the given lengths.
 * Provide this data to `bst_setup` on boot. This is called when the bound key changes.
 * @param data
 * @param data_len
 */
void bst_store_crypto_secret(char* bound_key, size_t bound_key_len);

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

/**
 * @brief Return a random number with 64 bits.
 * @return
 */
uint64_t bst_get_random();

#ifdef __cplusplus
}
#endif
