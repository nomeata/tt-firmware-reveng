#include "api.h"
/*
 * Minimal SDK for an embedded GME main binary -- OUR OWN, derived from the reverse-engineered
 * firmware (no tt-homebrew sdk.c). Just what the dumper needs: hold the api pointer and play a
 * bundled media file by index.
 */
system_api *api;

void initTT(system_api *apiPara) {
    api = apiPara;
}

/*
 * Play bundled media #media_index. The GME media table (header[0x04]) is N pairs of
 * (u32 file-offset, u32 length); the firmware's play_sound(filehandle, offset, length)
 * (system_api +0x2c) decrypts+decodes that byte range from the current-GME filehandle
 * (system_api p_filehandle_current_gme). Reads header[4] -> table -> (offset,len) -> play.
 * Uses only firmware-verified sysapi slots.
 */
void playSoundNow(unsigned int media_index) {
    int fh = *api->p_filehandle_current_gme;
    unsigned int media_table_off, off, len;

    api->seek(fh, 4, 0);                              /* header[0x04] = media table offset */
    api->read(fh, &media_table_off, 4);
    api->seek(fh, media_table_off + media_index * 8, 0);  /* entry i = 8 bytes: (offset,length) */
    api->read(fh, &off, 4);
    api->read(fh, &len, 4);
    api->play_sound(fh, off, len);
}
