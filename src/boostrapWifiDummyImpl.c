#include "boostrapWifi.h"
#include "prv_boostrapWifi.h"
#include <string.h>

#ifndef BST_NO_DEFAULT_PLATFORM
void __attribute__((weak)) bst_network_output(const char* data, size_t data_len)
{
    (void)data;
    (void)data_len;
}

bst_connect_state __attribute__((weak)) bst_get_connection_state()
{
    return BST_STATE_NO_CONNECTION;
}

void __attribute__((weak)) bst_connect_to_wifi(const char* ssid, const char* pwd, bst_state state)
{
    (void)ssid;
    (void)pwd;
    (void)state;
}

void __attribute__((weak)) bst_connect_advanced(const char* data)
{
    (void)data;
}

void __attribute__((weak)) bst_request_wifi_network_list()
{
}

void __attribute__((weak)) bst_store_bootstrap_data(char* bst_data, size_t bst_data_len) {
    (void)bst_data;
    (void)bst_data_len;
}

void __attribute__((weak)) bst_store_crypto_secret(char* bound_key, size_t bound_key_len) {
    (void)bound_key;
    (void)bound_key_len;
}

time_t __attribute__((weak)) bst_get_system_time_ms()
{
    return 0;
}

uint64_t __attribute__((weak)) bst_get_random()
{
    return 1;
}

#endif
