
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_mpegts.h"
#include "ngx_rtmp_mpegts_crc.h"

#include "ngx_rtmp_codec_module.h"

static u_char ngx_rtmp_mpegts_header[] = {

    /* https://en.wikipedia.org/wiki/MPEG_transport_stream#Packet */

    /* TS Header */
    0x47,                                               // Sync byte
    0x40, 0x00,                                         // TEI(1) + PUS(1) + TP(1) + PID(13)
    0x10,                                               // TSC(2) + AFF(1) + PF(1) + CC(4)
    0x00,                                               // adaption_field_length(8)
    
    /* PAT */
    0x00,                                               // table_id(8)
    0xb0, 0x0d,                                         // 1011b(4) + section_length(12)
    0x00, 0x01,                                         // transport_stream_id(16)
    0xc1, 0x00, 0x00,                                   // 11b(2) + VN(5) + CNI(1), section_no(8), last_section_no(8)
    /* PAT program loop */
    0x00, 0x01, 0xef, 0xff,                             // program_number(16), reserved(3) + program_map_pid(13)
    /* PAT crc (CRC-32-MPEG2) */
    0x36, 0x90, 0xe2, 0x3d,                             // !!! Needs to be recalculated each time any bit in PAT is modified (which we dont do at the moment) !!!

    /* stuffing 167 bytes */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* TS Header */
    0x47,                                               // Sync byte
    0x4f, 0xff,                                         // TEI(1) + PUS(1) + TP(1) + PID(13)
    0x10,                                               // TSC(2) + AFF(1) + PF(1) + CC(4)
    0x00,                                               // adaption_field_length(8)
    
    /* PMT */
    0x02,                                               // table_id(8)
    0xb0, 0x12,                                         // 1011b(4) + section_length(12) (section length set below. Ignore this value in here)
    0x00, 0x01,                                         // program_number(16)
    0xc1, 0x00, 0x00,                                   // 11b(2) + VN(5) + CNI(1), section_no(8), last_section_no(8)
    0xe1, 0x00,                                         // reserved(3) + PCR_PID(13)
    0xf0, 0x00,                                         // reserved(4) + program_info_length(12)
    
    /* PMT component loop, looped through when writing header */
    /* Max size of 14 bytes */
    /* Also includes the PMT CRC, calculated dynamically */
    0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff,

    /* stuffing 157 bytes */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static u_char ngx_rtmp_mpegts_header_h264[] = {
	//H.264 Video, PID 0x100
    0x1b,                                               // stream_type(8)
    0xe1, 0x00,                                         // reserved(3) + elementary_PID(13)
    0xf0, 0x00                                         // reserved(4) + ES_info_length(12)
};

static u_char ngx_rtmp_mpegts_header_mp3[] = {
	//MP3 Audio, PID 0x101
    0x03,                                               // stream_type(8)
    0xe1, 0x01,                                         // reserved(3) + elementary_PID(13)
    0xf0, 0x00                                          // reserved(4) + ES_info_length(12)
};

static u_char ngx_rtmp_mpegts_header_aac[] = {
    //ADTS AAC Audio, PID 0x101
    0x0f,                                               // stream_type(8)
    0xe1, 0x01,                                         // reserved(3) + elementary_PID(13)
    0xf0, 0x00                                          // reserved(4) + ES_info_length(12)
};

#define NGX_RTMP_MPEGTS_PMT_CRC_START_OFFSET 193
#define NGX_RTMP_MPEGTS_PMT_CRC_MIN_LENGTH 12
#define NGX_RTMP_MPEGTS_PMT_SECTION_LENGTH_OFFSET 195
#define NGX_RTMP_MPEGTS_PMT_LOOP_OFFSET 205
#define NGX_RTMP_MPEGTS_PID_SIZE 5

/* 700 ms PCR delay */
#define NGX_RTMP_HLS_DELAY  63000


static ngx_int_t
ngx_rtmp_mpegts_write_file(ngx_rtmp_mpegts_file_t *file, u_char *in,
    size_t in_size)
{
    u_char   *out;
    size_t    out_size, n;
    ssize_t   rc;

    static u_char  buf[1024];

    if (!file->encrypt) {
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, file->log, 0,
                       "mpegts: write %uz bytes", in_size);

        rc = ngx_write_fd(file->fd, in, in_size);
        if (rc < 0) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    /* encrypt */

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, file->log, 0,
                   "mpegts: write %uz encrypted bytes", in_size);

    out = buf;
    out_size = sizeof(buf);

    if (file->size > 0 && file->size + in_size >= 16) {
        ngx_memcpy(file->buf + file->size, in, 16 - file->size);

        in += 16 - file->size;
        in_size -= 16 - file->size;

        AES_cbc_encrypt(file->buf, out, 16, &file->key, file->iv, AES_ENCRYPT);

        out += 16;
        out_size -= 16;

        file->size = 0;
    }

    for ( ;; ) {
        n = in_size & ~0x0f;

        if (n > 0) {
            if (n > out_size) {
                n = out_size;
            }

            AES_cbc_encrypt(in, out, n, &file->key, file->iv, AES_ENCRYPT);

            in += n;
            in_size -= n;

        } else if (out == buf) {
            break;
        }

        rc = ngx_write_fd(file->fd, buf, out - buf + n);
        if (rc < 0) {
            return NGX_ERROR;
        }

        out = buf;
        out_size = sizeof(buf);
    }

    if (in_size) {
        ngx_memcpy(file->buf + file->size, in, in_size);
        file->size += in_size;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_mpegts_write_header(ngx_rtmp_mpegts_file_t *file, ngx_rtmp_codec_ctx_t *codec_ctx, ngx_uint_t mpegts_cc)
{
    ngx_int_t next_pid_offset = 0; //Used to track the number of PIDs we have and the offset in 5-byte chunks

    //MPEG-TS CC is 4 bits long. Need to truncate it here.
    mpegts_cc %= 0x0f;
    // And then put it in the headers
    ngx_rtmp_mpegts_header[3] = (ngx_rtmp_mpegts_header[3] & 0xf0) + (u_char)mpegts_cc;
    ngx_rtmp_mpegts_header[191] = (ngx_rtmp_mpegts_header[191] & 0xf0) + (u_char)mpegts_cc;

    //ngx_rtmp_mpegts_header 

    if (codec_ctx->video_codec_id)
    {
        //Put h264 PID in the PMT
        ngx_memcpy(ngx_rtmp_mpegts_header+NGX_RTMP_MPEGTS_PMT_LOOP_OFFSET+next_pid_offset, ngx_rtmp_mpegts_header_h264, NGX_RTMP_MPEGTS_PID_SIZE);

        next_pid_offset += NGX_RTMP_MPEGTS_PID_SIZE;
    }

    if (codec_ctx->audio_codec_id){
    	//Put Audio PID in the PMT
        if (codec_ctx->audio_codec_id == NGX_RTMP_AUDIO_AAC) {
            ngx_memcpy(ngx_rtmp_mpegts_header+NGX_RTMP_MPEGTS_PMT_LOOP_OFFSET+next_pid_offset, ngx_rtmp_mpegts_header_aac, NGX_RTMP_MPEGTS_PID_SIZE);
        }
        else 
        {
            ngx_memcpy(ngx_rtmp_mpegts_header+NGX_RTMP_MPEGTS_PMT_LOOP_OFFSET+next_pid_offset, ngx_rtmp_mpegts_header_mp3, NGX_RTMP_MPEGTS_PID_SIZE);
        }
    	next_pid_offset += NGX_RTMP_MPEGTS_PID_SIZE;
    }

    //Set section length of PMT
    //PMT is 13 bytes long without any programs in it. Add this in
    ngx_rtmp_mpegts_header[NGX_RTMP_MPEGTS_PMT_SECTION_LENGTH_OFFSET] = 13 + next_pid_offset;

    //Calculate CRC
    ngx_rtmp_mpegts_crc_t crc = ngx_rtmp_mpegts_crc_init();
    crc = ngx_rtmp_mpegts_crc_update(crc, ngx_rtmp_mpegts_header+NGX_RTMP_MPEGTS_PMT_CRC_START_OFFSET, NGX_RTMP_MPEGTS_PMT_CRC_MIN_LENGTH+next_pid_offset);
    crc = ngx_rtmp_mpegts_crc_finalize(crc);

    ngx_rtmp_mpegts_header[NGX_RTMP_MPEGTS_PMT_LOOP_OFFSET+next_pid_offset] = (crc >> 24) & 0xff;
    ngx_rtmp_mpegts_header[NGX_RTMP_MPEGTS_PMT_LOOP_OFFSET+next_pid_offset+1] = (crc >> 16) & 0xff;
    ngx_rtmp_mpegts_header[NGX_RTMP_MPEGTS_PMT_LOOP_OFFSET+next_pid_offset+2] = (crc >> 8) & 0xff;
    ngx_rtmp_mpegts_header[NGX_RTMP_MPEGTS_PMT_LOOP_OFFSET+next_pid_offset+3] = crc & 0xff;

    return ngx_rtmp_mpegts_write_file(file, ngx_rtmp_mpegts_header, sizeof(ngx_rtmp_mpegts_header));
}


static u_char *
ngx_rtmp_mpegts_write_pcr(u_char *p, uint64_t pcr)
{
    *p++ = (u_char) (pcr >> 25);
    *p++ = (u_char) (pcr >> 17);
    *p++ = (u_char) (pcr >> 9);
    *p++ = (u_char) (pcr >> 1);
    *p++ = (u_char) (pcr << 7 | 0x7e);
    *p++ = 0;

    return p;
}


static u_char *
ngx_rtmp_mpegts_write_pts(u_char *p, ngx_uint_t fb, uint64_t pts)
{
    ngx_uint_t val;

    val = fb << 4 | (((pts >> 30) & 0x07) << 1) | 1;
    *p++ = (u_char) val;

    val = (((pts >> 15) & 0x7fff) << 1) | 1;
    *p++ = (u_char) (val >> 8);
    *p++ = (u_char) val;

    val = (((pts) & 0x7fff) << 1) | 1;
    *p++ = (u_char) (val >> 8);
    *p++ = (u_char) val;

    return p;
}


ngx_int_t
ngx_rtmp_mpegts_write_frame(ngx_rtmp_mpegts_file_t *file,
    ngx_rtmp_mpegts_frame_t *f, ngx_buf_t *b)
{
    ngx_uint_t  pes_size, header_size, body_size, in_size, stuff_size, flags;
    u_char      packet[188], *p, *base;
    ngx_int_t   first, rc;

    ngx_log_debug6(NGX_LOG_DEBUG_CORE, file->log, 0,
                   "mpegts: pid=%ui, sid=%ui, pts=%uL, "
                   "dts=%uL, key=%ui, size=%ui",
                   f->pid, f->sid, f->pts, f->dts,
                   (ngx_uint_t) f->key, (size_t) (b->last - b->pos));

    first = 1;

    while (b->pos < b->last) {
        p = packet;

        f->cc++;

        *p++ = 0x47;
        *p++ = (u_char) (f->pid >> 8);

        if (first) {
            p[-1] |= 0x40;
        }

        *p++ = (u_char) f->pid;
        *p++ = 0x10 | (f->cc & 0x0f); /* payload */

        if (first) {

            packet[3] |= 0x20; /* adaptation */

            *p++ = 7;    /* size */
            *p++ = 0x50; /* random access + PCR */

            p = ngx_rtmp_mpegts_write_pcr(p, f->dts - NGX_RTMP_HLS_DELAY);

            /* PES header */

            *p++ = 0x00;
            *p++ = 0x00;
            *p++ = 0x01;
            *p++ = (u_char) f->sid;

            header_size = 5;
            flags = 0x80; /* PTS */

            if (f->dts != f->pts) {
                header_size += 5;
                flags |= 0x40; /* DTS */
            }

            pes_size = (b->last - b->pos) + header_size + 3;
            if (pes_size > 0xffff) {
                pes_size = 0;
            }

            *p++ = (u_char) (pes_size >> 8);
            *p++ = (u_char) pes_size;
            *p++ = 0x80; /* H222 */
            *p++ = (u_char) flags;
            *p++ = (u_char) header_size;

            p = ngx_rtmp_mpegts_write_pts(p, flags >> 6, f->pts +
                                                         NGX_RTMP_HLS_DELAY);

            if (f->dts != f->pts) {
                p = ngx_rtmp_mpegts_write_pts(p, 1, f->dts +
                                                    NGX_RTMP_HLS_DELAY);
            }

            first = 0;
        }

        body_size = (ngx_uint_t) (packet + sizeof(packet) - p);
        in_size = (ngx_uint_t) (b->last - b->pos);

        if (body_size <= in_size) {
            ngx_memcpy(p, b->pos, body_size);
            b->pos += body_size;

        } else {
            stuff_size = (body_size - in_size);

            if (packet[3] & 0x20) {

                /* has adaptation */

                base = &packet[5] + packet[4];
                p = ngx_movemem(base + stuff_size, base, p - base);
                ngx_memset(base, 0xff, stuff_size);
                packet[4] += (u_char) stuff_size;

            } else {

                /* no adaptation */

                packet[3] |= 0x20;
                p = ngx_movemem(&packet[4] + stuff_size, &packet[4],
                                p - &packet[4]);

                packet[4] = (u_char) (stuff_size - 1);
                if (stuff_size >= 2) {
                    packet[5] = 0;
                    ngx_memset(&packet[6], 0xff, stuff_size - 2);
                }
            }

            ngx_memcpy(p, b->pos, in_size);
            b->pos = b->last;
        }

        rc = ngx_rtmp_mpegts_write_file(file, packet, sizeof(packet));
        if (rc != NGX_OK) {
            return rc;
        }
    }

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mpegts_init_encryption(ngx_rtmp_mpegts_file_t *file,
    u_char *key, size_t key_len, uint64_t iv)
{
    if (AES_set_encrypt_key(key, key_len * 8, &file->key)) {
        return NGX_ERROR;
    }

    ngx_memzero(file->iv, 8);

    file->iv[8]  = (u_char) (iv >> 56);
    file->iv[9]  = (u_char) (iv >> 48);
    file->iv[10] = (u_char) (iv >> 40);
    file->iv[11] = (u_char) (iv >> 32);
    file->iv[12] = (u_char) (iv >> 24);
    file->iv[13] = (u_char) (iv >> 16);
    file->iv[14] = (u_char) (iv >> 8);
    file->iv[15] = (u_char) (iv);

    file->encrypt = 1;

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mpegts_open_file(ngx_rtmp_mpegts_file_t *file, u_char *path,
    ngx_log_t *log, ngx_rtmp_codec_ctx_t *codec_ctx, ngx_uint_t mpegts_cc)
{
    file->log = log;

    file->fd = ngx_open_file(path, NGX_FILE_WRONLY, NGX_FILE_TRUNCATE,
                             NGX_FILE_DEFAULT_ACCESS);

    if (file->fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, log, ngx_errno,
                      "hls: error creating fragment file");
        return NGX_ERROR;
    }

    file->size = 0;

    if (ngx_rtmp_mpegts_write_header(file, codec_ctx, mpegts_cc) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, log, ngx_errno,
                      "hls: error writing fragment header");
        ngx_close_file(file->fd);
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_rtmp_mpegts_close_file(ngx_rtmp_mpegts_file_t *file)
{
    u_char   buf[16];
    ssize_t  rc;

    if (file->encrypt) {
        ngx_memset(file->buf + file->size, 16 - file->size, 16 - file->size);

        AES_cbc_encrypt(file->buf, buf, 16, &file->key, file->iv, AES_ENCRYPT);

        rc = ngx_write_fd(file->fd, buf, 16);
        if (rc < 0) {
            return NGX_ERROR;
        }
    }

    ngx_close_file(file->fd);

    return NGX_OK;
}
