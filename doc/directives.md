# Directives

Table of Contents
=================

* [Core](#core)
    * [rtmp](#rtmp)
    * [server](#server)
    * [listen](#listen)
    * [application](#application)
    * [timeout](#timeout)
    * [ping](#ping)
    * [ping_timeout](#ping_timeout)
    * [max_streams](#max_streams)
    * [ack_window](#ack_window)
    * [chunk_size](#chunk_size)
    * [max_queue](#max_queue)
    * [max_message](#max_message)
    * [buflen](#buflen)
    * [out_queue](#out_queue)
    * [out_cork](#out_cork)
* [Access](#access)
    * [allow](#allow)
    * [deny](#deny)
* [Exec](#exec)
    * [exec_push](#exec_push)
    * [exec_pull](#exec_pull)
    * [exec](#exec)
    * [exec_options](#exec_options)
    * [exec_static](#exec_static)
    * [exec_kill_signal](#exec_kill_signal)
    * [respawn](#respawn)
    * [respawn_timeout](#respawn_timeout)
    * [exec_publish](#exec_publish)
    * [exec_play](#exec_play)
    * [exec_play_done](#exec_play_done)
    * [exec_publish_done](#exec_publish_done)
    * [exec_record_started](#exec_record_started)
    * [exec_record_done](#exec_record_done)
* [Live](#live)
    * [live](#live)
    * [meta](#meta)
    * [interleave](#interleave)
    * [wait_key](#wait_key)
    * [wait_video](#wait_video)
    * [publish_notify](#publish_notify)
    * [drop_idle_publisher](#drop_idle_publisher)
    * [sync](#sync)
    * [play_restart](#play_restart)
    * [idle_streams](#idle_streams)
* [Record](#record)
    * [record](#record)
    * [record_path](#record_path)
    * [record_suffix](#record_suffix)
    * [record_unique](#record_unique)
    * [record_append](#record_append)
    * [record_lock](#record_lock)
    * [record_max_size](#record_max_size)
    * [record_max_frames](#record_max_frames)
    * [record_interval](#record_interval)
    * [recorder](#recorder)
    * [record_notify](#record_notify)
* [Video on demand](#video-on-demand)
    * [play](#play)
    * [play_temp_path](#play_temp_path)
    * [play_local_path](#play_local_path)
* [Relay](#relay)
    * [pull](#pull)
    * [push](#push)
    * [push_reconnect](#push_reconnect)
    * [session_relay](#session_relay)
* [Notify](#notify)
    * [on_connect](#on_connect)
    * [on_play](#on_play)
    * [on_playlist](#on_playlist)
    * [on_publish](#on_publish)
    * [on_done](#on_done)
    * [on_play_done](#on_play_done)
    * [on_publish_done](#on_publish_done)
    * [on_record_started](#on_record_started)
    * [on_record_done](#on_record_done)
    * [on_update](#on_update)
    * [notify_update_timeout](#notify_update_timeout)
    * [notify_update_strict](#notify_update_strict)
    * [notify_relay_redirect](#notify_relay_redirect)
    * [notify_send_redirect](#notify_send_redirect)
    * [notify_method](#notify_method)
* [HLS](#hls)
    * [hls](#hls)
    * [hls_path](#hls_path)
    * [hls_fragment](#hls_fragment)
    * [hls_playlist_length](#hls_playlist_length)
    * [hls_sync](#hls_sync)
    * [hls_continuous](#hls_continuous)
    * [hls_nested](#hls_nested)
    * [hls_base_url](#hls_base_url)
    * [hls_cleanup](#hls_cleanup)
    * [hls_fragment_naming](#hls_fragment_naming)
    * [hls_fragment_naming_granularity](#hls_fragment_naming_granularity)
    * [hls_fragment_slicing](#hls_fragment_slicing)
    * [hls_variant](#hls_variant)
    * [hls_type](#hls_type)
    * [hls_allow_client_cache](#hls_allow_client_cache)
    * [hls_keys](#hls_keys)
    * [hls_key_path](#hls_key_path)
    * [hls_key_url](#hls_key_url)
    * [hls_fragments_per_key](#hls_fragments_per_key)
* [MPEG-DASH](#mpeg-dash)
    * [dash](#dash)
    * [dash_path](#dash_path)
    * [dash_fragment](#dash_fragment)
    * [dash_playlist_length](#dash_playlist_length)
    * [dash_nested](#dash_nested)
    * [dash_cleanup](#dash_cleanup)
    * [dash_clock_compensation](#dash_clock_compensation)
    * [dash_clock_helper_uri](#dash_clock_helper_uri)
* [Access log](#access-log)
    * [access_log](#access_log)
    * [log_format](#log_format)
* [Limits](#limits)
    * [max_connections](#max_connections)
* [Statistics](#statistics)
    * [rtmp_stat](#rtmp_stat)
    * [rtmp_stat_stylesheet](#rtmp_stat_stylesheet)
* [Multi-worker live streaming](#multi-worker-live-streaming)
    * [rtmp_auto_push](#rtmp_auto_push)
    * [rtmp_auto_push_reconnect](#rtmp_auto_push_reconnect)
    * [rtmp_socket_dir](#rtmp_socket_dir)
* [Control](#control)
    * [rtmp_control](#rtmp_control)

## Core
#### rtmp
syntax: `rtmp { ... }`  
context: root  
The block which holds all RTMP settings

#### server
syntax: `server { ... }`  
context: rtmp  
Declares RTMP server instance
```sh
rtmp {
  server {
  }
}
```

#### listen
syntax: `listen (addr[:port]|port|unix:path) [bind]  [ipv6only=on|off] [so_keepalive=on|off|keepidle:keepintvl:keepcnt|proxy_protocol]`  
context: server  

Adds listening socket to NGINX for accepting RTMP connections
```sh
server {
    listen 1935;
}
```

#### application
syntax: `application name { ... }`  
context: server  

Creates RTMP application. Unlike http location application name cannot
be a pattern.
```sh
server {
    listen 1935;
    application myapp {
    }
}
```

#### timeout
syntax: `timeout value`  
context: rtmp, server  

Socket timeout. This value is primarily used for writing. Most of time RTMP 
module does not expect any activity on all sockets except for publisher socket. 
If you want broken socket to get quickly disconnected use active tools like 
keepalive or RTMP ping. Default is 1 minute.
```sh
timeout 60s;
```

#### ping
syntax: `ping value`  
context: rtmp, server  

RTMP ping interval. Zero turns ping off. RTMP ping is a protocol feature for
active connection check. A special packet is sent to remote peer and a reply
is expected within a timeout specified with ping_timeout directive. If ping
reply is not received within this time then connection is closed. Default 
value for ping is 1 minute. Default ping timeout is 30 seconds.
```sh
ping 3m;
ping_timeout 30s;
```

#### ping_timeout
syntax: `ping_timeout value`  
context: rtmp, server  

See ping description above.

#### max_streams
syntax: `max_streams value`  
context: rtmp, server  

Sets maximum number of RTMP streams. Data streams are multiplexed into
a single data stream. Different channels are used for sending commands,
audio, video etc. Default value is 32 which is usually ok for many cases.
```sh
max_streams 32;
```

#### ack_window
syntax: `ack_window value`  
context: rtmp, server  

Sets RTMP acknowledge window size. It's the number of bytes received after
which peer should send acknowledge packet to remote side. Default value is
5000000.
```sh
ack_window 5000000;
```

#### chunk_size
syntax: `chunk_size value`  
context: rtmp, server  

Maximum chunk size for stream multiplexing. Default is 4096. The bigger
this value the lower CPU overhead. This value cannot be less than 128.
```sh
chunk_size 4096;
```

#### max_queue

#### max_message
syntax: `max_queue value`  
context: rtmp, server  

Maximum size of input data message. All input data comes split into
messages (and further in chunks). A partial message is kept in memory while
waiting for it to complete. In theory incoming message can be
very large which can be a problem for server stability. Default value
1M is enough for many cases.
```sh
max_message 1M;
```

### buflen
syntax: `buflen time`  
context: rtmp, server  

Sets default buffer length. Usually client sends RTMP `set_buflen` command
before playing and resets this setting. Default is `1000 ms`.
```sh
buflen 5s;
````

#### out_queue

#### out_cork

## Access

#### allow
Syntax: `allow [play|publish] address|subnet|all`  
Context: rtmp, server, application  

Allow publishing/playing from addresses specified or from all addresses.
Allow/deny directives are checked in order of appearance.
```sh
allow publish 127.0.0.1;
deny publish all;
allow play 192.168.0.0/24;
deny play all;
```

#### deny
Syntax: `deny [play|publish] address|subnet|all`  
Context: rtmp, server, application  

See allow for description.

## Exec

#### exec_push
Syntax: `exec_push command arg*`  
Context: rtmp, server, application

Specifies external command with arguments to be executed on
every stream published. When publishing stops the process
is terminated. Full path to binary should be specified as the
first argument. There are no assumptions about what this process should
do. However this feature is useful with ffmpeg for stream
transcoding. FFmpeg is supposed to connect to nginx-rtmp as a client 
and output transcoded stream back to nginx-rtmp as publisher. Substitutions
of form $var/${var} can be used within command line:
* $name - stream name
* $app - application name
* $addr - client address
* $flashver - client flash version
* $swfurl - client swf url
* $tcurl - client tc url
* $pageurl - client page url

Shell-style redirects can be specified in `exec_push` directive for writing output
and accepting input. Supported are
* truncating output `>file`
* appending output `>>file`
* descriptor redirects like `1>&2`
* input `<file`

Make sure there's no space between redirection character and stream name/number.

You can specify full path to the command to execute or short command name. In the latter
case binary is looked up in directories specified by the `PATH` environment variable.
By default nginx clears the environment which will usually make rtmp module run only binaries
located in standard directories like `/bin` and `/usr/bin`. To make this always work
please keep the original `PATH` variable value with the following nginx directive.
```sh
env PATH; 
```

The following ffmpeg call transcodes incoming stream to HLS-ready
stream (H264/AAC). FFmpeg should be compiled with libx264 & libfaac support
for this example to work.
```sh
application src {
    live on;
    exec_push ffmpeg -i rtmp://localhost/src/$name -vcodec libx264 -vprofile baseline -g 10 -s 300x200 -acodec libfaac -ar 44100 -ac 1 -f flv rtmp://localhost/hls/$name 2>>/var/log/ffmpeg-$name.log;
}

application hls {
    live on;
    hls on;
    hls_path /tmp/hls;
    hls_fragment 15s;
}
```

#### exec_pull
Syntax: `exec_pull command arg*`  
Context: rtmp, server, application

Specifies external command with arguments to be executed on play event.
The command is executed when first client connects to the stream and is
killed when the last one disconnects. This directive makes it possible
to pull remote stream in any format for local clients.

The feature works reliably only in single-worker mode. The reason for this
is we cannot make sure external process always connects to the right worker.
It will obviously connect to a random one. While this will still work in
most cases it's not a recommended architecture, it will be unstable and buggy.

Directive arguments are the same as for `exec_push`.
```sh
application myapp {
    live on;
    exec_pull ffmpeg -i http://example.com/video_$name.ts -c copy -f flv rtmp://localhost/$app/$name;
}
```

In the above configuration `exec_pull` directive serves all streams. That leads
to certain limitations on remote stream name format. It should be possible to construct
the remote url using available variables like `$app`, `$name` etc. When it's not possible
you can add `exec_options on` directive which permits setting additional stream options
in exec-family directives. The only option supported now is `name` option.
```sh
application myapp {
    live on;
    exec_options on;
    exec_pull ffmpeg -i http://example.com/tv1.ts -c copy -f flv rtmp://localhost/$app/$name name=mystream;
    exec_pull ffmpeg -i http://another.example.com/video_plus.ts -c copy -f flv rtmp://localhost/$app/$name name=anotherstream;
}
```

#### exec
Syntax: `exec command arg*`  
Context: rtmp, server, application

`exec` is an alias of `exec_push`

#### exec_options
Syntax: `exec_options on|off`  
Context: rtmp, server, application

The directive toggles exec options mode. When activated you can
add exec-family directive options. The only exec option supported is `name`.
This option makes it possible to apply exec only to specified stream.
Default if off.
```sh
exec_options on;
# call on_publish only for "mystream"
exec_publish http://localhost/on_publish name=mystream;

# call on_play only for "another"
exec_play http://localhost/on_play name=another;

# execute different ffmpeg's for different streams
exec_pull http://example.com/abc.ts -c copy -f flv rtmp://localhost/$name/$app name=mystream;
exec_pull http://my.example.com/tele.ts -c copy -f flv rtmp://localhost/$name/$app name=tv;
exec_pull http://enother.example.com/hello/f.ts -c copy -f flv rtmp://localhost/$name/$app name=fun;
```

#### exec_static
Syntax: `exec_static command arg*`  
Context: rtmp, server, application

Similar to `exec` but runs specified command at nginx start.
Does not support substitutions since has no session context.
```sh
exec_static ffmpeg -i http://example.com/video.ts -c copy -f flv rtmp://localhost/myapp/mystream;
```

#### exec_kill_signal
Syntax: `exec_kill_signal signal`  
Context: rtmp, server, application  

Sets process termination signal. Default is kill (SIGKILL).
You can specify numeric or symbolic name (for POSIX.1-1990 signals).
```sh
exec_kill_signal term;
exec_kill_signal usr1;
exec_kill_signal 3;
```

#### respawn
Syntax: `respawn on|off`  
Context: rtmp, server, application  

If turned on respawns child process when it's terminated while publishing
is still on. Default is on;
```sh
respawn off;
```

#### respawn_timeout
Syntax: `respawn_timeout timeout`  
Context: rtmp, server, application  

Sets respawn timeout to wait before starting new child instance.
Default is 5 seconds.
```sh
respawn_timeout 10s;
```

#### exec_publish
Syntax: `exec_publish command arg*`  
Context: rtmp, server, application

Specifies external command with arguments to be executed on
publish event. Return code is not analyzed. Substitutions of `exec`
are supported here as well. In addition `args` variable is supported
holding query string arguments.

#### exec_play
Syntax: `exec_play command arg*`  
Context: rtmp, server, application

Specifies external command with arguments to be executed on
play event. Return code is not analyzed. Substitution list
is the same as for `exec_publish`.

#### exec_play_done
Syntax: `exec_play_done command arg*`  
Context: rtmp, server, application

Specifies external command with arguments to be executed on
play_done event. Return code is not analyzed. Substitution list
is the same as for `exec_publish`.

#### exec_publish_done
Syntax: `exec_publish_done command arg*`  
Context: rtmp, server, application

Specifies external command with arguments to be executed on
publish_done event. Return code is not analyzed. Substitution list
is the same as for `exec_publish`.

#### exec_record_started
Syntax: `exec_record_started command arg*`  
Context: rtmp, server, application, recorder

Specifies external command with arguments to be executed when
recording is started.
* `recorder` - recorder name
* `path` - recorded file path (`/tmp/rec/mystream-1389499351.flv`)
* `filename` - path with directory omitted (`mystream-1389499351.flv`)
* `basename` - file name with extension omitted (`mystream-1389499351`)
* `dirname` - directory path (`/tmp/rec`)

#### exec_record_done
Syntax: `exec_record_done command arg*`  
Context: rtmp, server, application, recorder

Specifies external command with arguments to be executed when
recording is finished. Substitution of `exec_publish` are supported here
as well as additional variables 
* `recorder` - recorder name
* `path` - recorded file path (`/tmp/rec/mystream-1389499351.flv`)
* `filename` - path with directory omitted (`mystream-1389499351.flv`)
* `basename` - file name with extension omitted (`mystream-1389499351`)
* `dirname` - directory path (`/tmp/rec`)

Examples
```sh
# track client info
exec_play bash -c "echo $addr $pageurl >> /tmp/clients";
exec_publish bash -c "echo $addr $flashver >> /tmp/publishers";

# convert recorded file to mp4 format
exec_record_done ffmpeg -y -i $path -acodec libmp3lame -ar 44100 -ac 1 -vcodec libx264 $dirname/$basename.mp4;
```

## Live

#### live
Syntax: `live on|off`  
Context: rtmp, server, application  

Toggles live mode i.e. one-to-many broadcasting.
```sh
live on;
```

#### meta
Syntax: `meta on|copy|off`  
Context: rtmp, server, application  

Sets metadata sending mode. The value of `on` makes subscribers
receive reconstructed metadata packets containing predefined fields like
width, height etc. The value of `copy` makes clients receive exact copy of
publisher metadata block including both standard and specific fields. The
value of `off` turns off sending any RTMP metadata to subscribers.
Defaults to on.
```sh
meta copy;
```

#### interleave
Syntax: `interleave on|off`  
Context: rtmp, server, application  

Toggles interleave mode. In this mode audio and video
data is transmitted on the same RTMP chunk stream.
Defaults to off.
```sh
interleave on;
```

#### wait_key
Syntax: `wait_key on|off`  
Context: rtmp, server, application  

Makes video stream start with a key frame. Defaults to off.
```sh
wait_key on;
```

#### wait_video
Syntax: `wait_video on|off`  
Context: rtmp, server, application  

Disable audio until first video frame is sent. Defaults to off.
Can be combined with `wait_key` to make client receive video
key frame with all other data following it. However this usually
increases connection delay. You can tune keyframe interval in your
encoder to reduce the delay.

Recent versions of IE need this option to be enabled for normal playback.
```sh
wait_video on;
```

#### publish_notify
Syntax: `publish_notify on|off`  
Context: rtmp, server, application  

Send `NetStream.Play.PublishNotify` and `NetStream.Play.UnpublishNotify` to
subscribers. Defaults to off.
```sh
publish_notify on;
```

#### drop_idle_publisher
Syntax: `drop_idle_publisher timeout`  
Context: rtmp, server, application  

Drop publisher connection which has been idle (no audio/video data)
within specified time. Default is off. Note this only works when
connection is in publish mode (after sending `publish` command).
```sh
drop_idle_publisher 10s;
```

#### sync
Syntax: `sync timeout`  
Context: rtmp, server, application  

Synchronize audio and video streams. If subscriber bandwidth
is not enough to receive data at publisher rate, some frames are
dropped by server. This leads to synchronization problem. When 
timestamp difference exceeds the value specified as `sync` argument an 
absolute frame is sent fixing that. Default is 300ms.
```sh
sync 10ms;
```

#### play_restart
Syntax: `play_restart on|off`  
Context: rtmp, server, application  

If enabled nginx-rtmp sends NetStream.Play.Start and NetStream.Play.Stop
to each subscriber every time publisher starts or stops publishing. If disabled
each subscriber receives those notifications only at the start and end of
playback. Default is off.
```sh
play_restart off;
```

#### idle_streams
Syntax: `idle_streams on|off`  
Context: rtmp, server, application  

If disabled nginx-rtmp prevents subscribers from connecting to idle/nonexistent
live streams and disconnects all subscribers when stream publisher disconnects.
Default is on.
```sh
idle_streams off;
```

## Record

#### record
syntax: `record [off|all|audio|video|keyframes|manual]*`  
context: rtmp, server, application, recorder  

Toggles record mode. Stream can be recorded in flv file. This directive
specifies what exactly should be recorded:
* off - no recording at all
* all - audio & video (everything)
* audio - audio
* video - video
* keyframes - only key video frames
* manual - never start recorder automatically, use control interface to start/stop

There can be any compatible combination of keys in a single record directive.
```sh
record all;

record audio keyframes;
```

#### record_path
syntax: `record_path path`  
context: rtmp, server, application, recorder  

Specifies record path to put recorded flv files to.
```sh
record_path /tmp/rec;
```

#### record_suffix
syntax: `record_suffix value`  
context: rtmp, server, application, recorder  

Sets record file suffix. Defaults to '.flv'.
```sh
record_suffix _recorded.flv;
```

Record suffix can be a pattern in `strftime` format.
The following directive
```sh
record_suffix -%d-%b-%y-%T.flv;
```

will produce files of the form `mystream-24-Apr-13-18:23:38.flv`.
All supported `strftime` format options can be found on 
[strftime man page](http://pubs.opengroup.org/onlinepubs/009695399/functions/strftime.html).

#### record_unique
syntax: `record_unique on|off`  
context: rtmp, server, application, recorder  

If turned on appends current timestamp to recorded files. Otherwise the same file
is re-written each time new recording takes place. Default is off.
```sh
record_unique on;
```

#### record_append
syntax: `record_append on|off`  
context: rtmp, server, application, recorder  

Toggles file append mode. When turned on recorder appends new data to the old file
or creates it when it's missing. There's no time gap between the old data and the new
data in file. Default is off.
```sh
record_append on;
```

#### record_lock
syntax: `record_lock on|off`  
context: rtmp, server, application, recorder  

When turned on currently recorded file gets locked with `fcntl` call.
That can be checked from elsewhere to find out which file is being recorded.
Default is off.
```sh
record_lock on;
```

On FreeBSD you can use `flock` tool to check that. On Linux `flock` and `fcntl`
are unrelated so you are left with writing a simple script checking file lock status.
Here's an example of such script `isunlocked.py`.
```sh
#!/usr/bin/python

import fcntl, sys

sys.stderr.close()
fcntl.lockf(open(sys.argv[1], "a"), fcntl.LOCK_EX|fcntl.LOCK_NB)
```

#### record_max_size
syntax: `record_max_size size`  
context: rtmp, server, application, recorder  

Set maximum recorded file size.
```sh
record_max_size 128K;
```

#### record_max_frames
syntax: `record_max_frames nframes`  
context: rtmp, server, application, recorder  

Sets maximum number of video frames per recorded file.
```sh
record_max_frames 2;
```

#### record_interval
syntax: `record_interval time`  
context: rtmp, server, application, recorder
  
Restart recording after this number of (milli)seconds.
Off by default. Zero means no delay between recordings. If
record_unique is off then all record fragments are written to the
same file. Otherwise timestamp is appended which makes files
differ (given record_interval is longer than 1 second).
```sh
record_interval 1s;

record_interval 15m;
```

#### recorder
syntax: `recorder name {...}`  
context: application
  
Create recorder block. Multiple recorders can be created withing
single application. All the above mentioned recording-related 
directives can be specified in `recorder{}` block. All settings
are inherited from higher levels.
```sh
application {
    live on;

    # default recorder
    record all;
    record_path /var/rec;

    recorder audio {
        record audio;
        record_suffix .audio.flv;
    }

    recorder chunked {
        record all;
        record_interval 15s;
        record_path /var/rec/chunked;
    }
}
```

#### record_notify
syntax: `record_notify on|off`  
context: rtmp, server, application, recorder  
  
Toggles sending NetStream.Record.Start and NetStream.Record.Stop
status messages (onStatus) to publisher when specific recorder 
starts or stops recording file. Status description field holds
recorder name (empty for default recorder). Off by default.
```sh
recorder myrec {
    record all manual;
    record_path /var/rec;
    record_notify on;
}
```

## Video on demand

#### play
Syntax: `play dir|http://loc [dir|http://loc]*`  
Context: rtmp, server, application  

Play flv or mp4 file from specified directory or HTTP location.
If the argument is prefixed with `http://` then it is assumed
that file should be downloaded from remote http location before
playing. Note playing is not started until the whole file is
downloaded. You can use local nginx to cache files on local machine.

Multiple play locations can be specified in a single `play` directive.
When multiple `play` directives are specified the location lists
are merged and inherited from higher scopes. An attempt to play
each location is made until a successful location is found.
If such location is not found error status is sent to client.

Indexed FLVs are played with random seek capability.
Unindexed FLVs are played with seek/pause disabled
(restart-only mode). Use FLV indexer (for example, yamdi)
for indexing.

If you play FLVs recorded with the `record` directive please do not
forget to index them before playing. They are created unindexed.

Mp4 files can only be played if both video and audio codec are supported
by RTMP. The most common case is H264/AAC.
```sh
application vod {
    play /var/flvs;
}

application vod_http {
    play http://myserver.com/vod;
}

application vod_mirror {
    # try local location first, then access remote location
    play /var/local_mirror http://myserver.com/vod;
}
```

Playing /var/flvs/dir/file.flv:
```sh
ffplay rtmp://localhost/vod//dir/file.flv
```

The two slashes after `vod` make ffplay use `vod` and application name
and the rest of the url as playpath.

#### play_temp_path
Syntax: `play_temp_path dir`  
Context: rtmp, server, application  

Sets location where remote VOD files are stored before playing.
Default is `/tmp`;
```sh
play_temp_path /www;
play http://example.com/videos;
```

#### play_local_path
Syntax: `play_local_path dir`  
Context: rtmp, server, application  

Sets location where remote VOD files copied from `play_temp_path`
directory after they are completely downloaded. Empty value
disables the feature. By default it's empty. The feature can be used
for caching remote files locally. 

This path should be on the same device as `play_temp_path`.
```sh
# search file in /tmp/videos.
# if not found play from remote location
# and store in /tmp/videos

play_local_path /tmp/videos;
play /tmp/videos http://example.com/videos;
```

## Relay

#### pull
Syntax: `pull url [key=value]*`  
Context: application  

Creates pull relay. Stream is pulled from remote machine
and becomes available locally. It only happens when at least
one player is playing the stream locally.

Url syntax: `[rtmp://]host[:port][/app[/playpath]]`. If application
is missing then local application name is used. If playpath is missing
then current stream name is used instead.

The following parameters are supported:
* app - explicit application name
* name - local stream name to bind relay to; if empty or non-specified then
all local streams within application are pulled
* tcUrl - auto-constructed if empty
* pageUrl - page url to pretend
* swfUrl - swf url to pretend
* flashVer - flash version to pretend, default is 'LNX.11,1,102,55'
* playPath - remote play path
* live - toggles special behavior for live streaming, values: 0,1
* start - start time in seconds
* stop - stop time in seconds
* static - makes pull static, such pull is created at nginx start

If a value for a parameter contains spaces then you should use quotes around
the **WHOLE** key=value pair like this : `'pageUrl=FAKE PAGE URL'`.
```sh
pull rtmp://cdn.example.com/main/ch?id=12563 name=channel_a;

pull rtmp://cdn2.example.com/another/a?b=1&c=d pageUrl=http://www.example.com/video.html swfUrl=http://www.example.com/player.swf live=1;

pull rtmp://cdn.example.com/main/ch?id=12563 name=channel_a static;
```

#### push
Syntax: `push url [key=value]*`  
Context: application  

Push has the same syntax as pull. Unlike pull push directive publishes stream to remote server.

#### push_reconnect
Syntax: `push_reconnect time`  
Context: rtmp, server, application  

Timeout to wait before reconnecting pushed connection after disconnect. Default is 3 seconds.
```sh
push_reconnect 1s;
```

#### session_relay
Syntax: `session_relay on|off`  
Context: rtmp, server, application  

Toggles session relay mode. In this mode relay is destroyed when connection is closed.
When the setting is off relay is destroyed when stream is closed so that another relay
could possibly be created later. Default is off.
```sh
session_relay on;
```

## Notify

#### on_connect
Syntax: `on_connect url`  
Context: rtmp, server  

Sets HTTP connection callback. When clients issues connect command
an HTTP request is issued asynchronously and command processing is
suspended until it returns result code. If HTTP 2xx code is returned
then RTMP session continues. The code of 3xx makes RTMP redirect
to another application whose name is taken from `Location` HTTP
response header. Otherwise connection is dropped.

Note this directive is not allowed in application scope since
application is still unknown at connection stage.

HTTP request receives a number of arguments. POST method is used with
application/x-www-form-urlencoded MIME type. The following arguments are
passed to caller:
* call=connect
* addr - client IP address
* app - application name
* flashVer - client flash version
* swfUrl - client swf url
* tcUrl - tcUrl
* pageUrl - client page url

In addition to the above mentioned items all arguments passed explicitly to 
connect command are also sent with the callback. You should distinguish
connect arguments from play/publish arguments. Players usually have a special
way of setting connection string separate from play/publish stream name.
As an example here's how these arguments are set in JWPlayer
```sh
...
streamer: "rtmp://localhost/myapp?connarg1=a&connarg2=b",
file: "mystream?strarg1=c&strarg2=d",
...
```

Ffplay (with librtmp) example
```sh
ffplay "rtmp://localhost app=myapp?connarg1=a&connarg2=b playpath=mystream?strarg1=c&strarg2=d"
```

Usage example
```sh
on_connect http://example.com/my_auth;
```

Redirect example
```sh
location /on_connect {
    if ($arg_flashver != "my_secret_flashver") {
        rewrite ^.*$ fallback? permanent;
    }
    return 200;
}
```

#### on_play
Syntax: `on_play url`  
Context: rtmp, server, application  

Sets HTTP play callback. Each time a clients issues play command
an HTTP request is issued asynchronously and command processing is
suspended until it returns result code. HTTP result code is then 
analyzed.

* HTTP 2xx code continues RTMP session
* HTTP 3xx redirects RTMP to another stream whose name is taken from 
`Location` HTTP response header. If new stream name is started with `rtmp://`
then remote relay is created instead. Relays require that IP address is
specified instead of domain name and only work with nginx versions
greater than 1.3.10. See also `notify_relay_redirect`.
* Otherwise RTMP connection is dropped

Redirect example
```sh
http {
    ...
    location /local_redirect {
        rewrite ^.*$ newname? permanent;
    }
    location /remote_redirect {
        # no domain name here, only ip
        rewrite ^.*$ rtmp://192.168.1.123/someapp/somename? permanent;
    }
    ...
}
rtmp {
    ...
    application myapp1 {
        live on;
        # stream will be redirected to 'newname'
        on_play http://localhost:8080/local_redirect;
    }
    application myapp2 {
        live on;
        # stream will be pulled from remote location
        # requires nginx >= 1.3.10
        on_play http://localhost:8080/remote_redirect;
    }
    ...
}
```

HTTP request receives a number of arguments. POST method is used with
application/x-www-form-urlencoded MIME type. The following arguments are
passed to caller:
* call=play
* addr - client IP address
* clientid - nginx client id (displayed in log and stat)
* app - application name
* flashVer - client flash version
* swfUrl - client swf url
* tcUrl - tcUrl
* pageUrl - client page url
* name - stream name

In addition to the above mentioned items all arguments passed explicitly to 
play command are also sent with the callback. For example if stream is
accessed with the url `rtmp://localhost/app/movie?a=100&b=face&foo=bar` then
`a`, `b` & `foo` are also sent with callback.
```sh
on_play http://example.com/my_callback;
```

#### on_playlist
- syntax: `on_playlist url`
- default: empty
- context: rtmp, server, application 

Set on_playlist callback. In addition to common HTTP callback
variables it receives the following values:
* path - recorded playlist-file path

Triggered on every update of playlist written by HLS/DASH handler.

Example
```sh
on_playlist http://example.com/on-playlist;
```

#### on_publish
Syntax: `on_publish url`  
Context: rtmp, server, application  

The same as on_play above with the only difference that this directive sets
callback on publish command. Instead of remote pull push is performed in
this case.

#### on_done
Syntax: `on_done url`  
Context: rtmp, server, application  

Sets play/publish terminate callback. All the above applies here. However
HTTP status code is not checked for this callback.

#### on_play_done
Syntax: `on_play_done url`  
Context: rtmp, server, application  

Same behavior as `on_done` but only for play end event.

#### on_publish_done
Syntax: `on_publish_done url`  
Context: rtmp, server, application  

Same behavior as `on_done` but only for publish end event.

#### on_record_started
syntax: `on_record_started url`  
context: rtmp, server, application, recorder  
  
Set record_started callback. In addition to common HTTP callback
variables it receives the following values
* recorder - recorder name in config or empty string for inline recorder
* path - recording file path


#### on_record_done
syntax: `on_record_done url`  
context: rtmp, server, application, recorder  
  
Set record_done callback. In addition to common HTTP callback
variables it receives the following values
* recorder - recorder name in config or empty string for inline recorder
* path - recorded file path

Example
```sh
on_record_done http://example.com/recorded;
```

#### on_update
syntax: `on_update url`  
context: rtmp, server, application  
  
Set update callback. This callback is called with period of 
`notify_update_timeout`. If a request returns HTTP result other
than 2xx connection is terminated. This can be used to synchronize
expired sessions. Two additional arguments `time` and `timestamp` 
are passed to this handler:
* `time` is the number of seconds since play/publish call
* `timestamp` is RTMP timestamp of the last audio/video packet sent to the client

You can use `timestamp` argument to individually limit playback duration 
for each user.
```sh
on_update http://example.com/update;
```

#### notify_update_timeout
syntax: `notify_update_timeout timeout`  
context: rtmp, server, application  

Sets timeout between `on_update` callbacks. Default is 30 seconds.
```sh
notify_update_timeout 10s;
on_update http://example.com/update;
```

#### notify_update_strict
syntax: `notify_update_strict on|off`  
context: rtmp, server, application  

Toggles strict mode for `on_update` callbacks. Default is off.
When turned on all connection errors, timeouts as well as HTTP parse
errors and empty responses are treated as update failures and lead
to connection termination. When off only valid HTTP response codes
other that 2xx lead to failure.
```sh
notify_update_strict on;
on_update http://example.com/update;
```

#### notify_relay_redirect
syntax: `notify_relay_redirect on|off`  
context: rtmp, server, application  

Enables local stream redirect for `on_play` and `on_publish` remote
redirects. New stream name is MD5 hash of RTMP URL used for remote redirect.
Default is off.
```sh
notify_relay_redirect on;
```

#### notify_send_redirect
- syntax: `notify_send_redirect on|off`
- default: off
- context: rtmp, server, application  

Enables remote stream redirect by `on_publish` return value.
New stream name is get through HTTP redirect - `on_publish`
must return 30x code and Location header with new streaming URL.
Default is off.
```sh
notify_send_redirect on;
```

#### notify_method
syntax: `notify_method get|post`  
context: rtmp, server, application, recorder  

Sets HTTP method for notifications. Default is POST with 
`application/x-www-form-urlencoded` content type. In certain cases
GET is preferable, for example if you plan to handle the call
in `http{}` section of nginx. In this case you can use `arg_*` variables
to access arguments.
```sh
notify_method get;
```

With GET method handling notifications in `http{}` section can be done this way
```sh
location /on_play {
    if ($arg_pageUrl ~* localhost) {
        return 200;
    }
    return 500;
}
```

## HLS

#### hls
Syntax: `hls on|off`  
Context: rtmp, server, application  

Toggles HLS on the application.
```sh
hls on;
hls_path /tmp/hls;
hls_fragment 15s;
```

In `http{}` section set up the following location for clients to play HLS.
```sh
http {
    ...
    server {
        ...
        location /hls {
            types {
                application/vnd.apple.mpegurl m3u8;
            }
            root /tmp;
            add_header Cache-Control no-cache;

            # To avoid issues with cross-domain HTTP requests (e.g. during development)
            add_header Access-Control-Allow-Origin *;
        }
    }
}
```

#### hls_path
Syntax: `hls_path path`  
Context: rtmp, server, application  

Sets HLS playlist and fragment directory. If the directory does not
exist it will be created.

#### hls_fragment
Syntax: `hls_fragment time`  
Context: rtmp, server, application  

Sets HLS fragment length. Defaults to 5 seconds.

#### hls_playlist_length
Syntax: `hls_playlist_length time`  
Context: rtmp, server, application  

Sets HLS playlist length. Defaults to 30 seconds.
```sh
hls_playlist_length 10m;
```

#### hls_sync
Syntax: `hls_sync time`  
Context: rtmp, server, application  

Sets HLS timestamp synchronization threshold. Default is 2ms.
This feature prevents crackling noises after conversion
from low-resolution RTMP (1KHz) to high-resolution MPEG-TS (90KHz).
```sh
hls_sync 100ms;
```

#### hls_continuous
Syntax: `hls_continuous on|off`  
Context: rtmp, server, application  

Toggles HLS continuous mode. In this mode HLS sequence number
is started from where it stopped last time. Old fragments are
keeped. Default is off.
```sh
hls_continuous on;
```

#### hls_nested
Syntax: `hls_nested on|off`  
Context: rtmp, server, application  

Toggles HLS nested mode. In this mode a subdirectory
of `hls_path` is created for each stream. Playlist
and fragments are created in that subdirectory.
Default is off.
```sh
hls_nested on;
```

#### hls_base_url
Syntax: `hls_base_url url`  
Context: rtmp, server, application  

Sets base url for HLS playlist items. When empty those
items have no prefix and assumed to be at the same location
as parent playlist or one level lower when `hls_nested` is
used. This feature applies both to master (variant) and slave 
HLS playlists. It can let you download the playlist and play it
locally since it contains full references to child playlists or
fragments. Empty by default.
```sh
hls_base_url http://myserver.com/hls/;
```

#### hls_cleanup
Syntax: `hls_cleanup on|off`  
Context: rtmp, server, application  

Toggles HLS cleanup. By default the feature is on.
In this mode nginx cache manager process removes old
HLS fragments and playlists from HLS directory.
```sh
hls_cleanup off;
```

#### hls_fragment_naming
Syntax: `hls_fragment_naming sequential|timestamp|system`  
Context: rtmp, server, application  

Sets fragment naming mode.
* sequential - use increasing integers
* timestamp - use stream timestamp
* system - use system time

Default is sequential.
```sh
hls_fragment_naming system;
```

### hls_fragment_naming_granularity
Syntax: `hls_fragment_naming_granularity number`  
Context: rtmp, server, application  

Sets granularity for hls fragment ids. If above zero, changes ids
to divide the provided value. Default is zero.
```sh
# use system time rounded to 500ms as fragment names
hls_fragment_naming system;
hls_fragment_naming_granularity 500;
```

#### hls_fragment_slicing
Syntax: `hls_fragment_slicing plain|aligned`  
Context: rtmp, server, application  

Sets fragment slicing mode.
* plain - switch fragment when target duration is reached
* aligned - switch fragment when incoming timestamp is a multiple of fragment duration. This mode makes it possible to generate identical fragments on different nginx instances

Default is plain.
```sh
hls_fragment_slicing aligned;
```

#### hls_variant
Syntax: `hls_variant suffix [param*]`  
Context: rtmp, server, application  

Adds HLS variant entry. When suffix is matched on stream name
then variant playlist is created for the current stream with all
entries specified by `hls_variant` directives in current application.
Stripped name without suffix is used as variant stream name. The original
stream is processed as usual.

Optional parameters following the suffix are appended to `EXT-X-STREAM-INF` in
m3u8 playlist. See HLS spec. 3.3.10. EXT-X-STREAM-INF for the full list of supported
parameters.
```sh
rtmp {
    server {
        listen 1935;
    
        application src {
            live on;

            exec ffmpeg -i rtmp://localhost/src/$name
              -c:a libfdk_aac -b:a 32k  -c:v libx264 -b:v 128K -f flv rtmp://localhost/hls/$name_low
              -c:a libfdk_aac -b:a 64k  -c:v libx264 -b:v 256k -f flv rtmp://localhost/hls/$name_mid
              -c:a libfdk_aac -b:a 128k -c:v libx264 -b:v 512K -f flv rtmp://localhost/hls/$name_hi;
        }
        
        application hls {
            live on;

            hls on;
            hls_path /tmp/hls;
            hls_nested on;

            hls_variant _low BANDWIDTH=160000;
            hls_variant _mid BANDWIDTH=320000;
            hls_variant _hi  BANDWIDTH=640000;
        }
    }
}
```

#### hls_type
Syntax: `hls_type live|event`  
Context: rtmp, server, application  

Sets HLS playlist type specified in `X-PLAYLIST-TYPE` playlist directive.
Live HLS stream is usually played from the current live position which is 
several fragments to the end of playlist. Event HLS stream is always played 
from the start of playlist. When in `event` mode make sure playlist length 
is enough for the whole event. Default is `live`;
```sh
hls_type event;
```

#### hls_allow_client_cache
Syntax: `hls_allow_client_cache enabled|disabled`  
Context: rtmp, server, application  

Enables (or disables) client cache with `#EXT-X-ALLOW-CACHE` playlist
directive.  Setting value to enabled allows supported clients to
cache segments in a live DVR manner.  Setting value to disabled explicitly 
tells supported clients to never cache segments.
Unset by default (playlist directive will be absent).
```sh
hls_allow_client_cache enabled;
```

#### hls_keys
Syntax: `hls_keys on|off`  
Context: rtmp, server, application  

Enables HLS encryption. AES-128 method is used to encrypt the whole HLS fragments.
Off by default.
```sh
hls_keys on;
```

Here's the example configuration using the HLS encryption. This configuration
requires that nginx is built with `--with-http_ssl_module` for https support.
```sh
...

http {
    ...
    server {
        listen 443 ssl;
        server_name example.com;

        ssl_certificate /var/ssl/example.com.cert;
        ssl_certificate_key /var/ssl/example.com.key;

        location /keys {
            root /tmp;
        }
    }

    server {
        listen 80;
        server_name example.com;

        location /hls {
            root /tmp;
        }
    }
}
rtmp {
    server {
        listen 1935;

        application myapp {
            live on;

            hls on;
            hls_path /tmp/hls;

            hls_keys on;
            hls_key_path /tmp/keys;
            hls_key_url https://example.com/keys/;
            hls_fragments_per_key 10;
        }
    }
}
```

#### hls_key_path
Syntax: `hls_key_path path`  
Context: rtmp, server, application  

Sets the directory where auto-generated HLS keys are saved.
Key files have `.key` extension and pseudo-random 16-byte content
created with the OpenSSL `RAND_bytes()` routine.
If the directory does not exist it's created in runtime.
By default, `hls_path` directory is used for key files.
Remember however you should normally restrict access to key files which
is easier when these files are stored separately from playlist and fragments.
```sh
hls_key_path /tmp/keys;
```

#### hls_key_url
Syntax: `hls_key_url url`  
Context: rtmp, server, application  

Sets url for HLS key file entries. When empty those
items have no prefix and keys are assumed to be at the same location
as the playlist. Empty by default.
```sh
hls_key_url https://myserver.com/keys/;
```

Example playlist entry with the above setting
```sh
#EXT-X-KEY:METHOD=AES-128,URI="https://myserver.com/keys/337.key",IV=0x00000000000000000000000000000151
```

#### hls_fragments_per_key
Syntax: `hls_fragments_per_key value`  
Context: rtmp, server, application  

Sets the number of HLS fragments encrypted with the same key.
Zero means only one key is created at the publish start 
and all fragments within the session are encrypted with this key.
Default is zero.
```sh
hls_fragments_per_key 10;
```

## MPEG-DASH

#### dash
Syntax: `dash on|off`  
Context: rtmp, server, application  

Toggles MPEG-DASH on the application.
```sh
dash on;
dash_path /tmp/dash;
dash_fragment 15s;
```

In `http{}` section set up the following location for clients to play MPEG-DASH.
```sh
http {
    ...
    server {
        ...
        location /dash {
            root /tmp;
            add_header Cache-Control no-cache;

            # To avoid issues with cross-domain HTTP requests (e.g. during development)
            add_header Access-Control-Allow-Origin *;
        }
    }
}
```

#### dash_path
Syntax: `dash_path path`  
Context: rtmp, server, application  

Sets MPEG-DASH playlist and fragment directory. If the directory does not
exists it will be created.

#### dash_fragment
Syntax: `dash_fragment time`  
Context: rtmp, server, application  

Sets MPEG-DASH fragment length. Defaults to 5 seconds.

#### dash_playlist_length
Syntax: `dash_playlist_length time`  
Context: rtmp, server, application  

Sets MPEG-DASH playlist length. Defaults to 30 seconds.
```sh
dash_playlist_length 10m;
```

#### dash_nested
Syntax: `dash_nested on|off`  
Context: rtmp, server, application  

Toggles MPEG-DASH nested mode. In this mode a subdirectory
of `dash_path` is created for each stream. Playlist
and fragments are created in that subdirectory.
Default is off.
```sh
dash_nested on;
```

#### dash_cleanup
Syntax: `dash_cleanup on|off`  
Context: rtmp, server, application  

Toggles MPEG-DASH cleanup. By default the feature is on.
In this mode nginx cache manager process removes old
MPEG-DASH fragments and manifests from MPEG-DASH directory.
Init fragments are deleted after stream manifest is deleted.
```sh
dash_cleanup off;
```

#### dash\_clock_compensation
Syntax: `dash_clock_compensation off|ntp|http_head|http_iso`  
Context: rtmp, server, application  
Default: off

Toggles MPEG-DASH clock compentation element output into MPD.
In this mode nginx provides `UTCTiming` element for MPEG-DASH manifest.
Clock compensation provided by DASH-client if possible.
- ntp - use NTP protocol
- http_head - client must fetch header `Date` from URI (`dash_clock_helper_uri`)
- http_iso - client must fetch date in ISO format from URI (`dash_clock_helper_uri`)

Standard section: 4.7.2. Service Provider Requirements and Guidelines

```sh
dash\_clock_compensation off;
```

#### dash\_clock_helper_uri
Syntax: `dash_clock_helper_uri URI`  
Context: rtmp, server, application  
Default: none

URI helper resource for clock compensation for client.
Clock compensation type:
- ntp - address of NTP-server
- http\_head - full HTTP uri
- http\_iso - full HTTP uri

Standard section: 4.7.2. Service Provider Requirements and Guidelines

```sh
dash\_clock\_helper_uri http://rtmp-server/static/time.txt;

_or_

dash\_clock\_helper_uri http://rtmp-server/lua/time-iso;
```

## Access log

#### access_log
Syntax: `access_log off|path [format_name]`  
Context: rtmp, server, application  

Sets access log parameters. Logging is turned on by default.
To turn it off use `access_log off` directive. By default access logging
is done to the same file as HTTP access logger (`logs/access.log`).
You can specify another log file path in `access_log` directive.
Second argument is optional. It can be used to specify logging format by name.
See `log_format` directive for more details about formats.
```sh
log_format new '$remote_addr';
access_log logs/rtmp_access.log new;
access_log logs/rtmp_access.log;
access_log off;
```

#### log_format
Syntax: `log_format format_name format`  
Context: rtmp  

Creates named log format. Log formats look very much the same as nginx HTTP log
formats. Several variables are supported within log format:
* `connection` - connection number
* `remote_addr` - client address
* `app` - application name
* `name` - last stream name
* `args` - last stream play/publish arguments
* `flashver` - client flashVer
* `swfurl` - client swfUrl
* `tcurl` - client tcUrl
* `pageurl` - client pageUrl
* `command` - play/publish commands sent by client: `NONE`, `PLAY`, `PUBLISH`, `PLAY+PUBLISH`
* `bytes_sent` - number of bytes sent to client
* `bytes_received` - number of bytes received from client
* `time_local` - local time at the end of client connection
* `session_time` - connection duration in seconds
* `session_readable_time` - connection duration in human-readable format
* `msec` - current unix timestamp in SEC.MSEC format

Default log format has the name `combined`. Here's the definition of this format
```sh
$remote_addr [$time_local] $command "$app" "$name" "$args" - 
$bytes_received $bytes_sent "$pageurl" "$flashver" ($session_readable_time)
```

## Limits

#### max_connections
Syntax: `max_connections number`  
Context: rtmp, server, application  

Sets maximum number of connections for rtmp engine. Off by default.
```sh
max_connections 100;
```

## Statistics

Statistics module is NGINX HTTP module unlike all other modules listed
here. Hence statistics directives should be located within http{} block.

#### rtmp_stat
Syntax: `rtmp_stat all`  
Context: http, server, location  

Sets RTMP statistics handler to the current HTTP location. RTMP statistics is
dynamic XML document. To watch this document in browser as XHTML page
use rtmp_stat_stylesheet directive.
```sh
http {
    server {
        location /stat {
            rtmp_stat all;
            rtmp_stat_stylesheet stat.xsl;
        }
        location /stat.xsl {
            root /path/to/stat/xsl/file;
        }
    }
}
```

#### rtmp_stat_stylesheet
Syntax: `rtmp_stat_stylesheet path`  
Context: http, server, location  

Adds XML stylesheet reference to statistics XML to make it viewable
in browser. See rtmp_stat description and example for more information.

## Multi-worker live streaming

Multi-worker live streaming is implemented through pushing stream
to remaining nginx workers.

#### rtmp_auto_push
Syntax: `rtmp_auto_push on|off`  
Context: root  

Toggles auto-push (multi-worker live streaming) mode.
Default is off.

#### rtmp_auto_push_reconnect
Syntax: `rtmp_auto_push_reconnect timeout`  
Context: root  

Sets auto-push reconnect timeout when worker is killed.
Default is 100 milliseconds.

#### rtmp_socket_dir
Syntax: `rtmp_socket_dir dir`  
Context: root  

Sets directory for UNIX domains sockets used for stream pushing.
Default is `/tmp`.
```sh
rtmp_auto_push on;
rtmp_auto_push_reconnect 1s;
rtmp_socket_dir /var/sock;

rtmp {
    server {
        listen 1935;
        application myapp {
            live on;
        }
    }
}
```

## Control

Control module is NGINX HTTP module and should be located within http{} block.

#### rtmp_control
Syntax: `rtmp_control all`  
Context: http, server, location  

Sets RTMP control handler to the current HTTP location. 
```sh
http {
    server {
        location /control {
            rtmp_control all;
        }
    }
}
```

[More details about control module](control_modul.md)