
## DRM Common Encryption

This fork give the possibility of packaging dash "protected" stream.
Concretely it implement the minimal requirement of "common-encryption" as described in ISO/IEC 23001-7:2015, Information technology — MPEG systems technologies — Part 7: Common encryption in ISO Base Media File Format files - 2nd Edition.
You can read a brief description here : "https://w3c.github.io/encrypted-media/format-registry/stream/mp4.html#bib-CENC"

### How to use it :

You need at least to enable common_encryption and provide one key and one key id with the following directives :

```
dash_cenc on; # enable common encryption on all stream in this block
dash_cenc_kid XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX; # 16 bytes KEY-ID in hex
dash_cenc_key XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX; # 16 bytes KEY in hex
```

It enable automatically Clear-Key pseudo DRM. (use it only for testing purpose)

Currently the are two real DRM supported : Widevine and Microsoft Playready.

For widevine you need the following directives in addition :

```
dash_wdv on; # enable widevine signalling
dash_wdv_data AAAAbHBzc2gAAAAA7e+LqXnWSs6jyCfc1R0h7QAAA... ; # base64 encoded widevine pssh
```

For playready you need the following directives in addition  (you can use both widevine and playready together with the same kid:key pair):

```
dash_mspr on; # enable playready signalling
dash_mspr_data AAACsHBzc2gAAAAAmgTweZhAQoarkuZb4Ih...; # base64 encoded playready pssh
dash_mspr_kid AAATH/7xxxfUbpB8mhqA==; # base64 encoded playready kid
dash_mspr_pro kAIAAAEAAQCGAjwAVwBSAE0ASABFAEEARA...; # base64 encoded playready PRO (Playready Object)
```

### Implementation :

_TLDR;_ This was quite an adventure

The implementation is based on the ISO_IEC_23001-7_2016 normative document.
I also took lot of inspiration on kaltura nginx-vod module.

It implement the minimal requirement of the norm, the 'cenc' scheme, AES-CTR mode full sample and video NAL Subsample encryption.

Audio track are encrypted in full sample mode with AES-CTR.

Video track are encrypted in sub sample mode, assuming one NALU per frame, using enough clear text size at the beginning of the frame to keep the NAL header in clear. (the module does not analyse NAL Headers). 

The clear size is rounded to make encrypted size of data a multiple of the AES-CRT block size.

The implementation allow only one KID:KEY couple used for all tracks.

The implementation use 64bits IVs.

### Conformity :

This implementation have been tested with and known working :

Clearkey :
 - Firefox: dashjs/shakaplayer 
 - Chrome: dashjs/shakaplayer

Widevine :
- Firefox: dashjs/shakaplayer 
- Chrome: dashjs/shakaplayer

Playready:
 - Edge : dashjs/shakaplayer

Bitmovin player seem also to work.

Thanks: 

Special thanks to Eran Kornblau from Kaltura, Joey Parrish, and Jacob Timble.



