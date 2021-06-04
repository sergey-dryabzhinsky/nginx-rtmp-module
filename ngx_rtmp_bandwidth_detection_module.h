
/*
 * Copyright (C) Sergey Dryabzhinsky, 2016
 *
 * Based on http://permalink.gmane.org/gmane.comp.web.flash.red5/5869
 * And live & stat modules
 */


#ifndef _NGX_RTMP_BANDWIDTH_DETECTION_H_INCLUDED_
#define _NGX_RTMP_BANDWIDTH_DETECTION_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp.h"
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_bandwidth.h"
#include "ngx_rtmp_streams.h"


typedef struct ngx_rtmp_bandwidth_detection_ctx_s ngx_rtmp_bandwidth_detection_ctx_t;

struct ngx_rtmp_bandwidth_detection_ctx_s {
    uint64_t                            bytes_out;
    uint64_t                            bytes_out2;
    ngx_msec_t                          bw_begin_time;
    ngx_msec_t                          bw_begin_time2;
    ngx_uint_t                          pkt_sent;
    ngx_uint_t                          pkt_received;
    ngx_msec_t                          pkt_recv_time1;
    ngx_msec_t                          pkt_recv_time2;
    ngx_uint_t                          cum_latency;
    ngx_msec_t                          latency;
    unsigned                            active:1;
};

typedef struct {
    ngx_flag_t                          auto_start;             // Start bandwidth check on every connection
                                                                // without client call
    ngx_flag_t                          auto_sense;             // Do multiple calls onBWCheck, and onBWDone
                                                                // Or just fast onBWDone
    ngx_msec_t                          latency_min;            // Minimal detectable latency, msec
    ngx_msec_t                          latency_max;            // Maximum latency assumed, msec
    ngx_msec_t                          latency_undef;          // Default latency for modem, msec
                                                                // or satellite connections or undetectable one
    ngx_msec_t                          test_time;              // How long test bandwidth, msec
} ngx_rtmp_bandwidth_detection_app_conf_t;


extern ngx_module_t  ngx_rtmp_bandwidth_detection_module;


#endif /* _NGX_RTMP_BANDWIDTH_DETECTION_H_INCLUDED_ */
