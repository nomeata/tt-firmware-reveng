#ifndef API_H
#define API_H
/*
 * system_api -- the firmware's callback table handed to an embedded GME main binary
 * (the launcher does `(*binary)(&sysapi)`). Layout DERIVED FROM OUR OWN FIRMWARE RE of
 * gme_launch_binary_build_sysapi; NO tt-homebrew dependency. Only the fields the dumper
 * uses are named -- the rest are explicit padding, so offsets are exact and self-evident.
 *
 * 2N launcher @0x080a03f0 (struct base = &local_1b4; slot offset = 0x1b4 - localN):
 *     local_1a8 = DAT_080a0790  -> +0x0c  is_audio_playing
 *     local_1a0 = DAT_080a079c  -> +0x14  open
 *     local_19c = DAT_080a07a0  -> +0x18  read
 *     local_198 = DAT_080a07a8  -> +0x1c  write
 *     local_194 = DAT_080a0798  -> +0x20  close
 *     local_190 = DAT_080a07a4  -> +0x24  seek
 *     local_188 = DAT_080a07b8  -> +0x2c  play_sound(fh,off,len)
 *     local_180 = PTR_g_gb->akoidpara -> +0x34  fpAkOidPara (points at akoidpara BASE)
 *     local_17c = DAT_080a07b0  -> +0x38  p_filehandle_current_gme
 *   Binary loads at 0x08132000 (gme_alloc_binary_region@0x0802fb20 -> DAT_0802fb7c).
 *
 * ZC3201 launcher @0x08095090 (base = auStack_1c8; offset = 0x1c8 - localN): fields 0..11
 * identical to the 2N, then it DIVERGES:
 *     local_18c = *(gb+0x40)+0x22  -> +0x3c  fpAkOidPara (points at akoidpara+0x22)
 *     local_17c = DAT_08095458     -> +0x4c  p_filehandle_current_gme
 *   Binary loads at 0x080ea000 (FUN_08023324 -> DAT_08023370). See zc3201_sysapi_notes.md.
 */

#if !defined(build_for_2N) && !defined(build_for_ZC3201)
#error "define -Dbuild_for_2N or -Dbuild_for_ZC3201"
#endif

typedef struct system_api {
    char _pad00[0x0c];
    int  (*is_audio_playing)(void);                                 /* +0x0c */
    char _pad10[0x14 - 0x10];
    int  (*open)(unsigned short *path, unsigned int, unsigned int); /* +0x14 */
    int  (*read)(int fh, void *buf, unsigned int len);              /* +0x18 */
    int  (*write)(int fh, void *buf, unsigned int len);             /* +0x1c */
    int  (*close)(int fh);                                          /* +0x20 */
    int  (*seek)(int fh, unsigned int pos, int always_zero);        /* +0x24 */
    char _pad28[0x2c - 0x28];
    int  (*play_sound)(int fh, unsigned int off, unsigned int len); /* +0x2c */
#ifdef build_for_2N
    char _pad30[0x34 - 0x30];
    unsigned char *fpAkOidPara;              /* +0x34 -> akoidpara base */
    int  *p_filehandle_current_gme;          /* +0x38 */
#define First_time_exec 0xdec                /* akoidpara+0xdec; 2N loader clears 0xdec/0xdee */
#define FWHEAD_BASE     0x08000000           /* 2N Bios base */
#else /* build_for_ZC3201 */
    char _pad30[0x3c - 0x30];
    unsigned char *fpAkOidPara;              /* +0x3c -> akoidpara+0x22 */
    char _pad40[0x4c - 0x40];
    int  *p_filehandle_current_gme;          /* +0x4c */
#define First_time_exec 0xd6a                /* fpAkOidPara[0xd6a] = akoidpara+0xd8c (loader-cleared) */
#define FWHEAD_BASE     0x08000000           /* ZC3201 Bios base */
#endif
} system_api;

extern system_api *api;
void initTT(system_api *apiPara);
void playSoundNow(unsigned int media_index);

#endif /* API_H */
