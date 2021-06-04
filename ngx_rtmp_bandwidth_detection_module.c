
/*
 * Copyright (C) Sergey Dryabzhinsky, 2016
 *
 * Based on http://permalink.gmane.org/gmane.comp.web.flash.red5/5869
 * And live & stat modules
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_bandwidth_detection_module.h"
#include "ngx_rtmp_live_module.h"
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_transitions.h"


#define NGX_RTMP_BANDWIDTH_DETECTION_PAYLOAD_LENGTH    16*1024


static ngx_rtmp_connect_pt                      next_connect;


static ngx_int_t ngx_rtmp_bandwidth_detection_postconfiguration(ngx_conf_t *cf);
static void * ngx_rtmp_bandwidth_detection_create_app_conf(ngx_conf_t *cf);
static char * ngx_rtmp_bandwidth_detection_merge_app_conf(ngx_conf_t *cf,
       void *parent, void *child);
static char *ngx_rtmp_bandwidth_detection_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd,
       void *conf);
static ngx_int_t ngx_rtmp_bandwidth_detection_start(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in);
static ngx_int_t ngx_rtmp_bandwidth_detection_check_result(ngx_rtmp_session_t *s);


static u_char                              *payload;               // Payload data for all

static ngx_command_t  ngx_rtmp_bandwidth_detection_commands[] = {

    { ngx_string("bw_auto_start"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_bandwidth_detection_app_conf_t, auto_start),
      NULL },

    { ngx_string("bw_auto_sense"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_bandwidth_detection_app_conf_t, auto_sense),
      NULL },

    { ngx_string("bw_latency_min"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_rtmp_bandwidth_detection_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_bandwidth_detection_app_conf_t, latency_min),
      NULL },

    { ngx_string("bw_latency_max"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_rtmp_bandwidth_detection_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_bandwidth_detection_app_conf_t, latency_max),
      NULL },

    { ngx_string("bw_latency_undef"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_rtmp_bandwidth_detection_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_bandwidth_detection_app_conf_t, latency_undef),
      NULL },

    { ngx_string("bw_auto_sense_time"),
      NGX_RTMP_MAIN_CONF|NGX_RTMP_SRV_CONF|NGX_RTMP_APP_CONF|NGX_CONF_TAKE1,
      ngx_rtmp_bandwidth_detection_set_msec_slot,
      NGX_RTMP_APP_CONF_OFFSET,
      offsetof(ngx_rtmp_bandwidth_detection_app_conf_t, test_time),
      NULL },

      ngx_null_command
};


static ngx_rtmp_module_t  ngx_rtmp_bandwidth_detection_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_rtmp_bandwidth_detection_postconfiguration,        /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    ngx_rtmp_bandwidth_detection_create_app_conf,          /* create app configuration */
    ngx_rtmp_bandwidth_detection_merge_app_conf            /* merge app configuration */
};


ngx_module_t  ngx_rtmp_bandwidth_detection_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_bandwidth_detection_module_ctx,   /* module context */
    ngx_rtmp_bandwidth_detection_commands,      /* module directives */
    NGX_RTMP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_rtmp_bandwidth_detection_create_app_conf(ngx_conf_t *cf)
{
    ngx_rtmp_bandwidth_detection_app_conf_t     *acf;
    ngx_uint_t                                  i;

    acf = ngx_pcalloc(cf->pool, sizeof(ngx_rtmp_bandwidth_detection_app_conf_t));
    if (acf == NULL) {
        return NULL;
    }

    acf->auto_start     = NGX_CONF_UNSET;
    acf->auto_sense     = NGX_CONF_UNSET;
    acf->latency_max    = NGX_CONF_UNSET_MSEC;
    acf->latency_min    = NGX_CONF_UNSET_MSEC;
    acf->latency_undef  = NGX_CONF_UNSET_MSEC;
    acf->test_time      = NGX_CONF_UNSET_MSEC;

    /* Init payload only once with some random garbage */
    payload = ngx_pcalloc(cf->pool, NGX_RTMP_BANDWIDTH_DETECTION_PAYLOAD_LENGTH + 1);
    for (i=0; i<NGX_RTMP_BANDWIDTH_DETECTION_PAYLOAD_LENGTH; i++) {
        // make sure it is readable and not contain zero
        payload[i] = (ngx_random() % 32) + 32;
    }

    return acf;
}


static char *
ngx_rtmp_bandwidth_detection_merge_app_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_rtmp_bandwidth_detection_app_conf_t *prev = parent;
    ngx_rtmp_bandwidth_detection_app_conf_t *conf = child;

    ngx_conf_merge_value(conf->auto_start, prev->auto_start, 0);
    ngx_conf_merge_value(conf->auto_sense, prev->auto_sense, 0);
    ngx_conf_merge_msec_value(conf->latency_max, prev->latency_max, 800);
    ngx_conf_merge_msec_value(conf->latency_min, prev->latency_min, 10);
    ngx_conf_merge_msec_value(conf->latency_undef, prev->latency_undef, 100);
    ngx_conf_merge_msec_value(conf->test_time, prev->test_time, 2000);

    return NGX_CONF_OK;
}


static char *
ngx_rtmp_bandwidth_detection_set_msec_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                       *p = conf;
    ngx_str_t                  *value;
    ngx_msec_t                 *msp;

    msp = (ngx_msec_t *) (p + cmd->offset);

    value = cf->args->elts;

    if (value[1].len == sizeof("off") - 1 &&
        ngx_strncasecmp(value[1].data, (u_char *) "off", value[1].len) == 0)
    {
        *msp = 0;
        return NGX_CONF_OK;
    }

    return ngx_conf_set_msec_slot(cf, cmd, conf);
}


static ngx_int_t
ngx_rtmp_bandwidth_detection_on_result(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    ngx_rtmp_bandwidth_detection_app_conf_t         *acf;
    ngx_rtmp_bandwidth_detection_ctx_t       *ctx;

    static struct {
        double                  trans;
        ngx_uint_t              count;
    } v;

    static ngx_rtmp_amf_elt_t   in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.count, 0 },

    };

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "bandwidth_detection: _result");

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_bandwidth_detection_module);
    if (acf == NULL) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: _result - no app config!");
        return NGX_ERROR;
    }

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_bandwidth_detection_module);
    if (ctx == NULL || s->relay) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: _result - no context or relay!");
        return NGX_OK;
    }

    ngx_memzero(&v, sizeof(v));
    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: _result - no packet readed!");
        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "bandwidth_detection: _result: trans='%f' count='%ui'",
            v.trans, v.count);

    if (acf->auto_sense) {
        switch ((ngx_int_t)v.trans) {
            case NGX_RTMP_BANDWIDTH_DETECTION_BWCHECK_TRANS:
                return ngx_rtmp_bandwidth_detection_check_result(s);

            case NGX_RTMP_BANDWIDTH_DETECTION_BWDONE_TRANS:
                /* Need to test it. Maybe need to set this before send bwDone. */
                ctx->active = 0;
                break;
            default:
                return NGX_OK;
        }
    }
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_bandwidth_detection_on_error(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    ngx_rtmp_bandwidth_detection_ctx_t       *ctx;

    static struct {
        double                  trans;
    } v;

    static ngx_rtmp_amf_elt_t   in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, 0 },

    };

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "bandwidth_detection: _error");

    ngx_memzero(&v, sizeof(v));
    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: _error - no packet readed!");
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "bandwidth_detection: _error: trans='%f''",
            v.trans);

    ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_bandwidth_detection_module);
    if (ctx == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: _result - no context!");
        return NGX_OK;
    }

    /**
     * If onBWCheck / onBWDone unsupported - drop activity
     */
    switch ((ngx_int_t)v.trans) {
        case NGX_RTMP_BANDWIDTH_DETECTION_BWCHECK_TRANS:
            ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: _error - client not support onBWCheck?");
            ctx->active = 0;
            break;

        case NGX_RTMP_BANDWIDTH_DETECTION_BWDONE_TRANS:
            ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: _error - client not support onBWDone?");
            ctx->active = 0;
            break;
        default:
            return NGX_OK;
    }

    return NGX_OK;
}

/**
 * Start bandwidth detection here
 * Long version with multiple onBWCheck calls
 */
static ngx_int_t
ngx_rtmp_bandwidth_detection_start(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in)
{

    ngx_rtmp_bandwidth_detection_app_conf_t         *acf;
    ngx_rtmp_bandwidth_detection_ctx_t              *bw_ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "bandwidth_detection: start");

    if (s->relay) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: start - no relay please!");
        return NGX_ERROR;
    }

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_bandwidth_detection_module);
    if (acf == NULL) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: start - no app config!");
        return NGX_ERROR;
    }

    if (!acf->test_time) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: start - no test time!");
        return NGX_ERROR;
    }

    bw_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_bandwidth_detection_module);
    if (bw_ctx == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: start - no context! create new and set for module and session!");

        bw_ctx = ngx_palloc(s->connection->pool, sizeof(ngx_rtmp_bandwidth_detection_ctx_t));
        if (bw_ctx == NULL) {
            ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: start - no context created!");
            return NGX_ERROR;
        }

        ngx_rtmp_set_ctx(s, bw_ctx, ngx_rtmp_bandwidth_detection_module);
        ngx_memzero(bw_ctx, sizeof(*bw_ctx));

    }

    if (bw_ctx->active) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: start - already active!");
        return NGX_OK;
    } else {

        bw_ctx->active = 1;
        bw_ctx->bw_begin_time = ngx_cached_time->msec;
        bw_ctx->pkt_sent = 1;
        bw_ctx->pkt_received = 0;
        bw_ctx->pkt_recv_time1 = 0;
        bw_ctx->pkt_recv_time2 = 0;
        bw_ctx->cum_latency = 0;
        bw_ctx->latency = acf->latency_min;
        bw_ctx->bytes_out = s->out_bytes;
    }

    // Send first packet with empty payload - for latency calculation
    return ngx_rtmp_send_bwcheck(s, NULL, 0);
}


/**
 * FAST bandwidth detection here
 */
static ngx_int_t
ngx_rtmp_bandwidth_detection_fast(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in)
{

    ngx_rtmp_bandwidth_detection_app_conf_t         *acf;
    ngx_rtmp_bandwidth_detection_ctx_t              *bw_ctx;
    ngx_uint_t                                      timePassed, snd_cnt;
    double                                          deltaDown;
    double                                          deltaTime;
    double                                          kbitDown;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "bandwidth_detection: fast");

    if (s->relay) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: fast - no relay please!");
        return NGX_ERROR;
    }

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_bandwidth_detection_module);
    if (acf == NULL) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: fast - no app config!");
        return NGX_ERROR;
    }

    if (!acf->test_time) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: fast - no test time!");
        return NGX_ERROR;
    }

    bw_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_bandwidth_detection_module);
    if (bw_ctx == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: fast - no context! create new and set for module and session!");

        bw_ctx = ngx_palloc(s->connection->pool, sizeof(ngx_rtmp_bandwidth_detection_ctx_t));
        if (bw_ctx == NULL) {
            ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: fast - no context created!");
            return NGX_ERROR;
        }

        ngx_rtmp_set_ctx(s, bw_ctx, ngx_rtmp_bandwidth_detection_module);
        ngx_memzero(bw_ctx, sizeof(*bw_ctx));
    }

    // To prevent in _result call
    bw_ctx->active = 0;
    bw_ctx->bw_begin_time = ngx_cached_time->msec;
    bw_ctx->bw_begin_time2 = 0;
    bw_ctx->latency = acf->latency_min;
    bw_ctx->bytes_out2 = s->out_bytes;

    ngx_rtmp_send_bwcheck(s, NULL, 0);

    // Do some load for next call
    snd_cnt = 0;
    while (snd_cnt<5) {
        if (NGX_OK != ngx_rtmp_send_bwcheck(s, payload, NGX_RTMP_BANDWIDTH_DETECTION_PAYLOAD_LENGTH)) {
            break;
        }
        snd_cnt++;
    }

    /* Emulate accumulation between calls */
    bw_ctx->bw_begin_time = bw_ctx->bw_begin_time2;
    bw_ctx->bw_begin_time2 = ngx_cached_time->msec;
    bw_ctx->bytes_out = bw_ctx->bytes_out2;
    bw_ctx->bytes_out2 = s->out_bytes;

    timePassed = (bw_ctx->bw_begin_time2 - bw_ctx->bw_begin_time) / snd_cnt;

    bw_ctx->latency = ngx_min(timePassed, acf->latency_max);
    bw_ctx->latency = ngx_max(bw_ctx->latency, acf->latency_min);

    deltaDown = (bw_ctx->bytes_out2 - bw_ctx->bytes_out) *8/1000.;
    deltaTime = ( (bw_ctx->bw_begin_time2 - bw_ctx->bw_begin_time) - (bw_ctx->latency*snd_cnt))/1000.;

    if (deltaTime <= 0) deltaTime = (bw_ctx->bw_begin_time2 - bw_ctx->bw_begin_time)/1000.;
    if (deltaTime <= 0) deltaTime = 1.;

    kbitDown = deltaDown/deltaTime;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "bandwidth_detection: fast - check done!");
    ngx_log_debug5(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "bandwidth_detection: fast - kbitDown=%.3f, deltaDown=%.3f, deltaTime=%.3f, latency=%.3f, KBytes=%ui",
                   kbitDown, deltaDown, deltaTime, bw_ctx->latency, (bw_ctx->bytes_out2 - bw_ctx->bytes_out)/1024);

    return ngx_rtmp_send_bwdone(s, kbitDown, deltaDown, deltaTime, bw_ctx->latency);
}


/**
 * Bandwidth detection wrapper
 */
static ngx_int_t
ngx_rtmp_bandwidth_detection_wrapper(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in)
{
    ngx_rtmp_bandwidth_detection_app_conf_t         *acf;

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_bandwidth_detection_module);
    if (acf == NULL) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: wrapper - no app config!");
        return NGX_ERROR;
    }

    if (acf->auto_sense) {
        return ngx_rtmp_bandwidth_detection_start(s, h, in);
    } else {
        return ngx_rtmp_bandwidth_detection_fast(s, h, in);
    }
}


/**
 * Bandwidth detection from client side
 */
static ngx_int_t
ngx_rtmp_bandwidth_detection_clientcheck(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h, ngx_chain_t *in)
{

    ngx_rtmp_bandwidth_detection_app_conf_t         *acf;
    ngx_rtmp_bandwidth_detection_ctx_t              *bw_ctx;

    static struct {
        double                  trans;
        ngx_msec_t              time;
    } v;

    static ngx_rtmp_amf_elt_t   in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.time, 0 },

    };

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "bandwidth_detection: bwcheck");

    if (s->relay) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: bwcheck - no relay please!");
        return NGX_ERROR;
    }

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_bandwidth_detection_module);
    if (acf == NULL) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: bwcheck - no app config!");
        return NGX_ERROR;
    }

    bw_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_bandwidth_detection_module);
    if (bw_ctx == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: bwcheck - no context! create new and set for module and session!");

        bw_ctx = ngx_palloc(s->connection->pool, sizeof(ngx_rtmp_bandwidth_detection_ctx_t));
        if (bw_ctx == NULL) {
            ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: bwcheck - no context created!");
            return NGX_ERROR;
        }

        ngx_rtmp_set_ctx(s, bw_ctx, ngx_rtmp_bandwidth_detection_module);
        ngx_memzero(bw_ctx, sizeof(*bw_ctx));

    }

    ngx_memzero(&v, sizeof(v));
    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: bwcheck - no packet readed!");
        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
            "bandwidth_detection: bwcheck: trans='%f' time='%ui'",
            v.trans, v.time);

    return ngx_rtmp_send_onclientbwcheck(s, v.trans, s->out_bytes, s->in_bytes, v.time);
}


/**
 * End bandwidth detection here
 */
static ngx_int_t
ngx_rtmp_bandwidth_detection_check_result(ngx_rtmp_session_t *s)
{

    ngx_rtmp_bandwidth_detection_app_conf_t         *acf;
    ngx_rtmp_bandwidth_detection_ctx_t              *bw_ctx;
    ngx_uint_t                                      timePassed;
    ngx_uint_t                                      deltaDown;
    double                                          deltaTime;
    double                                          kbitDown;
    uint64_t                                        bytesOut;

    ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "bandwidth_detection: check");

    acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_bandwidth_detection_module);
    if (acf == NULL) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: check - no app config!");
        return NGX_ERROR;
    }

    if (!acf->test_time) {
        ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                       "bandwidth_detection: check - no test time!");
        return NGX_ERROR;
    }

    bw_ctx = ngx_rtmp_get_module_ctx(s, ngx_rtmp_bandwidth_detection_module);
    if (bw_ctx == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: check - no bw context!");
        return NGX_OK;
    }
    if (!bw_ctx->active) {
        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: check - not active!");
        return NGX_OK;
    }

    timePassed = ngx_cached_time->msec - bw_ctx->bw_begin_time;
    bw_ctx->pkt_received ++;

    if (bw_ctx->pkt_received == 1) {
        bw_ctx->latency = ngx_min(timePassed, acf->latency_max);
        bw_ctx->latency = ngx_max(bw_ctx->latency, acf->latency_min);
        bw_ctx->pkt_recv_time1 = ngx_cached_time->msec;
    }

    if (bw_ctx->pkt_received == 2) {
        bw_ctx->pkt_recv_time2 = ngx_cached_time->msec;
    }

    // If we have a hi-speed network with low latency send more to determine
    // better bandwidth numbers, send no more than 6 packets
    if (bw_ctx->pkt_received == 2 && timePassed < acf->test_time) {

        bw_ctx->pkt_sent ++;
        bw_ctx->cum_latency ++;
        return ngx_rtmp_send_bwcheck(s, payload, NGX_RTMP_BANDWIDTH_DETECTION_PAYLOAD_LENGTH);

    } else if (bw_ctx->pkt_received == bw_ctx->pkt_sent) {

        // See if we need to normalize latency
        if ( bw_ctx->latency >= acf->latency_undef ) {

            // make sure we detect sattelite and modem correctly
            if (bw_ctx->pkt_recv_time2 - bw_ctx->pkt_recv_time1 > 1000) {
                bw_ctx->latency = acf->latency_undef;
            }

        }

        bytesOut = s->out_bytes;
        deltaDown = (bytesOut - bw_ctx->bytes_out) *8/1000.;
        deltaTime = ( (ngx_cached_time->msec - bw_ctx->bw_begin_time) - (bw_ctx->latency*bw_ctx->cum_latency))/1000.;

        if (deltaTime <= 0) deltaTime = (ngx_cached_time->msec - bw_ctx->bw_begin_time)/1000.;
        if (deltaTime <= 0) deltaTime = 1.;

        kbitDown = deltaDown/deltaTime;

        ngx_log_debug0(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: check - done!");
        ngx_log_debug5(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                       "bandwidth_detection: check - kbitDown=%ui, deltaDown=%.3f, deltaTime=%.3f, latency=%.3f, KBytes=%ui",
                       kbitDown, deltaDown, deltaTime, bw_ctx->latency, (bytesOut - bw_ctx->bytes_out)/1024);

        return ngx_rtmp_send_bwdone(s, kbitDown, deltaDown, deltaTime, bw_ctx->latency);
    }

    if (bw_ctx->pkt_sent == 1 && bw_ctx->pkt_received == 1) {
        // First call
        return ngx_rtmp_send_bwcheck(s, payload, NGX_RTMP_BANDWIDTH_DETECTION_PAYLOAD_LENGTH);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_bandwidth_detection_connect(ngx_rtmp_session_t *s, ngx_rtmp_connect_t *v)
{
    ngx_rtmp_bandwidth_detection_app_conf_t         *acf;
    ngx_int_t   result;

    result = next_connect(s, v);

    if (result == NGX_OK) {

        acf = ngx_rtmp_get_module_app_conf(s, ngx_rtmp_bandwidth_detection_module);
        if (acf == NULL) {
            ngx_log_error(NGX_LOG_WARN, s->connection->log, 0,
                           "bandwidth_detection: check - no app config!");
            return NGX_ERROR;
        }

        if (acf->auto_start) {
            result = ngx_rtmp_bandwidth_detection_wrapper(s, NULL, NULL);
        }
    }

    return result;
}


static ngx_int_t
ngx_rtmp_bandwidth_detection_postconfiguration(ngx_conf_t *cf)
{
    ngx_rtmp_core_main_conf_t          *cmcf;
    ngx_rtmp_amf_handler_t             *ch;

    cmcf = ngx_rtmp_conf_get_module_main_conf(cf, ngx_rtmp_core_module);

    next_connect = ngx_rtmp_connect;
    ngx_rtmp_connect = ngx_rtmp_bandwidth_detection_connect;

    ch = ngx_array_push(&cmcf->amf);
    ngx_str_set(&ch->name, "_result");
    ch->handler = ngx_rtmp_bandwidth_detection_on_result;

    ch = ngx_array_push(&cmcf->amf);
    ngx_str_set(&ch->name, "_error");
    ch->handler = ngx_rtmp_bandwidth_detection_on_error;

    ch = ngx_array_push(&cmcf->amf);
    ngx_str_set(&ch->name, "checkBandwidth");
    ch->handler = ngx_rtmp_bandwidth_detection_wrapper;

    ch = ngx_array_push(&cmcf->amf);
    ngx_str_set(&ch->name, "onClientBWCheck");
    ch->handler = ngx_rtmp_bandwidth_detection_clientcheck;

    return NGX_OK;
}
