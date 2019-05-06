

#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_rtmp.h>
#include "ngx_rtmp_cenc.h"


void 
debug_counter(ngx_rtmp_session_t *s, uint8_t *c, uint8_t *k, size_t l)
{
    u_char hexc[AES_BLOCK_SIZE*2+1];
    u_char hexk[AES_BLOCK_SIZE*2+1];

    ngx_hex_dump(hexc, c, AES_BLOCK_SIZE);
    ngx_hex_dump(hexk, k, AES_BLOCK_SIZE);
    hexc[AES_BLOCK_SIZE*2] = '\0';
    hexk[AES_BLOCK_SIZE*2] = '\0';

    ngx_log_debug3(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
        "dash cenc_counter: %ui %s %s", l, hexc, hexk);
}


ngx_int_t
ngx_rtmp_cenc_read_hex(ngx_str_t src, u_char* dst)
{
    u_char  l, h;
    size_t  i;

    if (src.len != NGX_RTMP_CENC_KEY_SIZE*2) {
        return NGX_ERROR;
    }

    for (i = 0; i < NGX_RTMP_CENC_KEY_SIZE; i++) {
       l = ngx_tolower(src.data[i*2]);       
       l = l >= 'a' ? l - 'a' + 10 : l - '0'; 
       h = ngx_tolower(src.data[i*2+1]);       
       h = h >= 'a' ? h - 'a' + 10 : h - '0'; 
       dst[i] = (l << 4) | h; 
    }

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_cenc_rand_iv(u_char* iv)
{
    if(RAND_bytes(iv, NGX_RTMP_CENC_IV_SIZE) != 1) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


void
ngx_rtmp_cenc_increment_iv(u_char* iv)
{
    int i;

    for (i = NGX_RTMP_CENC_IV_SIZE - 1; i >= 0; i--) {
        iv[i]++;
        if (iv[i])
            break;
    }
}


ngx_int_t
ngx_rtmp_cenc_aes_ctr_encrypt(ngx_rtmp_session_t *s, uint8_t *key, uint8_t *iv,
    uint8_t *data, size_t data_len)
{
    /* aes-ctr implementation */

    EVP_CIPHER_CTX* ctx;
    size_t          j, len, left = data_len;
    int             i, w;
    uint8_t        *pos = data;
    uint8_t         counter[AES_BLOCK_SIZE], buf[AES_BLOCK_SIZE];

    ngx_memset(counter + NGX_RTMP_CENC_IV_SIZE, 0, NGX_RTMP_CENC_IV_SIZE);
    ngx_memcpy(counter, iv, NGX_RTMP_CENC_IV_SIZE);

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "dash rtmp_cenc_encrypt: evp_cipher_ctx failed");
        return NGX_ERROR;
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, key, NULL) != 1) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "dash rtmp_cenc_encrypt: evp_encrypt_init failed");
        return NGX_ERROR;
    }

    while (left > 0) {

        if (EVP_EncryptUpdate(ctx, buf, &w, counter, AES_BLOCK_SIZE) != 1) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "dash rtmp_cenc_encrypt: evp_encrypt_update failed");
            return NGX_ERROR;
        }

        len = (left < AES_BLOCK_SIZE) ? left : AES_BLOCK_SIZE;
        for (j = 0; j < len; j++)
            pos[j] ^= buf[j];
        pos += len;
        left -= len;

        for (i = AES_BLOCK_SIZE - 1; i >= 0; i--) {
            counter[i]++;
            if (counter[i])
                break;
        }
    }

    EVP_CIPHER_CTX_free(ctx);

    return NGX_OK;
}
 

ngx_int_t
ngx_rtmp_cenc_encrypt_full_sample(ngx_rtmp_session_t *s, uint8_t *key, uint8_t *iv,
    uint8_t *data, size_t data_len)
{
    return ngx_rtmp_cenc_aes_ctr_encrypt(s, key, iv, data, data_len);
}


ngx_int_t
ngx_rtmp_cenc_encrypt_sub_sample(ngx_rtmp_session_t *s, uint8_t *key, uint8_t *iv,
    uint8_t *data, size_t data_len, size_t *clear_data_len)
{
    size_t crypted_data_len;

    /* small sample : leave it in clear */
    if (data_len <= NGX_RTMP_CENC_MIN_CLEAR_SIZE) {
        *clear_data_len = data_len;
        return NGX_OK;
    }

    /* skip sufficient amount of data to leave nalu header/infos
     * in clear to conform to the norm */
    crypted_data_len = 
        ((data_len - NGX_RTMP_CENC_MIN_CLEAR_SIZE) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE;
    *clear_data_len = data_len - crypted_data_len;
    
    data += *clear_data_len;
    return ngx_rtmp_cenc_aes_ctr_encrypt(s, key, iv, data, crypted_data_len);

}


ngx_int_t
ngx_rtmp_cenc_content_protection_pssh(u_char* kid, ngx_str_t *dest_pssh)
{
    ngx_str_t  src_pssh;
    u_char     dest[NGX_RTMP_CENC_MAX_PSSH_SIZE];

    u_char pssh[] = {
        0x00, 0x00, 0x00, 0x34, 0x70, 0x73, 0x73, 0x68, // pssh box header
        0x01, 0x00, 0x00, 0x00,                         // header
        0x10, 0x77, 0xef, 0xec, 0xc0, 0xb2, 0x4d, 0x02, // systemID 
        0xac, 0xe3, 0x3c, 0x1e, 0x52, 0xe2, 0xfb, 0x4b,
        0x00, 0x00, 0x00, 0x01,                         // kid count
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // kid
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00                          // data size
    };

    ngx_memcpy(pssh+32, kid, NGX_RTMP_CENC_KEY_SIZE);

    src_pssh.len = sizeof(pssh);
    src_pssh.data = pssh;

    dest_pssh->len = ngx_base64_encoded_length(src_pssh.len);
    dest_pssh->data = dest;
    
    ngx_encode_base64(dest_pssh, &src_pssh);

    return NGX_OK;
}

