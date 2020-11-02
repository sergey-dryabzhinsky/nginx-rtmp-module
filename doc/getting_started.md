# Getting started with nginx rtmp

## Download, build and install

CD to build directory (home)
```sh
$ cd /usr/build
```

Download & unpack latest nginx-rtmp (you can also use http)
```sh
$ git clone git://github.com/sergey-dryabzhinsky/nginx-rtmp-module
```

Download & unpack nginx (you can also use svn)

```sh
$ wget http://nginx.org/download/nginx-1.2.4.tar.gz
$ tar xzf nginx-1.2.4.tar.gz
$ cd nginx-1.2.4
```

Build nginx with nginx-rtmp
```sh
$ ./configure --add-module=/usr/build/nginx-rtmp-module
$ make
$ make install
```

For nginx 1.3.4-1.5.0 more options are needed
```sh
$ ./configure --add-module=/usr/build/nginx-rtmp-module --with-http_ssl_module
$ make
$ make install
```

## Set up live streaming

To set up RTMP support you need to add `rtmp{}` section to `nginx.conf` (can be found in PREFIX/conf/nginx.conf). Stock `nginx.conf` contains only `http{}` section.

Use this `nginx.conf` instead of stock config:
```sh
#user  nobody;
worker_processes  1;
    
error_log  logs/error.log debug;
    
events {
    worker_connections  1024;
}

http {
    include       mime.types;
    default_type  application/octet-stream;
    
    sendfile        on;
    keepalive_timeout  65;

    server {
        listen       8080;
        server_name  localhost;
    
        # sample handlers
        #location /on_play {
        #    if ($arg_pageUrl ~* localhost) {
        #        return 201;
        #    }
        #    return 202;
        #}
        #location /on_publish {
        #    return 201;
        #}
    
        #location /vod {
        #    alias /var/myvideos;
        #}
    
        # rtmp stat
        location /stat {
            rtmp_stat all;
            rtmp_stat_stylesheet stat.xsl;
        }
        location /stat.xsl {
            # you can move stat.xsl to a different location
            root /usr/build/nginx-rtmp-module;
        }
    
        # rtmp control
        location /control {
            rtmp_control all;
        }
        error_page   500 502 503 504  /50x.html;
        location = /50x.html {
            root   html;
        }
    }
}
rtmp {
    server {
        listen 1935;
        ping 30s;
        notify_method get;
    
        application myapp {
            live on;
    
            # sample play/publish handlers
            #on_play http://localhost:8080/on_play;
            #on_publish http://localhost:8080/on_publish;
    
            # sample recorder
            #recorder rec1 {
            #    record all;
            #    record_interval 30s;
            #    record_path /tmp;
            #    record_unique on;
            #}

            # sample HLS
            #hls on;
            #hls_path /tmp/hls;
            #hls_sync 100ms;
        }

        # Video on demand
        #application vod {
        #    play /var/Videos;
        #}

        # Video on demand over HTTP
        #application vod_http {
        #    play http://localhost:8080/vod/;
        #}
    }
}
```

## Statistics

Navigate your browser to `http://localhost:8080/stat` to see current
streaming statistics, connected clients, bandwidth etc.

## Publishing with ffmpeg

The easiest way to publish live video stream is using ffmpeg (or avconv).
It's already installed on most systems and easy to install on others.

RTMP supports only a limited number of codecs. The most popular RTMP video
codecs are H264, Sorenson-H263 (aka flv) and audio codecs AAC, MP3,
Nellymoser, Speex. If your video is encoded with these codecs
(the most common pair is H264/AAC) then you do not need any conversion.
Otherwise you need to convert video to one of supported codecs.

We'll stream test file `/var/videos/test.mp4` to server with ffmpeg.

Streaming without conversion (given `test.mp4` codecs are compatible with RTMP)
```sh
$ ffmpeg -re -i /var/Videos/test.mp4 -c copy -f flv rtmp://localhost/myapp/mystream
```

Streaming and encoding audio (AAC) and video (H264), need `libx264` and `libfaac`
```sh
$ ffmpeg -re -i /var/Videos/test.mp4 -c:v libx264 -c:a libfaac -ar 44100 -ac 1 -f flv rtmp://localhost/myapp/mystream
```

Streaming and encoding audio (MP3) and video (H264), need `libx264` and `libmp3lame`
```sh
$ ffmpeg -re -i /var/Videos/test.mp4 -c:v libx264 -c:a libmp3lame -ar 44100 -ac 1 -f flv rtmp://localhost/myapp/mystream
```

Streaming and encoding audio (Nellymoser) and video (Sorenson H263)
```sh
$ ffmpeg -re -i /var/Videos/test.mp4 -c:v flv -c:a nellymoser -ar 44100 -ac 1 -f flv rtmp://localhost/myapp/mystream
```

## Publishing video from webcam
```sh
$ ffmpeg -f video4linux2 -i /dev/video0 -c:v libx264 -an -f flv rtmp://localhost/myapp/mystream
```

## Playing with ffplay
```sh
$ ffplay rtmp://localhost/myapp/mystream
```

## Publishing and playing with flash

See `test/rtmp-publisher` directory for test flash applets and html.
