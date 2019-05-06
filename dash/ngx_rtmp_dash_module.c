

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_rtmp.h>
#include <ngx_rtmp_codec_module.h>
#include "ngx_rtmp_live_module.h"
#include "ngx_rtmp_mp4.h"
#include "ngx_rtmp_dash_templates.h"
#include "ngx_rtmp_cenc.h"


static ngx_rtmp_publish_pt              next_publish;
static ngx_rtmp_close_stream_pt         next_close_stream;
static ngx_rtmp_stream_begin_pt         next_stream_begin;
static ngx_rtmp_stream_eof_pt           next_stream_eof;
static ngx_rtmp_playlist_pt             next_playlist;


static char * ngx_rtmp_dash_variant(ngx_conf_t *cf, ngx_command_t *cmd,
       void *conf);
static ngx_int_t ngx_rtmp_dash_postconfiguration(ngx_conf_t *cf);
static void * ngx_rtmp_dash_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_dash_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
static ngx_int_t ngx_rtmp_dash_write_init_segments(ngx_rtmp_session_t *s,
       ngx_rtmp_cenc_drm_info_t *drmi);
static ngx_int_t ngx_rtmp_dash_ensure_directory(ngx_rtmp_session_t *s);


#define NGX_RTMP_DASH_BUFSIZE           (1024*1024)
#define NGX_RTMP_DASH_MAX_MDAT          (10*1024*1024)
#define NGX_RTMP_DASH_MAX_SAMPLES       1024
#define NGX_RTMP_DASH_DIR_ACCESS        0744

#define NGX_RTMP_DASH_GMT_LENGTH        sizeof("1970-09-28T12:00:00+06:00")

typedef struct {
    uint32_t                            timestamp;
    uint32_t                            duration;
} ngx_rtmp_dash_frag_t;


typedef struct {
    ngx_uint_t                          id;
    ngx_uint_t                          opened;
    ngx_uint_t                          mdat_size;
    ngx_uint_t                          sample_count;
    ngx_uint_t                          sample_mask;
    ngx_fd_t                            fd;
    char                                type;
    uint32_t                            earliest_pres_time;
    uint32_t                            latest_pres_time;
    unsigned                            is_protected:1; 
    u_char                              key[NGX_RTMP_CENC_KEY_SIZE];
    u_char                              iv[NGX_RTMP_CENC_IV_SIZE];
    ngx_rtmp_mp4_sample_t               samples[NGX_RTMP_DASH_MAX_SAMPLES];
} ngx_rtmp_dash_track_t;


typedef struct {
    ngx_str_t                           suffix;
    ngx_array_t                         args;
} ngx_rtmp_dash_variant_t;


typedef struct {
    ngx_str_t                           segments;
    ngx_str_t                           segments_bak;
    ngx_str_t                           playlist;
    ngx_str_t                           playlist_bak;
    ngx_str_t                           var_playlist;
    ngx_str_t                           var_playlist_bak;
    ngx_str_t                           name;
    ngx_str_t                           varname;
    ngx_str_t                           stream;
    ngx_time_t                          start_time;

    ngx_uint_t                          nfrags;
    ngx_uint_t                          frag;
    ngx_rtmp_dash_frag_t               *frags; /* circular 2 * winfrags + 1 */

    unsigned                            opened:1;
    unsigned                            has_video:1;
    unsigned                            has_audio:1;
    unsigned                            start_cuepoint:1;
    unsigned                            end_cuepoint:1;

    uint32_t                            cuepoint_starttime;
    uint32_t                            cuepoint_endtime;
    uint32_t                            cuepoint_duration;
    uint32_t                            cuepoint_id;

    ngx_file_t                          video_file;
    ngx_file_t                          audio_file;

    ngx_uint_t                          id;

    ngx_rtmp_dash_track_t               audio;
    ngx_rtmp_dash_track_t               video;
    ngx_rtmp_dash_variant_t            *var;

    ngx_rtmp_cenc_drm_info_t            drm_info;

} ngx_rtmp_dash_ctx_t;


typedef struct {
    ngx_str_t                           path;
    ngx_msec_t                          playlen;
} ngx_rtmp_dash_cleanup_t;


#define NGX_RTMP_DASH_CLOCK_COMPENSATION_OFF       1
#define NGX_RTMP_DASH_CLOCK_COMPENSATION_NTP       2
#define NGX_RTMP_DASH_CLOCK_COMPENSATION_HTTP_HEAD 3
#define NGX_RTMP_DASH_CLOCK_COMPENSATION_HTTP_ISO  4

static ngx_conf_enum_t                  ngx_rtmp_dash_clock_compensation_type_slots[] = {
    { ngx_string("off"),                NGX_RTMP_DASH_CLOCK_COMPENSATION_OFF },
    { ngx_string("ntp"),                NGX_RTMP_DASH_CLOCK_COMPENSATION_NTP },
    { ngx_string("http_head"),          NGX_RTMP_DASH_CLOCK_COMPENSATION_HTTP_HEAD },
    { ngx_string("http_iso"),           NGX_RTMP_DASH_CLOCK_COMPENSATION_HTTP_ISO },
    { ngx_null_string,                  0 }
};

#define NGX_RTMP_DASH_AD_MARKERS_OFF                1
#define NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT        2
#define NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT_SCTE35 3

static ngx_conf_enum_t                   ngx_rtmp_dash_ad_markers_type_slots[] = {
    { ngx_string("off"),                 NGX_RTMP_DASH_AD_MARKERS_OFF },
    { ngx_string("on_cuepoint"),         NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT },
    { ngx_string("on_cuepoint_scte35"),  NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT_SCTE35 },
    { ngx_null_string,                   0 }
};

typedef struct {
    ngx_flag_t                          dash;
    ngx_msec_t                          fraglen;
    ngx_msec_t                          playlen;
    ngx_flag_t                          nested;
    ngx_flag_t                          cenc;
    ngx_str_t                           cenc_key;
    ngx_str_t                           cenc_kid;
    ngx_flag_t                          wdv;
    ngx_str_t                           wdv_data;
    ngx_flag_t                          mspr;
    ngx_str_t                           mspr_data;
    ngx_str_t                           mspr_kid;
    ngx_str_t                           mspr_pro;
    ngx_flag_t                          repetition;
    ngx_uint_t                          clock_compensation;     // Try to compensate clock drift
                                                                //  between client and server (on client side)
    ngx_str_t                           clock_helper_uri;       // Use uri to static file on HTTP server
                                                                // - same machine as RTMP/DASH)
                                                                // - or NTP server address
    ngx_str_t                           path;
    ngx_uint_t                          winfrags;
    ngx_flag_t                          cleanup;
    ngx_path_t                         *slot;
    ngx_array_t                        *variant;
    ngx_uint_t                          ad_markers;
} ngx_rtmp_dash_app_conf_t;


static ngx_command_t ngx_rtmp_dash_commands[] = {

    { ngx_string("dash"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, dash),
      NULL },

    { ngx_string("dash_fragment"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, fraglen),
      NULL },

    { ngx_string("dash_path"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, path),
      NULL },

    { ngx_string("dash_playlist_length"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, playlen),
      NULL },

    { ngx_string("dash_cleanup"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, cleanup),
      NULL },

    { ngx_string("dash_nested"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, nested),
      NULL },

    { ngx_string("dash_repetition"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, repetition),
      NULL },

    { ngx_string("dash_cenc"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, cenc),
      NULL },

    { ngx_string("dash_cenc_key"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, cenc_key),
      NULL },

    { ngx_string("dash_cenc_kid"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, cenc_kid),
      NULL },

    { ngx_string("dash_wdv"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, wdv),
      NULL },

    { ngx_string("dash_wdv_data"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, wdv_data),
      NULL },

    { ngx_string("dash_mspr"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, mspr),
      NULL },

    { ngx_string("dash_mspr_data"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, mspr_data),
      NULL },

    { ngx_string("dash_mspr_kid"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, mspr_kid),
      NULL },

    { ngx_string("dash_mspr_pro"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, mspr_pro),
      NULL },

    { ngx_string("dash_clock_compensation"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, clock_compensation),
      &ngx_rtmp_dash_clock_compensation_type_slots },

    { ngx_string("dash_clock_helper_uri"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, clock_helper_uri),
      NULL },

    { ngx_string("dash_variant"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_1MORE,
      ngx_rtmp_dash_variant,
      NGX_RTMP_APP_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("dash_ad_markers"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_dash_app_conf_t, ad_markers),
      &ngx_rtmp_dash_ad_markers_type_slots },

    ngx_null_command
};


static ngx_rtmp_module_t  ngx_rtmp_dash_module_ctx = {
    NULL,                               /* preconfiguration */
    ngx_rtmp_dash_postconfiguration,    /* postconfiguration */

    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */

    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */

    ngx_rtmp_dash_create_app_conf,      /* create location configuration */
    ngx_rtmp_dash_merge_app_conf,       /* merge location configuration */
};


ngx_module_t  ngx_rtmp_dash_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_dash_module_ctx,          /* module context */
    ngx_rtmp_dash_commands,             /* module directives */
    NGX_RTMP_MODULE,                    /* module type */
    NULL,                               /* init master */
    NULL,                               /* init module */
    NULL,                               /* init process */
    NULL,                               /* init thread */
    NULL,                               /* exit thread */
    NULL,                               /* exit process */
    NULL,                               /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_rtmp_dash_frag_t *
ngx_rtmp_dash_get_frag(ngx_rtmp_session_t *s, ngx_int_t n)
{
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_dash_app_conf_t  *dacf;

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    return &ctx->frags[(ctx->frag + n) % (dacf->winfrags * 2 + 1)];
}


static void
ngx_rtmp_dash_next_frag(ngx_rtmp_session_t *s)
{
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_dash_app_conf_t  *dacf;

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    if (ctx->nfrags == dacf->winfrags) {
        ctx->frag++;
    } else {
        ctx->nfrags++;
    }
}


static ngx_int_t
ngx_rtmp_dash_rename_file(u_char *src, u_char *dst)
{
    /* rename file with overwrite */

#if (NGX_WIN32)
    return MoveFileEx((LPCTSTR) src, (LPCTSTR) dst, MOVEFILE_REPLACE_EXISTING);
#else
    return ngx_rename_file(src, dst);
#endif
}


static ngx_uint_t
ngx_rtmp_dash_gcd(ngx_uint_t m, ngx_uint_t n)
{
    /* greatest common divisor */

    ngx_uint_t   temp;

    while (n) {
        temp=n;
        n=m % n;
        m=temp;
    }
    return m;
}


static u_char *
ngx_rtmp_dash_write_segment(u_char *p, u_char *last, ngx_uint_t t,
    ngx_uint_t d, ngx_uint_t r)
{
    if (r == 0) {
        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_TIME, t, d);
    } else {
        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_TIME_WITH_REPETITION, t, d, r);
    }

    return p;
}


static u_char *
ngx_rtmp_dash_write_segment_timeline(ngx_rtmp_session_t *s, ngx_rtmp_dash_ctx_t *ctx, 
    ngx_rtmp_dash_app_conf_t *dacf, u_char *p, u_char *last)
{
    ngx_uint_t              i, t, d, r;
    ngx_rtmp_dash_frag_t    *f;

    for (i = 0; i < ctx->nfrags; i++) {
        f = ngx_rtmp_dash_get_frag(s, i);

        if (dacf->repetition) {
            if (i == 0) {
                t = f->timestamp;
                d = f->duration;
                r = 0;
            } else {
                if (f->duration == d) {
                    r++;
                } else {
                    p = ngx_rtmp_dash_write_segment(p, last, t, d, r);
                    t = f->timestamp;
                    d = f->duration;
                    r = 0;
                }
            }
            if (i == ctx->nfrags - 1) {
                p = ngx_rtmp_dash_write_segment(p, last, t, d, r);
            }
        } else {
            t = f->timestamp;
            d = f->duration;
            p = ngx_rtmp_dash_write_segment(p, last, t, d, 0);
        }
    }

    return p;
}


static u_char *
ngx_rtmp_dash_write_content_protection(ngx_rtmp_session_t *s,
    ngx_rtmp_cenc_drm_info_t *drmi, u_char *p, u_char *last)
{
    u_char     *k;
    ngx_str_t   cenc_pssh;

    k = drmi->kid; 

    ngx_rtmp_cenc_content_protection_pssh(k, &cenc_pssh);

    p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_CONTENT_PROTECTION_CENC,
        k[0], k[1], k[2], k[3],
        k[4], k[5], k[6], k[7],
        k[8], k[9], k[10], k[11], k[12], k[13], k[14], k[15]);


    p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_CONTENT_PROTECTION_PSSH_CENC,
        &cenc_pssh);

    if (drmi->wdv) {
         p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_CONTENT_PROTECTION_PSSH_WDV,
             &drmi->wdv_data);
    }

    if (drmi->mspr) {
         p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_CONTENT_PROTECTION_PSSH_MSPR,
             &drmi->mspr_data,
             &drmi->mspr_kid,
	     &drmi->mspr_pro);
    }

    return p;
}


static ngx_int_t
ngx_rtmp_dash_write_variant_playlist(ngx_rtmp_session_t *s)
{
    char                      *sep;
    u_char                    *p, *last;
    ssize_t                    n;
    ngx_fd_t                   fd, fds;
    struct tm                  tm;
    ngx_uint_t                 i, j, k, frame_rate_num, frame_rate_denom;
    ngx_uint_t                 depth_msec, depth_sec;
    ngx_uint_t                 update_period, update_period_msec;
    ngx_uint_t                 start_time, buffer_time, buffer_time_msec;
    ngx_uint_t                 presentation_delay, presentation_delay_msec;
    ngx_uint_t                 gcd, par_x, par_y;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    ngx_rtmp_dash_frag_t      *f;
    ngx_rtmp_dash_app_conf_t  *dacf;
    ngx_rtmp_dash_variant_t   *var;
    ngx_str_t                 *arg;

    ngx_rtmp_playlist_t        v;

    static u_char              buffer[NGX_RTMP_DASH_BUFSIZE];
    static u_char              available_time[NGX_RTMP_DASH_GMT_LENGTH];
    static u_char              publish_time[NGX_RTMP_DASH_GMT_LENGTH];
    static u_char              buffer_depth[sizeof("P00Y00M00DT00H00M00.000S")];
    static u_char              frame_rate[(NGX_INT_T_LEN * 2) + 2];
    static u_char              seg_path[NGX_MAX_PATH + 1];

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (dacf == NULL || ctx == NULL || codec_ctx == NULL) {
        return NGX_ERROR;
    }

    fd = ngx_open_file(ctx->var_playlist_bak.data, NGX_FILE_WRONLY,
                       NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: open failed: '%V'", &ctx->var_playlist_bak);
        return NGX_ERROR;
    }

    /* availabity and publish time should be relative to peer epoch */
    start_time = ctx->start_time.sec - (s->peer_epoch/1000);
    ngx_log_debug4(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "Fixing start_time=%uD %uD epoch=%uD new_start_time=%uD",
            (uint32_t)ctx->start_time.sec, (uint32_t)ctx->start_time.msec,
            (uint32_t)s->peer_epoch,
            (uint32_t)start_time);

    /**
     * Availability time must be equal stream start time
     * Cos segments time counting from it
     */
    ngx_libc_gmtime(start_time, &tm);
    *ngx_sprintf(available_time, "%4d-%02d-%02dT%02d:%02d:%02dZ",
             tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour,
             tm.tm_min, tm.tm_sec
             ) = 0;

    /* Stream publish time */
    *ngx_sprintf(publish_time, "%s", available_time) = 0;

    depth_sec = (ngx_uint_t) (
                 ngx_rtmp_dash_get_frag(s, ctx->nfrags - 1)->timestamp +
                 ngx_rtmp_dash_get_frag(s, ctx->nfrags - 1)->duration - 
                 ngx_rtmp_dash_get_frag(s, 0)->timestamp);

    depth_msec = depth_sec % 1000;
    depth_sec -= depth_msec;
    depth_sec /= 1000;

    ngx_libc_gmtime(depth_sec, &tm);

    *ngx_sprintf(buffer_depth, "P%dY%02dM%02dDT%dH%02dM%02d.%03dS",
                 tm.tm_year - 70, tm.tm_mon,
                 tm.tm_mday - 1, tm.tm_hour,
                 tm.tm_min, tm.tm_sec,
                 depth_msec) = 0;

    last = buffer + sizeof(buffer);

    /**
     * Calculate playlist minimal update period
     * This should be more than biggest segment duration
     * Cos segments rounded by keyframe/GOP.
     * And that time not always equals to fragment length.
     */
    update_period = dacf->fraglen;

    for (i = 0; i < ctx->nfrags; i++) {
        f = ngx_rtmp_dash_get_frag(s, i);
        if (f->duration > update_period) {
            update_period = f->duration;
        }
    }

    // Reasonable delay for streaming
    presentation_delay = update_period * 2 + 1000;
    presentation_delay_msec = presentation_delay % 1000;
    presentation_delay -= presentation_delay_msec;
    presentation_delay /= 1000;

    // Calculate msec part and seconds
    update_period_msec = update_period % 1000;
    update_period -= update_period_msec;
    update_period /= 1000;

    // Buffer length by default fragment length
    buffer_time = dacf->fraglen;
    buffer_time_msec = buffer_time % 1000;
    buffer_time -= buffer_time_msec;
    buffer_time /= 1000;

    // Fill DASH header
    p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_HEADER,
                     // availabilityStartTime
                     available_time,
                     // publishTime
                     publish_time,
                     // minimumUpdatePeriod
                     update_period, update_period_msec,
                     // minBufferTime
                     buffer_time, buffer_time_msec,
                     // timeShiftBufferDepth
                     buffer_depth,
                     // suggestedPresentationDelay
                     presentation_delay, presentation_delay_msec
                     );

    p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_PERIOD);

    n = ngx_write_fd(fd, buffer, p - buffer);

    sep = (dacf->nested ? "/" : "-");
    var = dacf->variant->elts;

    if (ctx->has_video) {
        frame_rate_num = (ngx_uint_t) (codec_ctx->frame_rate * 1000.);

        if (frame_rate_num % 1000 == 0) {
            *ngx_sprintf(frame_rate, "%ui", frame_rate_num / 1000) = 0;
        } else {
            frame_rate_denom = 1000;
            switch (frame_rate_num) {
                case 23976:
                    frame_rate_num = 24000;
                    frame_rate_denom = 1001;
                    break;
                case 29970:
                    frame_rate_num = 30000;
                    frame_rate_denom = 1001;
                    break;
                case 59940:
                    frame_rate_num = 60000;
                    frame_rate_denom = 1001;
                    break;
            }

            *ngx_sprintf(frame_rate, "%ui/%ui", frame_rate_num, frame_rate_denom) = 0;
        }

        gcd = ngx_rtmp_dash_gcd(codec_ctx->width, codec_ctx->height);
        par_x = codec_ctx->width / gcd;
        par_y = codec_ctx->height / gcd;

        p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_ADAPTATIONSET_VIDEO,
                         codec_ctx->width,
                         codec_ctx->height,
                         frame_rate,
                         par_x, par_y);

        switch (dacf->ad_markers) {
            case NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT:
            case NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT_SCTE35:
                p = ngx_slprintf(p, last, NGX_RTMP_DASH_INBAND_EVENT);
        }

        if (dacf->cenc) {
            p = ngx_rtmp_dash_write_content_protection(s, &ctx->drm_info, p, last);
        }

        n = ngx_write_fd(fd, buffer, p - buffer);

        for (j = 0; j < dacf->variant->nelts; j++, var++) {

            /* read segments file */
            if (dacf->nested) {
                *ngx_sprintf(seg_path, "%V/%V%V/index.seg",
                             &dacf->path, &ctx->varname, &var->suffix) = 0;
            } else {
                *ngx_sprintf(seg_path, "%V/%V%V.seg",
                             &dacf->path, &ctx->varname, &var->suffix) = 0;
            }

            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                          "dash: read segments file for variant '%s'", seg_path);

            fds = ngx_open_file(seg_path, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);

            if (fds == NGX_INVALID_FILE) {
                ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                              "dash: open failed: segments '%s'", seg_path);
                continue;
            } 

            p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_REPRESENTATION_VARIANT_VIDEO,
                             &ctx->varname, &var->suffix,
                             codec_ctx->avc_profile,
                             codec_ctx->avc_compat,
                             codec_ctx->avc_level);

            arg = var->args.elts;
            for (k = 0; k < var->args.nelts && k < 3 ; k++, arg++) {
                p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_VARIANT_ARG, arg);
            }

            p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_VARIANT_ARG_FOOTER);

            p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_SEGMENTTPL_VARIANT_VIDEO,
                             &ctx->varname, &var->suffix, sep,
                             &ctx->varname, &var->suffix, sep);

            n = ngx_write_fd(fd, buffer, p - buffer);

            while ((n = ngx_read_fd(fds, buffer, sizeof(buffer)))) {
                n = ngx_write_fd(fd, buffer, n);
            }

            ngx_close_file(fds);

            p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_REPRESENTATION_VIDEO_FOOTER);
            n = ngx_write_fd(fd, buffer, p - buffer);

        }

        p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_ADAPTATIONSET_VIDEO_FOOTER);
        n = ngx_write_fd(fd, buffer, p - buffer);
    }

    if (ctx->has_audio) {
        p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_ADAPTATIONSET_AUDIO);

        if (dacf->cenc) {
            p = ngx_rtmp_dash_write_content_protection(s, &ctx->drm_info, p, last);
        }

        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_REPRESENTATION_AUDIO,
                         &ctx->name,
                         codec_ctx->audio_codec_id == NGX_RTMP_AUDIO_AAC ?
                         (codec_ctx->aac_sbr ? "40.5" : "40.2") : "6b",
                         codec_ctx->sample_rate,
                         (ngx_uint_t) (codec_ctx->audio_data_rate * 1000),
                         &ctx->name, sep,
                         &ctx->name, sep);

        p = ngx_rtmp_dash_write_segment_timeline(s, ctx, dacf, p, last);

        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_REPRESENTATION_AUDIO_FOOTER);

        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_ADAPTATIONSET_AUDIO_FOOTER);

        n = ngx_write_fd(fd, buffer, p - buffer);
    }

    p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_PERIOD_FOOTER);
    n = ngx_write_fd(fd, buffer, p - buffer);

    /* UTCTiming value */
    switch (dacf->clock_compensation) {
        case NGX_RTMP_DASH_CLOCK_COMPENSATION_NTP:
                p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_CLOCK,
                                 "ntp",
                                 &dacf->clock_helper_uri
                );
                n = ngx_write_fd(fd, buffer, p - buffer);
        break;
        case NGX_RTMP_DASH_CLOCK_COMPENSATION_HTTP_HEAD:
                p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_CLOCK,
                                 "http-head",
                                 &dacf->clock_helper_uri
                );
                n = ngx_write_fd(fd, buffer, p - buffer);
        break;
        case NGX_RTMP_DASH_CLOCK_COMPENSATION_HTTP_ISO:
                p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_CLOCK,
                                 "http-iso",
                                 &dacf->clock_helper_uri
                );
                n = ngx_write_fd(fd, buffer, p - buffer);
        break;
    }

    p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_FOOTER);
    n = ngx_write_fd(fd, buffer, p - buffer);

    if (n < 0) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: write failed: '%V'", &ctx->var_playlist_bak);
        ngx_close_file(fd);
        return NGX_ERROR;
    }

    ngx_close_file(fd);

    if (ngx_rtmp_dash_rename_file(ctx->var_playlist_bak.data, ctx->var_playlist.data)
        == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: rename failed: '%V'->'%V'",
                      &ctx->var_playlist_bak, &ctx->var_playlist);
        return NGX_ERROR;
    }

    ngx_memzero(&v, sizeof(v));
    ngx_str_set(&(v.module), "dash");
    v.playlist.data = ctx->playlist.data;
    v.playlist.len = ctx->playlist.len;
    return next_playlist(s, &v);
}


static ngx_int_t
ngx_rtmp_dash_write_playlist(ngx_rtmp_session_t *s)
{
    char                      *sep;
    u_char                    *p, *last;
    ssize_t                    n;
    ngx_fd_t                   fd, fds;
    struct tm                  tm;
    ngx_str_t                  noname, *name;
    ngx_uint_t                 i, frame_rate_num, frame_rate_denom;
    ngx_uint_t                 depth_msec, depth_sec;
    ngx_uint_t                 update_period, update_period_msec;
    ngx_uint_t                 start_time, buffer_time, buffer_time_msec;
    ngx_uint_t                 presentation_delay, presentation_delay_msec;
    ngx_uint_t                 gcd, par_x, par_y;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    ngx_rtmp_dash_frag_t      *f;
    ngx_rtmp_dash_app_conf_t  *dacf;

    ngx_rtmp_playlist_t        v;

    static u_char              buffer[NGX_RTMP_DASH_BUFSIZE];
    static u_char              available_time[NGX_RTMP_DASH_GMT_LENGTH];
    static u_char              publish_time[NGX_RTMP_DASH_GMT_LENGTH];
    static u_char              buffer_depth[sizeof("P00Y00M00DT00H00M00.000S")];
    static u_char              frame_rate[(NGX_INT_T_LEN * 2) + 2];

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (dacf == NULL || ctx == NULL || codec_ctx == NULL) {
        return NGX_ERROR;
    }

    if (ctx->id == 0) {
        ngx_rtmp_dash_write_init_segments(s, &ctx->drm_info);
    }

    fd = ngx_open_file(ctx->playlist_bak.data, NGX_FILE_WRONLY,
                       NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: open failed: '%V'", &ctx->playlist_bak);
        return NGX_ERROR;
    }
   
    /* write segments file */
    fds = ngx_open_file(ctx->segments_bak.data, NGX_FILE_WRONLY,
                        NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);

    if (fds == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: open failed: '%V'", &ctx->segments_bak);
        return NGX_ERROR;
    }

    /* availabity and publish time should be relative to peer epoch */
    start_time = ctx->start_time.sec - (s->peer_epoch/1000);
    ngx_log_debug4(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "Fixing start_time=%uD %uD epoch=%uD new_start_time=%uD",
            (uint32_t)ctx->start_time.sec, (uint32_t)ctx->start_time.msec,
            (uint32_t)s->peer_epoch,
            (uint32_t)start_time);

    /**
     * Availability time must be equal stream start time
     * Cos segments time counting from it
     */
    ngx_libc_gmtime(start_time, &tm);
    *ngx_sprintf(available_time, "%4d-%02d-%02dT%02d:%02d:%02dZ",
             tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour,
             tm.tm_min, tm.tm_sec
             ) = 0;

    /* Stream publish time */
    *ngx_sprintf(publish_time, "%s", available_time) = 0;

    depth_sec = (ngx_uint_t) (
                 ngx_rtmp_dash_get_frag(s, ctx->nfrags - 1)->timestamp +
                 ngx_rtmp_dash_get_frag(s, ctx->nfrags - 1)->duration - 
                 ngx_rtmp_dash_get_frag(s, 0)->timestamp);

    depth_msec = depth_sec % 1000;
    depth_sec -= depth_msec;
    depth_sec /= 1000;

    ngx_libc_gmtime(depth_sec, &tm);

    *ngx_sprintf(buffer_depth, "P%dY%02dM%02dDT%dH%02dM%02d.%03dS",
                 tm.tm_year - 70, tm.tm_mon,
                 tm.tm_mday - 1, tm.tm_hour,
                 tm.tm_min, tm.tm_sec,
                 depth_msec) = 0;

    last = buffer + sizeof(buffer);

    /**
     * Calculate playlist minimal update period
     * This should be more than biggest segment duration
     * Cos segments rounded by keyframe/GOP.
     * And that time not always equals to fragment length.
     */
    update_period = dacf->fraglen;

    for (i = 0; i < ctx->nfrags; i++) {
        f = ngx_rtmp_dash_get_frag(s, i);
        if (f->duration > update_period) {
            update_period = f->duration;
        }
    }

    // Reasonable delay for streaming
    presentation_delay = update_period * 2 + 1000;
    presentation_delay_msec = presentation_delay % 1000;
    presentation_delay -= presentation_delay_msec;
    presentation_delay /= 1000;

    // Calculate msec part and seconds
    update_period_msec = update_period % 1000;
    update_period -= update_period_msec;
    update_period /= 1000;

    // Buffer length by default fragment length
    buffer_time = dacf->fraglen;
    buffer_time_msec = buffer_time % 1000;
    buffer_time -= buffer_time_msec;
    buffer_time /= 1000;

    // Fill DASH header
    p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_HEADER,
                     // availabilityStartTime
                     available_time,
                     // publishTime
                     publish_time,
                     // minimumUpdatePeriod
                     update_period, update_period_msec,
                     // minBufferTime
                     buffer_time, buffer_time_msec,
                     // timeShiftBufferDepth
                     buffer_depth,
                     // suggestedPresentationDelay
                     presentation_delay, presentation_delay_msec
                     );

    p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_PERIOD);

    n = ngx_write_fd(fd, buffer, p - buffer);

    ngx_str_null(&noname);

    name = (dacf->nested ? &noname : &ctx->name);
    sep = (dacf->nested ? "" : "-");

    if (ctx->has_video) {
        frame_rate_num = (ngx_uint_t) (codec_ctx->frame_rate * 1000.);

        if (frame_rate_num % 1000 == 0) {
            *ngx_sprintf(frame_rate, "%ui", frame_rate_num / 1000) = 0;
        } else {
            frame_rate_denom = 1000;
            switch (frame_rate_num) {
                case 23976:
                    frame_rate_num = 24000;
                    frame_rate_denom = 1001;
                    break;
                case 29970:
                    frame_rate_num = 30000;
                    frame_rate_denom = 1001;
                    break;
                case 59940:
                    frame_rate_num = 60000;
                    frame_rate_denom = 1001;
                    break;
            }

            *ngx_sprintf(frame_rate, "%ui/%ui", frame_rate_num, frame_rate_denom) = 0;
        }

        gcd = ngx_rtmp_dash_gcd(codec_ctx->width, codec_ctx->height);
        par_x = codec_ctx->width / gcd;
        par_y = codec_ctx->height / gcd;

        p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_ADAPTATIONSET_VIDEO,
                         codec_ctx->width,
                         codec_ctx->height,
                         frame_rate,
                         par_x, par_y);

        switch (dacf->ad_markers) {
            case NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT:
            case NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT_SCTE35:
                p = ngx_slprintf(p, last, NGX_RTMP_DASH_INBAND_EVENT);
        }

        if (dacf->cenc) {
            p = ngx_rtmp_dash_write_content_protection(s, &ctx->drm_info, p, last);
        }
   
        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_REPRESENTATION_VIDEO,
                         &ctx->name,
                         codec_ctx->avc_profile,
                         codec_ctx->avc_compat,
                         codec_ctx->avc_level,
                         codec_ctx->width,
                         codec_ctx->height,
                         frame_rate,
                         (ngx_uint_t) (codec_ctx->video_data_rate * 1000),
                         name, sep,
                         name, sep);

        n = ngx_write_fd(fd, buffer, p - buffer);

        p = buffer;
        p = ngx_rtmp_dash_write_segment_timeline(s, ctx, dacf, p, last);

        ngx_write_fd(fds, buffer, p - buffer);

        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_REPRESENTATION_VIDEO_FOOTER);

        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_ADAPTATIONSET_VIDEO_FOOTER);

        n = ngx_write_fd(fd, buffer, p - buffer);
    }

    if (ctx->has_audio) {
        p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_ADAPTATIONSET_AUDIO);

        if (dacf->cenc) {
            p = ngx_rtmp_dash_write_content_protection(s, &ctx->drm_info, p, last);
        }

        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_REPRESENTATION_AUDIO,
                         &ctx->name,
                         codec_ctx->audio_codec_id == NGX_RTMP_AUDIO_AAC ?
                         (codec_ctx->aac_sbr ? "40.5" : "40.2") : "6b",
                         codec_ctx->sample_rate,
                         (ngx_uint_t) (codec_ctx->audio_data_rate * 1000),
                         name, sep,
                         name, sep);

        p = ngx_rtmp_dash_write_segment_timeline(s, ctx, dacf, p, last);

        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_REPRESENTATION_AUDIO_FOOTER);

        p = ngx_slprintf(p, last, NGX_RTMP_DASH_MANIFEST_ADAPTATIONSET_AUDIO_FOOTER);

        n = ngx_write_fd(fd, buffer, p - buffer);
    }

    p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_PERIOD_FOOTER);
    n = ngx_write_fd(fd, buffer, p - buffer);

    /* UTCTiming value */
    switch (dacf->clock_compensation) {
        case NGX_RTMP_DASH_CLOCK_COMPENSATION_NTP:
                p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_CLOCK,
                                 "ntp",
                                 &dacf->clock_helper_uri
                );
                n = ngx_write_fd(fd, buffer, p - buffer);
        break;
        case NGX_RTMP_DASH_CLOCK_COMPENSATION_HTTP_HEAD:
                p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_CLOCK,
                                 "http-head",
                                 &dacf->clock_helper_uri
                );
                n = ngx_write_fd(fd, buffer, p - buffer);
        break;
        case NGX_RTMP_DASH_CLOCK_COMPENSATION_HTTP_ISO:
                p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_CLOCK,
                                 "http-iso",
                                 &dacf->clock_helper_uri
                );
                n = ngx_write_fd(fd, buffer, p - buffer);
        break;
    }

    p = ngx_slprintf(buffer, last, NGX_RTMP_DASH_MANIFEST_FOOTER);
    n = ngx_write_fd(fd, buffer, p - buffer);

    if (n < 0) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: write failed: '%V'", &ctx->playlist_bak);
        ngx_close_file(fd);
        return NGX_ERROR;
    }

    ngx_close_file(fd);
    ngx_close_file(fds);

    if (ngx_rtmp_dash_rename_file(ctx->playlist_bak.data, ctx->playlist.data)
        == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: rename failed: '%V'->'%V'",
                      &ctx->playlist_bak, &ctx->playlist);
        return NGX_ERROR;
    }

    if (ngx_rtmp_dash_rename_file(ctx->segments_bak.data, ctx->segments.data)
        == NGX_FILE_ERROR)
    {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: rename failed: '%V'->'%V'",
                      &ctx->segments_bak, &ctx->segments);
        return NGX_ERROR;
    }

    /* try to write the variant file only once, check the max flag */
    if (ctx->var && ctx->var->args.nelts > 3) {
        return ngx_rtmp_dash_write_variant_playlist(s);
    }

    ngx_memzero(&v, sizeof(v));
    ngx_str_set(&(v.module), "dash");
    v.playlist.data = ctx->playlist.data;
    v.playlist.len = ctx->playlist.len;
    return next_playlist(s, &v);
}


static ngx_int_t
ngx_rtmp_dash_write_init_segments(ngx_rtmp_session_t *s, ngx_rtmp_cenc_drm_info_t *drmi)
{
    ngx_fd_t                   fd;
    ngx_int_t                  rc;
    ngx_buf_t                  b;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    ngx_rtmp_dash_app_conf_t  *dacf;

    static u_char          buffer[NGX_RTMP_DASH_BUFSIZE];

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (dacf == NULL || ctx == NULL || codec_ctx == NULL) {
        return NGX_ERROR;
    }

    /* init video */

    *ngx_sprintf(ctx->stream.data + ctx->stream.len, "init.m4v") = 0;

    fd = ngx_open_file(ctx->stream.data, NGX_FILE_RDWR, NGX_FILE_TRUNCATE,
                       NGX_FILE_DEFAULT_ACCESS);

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: error creating video init file");
        return NGX_ERROR;
    }

    b.start = buffer;
    b.end = b.start + sizeof(buffer);
    b.pos = b.last = b.start;

    ngx_rtmp_mp4_write_ftyp(&b);
    if (dacf->cenc) {
        ngx_rtmp_mp4_write_moov(s, &b, NGX_RTMP_MP4_EVIDEO_TRACK, drmi);
    } else {
        ngx_rtmp_mp4_write_moov(s, &b, NGX_RTMP_MP4_VIDEO_TRACK, NULL);
    } 

    rc = ngx_write_fd(fd, b.start, (size_t) (b.last - b.start));
    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: writing video init failed");
    }

    ngx_close_file(fd);

    /* init audio */

    *ngx_sprintf(ctx->stream.data + ctx->stream.len, "init.m4a") = 0;

    fd = ngx_open_file(ctx->stream.data, NGX_FILE_RDWR, NGX_FILE_TRUNCATE,
                       NGX_FILE_DEFAULT_ACCESS);

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: error creating dash audio init file");
        return NGX_ERROR;
    }

    b.pos = b.last = b.start;

    ngx_rtmp_mp4_write_ftyp(&b);
    if (dacf->cenc) {
        ngx_rtmp_mp4_write_moov(s, &b, NGX_RTMP_MP4_EAUDIO_TRACK, drmi);
    } else {
        ngx_rtmp_mp4_write_moov(s, &b, NGX_RTMP_MP4_AUDIO_TRACK, NULL);
    } 

    rc = ngx_write_fd(fd, b.start, (size_t) (b.last - b.start));
    if (rc == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: writing audio init failed");
    }

    ngx_close_file(fd);

    return NGX_OK;
}


static void
ngx_rtmp_dash_close_fragment(ngx_rtmp_session_t *s, ngx_rtmp_dash_track_t *t)
{
    u_char                    *pos, *pos1;
    size_t                     left;
    ssize_t                    n;
    ngx_fd_t                   fd;
    ngx_buf_t                  b;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_dash_frag_t      *f;

    static u_char              buffer[NGX_RTMP_DASH_BUFSIZE];

    if (!t->opened) {
        return;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: close fragment id=%ui, type=%c, pts=%uD",
                   t->id, t->type, t->earliest_pres_time);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    b.start = buffer;
    b.end = buffer + sizeof(buffer);
    b.pos = b.last = b.start;

    if (ctx->start_cuepoint) {

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "dash : onCuepoint write start emsg : epts='%uD', lpts='%uD', cpts='%uD', "\
            "ecpts='%uD', duration='%uD', prid='%uD'",
            t->earliest_pres_time, t->latest_pres_time, ctx->cuepoint_starttime, 
            ctx->cuepoint_endtime, ctx->cuepoint_duration, ctx->cuepoint_id);

        /* should be ngx_rtmp_mp4_write_emsg(&b, t->earliest_pres_time, 
           thanks to dashjs
         */
        ngx_rtmp_mp4_write_emsg(&b, 0, 
                                ctx->cuepoint_starttime, 
                                ctx->cuepoint_duration,
                                ctx->cuepoint_id);

        pos = b.last;
        b.last = pos;
        ctx->start_cuepoint = 0;
        ctx->cuepoint_duration = 0;
        ctx->end_cuepoint = 1;

    } else if (ctx->end_cuepoint && ctx->cuepoint_endtime >= t->earliest_pres_time 
        && ctx->cuepoint_endtime <= t->latest_pres_time) {

        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "dash : onCuepoint write end emsg : epts='%uD', lpts='%uD', cpts='%uD', "\
            "ecpts='%uD', duration='%uD', prid='%uD'",
            t->earliest_pres_time, t->latest_pres_time, ctx->cuepoint_starttime, 
            ctx->cuepoint_endtime, ctx->cuepoint_duration, ctx->cuepoint_id);

        /* end marker have duration set to zero and prid set to zero */
        ngx_rtmp_mp4_write_emsg(&b, 0, 
                                ctx->cuepoint_endtime, 
                                0,
                                0);

        pos = b.last;
        b.last = pos;
        ctx->end_cuepoint = 0;
    } else if (ctx->end_cuepoint && ctx->cuepoint_endtime < t->earliest_pres_time ) {
        /* fallback */
        ctx->end_cuepoint = 0;
    }   

    ngx_rtmp_mp4_write_styp(&b);

    pos = b.last;
    b.last += 44; /* leave room for sidx */

    ngx_rtmp_mp4_write_moof(&b, t->earliest_pres_time, t->type, t->sample_count,
                            t->samples, t->sample_mask, t->id, t->is_protected);
    pos1 = b.last;
    b.last = pos;

    ngx_rtmp_mp4_write_sidx(&b, t->mdat_size + 8 + (pos1 - (pos + 44)),
                            t->earliest_pres_time, t->latest_pres_time);
    b.last = pos1;
    ngx_rtmp_mp4_write_mdat(&b, t->mdat_size + 8);

    /* move the data down to make room for the headers */

    f = ngx_rtmp_dash_get_frag(s, ctx->nfrags);

    *ngx_sprintf(ctx->stream.data + ctx->stream.len, "%uD.m4%c",
                 f->timestamp, t->type) = 0;

    fd = ngx_open_file(ctx->stream.data, NGX_FILE_RDWR,
                       NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: error creating dash temp video file");
        goto done;
    }

    if (ngx_write_fd(fd, b.pos, (size_t) (b.last - b.pos)) == NGX_ERROR) {
        goto done;
    }

    left = (size_t) t->mdat_size;

#if (NGX_WIN32)
    if (SetFilePointer(t->fd, 0, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "dash: SetFilePointer error");
        goto done;
    }
#else
    if (lseek(t->fd, 0, SEEK_SET) == -1) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: lseek error");
        goto done;
    }
#endif

    while (left > 0) {

        n = ngx_read_fd(t->fd, buffer, ngx_min(sizeof(buffer), left));
        if (n == NGX_ERROR) {
            break;
        }

        n = ngx_write_fd(fd, buffer, (size_t) n);
        if (n == NGX_ERROR) {
            break;
        }

        left -= n;
    }

done:

    if (fd != NGX_INVALID_FILE) {
        ngx_close_file(fd);
    }

    ngx_close_file(t->fd);

    t->fd = NGX_INVALID_FILE;
    t->opened = 0;
}


static ngx_int_t
ngx_rtmp_dash_close_fragments(ngx_rtmp_session_t *s)
{
    ngx_rtmp_dash_ctx_t  *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);
    if (ctx == NULL || !ctx->opened) {
        return NGX_OK;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: close fragments");

    ngx_rtmp_dash_close_fragment(s, &ctx->video);
    ngx_rtmp_dash_close_fragment(s, &ctx->audio);

    ngx_rtmp_dash_next_frag(s);

    ngx_rtmp_dash_write_playlist(s);

    ctx->id++;
    ctx->opened = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_dash_open_fragment(ngx_rtmp_session_t *s, ngx_rtmp_dash_track_t *t,
    ngx_uint_t id, char type)
{
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_dash_app_conf_t  *dacf;

    if (t->opened) {
        return NGX_OK;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: open fragment id=%ui, type='%c'", id, type);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);
    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);

    *ngx_sprintf(ctx->stream.data + ctx->stream.len, "raw.m4%c", type) = 0;

    t->fd = ngx_open_file(ctx->stream.data, NGX_FILE_RDWR,
                          NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);

    if (t->fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: error creating fragment file");
        return NGX_ERROR;
    }

    t->id = id;
    t->type = type;
    t->sample_count = 0;
    t->earliest_pres_time = 0;
    t->latest_pres_time = 0;
    t->mdat_size = 0;
    t->opened = 1;
     
    if (dacf->cenc) {

        if (ngx_rtmp_cenc_read_hex(dacf->cenc_key, t->key) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "dash: error cenc key is invalid");
            return NGX_ERROR;
        }

        t->is_protected = 1;
    }

    if (type == 'v') {
        t->sample_mask = NGX_RTMP_MP4_SAMPLE_SIZE|
                         NGX_RTMP_MP4_SAMPLE_DURATION|
                         NGX_RTMP_MP4_SAMPLE_DELAY|
                         NGX_RTMP_MP4_SAMPLE_KEY;
    } else {
        t->sample_mask = NGX_RTMP_MP4_SAMPLE_SIZE|
                         NGX_RTMP_MP4_SAMPLE_DURATION;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_dash_open_fragments(ngx_rtmp_session_t *s)
{
    ngx_rtmp_dash_ctx_t  *ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: open fragments");

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    if (ctx->opened) {
        return NGX_OK;
    }

    if (ngx_rtmp_dash_ensure_directory(s) != NGX_OK) {
        return NGX_ERROR;
    }

    ngx_rtmp_dash_open_fragment(s, &ctx->video, ctx->id, 'v');

    ngx_rtmp_dash_open_fragment(s, &ctx->audio, ctx->id, 'a');

    ctx->opened = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_dash_ensure_directory(ngx_rtmp_session_t *s)
{
    size_t                     len;
    ngx_file_info_t            fi;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_dash_app_conf_t  *dacf;

    static u_char              path[NGX_MAX_PATH + 1];

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);

    *ngx_snprintf(path, sizeof(path) - 1, "%V", &dacf->path) = 0;

    if (ngx_file_info(path, &fi) == NGX_FILE_ERROR) {

        if (ngx_errno != NGX_ENOENT) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "dash: " ngx_file_info_n " failed on '%V'",
                          &dacf->path);
            return NGX_ERROR;
        }

        /* ENOENT */

        if (ngx_create_dir(path, NGX_RTMP_DASH_DIR_ACCESS) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "dash: " ngx_create_dir_n " failed on '%V'",
                          &dacf->path);
            return NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "dash: directory '%V' created", &dacf->path);

    } else {

        if (!ngx_is_dir(&fi)) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                          "dash: '%V' exists and is not a directory",
                          &dacf->path);
            return  NGX_ERROR;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "dash: directory '%V' exists", &dacf->path);
    }

    if (!dacf->nested) {
        return NGX_OK;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    len = dacf->path.len;
    if (dacf->path.data[len - 1] == '/') {
        len--;
    }

    *ngx_snprintf(path, sizeof(path) - 1, "%*s/%V", len, dacf->path.data,
                  &ctx->name) = 0;

    if (ngx_file_info(path, &fi) != NGX_FILE_ERROR) {

        if (ngx_is_dir(&fi)) {
            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                           "dash: directory '%s' exists", path);
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "dash: '%s' exists and is not a directory", path);

        return  NGX_ERROR;
    }

    if (ngx_errno != NGX_ENOENT) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: " ngx_file_info_n " failed on '%s'", path);
        return NGX_ERROR;
    }

    /* NGX_ENOENT */

    if (ngx_create_dir(path, NGX_RTMP_DASH_DIR_ACCESS) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                      "dash: " ngx_create_dir_n " failed on '%s'", path);
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: directory '%s' created", path);

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_dash_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    u_char                    *p, *pp;
    size_t                     len;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_dash_frag_t      *f;
    ngx_rtmp_dash_app_conf_t  *dacf;
    ngx_rtmp_dash_variant_t   *var;
    ngx_uint_t                 n;

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    if (dacf == NULL || !dacf->dash || dacf->path.len == 0) {
        goto next;
    }

    if (s->auto_pushed) {
        goto next;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: publish: name='%s' type='%s'", v->name, v->type);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    if (ctx == NULL) {
        ctx = ngx_pcalloc(s->connection->pool, sizeof(ngx_rtmp_dash_ctx_t));
        if (ctx == NULL) {
            goto next;
        }
        ngx_rtmp_set_ctx(s, ctx, ngx_rtmp_dash_module);

    } else {
        if (ctx->opened) {
            goto next;
        }

        f = ctx->frags;
        ngx_memzero(ctx, sizeof(ngx_rtmp_dash_ctx_t));
        ctx->frags = f;
    }

    if (ctx->frags == NULL) {
        ctx->frags = ngx_pcalloc(s->connection->pool,
                                 sizeof(ngx_rtmp_dash_frag_t) *
                                 (dacf->winfrags * 2 + 1));
        if (ctx->frags == NULL) {
            return NGX_ERROR;
        }
    }

    ctx->id = 0;

    if (ngx_strstr(v->name, "..")) {
        ngx_log_error(NGX_LOG_ERR, s->connection->log, 0,
                      "dash: bad stream name: '%s'", v->name);
        return NGX_ERROR;
    }

    ctx->name.len = ngx_strlen(v->name);
    ctx->name.data = ngx_palloc(s->connection->pool, ctx->name.len + 1);

    if (ctx->name.data == NULL) {
        return NGX_ERROR;
    }

    *ngx_cpymem(ctx->name.data, v->name, ctx->name.len) = 0;

    len = dacf->path.len + 1 + ctx->name.len + sizeof(".mpd");
    if (dacf->nested) {
        len += sizeof("/index") - 1;
    }

    ctx->playlist.data = ngx_palloc(s->connection->pool, len);
    p = ngx_cpymem(ctx->playlist.data, dacf->path.data, dacf->path.len);

    if (p[-1] != '/') {
        *p++ = '/';
    }

    p = ngx_cpymem(p, ctx->name.data, ctx->name.len);

    /*
     * ctx->stream holds initial part of stream file path
     * however the space for the whole stream path
     * is allocated
     */

    ctx->stream.len = p - ctx->playlist.data + 1;
    ctx->stream.data = ngx_palloc(s->connection->pool,
                                  ctx->stream.len + NGX_INT32_LEN +
                                  sizeof(".m4x"));

    ngx_memcpy(ctx->stream.data, ctx->playlist.data, ctx->stream.len - 1);
    ctx->stream.data[ctx->stream.len - 1] = (dacf->nested ? '/' : '-');

    if (dacf->variant) {
        var = dacf->variant->elts;
        for (n = 0; n < dacf->variant->nelts; n++, var++) {
            if (ctx->name.len > var->suffix.len &&
                ngx_memcmp(var->suffix.data,
                    ctx->name.data + ctx->name.len - var->suffix.len,
                    var->suffix.len)
                == 0)
            {
                len = (size_t) (ctx->name.len - var->suffix.len);

                ctx->varname.len = len;
                ctx->varname.data = ngx_palloc(s->connection->pool,
                        ctx->varname.len + 1);
                pp = ngx_cpymem(ctx->varname.data,
                                ctx->name.data, len);
                
                *pp = 0;

                ctx->var = var;

                len = (size_t) (p - ctx->playlist.data);

                ctx->var_playlist.len = len - var->suffix.len + sizeof(".mpd")
                    -1;
                ctx->var_playlist.data = ngx_palloc(s->connection->pool,
                        ctx->var_playlist.len + 1);
                pp = ngx_cpymem(ctx->var_playlist.data,
                        ctx->playlist.data, len - var->suffix.len);
                pp = ngx_cpymem(pp, ".mpd", sizeof(".mpd") - 1);
                *pp = 0;

                ctx->var_playlist_bak.len = ctx->var_playlist.len +
                                            sizeof(".bak") - 1;
                ctx->var_playlist_bak.data = ngx_palloc(s->connection->pool,
                                                 ctx->var_playlist_bak.len + 1);
                pp = ngx_cpymem(ctx->var_playlist_bak.data,
                                ctx->var_playlist.data,
                                ctx->var_playlist.len);
                pp = ngx_cpymem(pp, ".bak", sizeof(".bak") - 1);
                *pp = 0;

                break;
            }
        }
    }

    if (dacf->nested) {
        p = ngx_cpymem(p, "/index.mpd", sizeof("/index.mpd") - 1);
    } else {
        p = ngx_cpymem(p, ".mpd", sizeof(".mpd") - 1);
    }

    ctx->playlist.len = p - ctx->playlist.data;

    *p = 0;

    /* playlist bak (new playlist) path */

    ctx->playlist_bak.data = ngx_palloc(s->connection->pool,
                                        ctx->playlist.len + sizeof(".bak"));
    p = ngx_cpymem(ctx->playlist_bak.data, ctx->playlist.data,
                   ctx->playlist.len);
    p = ngx_cpymem(p, ".bak", sizeof(".bak") - 1);

    ctx->playlist_bak.len = p - ctx->playlist_bak.data;

    *p = 0;

    /* segments path */

    ctx->segments.data = ngx_palloc(s->connection->pool,
                                        ctx->playlist.len - 4 + sizeof(".seg"));
    p = ngx_cpymem(ctx->segments.data, ctx->playlist.data,
                   ctx->playlist.len - 4);
    p = ngx_cpymem(p, ".seg", sizeof(".seg") - 1);

    ctx->segments.len = p - ctx->segments.data;

    *p = 0;

    /* segments bak (new segments) path */

    ctx->segments_bak.data = ngx_palloc(s->connection->pool,
                                        ctx->playlist.len - 4 + sizeof(".sbk"));
    p = ngx_cpymem(ctx->segments_bak.data, ctx->playlist.data,
                   ctx->playlist.len - 4);
    p = ngx_cpymem(p, ".sbk", sizeof(".sbk") - 1);

    ctx->segments_bak.len = p - ctx->segments_bak.data;

    *p = 0;

    ngx_log_debug5(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: playlist='%V' playlist_bak='%V' segments='%V' segments_bak='%V' stream_pattern='%V'",
                   &ctx->playlist, &ctx->playlist_bak, &ctx->segments, &ctx->segments_bak, &ctx->stream);

    ctx->start_time = *ngx_cached_time;

    if (ngx_rtmp_dash_ensure_directory(s) != NGX_OK) {
        return NGX_ERROR;
    }
   
    /* drm info */
    if (dacf->cenc) {
        if (ngx_rtmp_cenc_read_hex(dacf->cenc_kid, ctx->drm_info.kid) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, 0, "dash: error cenc kid is invalid");
            return NGX_ERROR;
        }
        if (dacf->wdv) {
            ctx->drm_info.wdv = 1;
            ctx->drm_info.wdv_data = dacf->wdv_data;
        }
        if (dacf->mspr) {
            ctx->drm_info.mspr = 1;
            ctx->drm_info.mspr_data = dacf->mspr_data;
            ctx->drm_info.mspr_kid = dacf->mspr_kid;
            ctx->drm_info.mspr_pro = dacf->mspr_pro;
        }
    }

next:
    return next_publish(s, v);
}


static ngx_int_t
ngx_rtmp_dash_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v)
{
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_dash_app_conf_t  *dacf;

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    if (dacf == NULL || !dacf->dash || ctx == NULL) {
        goto next;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: delete stream");

    ngx_rtmp_dash_close_fragments(s);

next:
    return next_close_stream(s, v);
}

static void
ngx_rtmp_dash_update_fragments(ngx_rtmp_session_t *s, ngx_int_t boundary,
    uint32_t timestamp)
{
    int32_t                    d;
    ngx_int_t                  hit;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_dash_frag_t      *f;
    ngx_rtmp_dash_app_conf_t  *dacf;

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);
    f = ngx_rtmp_dash_get_frag(s, ctx->nfrags);

    ngx_log_debug4(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: update_fragments: timestamp=%ui, f-timestamp=%ui, boundary=%i, dacf-fraglen=%ui",
                   timestamp, f->timestamp, boundary, dacf->fraglen);

    d = (int32_t) (timestamp - f->timestamp);

    if (d >= 0) {

        f->duration = timestamp - f->timestamp;
        hit = (f->duration >= dacf->fraglen);

    } else {

        /* sometimes clients generate slightly unordered frames */
        hit = (-d > 1000);
    }

    ngx_log_debug3(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: update_fragments: d=%i, f-duration=%ui, hit=%i",
                   d, f->duration, hit);

    if (ctx->has_video && !hit) {
        boundary = 0;
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: update_fragments: boundary=0 cos has_video && !hit");
    }

    if (!ctx->has_video && ctx->has_audio) {
        boundary = hit;
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: update_fragments: boundary=hit cos !has_video && has_audio");
    }

    if (ctx->audio.mdat_size >= NGX_RTMP_DASH_MAX_MDAT) {
        boundary = 1;
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: update_fragments: boundary=1 cos audio max mdat");
    }

    if (ctx->video.mdat_size >= NGX_RTMP_DASH_MAX_MDAT) {
        boundary = 1;
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: update_fragments: boundary=1 cos video max mdat");
    }

    if (!ctx->opened) {
        boundary = 1;
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: update_fragments: boundary=1 cos !opened");
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "dash: update_fragments: boundary=%i",
                   boundary);

    if (boundary) {
        ngx_rtmp_dash_close_fragments(s);
        ngx_rtmp_dash_open_fragments(s);

        f = ngx_rtmp_dash_get_frag(s, ctx->nfrags);
        f->timestamp = timestamp;
    }
}


static ngx_int_t
ngx_rtmp_dash_append(ngx_rtmp_session_t *s, ngx_chain_t *in,
    ngx_rtmp_dash_track_t *t, ngx_int_t key, uint32_t timestamp, uint32_t delay)
{
    u_char                    *p;
    size_t                     size, bsize, csize;
    ngx_rtmp_mp4_sample_t     *smpl;

    static u_char              buffer[NGX_RTMP_DASH_BUFSIZE];

    p = buffer;
    size = 0;

    for (; in && size < sizeof(buffer); in = in->next) {

        bsize = (size_t) (in->buf->last - in->buf->pos);
        if (size + bsize > sizeof(buffer)) {
            bsize = (size_t) (sizeof(buffer) - size);
        }

        p = ngx_cpymem(p, in->buf->pos, bsize);
        size += bsize;
    }

    ngx_rtmp_dash_update_fragments(s, key, timestamp);

    if (t->sample_count == 0) {
        t->earliest_pres_time = timestamp;
        if (t->is_protected) {
            ngx_rtmp_cenc_rand_iv(t->iv);
        }
    }

    t->latest_pres_time = timestamp;

    if (t->sample_count < NGX_RTMP_DASH_MAX_SAMPLES) {

        if (t->is_protected) {
            if (t->type == 'v') {
                ngx_rtmp_cenc_encrypt_sub_sample(s, t->key, t->iv, buffer, size, &csize); 
                ngx_log_debug4(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                    "dash: cenc crypt video sample: count=%ui, key=%ui, size=%ui, csize=%ui",
                     t->sample_count, key, size, csize);
            } else {
                ngx_rtmp_cenc_encrypt_full_sample(s, t->key, t->iv, buffer, size); 
                ngx_log_debug3(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                    "dash: cenc crypt audio sample: count=%ui, key=%ui, size=%ui",
                     t->sample_count, key, size);
            }
        }

        if (ngx_write_fd(t->fd, buffer, size) == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, s->connection->log, ngx_errno,
                          "dash: " ngx_write_fd_n " failed");
            return NGX_ERROR;
        } 
        
        smpl = &t->samples[t->sample_count];

        smpl->delay = delay;
        smpl->size = (uint32_t) size;
        smpl->duration = 0;
        smpl->timestamp = timestamp;
        smpl->key = (key ? 1 : 0);

        if (t->is_protected) {
            smpl->is_protected = 1;
            ngx_memcpy(smpl->iv, t->iv, NGX_RTMP_CENC_IV_SIZE);
            ngx_rtmp_cenc_increment_iv(t->iv);
            if (t->type == 'v') {
		    smpl->clear_size = (uint32_t) csize;
            }
        }

        if (t->sample_count > 0) {
            smpl = &t->samples[t->sample_count - 1];
            smpl->duration = timestamp - smpl->timestamp;
        }

        t->sample_count++;
        t->mdat_size += (ngx_uint_t) size;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_dash_audio(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{
    u_char                     htype;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    ngx_rtmp_dash_app_conf_t  *dacf;

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (dacf == NULL || !dacf->dash || ctx == NULL ||
        codec_ctx == NULL || h->mlen < 2)
    {
        return NGX_OK;
    }

    /* Only AAC is supported */

    if (codec_ctx->audio_codec_id != NGX_RTMP_AUDIO_AAC ||
        codec_ctx->aac_header == NULL)
    {
        return NGX_OK;
    }

    if (in->buf->last - in->buf->pos < 2) {
        return NGX_ERROR;
    }

    /* skip AAC config */

    htype = in->buf->pos[1];
    if (htype != 1) {
        return NGX_OK;
    }

    ctx->has_audio = 1;

    /* skip RTMP & AAC headers */

    in->buf->pos += 2;

    return ngx_rtmp_dash_append(s, in, &ctx->audio, 0, h->timestamp, 0);
}


static ngx_int_t
ngx_rtmp_dash_video(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{
    u_char                    *p;
    uint8_t                    ftype, htype;
    uint32_t                   delay;
    ngx_rtmp_dash_ctx_t       *ctx;
    ngx_rtmp_codec_ctx_t      *codec_ctx;
    ngx_rtmp_dash_app_conf_t  *dacf;

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);
    codec_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_codec_module);

    if (dacf == NULL || !dacf->dash || ctx == NULL || codec_ctx == NULL ||
        codec_ctx->avc_header == NULL || h->mlen < 5)
    {
        return NGX_OK;
    }

    /* Only H264 is supported */

    if (codec_ctx->video_codec_id != NGX_RTMP_VIDEO_H264) {
        return NGX_OK;
    }

    if (in->buf->last - in->buf->pos < 5) {
        return NGX_ERROR;
    }

    /* check what header it is */
    ftype = (in->buf->pos[0] & 0xf0) >> 4;

    /* skip AVC config */

    htype = in->buf->pos[1];
    if (htype != 1) {
        return NGX_OK;
    }

    p = (u_char *) &delay;

    p[0] = in->buf->pos[4];
    p[1] = in->buf->pos[3];
    p[2] = in->buf->pos[2];
    p[3] = 0;

    ctx->has_video = 1;

    /* skip RTMP & H264 headers */

    in->buf->pos += 5;

    return ngx_rtmp_dash_append(s, in, &ctx->video, ftype == 1, h->timestamp,
                                delay);
}


static ngx_int_t
ngx_rtmp_dash_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v)
{
    return next_stream_begin(s, v);
}


static ngx_int_t
ngx_rtmp_dash_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v)
{
    ngx_rtmp_dash_close_fragments(s);

    return next_stream_eof(s, v);
}


static ngx_int_t
ngx_rtmp_dash_cleanup_dir(ngx_str_t *ppath, ngx_msec_t playlen)
{
    time_t           mtime, max_age;
    u_char          *p;
    u_char           path[NGX_MAX_PATH + 1], mpd_path[NGX_MAX_PATH + 1];
    ngx_dir_t        dir;
    ngx_err_t        err;
    ngx_str_t        name, spath, mpd;
    ngx_int_t        nentries, nerased;
    ngx_file_info_t  fi;

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                   "dash: cleanup path='%V' playlen=%M", ppath, playlen);

    if (ngx_open_dir(ppath, &dir) != NGX_OK) {
        ngx_log_debug1(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, ngx_errno,
                       "dash: cleanup open dir failed '%V'", ppath);
        return NGX_ERROR;
    }

    nentries = 0;
    nerased = 0;

    for ( ;; ) {
        ngx_set_errno(0);

        if (ngx_read_dir(&dir) == NGX_ERROR) {
            err = ngx_errno;

            if (ngx_close_dir(&dir) == NGX_ERROR) {
                ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, ngx_errno,
                              "dash: cleanup " ngx_close_dir_n " \"%V\" failed",
                              ppath);
            }

            if (err == NGX_ENOMOREFILES) {
                return nentries - nerased;
            }

            ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, err,
                          "dash: cleanup " ngx_read_dir_n
                          " '%V' failed", ppath);
            return NGX_ERROR;
        }

        name.data = ngx_de_name(&dir);
        if (name.data[0] == '.') {
            continue;
        }

        name.len = ngx_de_namelen(&dir);

        p = ngx_snprintf(path, sizeof(path) - 1, "%V/%V", ppath, &name);
        *p = 0;

        spath.data = path;
        spath.len = p - path;

        nentries++;

        if (!dir.valid_info && ngx_de_info(path, &dir) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_CRIT, ngx_cycle->log, ngx_errno,
                          "dash: cleanup " ngx_de_info_n " \"%V\" failed",
                          &spath);

            continue;
        }

        if (ngx_de_is_dir(&dir)) {

            if (ngx_rtmp_dash_cleanup_dir(&spath, playlen) == 0) {
                ngx_log_debug1(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                               "dash: cleanup dir '%V'", &name);

                /*
                 * null-termination gets spoiled in win32
                 * version of ngx_open_dir
                 */

                *p = 0;

                if (ngx_delete_dir(path) == NGX_FILE_ERROR) {
                    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                                  "dash: cleanup " ngx_delete_dir_n
                                  " failed on '%V'", &spath);
                } else {
                    nerased++;
                }
            }

            continue;
        }

        if (!ngx_de_is_file(&dir)) {
            continue;
        }

        if (name.len >= 8 && name.data[name.len - 8] == 'i' &&
                             name.data[name.len - 7] == 'n' &&
                             name.data[name.len - 6] == 'i' &&
                             name.data[name.len - 5] == 't' &&
                             name.data[name.len - 4] == '.' &&
                             name.data[name.len - 3] == 'm' &&
                             name.data[name.len - 2] == '4')
        {
            if (name.len == 8) {
                ngx_str_set(&mpd, "index");
            } else {
                mpd.data = name.data;
                mpd.len = name.len - 9;
            }

            p = ngx_snprintf(mpd_path, sizeof(mpd_path) - 1, "%V/%V.mpd",
                             ppath, &mpd);
            *p = 0;

            if (ngx_file_info(mpd_path, &fi) != NGX_FILE_ERROR) {
                ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                               "dash: cleanup '%V' delayed, mpd exists '%s'",
                               &name, mpd_path);
                continue;
            }

            ngx_log_debug2(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                           "dash: cleanup '%V' allowed, mpd missing '%s'",
                           &name, mpd_path);

            max_age = playlen / 500;

        } else if (name.len >= 4 && name.data[name.len - 4] == '.' &&
                                    name.data[name.len - 3] == 'm' &&
                                    name.data[name.len - 2] == '4' &&
                                    name.data[name.len - 1] == 'v')
        {
            max_age = playlen / 500;

        } else if (name.len >= 4 && name.data[name.len - 4] == '.' &&
                                    name.data[name.len - 3] == 'm' &&
                                    name.data[name.len - 2] == '4' &&
                                    name.data[name.len - 1] == 'a')
        {
            max_age = playlen / 500;

        } else if (name.len >= 4 && name.data[name.len - 4] == '.' &&
                                    name.data[name.len - 3] == 'm' &&
                                    name.data[name.len - 2] == 'p' &&
                                    name.data[name.len - 1] == 'd')
        {
            max_age = playlen / 500;

        } else if (name.len >= 4 && name.data[name.len - 4] == '.' &&
                                    name.data[name.len - 3] == 'r' &&
                                    name.data[name.len - 2] == 'a' &&
                                    name.data[name.len - 1] == 'w')
        {
            max_age = playlen / 500;

        } else {
            ngx_log_debug1(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                           "dash: cleanup skip unknown file type '%V'", &name);
            continue;
        }

        mtime = ngx_de_mtime(&dir);
        if (mtime + max_age > ngx_cached_time->sec) {
            continue;
        }

        ngx_log_debug3(NGX_LOG_DEBUG_RTMP, ngx_cycle->log, 0,
                       "dash: cleanup '%V' mtime=%T age=%T",
                       &name, mtime, ngx_cached_time->sec - mtime);

        if (ngx_delete_file(path) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                          "dash: cleanup " ngx_delete_file_n " failed on '%V'",
                          &spath);
            continue;
        }

        nerased++;
    }
}


#if (nginx_version >= 1011005)
static ngx_msec_t
#else
static time_t
#endif
ngx_rtmp_dash_cleanup(void *data)
{
    ngx_rtmp_dash_cleanup_t *cleanup = data;

    ngx_rtmp_dash_cleanup_dir(&cleanup->path, cleanup->playlen);

    // Next callback in doubled playlist length time to make sure what all 
    // players read all segments
#if (nginx_version >= 1011005)
    return cleanup->playlen * 2;
#else
    return cleanup->playlen / 500;
#endif
}


static char *
ngx_rtmp_dash_variant(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_rtmp_dash_app_conf_t  *dacf = conf;

    ngx_str_t                *value, *arg;
    ngx_uint_t                n;
    ngx_rtmp_dash_variant_t   *var;

    value = cf->args->elts;

    if (dacf->variant == NULL) {
        dacf->variant = ngx_array_create(cf->pool, 1,
                                         sizeof(ngx_rtmp_dash_variant_t));
        if (dacf->variant == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    var = ngx_array_push(dacf->variant);
    if (var == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(var, sizeof(ngx_rtmp_dash_variant_t));

    var->suffix = value[1];

    if (cf->args->nelts == 2) {
        return NGX_CONF_OK;
    }

    if (ngx_array_init(&var->args, cf->pool, cf->args->nelts - 2,
                       sizeof(ngx_str_t))
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    arg = ngx_array_push_n(&var->args, cf->args->nelts - 2);
    if (arg == NULL) {
        return NGX_CONF_ERROR;
    }

    for (n = 2; n < cf->args->nelts; n++) {
        *arg++ = value[n];
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_rtmp_dash_playlist(ngx_rtmp_session_t *s, ngx_rtmp_playlist_t *v)
{
    return next_playlist(s, v);
}


static ngx_int_t
ngx_rtmp_dash_on_cuepoint(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{
    ngx_int_t                  res;
    ngx_rtmp_dash_ctx_t       *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    static struct {
        double                  time;
        double                  duration;
        u_char                  name[128];
        u_char                  type[128];
        u_char                  ptype[128];
    } v;

    static ngx_rtmp_amf_elt_t   in_pr_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("type"),
          v.ptype, sizeof(v.ptype) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("duration"),
          &v.duration, sizeof(v.duration) },

    };

    static ngx_rtmp_amf_elt_t   in_dt_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("time"),
          &v.time, sizeof(v.time) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("name"),
          v.name, sizeof(v.name) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("type"),
          v.type, sizeof(v.type) },

        { NGX_RTMP_AMF_OBJECT,
          ngx_string("parameters"),
          in_pr_elts, sizeof(in_pr_elts) },

    };
    
    static ngx_rtmp_amf_elt_t   in_elts[] = {

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          in_dt_elts, sizeof(in_dt_elts) },

    };

    ngx_memzero(&v, sizeof(v));
    res = ngx_rtmp_receive_amf(s, in, in_elts,
        sizeof(in_elts) / sizeof(in_elts[0]));

    if (res == NGX_OK && v.duration > 0) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "dash : onCuepoint : ts='%ui', time='%f', name='%s' type='%s' ptype='%s' duration='%f'",
            h->timestamp, v.time, v.name, v.type, v.ptype, v.duration);
        ctx->start_cuepoint = 1;
        ctx->cuepoint_starttime = h->timestamp;
        ctx->cuepoint_endtime = h->timestamp+(v.duration*1000); 
        ctx->cuepoint_duration = v.duration;
        ctx->cuepoint_id = 0;
    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "dash : onCuepoint : amf not understood");
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_dash_on_cuepoint_scte35(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{
    ngx_int_t                  res;
    ngx_rtmp_dash_ctx_t       *ctx;

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    static struct {
        unsigned                ooni;
        unsigned                splice_event_ci;
        unsigned                splice_imd;
        double                  avail_num;
        double                  avail_expected;
        double                  duration;
        double                  prtime;
        double                  prgid;
        double                  sctype;
        double                  sevtid;
        u_char                  type[128];
        u_char                  mtype[128];
    }  v;

    static ngx_rtmp_amf_elt_t   in_pr_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("messageType"),
          v.mtype, sizeof(v.mtype) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("splice_command_type"),
          &v.sctype, sizeof(v.sctype) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("splice_event_id"),
          &v.sevtid, sizeof(v.sevtid) },

        { NGX_RTMP_AMF_BOOLEAN,
          ngx_string("splice_event_cancel_indicator"),
          &v.splice_event_ci, sizeof(v.splice_event_ci) },

        { NGX_RTMP_AMF_BOOLEAN,
          ngx_string("out_of_network_indicator"),
          &v.ooni, sizeof(v.ooni) },

        { NGX_RTMP_AMF_BOOLEAN,
          ngx_string("splice_immediate"),
          &v.splice_imd, sizeof(v.splice_imd) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("pre_roll_time"),
          &v.prtime, sizeof(v.prtime) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("break_duration"),
          &v.duration, sizeof(v.duration) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("unique_program_id"),
          &v.prgid, sizeof(v.prgid) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("avail_num"),
          &v.avail_num, sizeof(v.avail_num) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("avail_expected"),
          &v.avail_expected, sizeof(v.avail_expected) },

    };

    static ngx_rtmp_amf_elt_t   in_dt_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("type"),
          v.type, sizeof(v.type) },

        { NGX_RTMP_AMF_OBJECT,
          ngx_string("parameters"),
          in_pr_elts, sizeof(in_pr_elts) },

    };
    
    static ngx_rtmp_amf_elt_t   in_elts[] = {

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          in_dt_elts, sizeof(in_dt_elts) },

    };

    ngx_memzero(&v, sizeof(v));
    res = ngx_rtmp_receive_amf(s, in, in_elts,
        sizeof(in_elts) / sizeof(in_elts[0]));

    if (res == NGX_OK && v.duration > 0) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "dash : onCuepoint_scte35 : ts='%ui', type='%s', mtype='%s', sctype='%f', "\
            "scid='%f', prgid='%f', duration='%f', avail_num='%f', avail_expected='%f'",
            h->timestamp, v.type, v.mtype, v.sctype, 
            v.sevtid, v.prgid, v.duration, v.avail_num, v.avail_expected);
        ctx->start_cuepoint = 1;
        ctx->cuepoint_starttime = h->timestamp;
        ctx->cuepoint_endtime = h->timestamp + v.duration; 
        ctx->cuepoint_duration = v.duration;
        ctx->cuepoint_id = v.prgid;
    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "dash : onCuepoint_scte35 : amf not understood");
    }
    
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_dash_on_cuepoint_cont_scte35(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{
    ngx_int_t                  res;

    static struct {
        double                  segupid;
        double                  segduration;
        double                  segrduration;
        u_char                  type[128];
        u_char                  mtype[128];
    }  v;

    static ngx_rtmp_amf_elt_t   in_pr_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("messageType"),
          v.mtype, sizeof(v.mtype) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("segmentation_upid"),
          &v.segupid, sizeof(v.segupid) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("segmentation_duration"),
          &v.segduration, sizeof(v.segduration) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("segmentation_remaining_duration"),
          &v.segrduration, sizeof(v.segrduration) },

    };

    static ngx_rtmp_amf_elt_t   in_dt_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("type"),
          v.type, sizeof(v.type) },

        { NGX_RTMP_AMF_OBJECT,
          ngx_string("parameters"),
          in_pr_elts, sizeof(in_pr_elts) },

    };
    
    static ngx_rtmp_amf_elt_t   in_elts[] = {

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          in_dt_elts, sizeof(in_dt_elts) },

    };

    ngx_memzero(&v, sizeof(v));
    res = ngx_rtmp_receive_amf(s, in, in_elts,
        sizeof(in_elts) / sizeof(in_elts[0]));

    if (res == NGX_OK && v.segrduration > 0) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "dash : onCuepoint_cont_scte35 : ts='%ui', type='%s', "\
            "segupid='%f', segduration='%f', segrduration='%f'",
            h->timestamp, v.mtype, v.segupid, v.segduration, v.segrduration);
    } else {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "dash : onCuepoint_cont_scte35 : amf not understood");
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_dash_ad_markers(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
    ngx_chain_t *in)
{
    ngx_rtmp_dash_app_conf_t  *dacf;
    ngx_rtmp_dash_ctx_t       *ctx;

    dacf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_dash_module);
    if (dacf == NULL || !dacf->dash || dacf->path.len == 0) {
        return NGX_OK;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_dash_module);

    switch (dacf->ad_markers) {
        case NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT:
            return ngx_rtmp_dash_on_cuepoint(s, h, in);
        break;
        case NGX_RTMP_DASH_AD_MARKERS_ON_CUEPOINT_SCTE35:
	    if (ctx->end_cuepoint)
                return ngx_rtmp_dash_on_cuepoint_cont_scte35(s, h, in);
            else
                return ngx_rtmp_dash_on_cuepoint_scte35(s, h, in);
        break;
        default:
            return NGX_OK;
    }

    return NGX_OK;
}


static void *
ngx_rtmp_dash_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtmp_dash_app_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_dash_app_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->dash = NGX_CONF_UNSET;
    conf->fraglen = NGX_CONF_UNSET_MSEC;
    conf->playlen = NGX_CONF_UNSET_MSEC;
    conf->cleanup = NGX_CONF_UNSET;
    conf->nested = NGX_CONF_UNSET;
    conf->repetition = NGX_CONF_UNSET;
    conf->cenc = NGX_CONF_UNSET;
    conf->wdv = NGX_CONF_UNSET;
    conf->mspr = NGX_CONF_UNSET;
    conf->clock_compensation = NGX_CONF_UNSET;
    conf->ad_markers = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_rtmp_dash_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_dash_app_conf_t    *prev = parent;
    ngx_rtmp_dash_app_conf_t    *conf = child;
    ngx_rtmp_dash_cleanup_t     *cleanup;

    ngx_conf_merge_value(conf->dash, prev->dash, 0);
    ngx_conf_merge_msec_value(conf->fraglen, prev->fraglen, 5000);
    ngx_conf_merge_msec_value(conf->playlen, prev->playlen, 30000);
    ngx_conf_merge_value(conf->cleanup, prev->cleanup, 1);
    ngx_conf_merge_value(conf->nested, prev->nested, 0);
    ngx_conf_merge_value(conf->repetition, prev->repetition, 0);
    ngx_conf_merge_value(conf->cenc, prev->cenc, 0);
    ngx_conf_merge_str_value(conf->cenc_key, prev->cenc_key, "");
    ngx_conf_merge_str_value(conf->cenc_kid, prev->cenc_kid, "");
    ngx_conf_merge_value(conf->wdv, prev->wdv, 0);
    ngx_conf_merge_str_value(conf->wdv_data, prev->wdv_data, "");
    ngx_conf_merge_value(conf->mspr, prev->mspr, 0);
    ngx_conf_merge_str_value(conf->mspr_data, prev->mspr_data, "");
    ngx_conf_merge_str_value(conf->mspr_kid, prev->mspr_kid, "");
    ngx_conf_merge_str_value(conf->mspr_pro, prev->mspr_pro, "");
    ngx_conf_merge_uint_value(conf->clock_compensation, prev->clock_compensation,
                              NGX_RTMP_DASH_CLOCK_COMPENSATION_OFF);
    ngx_conf_merge_str_value(conf->clock_helper_uri, prev->clock_helper_uri, "");
    ngx_conf_merge_uint_value(conf->ad_markers, prev->ad_markers,
                              NGX_RTMP_DASH_AD_MARKERS_OFF);

    if (conf->fraglen) {
        conf->winfrags = conf->playlen / conf->fraglen;
    }

    /* schedule cleanup */

    if (conf->dash && conf->path.len && conf->cleanup) {
        if (conf->path.data[conf->path.len - 1] == '/') {
            conf->path.len--;
        }

        cleanup = ngx_pcalloc(cf->pool, sizeof(*cleanup));
        if (cleanup == NULL) {
            return NGX_CONF_ERROR;
        }

        cleanup->path = conf->path;
        cleanup->playlen = conf->playlen;

        conf->slot = ngx_pcalloc(cf->pool, sizeof(*conf->slot));
        if (conf->slot == NULL) {
            return NGX_CONF_ERROR;
        }

        conf->slot->manager = ngx_rtmp_dash_cleanup;
        conf->slot->name = conf->path;
        conf->slot->data = cleanup;
        conf->slot->conf_file = cf->conf_file->file.name.data;
        conf->slot->line = cf->conf_file->line;

        if (ngx_add_path(cf, &conf->slot) != NGX_OK) {
            return NGX_CONF_ERROR;
        }
    }

    ngx_conf_merge_str_value(conf->path, prev->path, "");

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_rtmp_dash_postconfiguration(ngx_conf_t *cf)
{
    ngx_rtmp_handler_pt        *h;
    ngx_rtmp_core_main_conf_t  *cmcf;
    ngx_rtmp_amf_handler_t     *ch;

    cmcf = ngx_rtmp_conf_get_module_main_conf(cf, ngx_rtmp_core_module);

    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_VIDEO]);
    *h = ngx_rtmp_dash_video;

    h = ngx_array_push(&cmcf->events[NGX_RTMP_MSG_AUDIO]);
    *h = ngx_rtmp_dash_audio;

    next_publish = ngx_rtmp_publish;
    ngx_rtmp_publish = ngx_rtmp_dash_publish;

    next_close_stream = ngx_rtmp_close_stream;
    ngx_rtmp_close_stream = ngx_rtmp_dash_close_stream;

    next_stream_begin = ngx_rtmp_stream_begin;
    ngx_rtmp_stream_begin = ngx_rtmp_dash_stream_begin;

    next_stream_eof = ngx_rtmp_stream_eof;
    ngx_rtmp_stream_eof = ngx_rtmp_dash_stream_eof;

    next_playlist = ngx_rtmp_playlist;
    ngx_rtmp_playlist = ngx_rtmp_dash_playlist;

    ch = ngx_array_push(&cmcf->amf);
    ngx_str_set(&ch->name, "onCuePoint");
    ch->handler = ngx_rtmp_dash_ad_markers;

    return NGX_OK;
}
