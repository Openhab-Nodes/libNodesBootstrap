
#include <stdio.h>

#include "test_platform_impl.h"
#include "spritz.h"
#include <gtest/gtest.h>

TEST(TestCrypto, BasicTest) {
    unsigned char       out[32];
    const unsigned char msg[] = { 'a', 'r', 'c', 'f', 'o', 'u', 'r' };
    unsigned              i;

    spritz_hash(out, sizeof out, msg, 7);

    const unsigned char expect_hash[] = {0xff, 0x8c, 0xf2, 0x68, 0x09, 0x4c, 0x87, 0xb9, 0x5f, 0x74, 0xce, 0x6f, 0xee, 0x9d, 0x30, 0x03, 0xa5, 0xf9, 0xfe, 0x69, 0x44, 0x65, 0x3c, 0xd5, 0x0e, 0x66, 0xbf, 0x18, 0x9c, 0x63, 0xf6, 0x99 };
    for (i = 0; i < sizeof out; i++) {
        ASSERT_EQ(expect_hash[i], out[i]);
    }

    unsigned const char nonce[] = "nonce";
    unsigned const char key[] = "secret";
    unsigned char* encryped = out;

    memset(encryped, 0, sizeof out);

    spritz_encrypt(encryped,msg,sizeof msg,nonce,sizeof nonce,key, sizeof key);

    //bst_platform::print_out_array(encryped, sizeof msg);

    spritz_decrypt(out,encryped,sizeof msg,nonce,sizeof nonce,key, sizeof key);

    for (i = 0; i < sizeof msg; i++) {
        ASSERT_EQ(msg[i], out[i]);
    }
    putchar('\n');
}


static bool operator ==(const bst_crc_value& v1, const bst_crc_value& v2) {
    return v1.crc[0] == v2.crc[0] && v1.crc[1] == v2.crc[1];
}

TEST(TestCrypto, TestInput) {
    unsigned char crypto[] = {97, 112, 112, 95, 115, 101, 99, 114, 101, 116, };
    unsigned char prv_app_nonce[] = {218, 105, 215, 63, 71, 59, 47, 103, };
    unsigned char message[]={66,83,84,119,105,102,105,49,126,181,0,225,111,237,132,86,70,160,90,83,184,44,92,31,115,47,63,34,100,103,83,86,48,235,197,138,164,150,1,23,87,82,170,68,127,20,154,80,194,239,230,106,89,140,197,95,34,245,102,249,66,141,152,10,58,95,199,54,25,172,31,231,48,71,203,23,13,237,218,180,215,100,197,104,44,149,232,79,192,213,203,92,66,21,28,162,162,10,205,225,235,235,153,3,220,36,203,137,218,48,98,91,126,55,112,253,106,104,75,141,168,198,40,40,255,23,124,151,26,61,237,75,176,29,27,10,151,237,81,195,188,244,82,53,65,172,229,8,170,168,204,254,165,193,87,234,224,232,189,254,44,122,148,155,145,127,243,253,218,135,119,191,60,219,226,69,218,226,186,111,223,37,39,235,61,88,111,242,139,110,105,181,184,15,193,105,198,153,85,242,62,90,33,69,190,129,193,48,20,98,104,145,27,132,119,22,136,67,178,243,184,109,174,212,255,164,254,98,173,238,229,175,27,0,186,244,145,229,238,229,237,37,50,245,165,170,168,7,233,55,52,45,180,169,152,198,99,182,28,90,148,201,249,7,78,73,102,127,124,185,52,17,79,235,49,42,233,3,153,80,165,64,208,65,149,213,96,242,111,226,188,68,27,22,176,247,38,86,125,53,1,220,53,93,27,175,152,185,175,46,30,233,204,187,83,211,103,250,237,251,117,169,155,183,246,148,197,66,17,79,162,198,241,69,247,237,55,68,57,160,145,84,12,61,187,139,65,142,116,250,109,13,58,171,158,185,96,109,66,57,65,151,241,58,123,19,155,238,113,239,115,83,129,106,129,251,128,225,50,126,236,196,233,133,22,68,106,255,60,3,216,126,56,69,137,158,202,209,98,93,214,239,62,21,6,184,138,120,230,95,193,77,147,91,175,77,228,210,149,59,24,135,242,106,215,225,106,152,13,249,77,45,16,121,221,149,173,53,205,52,40,58,117,129,14,48,28,240,150,25,147,233,114,254,246,76,165,40,153,151,178,109,156,170,206,247,16,72,149,77,251,166,47,30,105,88,79,202,44,52,27,162,148,220,8,40,1,226,44,54,249,177,150,176,76,141,175,17,238,225,73,244,37,83,68,132,99,196,98,133,51,151,};
    ASSERT_EQ(10,(int)sizeof(crypto));
    ASSERT_EQ( 8, (int)sizeof(prv_app_nonce));
    int offset = 8 + 2 + 1;
    int len = sizeof (message) - offset;
    unsigned char* msg = (unsigned char*)message + offset;
    spritz_decrypt(msg,msg,len,
                   (unsigned char*)prv_app_nonce,sizeof (prv_app_nonce),
                   (unsigned char*)crypto, sizeof(crypto));

    char* c = strstr((char*)message+28, "Heating");
    ASSERT_NE(0, (int)c);

    bst_crc_value v, cmp = {message[8], message[9]};
    v = bst_crc16(message+offset, len);
    ASSERT_TRUE(cmp == v);
}
