# NGINX-based Media Streaming Server

## nginx-rtmp-module (dash enhanced version)

Forked from https://github.com/sergey-dryabzhinsky/ which was the most up to date version (until now)

Notable new features :

 - add the possibility to have dash variant like (show below configuration, using ffmpeg to trancode in 3 variants).
   note the "max" flag which indicate which representation should have max witdh and height and so use it to create the variant manifest.
   you can also use any encoder to directly push the variant.
 - add the support of using repetition in manifest to shorten them (optopn dash_repetition)
 - add the support of common-encryption; currently working DRM are ClearKey/Widevine/Playready (see specific doc Here)
 - add the support of inband scte event, from rtmp AMF event to dash (InbandEvent in manifest and emsg box in mp4 fragment)


See original doc here for full list of options.

```
  rtmp {
    server {
    listen 1935;
   
    application ingest {
      live on;
      exec /usr/bin/ffmpeg -i rtmp://localhost/$app/$name \
           -c:a libfdk_aac -b:a 64k -c:v libx264 -preset fast -profile:v baseline -vsync cfr -s 1024x576 -b:v 1024K -bufsize 1024k \
           -f flv rtmp://localhost/dash/$name_hi \
           -c:a libfdk_aac -b:a 64k -c:v libx264 -preset fast -profile:v baseline -vsync cfr -s 640x360 -b:v 832K -bufsize 832k \
           -f flv rtmp://localhost/dash/$name_med \
           -c:a libfdk_aac -b:a 64k -c:v libx264 -preset fast -profile:v baseline -vsync cfr -s 320x180 -b:v 256K -bufsize 256k \
           -f flv rtmp://localhost/dash/$name_low
    }
      
    application dash {
      live on;
      dash on;
      dash_nested on; 
      dash_repetition on;
      dash_path /dev/shm/dash;
      dash_fragment 4; # 4 second is generaly a good choice for live
      dash_playlist_length 120; # keep 120s of tail
      dash_cleanup on;
      dash_variant _low bandwidth="256000" width="320" height="180";
      dash_variant _med bandwidth="832000" width="640" height="360";
      dash_variant _hi bandwidth="1024000" width="1024" height="576" max;
    }
  }

   server {
      listen 443 ssl;
      location / {
        root /var/www;
        add_header Cache-Control no-cache;
        add_header 'Access-Control-Allow-Origin' '*';
      }
      location /dash/live/index.mpd {
        alias /dev/shm/dash/live/index.mpd;
        add_header 'Access-Control-Allow-Origin' '*';
        add_header Cache-Control 'public, max-age=0, s-maxage=2';
      }
      location /dash/live {
        alias /dev/shm/dash/live;
        add_header 'Access-Control-Allow-Origin' '*';
        add_header Cache-Control 'public, max-age=600, s-maxage=600';
      }

      server_name live.site.net;
      ssl_certificate /etc/letsencrypt/live/live.site.net/fullchain.pem;
      ssl_certificate_key /etc/letsencrypt/live/live.sit.net/privkey.pem;

    }
}
```


