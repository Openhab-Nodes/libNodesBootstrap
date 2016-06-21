
#ifdef BST_DEBUG_FULL
#include <string.h>
#include "prv_bootstrapWifi.h"
#include "spritz.h"

void debug_output_java_packet(const char* data, size_t data_len) {
  BST_DBG("byte crypto[] = {");
  for (int i=0;i<prv_instance.crypto_secret_len;++i)
    BST_DBG("%d, ", (signed char)prv_instance.crypto_secret[i]);
  BST_DBG("};\n");

  BST_DBG("byte prv_app_nonce[] = {");
  for (int i=0;i<BST_NONCE_SIZE;++i)
    BST_DBG("%d, ", (signed char)prv_instance.state.prv_app_nonce[i]);
  BST_DBG("};\n");

  BST_DBG("byte message[] = {");
  int wrap = 0;
  for (int i=0;i<data_len;++i) {
    BST_DBG("%d, ", (signed char)data[i]);
    if (++wrap == 20) {
      wrap = 0;
      BST_DBG("\n");
    }
  }
  BST_DBG("};\n");
}

/// Use this in your main loop to output the received data or into bst_network_output to
/// output the outgoing data with secret key and app nonce.
/// This can be used for test cases etc.
void debug_output_packet(const char* data, size_t data_len) {
  BST_DBG("unsigned char crypto[] = {");
  for (int i=0;i<prv_instance.crypto_secret_len;++i)
    BST_DBG("%d, ", (int)prv_instance.crypto_secret[i]);
  BST_DBG("}\n");

  BST_DBG("unsigned char prv_app_nonce[] = {");
  for (int i=0;i<BST_NONCE_SIZE;++i)
    BST_DBG("%d, ", (int)prv_instance.state.prv_app_nonce[i]);
  BST_DBG("}\n");

  BST_DBG("unsigned char message[] = {");
  for (int i=0;i<data_len;++i)
    BST_DBG("%d, ", (int)data[i]);
  BST_DBG("}\n");
}

/// Use this in bst_network_output. This will decrypt and crc check the outgoing packet,
/// print some debug information about it and output the app nonce.
void debug_outgoing_packet(const char* data, size_t data_len) {
  bst_udp_send_pkt_t* pkt = (bst_udp_send_pkt_t*)data;
  const size_t offset = sizeof(bst_udp_receive_pkt_t);

  unsigned char dec[512];
  memcpy(dec, (unsigned char*)pkt, data_len);
  unsigned char* in = (unsigned char*)pkt+offset;
  spritz_decrypt(&dec[offset],in,data_len-offset,
                 (unsigned char*)prv_instance.state.prv_app_nonce,BST_NONCE_SIZE,
                 (unsigned char*)prv_instance.crypto_secret,prv_instance.crypto_secret_len);
  bool ok = prv_crc16_is_valid((bst_udp_receive_pkt_t*)dec, data_len);
  BST_DBG("out: len %d, offset %d, crc %d\nkeylen %d, key %.8s, nonce %d.%d.%d.%d.%d.%d.%d.%d\n",
    data_len, offset, ok, prv_instance.crypto_secret_len, prv_instance.crypto_secret,
    prv_instance.state.prv_app_nonce[0], prv_instance.state.prv_app_nonce[1],
    prv_instance.state.prv_app_nonce[2], prv_instance.state.prv_app_nonce[3],
    prv_instance.state.prv_app_nonce[4], prv_instance.state.prv_app_nonce[5],
    prv_instance.state.prv_app_nonce[6], prv_instance.state.prv_app_nonce[7]);
}

#endif

/*
// We do not use multicast at the moment, but be prepared.
void bst_network_output(const char *data, size_t data_len) {
  udpIPv4.beginPacketMulticast(multiIP, 8711, WiFi.softAPIP());
  udpIPv4.write(data, data_len);
  udpIPv4.endPacket();
}
*/
