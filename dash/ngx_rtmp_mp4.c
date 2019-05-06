

#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_mp4.h"
#include <ngx_rtmp_codec_module.h>


static ngx_int_t
ngx_rtmp_mp4_field_32(ngx_buf_t *b, uint32_t n)
{
    u_char  bytes[4];

    bytes[0] = ((uint32_t) n >> 24) & 0xFF;
    bytes[1] = ((uint32_t) n >> 16) & 0xFF;
    bytes[2] = ((uint32_t) n >> 8) & 0xFF;
    bytes[3] = (uint32_t) n & 0xFF;

    if (b->last + sizeof(bytes) > b->end) {
        return NGX_ERROR;
    }

    b->last = ngx_cpymem(b->last, bytes, sizeof(bytes));

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_field_24(ngx_buf_t *b, uint32_t n)
{
    u_char  bytes[3];

    bytes[0] = ((uint32_t) n >> 16) & 0xFF;
    bytes[1] = ((uint32_t) n >> 8) & 0xFF;
    bytes[2] = (uint32_t) n & 0xFF;

    if (b->last + sizeof(bytes) > b->end) {
        return NGX_ERROR;
    }

    b->last = ngx_cpymem(b->last, bytes, sizeof(bytes));

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_field_16(ngx_buf_t *b, uint16_t n)
{
    u_char  bytes[2];

    bytes[0] = ((uint32_t) n >> 8) & 0xFF;
    bytes[1] = (uint32_t) n & 0xFF;

    if (b->last + sizeof(bytes) > b->end) {
        return NGX_ERROR;
    }

    b->last = ngx_cpymem(b->last, bytes, sizeof(bytes));

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_field_8(ngx_buf_t *b, uint8_t n)
{
    u_char  bytes[1];

    bytes[0] = n & 0xFF;

    if (b->last + sizeof(bytes) > b->end) {
        return NGX_ERROR;
    }

    b->last = ngx_cpymem(b->last, bytes, sizeof(bytes));

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_put_descr(ngx_buf_t *b, int tag, size_t size)
{
    ngx_rtmp_mp4_field_8(b, (uint8_t) tag);
    ngx_rtmp_mp4_field_8(b, size & 0x7F);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_data(ngx_buf_t *b, void *data, size_t n)
{
    if (b->last + n > b->end) {
        return NGX_ERROR;
    }

    b->last = ngx_cpymem(b->last, (u_char *) data, n);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_box(ngx_buf_t *b, const char box[4])
{
    if (b->last + 4 > b->end) {
        return NGX_ERROR;
    }

    b->last = ngx_cpymem(b->last, (u_char *) box, 4);

    return NGX_OK;
}


static u_char *
ngx_rtmp_mp4_start_box(ngx_buf_t *b, const char box[4])
{
    u_char  *p;

    p = b->last;

    if (ngx_rtmp_mp4_field_32(b, 0) != NGX_OK) {
        return NULL;
    }

    if (ngx_rtmp_mp4_box(b, box) != NGX_OK) {
        return NULL;
    }

    return p;
}


static ngx_int_t
ngx_rtmp_mp4_update_box_size(ngx_buf_t *b, u_char *p)
{
    u_char  *curpos;

    if (p == NULL) {
        return NGX_ERROR;
    }

    curpos = b->last;

    b->last = p;

    ngx_rtmp_mp4_field_32(b, (uint32_t) (curpos - p));

    b->last = curpos;

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_matrix(ngx_buf_t *buf, uint32_t a, uint32_t b, uint32_t c,
    uint32_t d, uint32_t tx, uint32_t ty)
{

/*
 * transformation matrix
 * |a  b  u|
 * |c  d  v|
 * |tx ty w|
 */

    ngx_rtmp_mp4_field_32(buf, a << 16);  /* 16.16 format */
    ngx_rtmp_mp4_field_32(buf, b << 16);  /* 16.16 format */
    ngx_rtmp_mp4_field_32(buf, 0);        /* u in 2.30 format */
    ngx_rtmp_mp4_field_32(buf, c << 16);  /* 16.16 format */
    ngx_rtmp_mp4_field_32(buf, d << 16);  /* 16.16 format */
    ngx_rtmp_mp4_field_32(buf, 0);        /* v in 2.30 format */
    ngx_rtmp_mp4_field_32(buf, tx << 16); /* 16.16 format */
    ngx_rtmp_mp4_field_32(buf, ty << 16); /* 16.16 format */
    ngx_rtmp_mp4_field_32(buf, 1 << 30);  /* w in 2.30 format */

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mp4_write_ftyp(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "ftyp");

    /* major brand */
    ngx_rtmp_mp4_box(b, "iso6");

    /* minor version */
    ngx_rtmp_mp4_field_32(b, 1);

    /* compatible brands */
    ngx_rtmp_mp4_box(b, "isom");
    ngx_rtmp_mp4_box(b, "iso6");
    ngx_rtmp_mp4_box(b, "dash");

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mp4_write_styp(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "styp");

    /* major brand */
    ngx_rtmp_mp4_box(b, "iso6");

    /* minor version */
    ngx_rtmp_mp4_field_32(b, 1);

    /* compatible brands */
    ngx_rtmp_mp4_box(b, "isom");
    ngx_rtmp_mp4_box(b, "iso6");
    ngx_rtmp_mp4_box(b, "dash");

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_mvhd(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "mvhd");

    /* version */
    ngx_rtmp_mp4_field_32(b, 0);

    /* creation time */
    ngx_rtmp_mp4_field_32(b, 0);

    /* modification time */
    ngx_rtmp_mp4_field_32(b, 0);

    /* timescale */
    ngx_rtmp_mp4_field_32(b, 1000);

    /* duration */
    ngx_rtmp_mp4_field_32(b, 0);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0x00010000);
    ngx_rtmp_mp4_field_16(b, 0x0100);
    ngx_rtmp_mp4_field_16(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    ngx_rtmp_mp4_write_matrix(b, 1, 0, 0, 1, 0, 0);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    /* next track id */
    ngx_rtmp_mp4_field_32(b, 1);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_tkhd(ngx_rtmp_session_t *s, ngx_buf_t *b,
    ngx_rtmp_mp4_track_type_t ttype)
{
    u_char                *pos;
    ngx_rtmp_codec_ctx_t  *codec_ctx;

    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    pos = ngx_rtmp_mp4_start_box(b, "tkhd");

    /* version */
    ngx_rtmp_mp4_field_8(b, 0);

    /* flags: TrackEnabled */
    ngx_rtmp_mp4_field_24(b, 0x0000000f);

    /* creation time */
    ngx_rtmp_mp4_field_32(b, 0);

    /* modification time */
    ngx_rtmp_mp4_field_32(b, 0);

    /* track id */
    ngx_rtmp_mp4_field_32(b, 1);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);

    /* duration */
    ngx_rtmp_mp4_field_32(b, 0);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    /* reserved */
    if (ttype == NGX_RTMP_MP4_VIDEO_TRACK ||
        ttype == NGX_RTMP_MP4_EVIDEO_TRACK) {
        ngx_rtmp_mp4_field_16(b, 0);
    } else { 
        /* reserved */
        ngx_rtmp_mp4_field_16(b, 0x0100);
    }

    /* reserved */
    ngx_rtmp_mp4_field_16(b, 0);

    ngx_rtmp_mp4_write_matrix(b, 1, 0, 0, 1, 0, 0);

    if (ttype == NGX_RTMP_MP4_VIDEO_TRACK ||
        ttype == NGX_RTMP_MP4_EVIDEO_TRACK) {
        ngx_rtmp_mp4_field_32(b, (uint32_t) codec_ctx->width << 16);
        ngx_rtmp_mp4_field_32(b, (uint32_t) codec_ctx->height << 16);
    } else {
        ngx_rtmp_mp4_field_32(b, 0);
        ngx_rtmp_mp4_field_32(b, 0);
    } 

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_mdhd(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "mdhd");

    /* version */
    ngx_rtmp_mp4_field_32(b, 0);

    /* creation time */
    ngx_rtmp_mp4_field_32(b, 0);

    /* modification time */
    ngx_rtmp_mp4_field_32(b, 0);

    /* time scale*/
    ngx_rtmp_mp4_field_32(b, 1000);

    /* duration */
    ngx_rtmp_mp4_field_32(b, 0);

    /* lanuguage */
    ngx_rtmp_mp4_field_16(b, 0x15C7);

    /* reserved */
    ngx_rtmp_mp4_field_16(b, 0);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_hdlr(ngx_buf_t *b, ngx_rtmp_mp4_track_type_t ttype)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "hdlr");

    /* version and flags */
    ngx_rtmp_mp4_field_32(b, 0);

    /* pre defined */
    ngx_rtmp_mp4_field_32(b, 0);

    if (ttype == NGX_RTMP_MP4_VIDEO_TRACK ||
        ttype == NGX_RTMP_MP4_EVIDEO_TRACK) {
        ngx_rtmp_mp4_box(b, "vide");
    } else {
        ngx_rtmp_mp4_box(b, "soun");
    }

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    if (ttype == NGX_RTMP_MP4_VIDEO_TRACK ||
        ttype == NGX_RTMP_MP4_EVIDEO_TRACK) {
        /* video handler string, NULL-terminated */
        ngx_rtmp_mp4_data(b, "VideoHandler", sizeof("VideoHandler"));
    } else {
        /* sound handler string, NULL-terminated */
        ngx_rtmp_mp4_data(b, "SoundHandler", sizeof("SoundHandler"));
    }

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_vmhd(ngx_buf_t *b)
{
    /* size is always 20, apparently */
    ngx_rtmp_mp4_field_32(b, 20);

    ngx_rtmp_mp4_box(b, "vmhd");

    /* version and flags */
    ngx_rtmp_mp4_field_32(b, 0x01);

    /* reserved (graphics mode=copy) */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_smhd(ngx_buf_t *b)
{
    /* size is always 16, apparently */
    ngx_rtmp_mp4_field_32(b, 16);

    ngx_rtmp_mp4_box(b, "smhd");

    /* version and flags */
    ngx_rtmp_mp4_field_32(b, 0);

    /* reserved (balance normally=0) */
    ngx_rtmp_mp4_field_16(b, 0);
    ngx_rtmp_mp4_field_16(b, 0);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_dref(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "dref");

    /* version and flags */
    ngx_rtmp_mp4_field_32(b, 0);

    /* entry count */
    ngx_rtmp_mp4_field_32(b, 1);

    /* url size */
    ngx_rtmp_mp4_field_32(b, 0xc);

    ngx_rtmp_mp4_box(b, "url ");

    /* version and flags */
    ngx_rtmp_mp4_field_32(b, 0x00000001);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_dinf(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "dinf");

    ngx_rtmp_mp4_write_dref(b);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_frma(ngx_buf_t *b, const char format[4])
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "frma");

    /* original_format */
    ngx_rtmp_mp4_box(b, format);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_schm(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "schm");

    /* version and flags */
    ngx_rtmp_mp4_field_32(b, 0);

    /* scheme_type */
    ngx_rtmp_mp4_box(b, "cenc");

    /* scheme_version */
    ngx_rtmp_mp4_field_32(b, 65536);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_pssh_cenc(ngx_buf_t *b,
    ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;
    u_char   sid[] = {
        0x10, 0x77, 0xef, 0xec, 0xc0, 0xb2, 0x4d, 0x02, 
        0xac, 0xe3, 0x3c, 0x1e, 0x52, 0xe2, 0xfb, 0x4b
    };

    pos = ngx_rtmp_mp4_start_box(b, "pssh");

    /* version and flags */
    ngx_rtmp_mp4_field_32(b, 0x01000000);

    /* system ID : org.w3.clearkey */
    ngx_rtmp_mp4_data(b, sid, NGX_RTMP_CENC_KEY_SIZE);

    /* kid count */
    ngx_rtmp_mp4_field_32(b, 1);

    /* default KID */
    ngx_rtmp_mp4_data(b, drmi->kid, NGX_RTMP_CENC_KEY_SIZE);

    /* data size */
    ngx_rtmp_mp4_field_32(b, 0);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_pssh_wdv(ngx_buf_t *b,
    ngx_rtmp_cenc_drm_info_t *drmi)
{
    ngx_str_t   dest, src;
    u_char     *pos;
    u_char      buf[NGX_RTMP_CENC_MAX_PSSH_SIZE];

    u_char      sid[] = {
        0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce, 
        0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed
    };

    pos = ngx_rtmp_mp4_start_box(b, "pssh");

    /* assuming v0 pssh for widevine */
    ngx_rtmp_mp4_field_32(b, 0);

    /* system ID : com.widevine.alpha */
    ngx_rtmp_mp4_data(b, sid, NGX_RTMP_CENC_KEY_SIZE);

    /* decode base64 wdv_data */
    dest.data = buf;
    src.len = ngx_base64_decoded_length(drmi->wdv_data.len) - 32;
    ngx_decode_base64(&dest, &drmi->wdv_data);
    
    /* data size */
    ngx_rtmp_mp4_field_32(b, src.len);

    /* data */
    ngx_rtmp_mp4_data(b, dest.data + 32, src.len);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_pssh_mspr(ngx_buf_t *b,
    ngx_rtmp_cenc_drm_info_t *drmi)
{
    ngx_str_t   dest, src;
    u_char     *pos;
    u_char      buf[NGX_RTMP_CENC_MAX_PSSH_SIZE];

    u_char      sid[] = {
        0x9a, 0x04, 0xf0, 0x79, 0x98, 0x40, 0x42, 0x86, 
        0xab, 0x92, 0xe6, 0x5b, 0xe0, 0x88, 0x5f, 0x95
    };

    pos = ngx_rtmp_mp4_start_box(b, "pssh");

    /* assuming v0 pssh for playready */
    ngx_rtmp_mp4_field_32(b, 0);

    /* system ID : com.microsoft.playready */
    ngx_rtmp_mp4_data(b, sid, NGX_RTMP_CENC_KEY_SIZE);

    /* decode base64 mspr_data */
    dest.data = buf;
    src.len = ngx_base64_decoded_length(drmi->mspr_data.len) - 32;
    ngx_decode_base64(&dest, &drmi->mspr_data);
    
    /* data size */
    ngx_rtmp_mp4_field_32(b, src.len);

    /* data */
    ngx_rtmp_mp4_data(b, dest.data + 32, src.len);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}



static ngx_int_t
ngx_rtmp_mp4_write_tenc(ngx_buf_t *b, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "tenc");

    /* version and flags */
    ngx_rtmp_mp4_field_32(b, 0);

    /* reserved */
    ngx_rtmp_mp4_field_8(b, 0);
    ngx_rtmp_mp4_field_8(b, 0);

    /* default isProtected */
    ngx_rtmp_mp4_field_8(b, 1);

    /* default per_sample_iv_size */
    ngx_rtmp_mp4_field_8(b, NGX_RTMP_CENC_IV_SIZE);

    /* default KID */
    ngx_rtmp_mp4_data(b, drmi->kid, NGX_RTMP_CENC_KEY_SIZE);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_schi(ngx_buf_t *b, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "schi");

    ngx_rtmp_mp4_write_tenc(b, drmi);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_sinf(ngx_buf_t *b, 
    const char format[4], ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "sinf");

    ngx_rtmp_mp4_write_frma(b, format);
    ngx_rtmp_mp4_write_schm(b);
    ngx_rtmp_mp4_write_schi(b, drmi);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_avcc(ngx_rtmp_session_t *s, ngx_buf_t *b)
{
    u_char                *pos, *p;
    ngx_chain_t           *in;
    ngx_rtmp_codec_ctx_t  *codec_ctx;

    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (codec_ctx == NULL) {
        return NGX_ERROR;
    }

    in = codec_ctx->avc_header;
    if (in == NULL) {
        return NGX_ERROR;
    }

    pos = ngx_rtmp_mp4_start_box(b, "avcC");

    /* assume config fits one chunk (highly probable) */

    /*
     * Skip:
     * - flv fmt
     * - H264 CONF/PICT (0x00)
     * - 0
     * - 0
     * - 0
     */

    p = in->buf->pos + 5;

    if (p < in->buf->last) {
        ngx_rtmp_mp4_data(b, p, (size_t) (in->buf->last - p));
    } else {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: invalid avcc received");
    }

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_video(ngx_rtmp_session_t *s, ngx_buf_t *b)
{
    u_char                *pos;
    ngx_rtmp_codec_ctx_t  *codec_ctx;

    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    pos = ngx_rtmp_mp4_start_box(b, "avc1");

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_16(b, 0);

    /* data reference index */
    ngx_rtmp_mp4_field_16(b, 1);

    /* codec stream version & revision */
    ngx_rtmp_mp4_field_16(b, 0);
    ngx_rtmp_mp4_field_16(b, 0);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    /* width & height */
    ngx_rtmp_mp4_field_16(b, (uint16_t) codec_ctx->width);
    ngx_rtmp_mp4_field_16(b, (uint16_t) codec_ctx->height);

    /* horizontal & vertical resolutions 72 dpi */
    ngx_rtmp_mp4_field_32(b, 0x00480000);
    ngx_rtmp_mp4_field_32(b, 0x00480000);

    /* data size */
    ngx_rtmp_mp4_field_32(b, 0);

    /* frame count */
    ngx_rtmp_mp4_field_16(b, 1);

    /* compressor name */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_16(b, 0x18);
    ngx_rtmp_mp4_field_16(b, 0xffff);

    ngx_rtmp_mp4_write_avcc(s, b);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_evideo(ngx_rtmp_session_t *s,
    ngx_buf_t *b, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char                *pos;
    ngx_rtmp_codec_ctx_t  *codec_ctx;

    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    pos = ngx_rtmp_mp4_start_box(b, "encv");

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_16(b, 0);

    /* data reference index */
    ngx_rtmp_mp4_field_16(b, 1);

    /* codec stream version & revision */
    ngx_rtmp_mp4_field_16(b, 0);
    ngx_rtmp_mp4_field_16(b, 0);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    /* width & height */
    ngx_rtmp_mp4_field_16(b, (uint16_t) codec_ctx->width);
    ngx_rtmp_mp4_field_16(b, (uint16_t) codec_ctx->height);

    /* horizontal & vertical resolutions 72 dpi */
    ngx_rtmp_mp4_field_32(b, 0x00480000);
    ngx_rtmp_mp4_field_32(b, 0x00480000);

    /* data size */
    ngx_rtmp_mp4_field_32(b, 0);

    /* frame count */
    ngx_rtmp_mp4_field_16(b, 1);

    /* compressor name */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_16(b, 0x18);
    ngx_rtmp_mp4_field_16(b, 0xffff);

    ngx_rtmp_mp4_write_avcc(s, b);

    ngx_rtmp_mp4_write_sinf(b, "avc1", drmi);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_esds(ngx_rtmp_session_t *s, ngx_buf_t *b)
{
    size_t                 dsi_len;
    u_char                *pos, *dsi;
    ngx_buf_t             *db;
    ngx_rtmp_codec_ctx_t  *codec_ctx;

    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (codec_ctx == NULL || codec_ctx->aac_header == NULL) {
        return NGX_ERROR;
    }

    db = codec_ctx->aac_header->buf;
    if (db == NULL) {
        return NGX_ERROR;
    }

    dsi = db->pos + 2;
    if (dsi > db->last) {
        return NGX_ERROR;
    }

    dsi_len = db->last - dsi;

    pos = ngx_rtmp_mp4_start_box(b, "esds");

    /* version */
    ngx_rtmp_mp4_field_32(b, 0);


    /* ES Descriptor */

    ngx_rtmp_mp4_put_descr(b, 0x03, 23 + dsi_len);

    /* ES_ID */
    ngx_rtmp_mp4_field_16(b, 1);

    /* flags */
    ngx_rtmp_mp4_field_8(b, 0);


    /* DecoderConfig Descriptor */

    ngx_rtmp_mp4_put_descr(b, 0x04, 15 + dsi_len);

    /* objectTypeIndication: Audio ISO/IEC 14496-3 (AAC) */
    ngx_rtmp_mp4_field_8(b, 0x40);

    /* streamType: AudioStream */
    ngx_rtmp_mp4_field_8(b, 0x15);

    /* bufferSizeDB */
    ngx_rtmp_mp4_field_24(b, 0);

    /* maxBitrate */
    ngx_rtmp_mp4_field_32(b, 0x0001F151);

    /* avgBitrate */
    ngx_rtmp_mp4_field_32(b, 0x0001F14D);


    /* DecoderSpecificInfo Descriptor */

    ngx_rtmp_mp4_put_descr(b, 0x05, dsi_len);
    ngx_rtmp_mp4_data(b, dsi, dsi_len);


    /* SL Descriptor */

    ngx_rtmp_mp4_put_descr(b, 0x06, 1);
    ngx_rtmp_mp4_field_8(b, 0x02);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_audio(ngx_rtmp_session_t *s, ngx_buf_t *b)
{
    u_char                *pos;
    ngx_rtmp_codec_ctx_t  *codec_ctx;

    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    pos = ngx_rtmp_mp4_start_box(b, "mp4a");

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_16(b, 0);

    /* data reference index */
    ngx_rtmp_mp4_field_16(b, 1);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    /* channel count */
    ngx_rtmp_mp4_field_16(b, (uint16_t) codec_ctx->audio_channels);

    /* sample size */
    ngx_rtmp_mp4_field_16(b, (uint16_t) (codec_ctx->sample_size * 8));

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);

    /* time scale */
    ngx_rtmp_mp4_field_16(b, 1000);

    /* sample rate */
    ngx_rtmp_mp4_field_16(b, (uint16_t) codec_ctx->sample_rate);

    ngx_rtmp_mp4_write_esds(s, b);
#if 0
    /* tag size*/
    ngx_rtmp_mp4_field_32(b, 8);

    /* null tag */
    ngx_rtmp_mp4_field_32(b, 0);
#endif
    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_eaudio(ngx_rtmp_session_t *s, 
    ngx_buf_t *b, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char                *pos;
    ngx_rtmp_codec_ctx_t  *codec_ctx;

    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    pos = ngx_rtmp_mp4_start_box(b, "enca");

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_16(b, 0);

    /* data reference index */
    ngx_rtmp_mp4_field_16(b, 1);

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);
    ngx_rtmp_mp4_field_32(b, 0);

    /* channel count */
    ngx_rtmp_mp4_field_16(b, (uint16_t) codec_ctx->audio_channels);

    /* sample size */
    ngx_rtmp_mp4_field_16(b, (uint16_t) (codec_ctx->sample_size * 8));

    /* reserved */
    ngx_rtmp_mp4_field_32(b, 0);

    /* time scale */
    ngx_rtmp_mp4_field_16(b, 1000);

    /* sample rate */
    ngx_rtmp_mp4_field_16(b, (uint16_t) codec_ctx->sample_rate);

    ngx_rtmp_mp4_write_esds(s, b);
#if 0
    /* tag size*/
    ngx_rtmp_mp4_field_32(b, 8);

    /* null tag */
    ngx_rtmp_mp4_field_32(b, 0);
#endif

    ngx_rtmp_mp4_write_sinf(b, "mp4a", drmi);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_stsd(ngx_rtmp_session_t *s, ngx_buf_t *b,
    ngx_rtmp_mp4_track_type_t ttype, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "stsd");

    /* version & flags */
    ngx_rtmp_mp4_field_32(b, 0);

    /* entry count */
    ngx_rtmp_mp4_field_32(b, 1);

    if (ttype == NGX_RTMP_MP4_VIDEO_TRACK) { 
        ngx_rtmp_mp4_write_video(s, b);
    } else if (ttype == NGX_RTMP_MP4_EVIDEO_TRACK){
        ngx_rtmp_mp4_write_evideo(s, b, drmi);
    } else if (ttype == NGX_RTMP_MP4_AUDIO_TRACK){
        ngx_rtmp_mp4_write_audio(s, b);
    } else if (ttype == NGX_RTMP_MP4_EAUDIO_TRACK){
        ngx_rtmp_mp4_write_eaudio(s, b, drmi);
    }

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_stts(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "stts");

    ngx_rtmp_mp4_field_32(b, 0); /* version */
    ngx_rtmp_mp4_field_32(b, 0); /* entry count */

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_stsc(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "stsc");

    ngx_rtmp_mp4_field_32(b, 0); /* version */
    ngx_rtmp_mp4_field_32(b, 0); /* entry count */

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_stsz(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "stsz");

    ngx_rtmp_mp4_field_32(b, 0); /* version */
    ngx_rtmp_mp4_field_32(b, 0); /* entry count */
    ngx_rtmp_mp4_field_32(b, 0); /* moar zeros */

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_stco(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "stco");

    ngx_rtmp_mp4_field_32(b, 0); /* version */
    ngx_rtmp_mp4_field_32(b, 0); /* entry count */

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_stbl(ngx_rtmp_session_t *s, ngx_buf_t *b,
    ngx_rtmp_mp4_track_type_t ttype, ngx_rtmp_cenc_drm_info_t *drmi) 
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "stbl");

    ngx_rtmp_mp4_write_stsd(s, b, ttype, drmi);
    ngx_rtmp_mp4_write_stts(b);
    ngx_rtmp_mp4_write_stsc(b);
    ngx_rtmp_mp4_write_stsz(b);
    ngx_rtmp_mp4_write_stco(b);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_minf(ngx_rtmp_session_t *s, ngx_buf_t *b,
    ngx_rtmp_mp4_track_type_t ttype, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "minf");

    if (ttype == NGX_RTMP_MP4_VIDEO_TRACK ||
        ttype == NGX_RTMP_MP4_EVIDEO_TRACK) {
        ngx_rtmp_mp4_write_vmhd(b);
    } else {
        ngx_rtmp_mp4_write_smhd(b);
    }

    ngx_rtmp_mp4_write_dinf(b);
    ngx_rtmp_mp4_write_stbl(s, b, ttype, drmi);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_mdia(ngx_rtmp_session_t *s, ngx_buf_t *b,
    ngx_rtmp_mp4_track_type_t ttype, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "mdia");

    ngx_rtmp_mp4_write_mdhd(b);
    ngx_rtmp_mp4_write_hdlr(b, ttype);
    ngx_rtmp_mp4_write_minf(s, b, ttype, drmi);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}

static ngx_int_t
ngx_rtmp_mp4_write_trak(ngx_rtmp_session_t *s, ngx_buf_t *b,
    ngx_rtmp_mp4_track_type_t ttype, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "trak");

    ngx_rtmp_mp4_write_tkhd(s, b, ttype);
    ngx_rtmp_mp4_write_mdia(s, b, ttype, drmi);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_mvex(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "mvex");

    ngx_rtmp_mp4_field_32(b, 0x20);

    ngx_rtmp_mp4_box(b, "trex");

    /* version & flags */
    ngx_rtmp_mp4_field_32(b, 0);

    /* track id */
    ngx_rtmp_mp4_field_32(b, 1);

    /* default sample description index */
    ngx_rtmp_mp4_field_32(b, 1);

    /* default sample duration */
    ngx_rtmp_mp4_field_32(b, 0);

    /* default sample size, 1024 for AAC */
    ngx_rtmp_mp4_field_32(b, 0);

    /* default sample flags, key on */
    ngx_rtmp_mp4_field_32(b, 0);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mp4_write_moov(ngx_rtmp_session_t *s, ngx_buf_t *b,
    ngx_rtmp_mp4_track_type_t ttype, ngx_rtmp_cenc_drm_info_t *drmi)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "moov");

    ngx_rtmp_mp4_write_mvhd(b);
    ngx_rtmp_mp4_write_mvex(b);
    ngx_rtmp_mp4_write_trak(s, b, ttype, drmi);

    if (ttype == NGX_RTMP_MP4_EVIDEO_TRACK ||
        ttype == NGX_RTMP_MP4_EAUDIO_TRACK) {
        ngx_rtmp_mp4_write_pssh_cenc(b, drmi);
        if (drmi->wdv) {
            ngx_rtmp_mp4_write_pssh_wdv(b, drmi);
        }
        if (drmi->mspr) {
            ngx_rtmp_mp4_write_pssh_mspr(b, drmi);
        }
    }

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_tfhd(ngx_buf_t *b)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "tfhd");

    /* version & flags */
    ngx_rtmp_mp4_field_32(b, 0x00020000);

    /* track id */
    ngx_rtmp_mp4_field_32(b, 1);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_tfdt(ngx_buf_t *b, uint32_t earliest_pres_time)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "tfdt");

    /* version == 1 aka 64 bit integer */
    ngx_rtmp_mp4_field_32(b, 0x00000000);
    ngx_rtmp_mp4_field_32(b, earliest_pres_time);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_trun(ngx_buf_t *b, char type,
    uint32_t sample_count, ngx_rtmp_mp4_sample_t *samples,
    ngx_uint_t sample_mask, u_char *moof_pos, ngx_flag_t is_protected)
{
    u_char    *pos;
    uint32_t   i, offset, nitems, flags;

    pos = ngx_rtmp_mp4_start_box(b, "trun");

    nitems = 0;

    /* data offset present */
    flags = 0x01;

    if (sample_mask & NGX_RTMP_MP4_SAMPLE_DURATION) {
        nitems++;
        flags |= 0x000100;
    }

    if (sample_mask & NGX_RTMP_MP4_SAMPLE_SIZE) {
        nitems++;
        flags |= 0x000200;
    }

    if (sample_mask & NGX_RTMP_MP4_SAMPLE_KEY) {
        nitems++;
        flags |= 0x000400;
    }

    if (sample_mask & NGX_RTMP_MP4_SAMPLE_DELAY) {
        nitems++;
        flags |= 0x000800;
    }

    if (is_protected) {
        /* if cenc is enabled we neeed to add 
         * saiz saiz senc size to the data offset */
        offset = (pos - moof_pos) + 20 + (sample_count * nitems * 4);
        if (type == 'v') {
            /* video use sub sample senc */
            offset += 17 + 20 + 16 + (sample_count * (NGX_RTMP_CENC_IV_SIZE + 8)) + 8;
        } else {
            /* audio use full sample senc */
            offset += 17 + 20 + 16 + (sample_count * NGX_RTMP_CENC_IV_SIZE) + 8;
        }
    } else {
        offset = (pos - moof_pos) + 20 + (sample_count * nitems * 4) + 8;
    }

    ngx_rtmp_mp4_field_32(b, flags);
    ngx_rtmp_mp4_field_32(b, sample_count);
    ngx_rtmp_mp4_field_32(b, offset);

    for (i = 0; i < sample_count; i++, samples++) {

        if (sample_mask & NGX_RTMP_MP4_SAMPLE_DURATION) {
            ngx_rtmp_mp4_field_32(b, samples->duration);
        }

        if (sample_mask & NGX_RTMP_MP4_SAMPLE_SIZE) {
            ngx_rtmp_mp4_field_32(b, samples->size);
        }

        if (sample_mask & NGX_RTMP_MP4_SAMPLE_KEY) {
            ngx_rtmp_mp4_field_32(b, samples->key ? 0x00000000 : 0x00010000);
        }

        if (sample_mask & NGX_RTMP_MP4_SAMPLE_DELAY) {
            ngx_rtmp_mp4_field_32(b, samples->delay);
        }
    }

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_saiz(ngx_buf_t *b, char type, uint32_t sample_count)
{
    u_char    *pos;

    pos = ngx_rtmp_mp4_start_box(b, "saiz");

    /* version & flag */
    ngx_rtmp_mp4_field_32(b, 0);

    /* defaut sample info size */
    if (type == 'v') {
        /* sub sample */
        ngx_rtmp_mp4_field_8(b, NGX_RTMP_CENC_IV_SIZE + 8);
    } else {
        /* full sample */
        ngx_rtmp_mp4_field_8(b, NGX_RTMP_CENC_IV_SIZE);
    }

    /* sample count */
    ngx_rtmp_mp4_field_32(b, sample_count);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_saio(ngx_buf_t *b, u_char *moof_pos)
{
    u_char    *pos;
    uint32_t   offset;

    pos = ngx_rtmp_mp4_start_box(b, "saio");

    /* version & flag */
    ngx_rtmp_mp4_field_32(b, 0);

    /* entry count */
    ngx_rtmp_mp4_field_32(b, 1);

    /* entry 0 is offset to the first IV in senc box */
    offset = (pos - moof_pos) + 20 + 16;
    ngx_rtmp_mp4_field_32(b, offset);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_senc(ngx_buf_t *b, char type,
    uint32_t sample_count, ngx_rtmp_mp4_sample_t *samples)
{
    u_char    *pos;
    uint32_t   i;

    pos = ngx_rtmp_mp4_start_box(b, "senc");

    /* version & flag */
    if (type == 'v') {
        /* video use sub_sample flag 0x02 */
        ngx_rtmp_mp4_field_32(b, 0x02);
    } else {
        /* audio use full_sample flag 0x00 */
        ngx_rtmp_mp4_field_32(b, 0);
    }

    /* sample count */
    ngx_rtmp_mp4_field_32(b, sample_count);

    for (i = 0; i < sample_count; i++, samples++) {

        /* IV per sample */
        ngx_rtmp_mp4_data(b, samples->iv, NGX_RTMP_CENC_IV_SIZE);

        /* subsample informations */
        if (type == 'v') {

            /* sub sample count */
            ngx_rtmp_mp4_field_16(b, 1);

            /* sub sample clear data size */
            ngx_rtmp_mp4_field_16(b, samples->clear_size);

            /* sub sample protected data size */
            ngx_rtmp_mp4_field_32(b, samples->size - samples->clear_size);

        }
    }

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_traf(ngx_buf_t *b, uint32_t earliest_pres_time,
    char type, uint32_t sample_count, ngx_rtmp_mp4_sample_t *samples,
    ngx_uint_t sample_mask, u_char *moof_pos, ngx_flag_t is_protected)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "traf");

    ngx_rtmp_mp4_write_tfhd(b);
    ngx_rtmp_mp4_write_tfdt(b, earliest_pres_time);
    ngx_rtmp_mp4_write_trun(b, type, sample_count, samples, sample_mask, 
        moof_pos, is_protected);

    if (is_protected) {
        ngx_rtmp_mp4_write_saiz(b, type, sample_count);
        ngx_rtmp_mp4_write_saio(b, moof_pos);
        ngx_rtmp_mp4_write_senc(b, type, sample_count, samples);
    }

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mp4_write_mfhd(ngx_buf_t *b, uint32_t index)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "mfhd");

    /* don't know what this is */
    ngx_rtmp_mp4_field_32(b, 0);

    /* fragment index. */
    ngx_rtmp_mp4_field_32(b, index);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mp4_write_sidx(ngx_buf_t *b, ngx_uint_t reference_size,
    uint32_t earliest_pres_time, uint32_t latest_pres_time)
{
    u_char    *pos;
    uint32_t   duration;

    duration = latest_pres_time - earliest_pres_time;

    pos = ngx_rtmp_mp4_start_box(b, "sidx");

    /* version */
    ngx_rtmp_mp4_field_32(b, 0);

    /* reference id */
    ngx_rtmp_mp4_field_32(b, 1);

    /* timescale */
    ngx_rtmp_mp4_field_32(b, 1000);

    /* earliest presentation time */
    ngx_rtmp_mp4_field_32(b, earliest_pres_time);

    /* first offset */
    ngx_rtmp_mp4_field_32(b, duration); /*TODO*/

    /* reserved */
    ngx_rtmp_mp4_field_16(b, 0);

    /* reference count = 1 */
    ngx_rtmp_mp4_field_16(b, 1);

    /* 1st bit is reference type, the rest is reference size */
    ngx_rtmp_mp4_field_32(b, reference_size);

    /* subsegment duration */
    ngx_rtmp_mp4_field_32(b, duration);

    /* first bit is startsWithSAP (=1), next 3 bits are SAP type (=001) */
    ngx_rtmp_mp4_field_8(b, 0x90);

    /* SAP delta time */
    ngx_rtmp_mp4_field_24(b, 0);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mp4_write_moof(ngx_buf_t *b, uint32_t earliest_pres_time,
    char type, uint32_t sample_count, ngx_rtmp_mp4_sample_t *samples,
    ngx_uint_t sample_mask, uint32_t index, ngx_flag_t is_protected)
{
    u_char  *pos;

    pos = ngx_rtmp_mp4_start_box(b, "moof");

    ngx_rtmp_mp4_write_mfhd(b, index);
    ngx_rtmp_mp4_write_traf(b, earliest_pres_time, type, sample_count,
        samples, sample_mask, pos, is_protected);

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mp4_write_mdat(ngx_buf_t *b, ngx_uint_t size)
{
    ngx_rtmp_mp4_field_32(b, size);

    ngx_rtmp_mp4_box(b, "mdat");

    return NGX_OK;
}

ngx_int_t
ngx_rtmp_mp4_write_emsg(ngx_buf_t *b,
    uint32_t earliest_pres_time, uint32_t cuepoint_time, uint32_t duration_time, uint32_t id)
{
    u_char    *pos;
    uint32_t   delta_time;
    uint32_t   timescale = 1000;

    delta_time = cuepoint_time - earliest_pres_time; 

    pos = ngx_rtmp_mp4_start_box(b, "emsg");

    /* version & flag */
    ngx_rtmp_mp4_field_32(b, 0);

    /* scheme_id_uri */
    ngx_rtmp_mp4_data(b, "urn:scte:scte35:2013:xml", sizeof("urn:scte:scte35:2013:xml"));

    /* value */
    ngx_rtmp_mp4_data(b, "1", sizeof("1"));

    /* timescale */
    ngx_rtmp_mp4_field_32(b, timescale);

    /* presentation_time_delta */
    ngx_rtmp_mp4_field_32(b, delta_time);

    /* duration */
    ngx_rtmp_mp4_field_32(b, duration_time);

    /* id */
    ngx_rtmp_mp4_field_32(b, id);

#define SCTE_EVENT "<SpliceInfoSection ptsAdjustment=\"0\" scte35:tier=\"4095\">\
 <SpliceInsert spliceEventId=\"1\" spliceEventCancelIndicator=\"false\" outOfNetworkIndicator=\"false\"\
  uniqueProgramId=\"1\" availNum=\"0\" availsExpected=\"0\" spliceImmediateFlag=\"true\" >\
 <Program><SpliceTime ptsTime=\"\"/></Program>\
 <BreakDuration autoReturn=\"false\" duration=\"\"/>\
</SpliceInsert></SpliceInfoSection>"    

    /* data */
    ngx_rtmp_mp4_data(b, SCTE_EVENT, sizeof(SCTE_EVENT));

    ngx_rtmp_mp4_update_box_size(b, pos);

    return NGX_OK;

}

