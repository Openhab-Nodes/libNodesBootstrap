#pragma once

#ifndef BST_STORAGE_RAM_SIZE
#define BST_STORAGE_RAM_SIZE 512
#endif

#ifndef BST_NONCE_SIZE
#define BST_NONCE_SIZE 8
#endif

#ifndef BST_UID_SIZE
#define BST_UID_SIZE 6
#endif

#ifndef BST_CRC_SIZE
#define BST_CRC_SIZE 2
#endif

#ifndef BST_BINDKEY_MAX_SIZE
#define BST_BINDKEY_MAX_SIZE 32
#endif

#ifndef BST_AUTH_SIZE
#define BST_AUTH_SIZE 32
#endif

// We always send fixed size packets.
// If this value is too small, we cannot send
// all neighbour wireless ssids to a connected
// app.
#ifndef BST_NETWORK_PACKET_SIZE
#define BST_NETWORK_PACKET_SIZE 512
#endif

#ifndef BST_NETWORK_HEADER
#define BST_NETWORK_HEADER "BSTwifi1"
#endif

// BST_NO_ERROR_MESSAGES
// Define BST_NO_ERROR_MESSAGES if you do not want
// to have english error messages for common errors
// like BST_STATE_FAILED_SSID_NOT_FOUND. Error messages
// appear in the app for a device as detailed status message.
