#include <utils/util.h>
#include <storage/nx_sd.h>
#include "../fs/readers/folderReader.h"
#include "../fs/fstypes.h"
#include "../fs/fscopy.h"
#include <utils/sprintf.h>
#include <stdlib.h>
#include <string.h>
#include "../gfx/gfx.h"
#include "../gfx/gfxutils.h"
#include "../gfx/menu.h"
#include "../hid/hid.h"
#include "../utils/utils.h"
#include "../fs/fsutils.h"

typedef struct {
    u32 fixed_bits;
    u32 deleted_junk;
    u32 scanned_count;
    u32 start_time;
    u32 aborted;
    u32 output_y;
} TraversalStats;



ErrCode_t _FolderDelete(const char *path)
{
    ErrCode_t ret = newErrCode(0);
    FRESULT res;
    DIR dir;
    FILINFO fno;
    char subpath[512];

    res = f_opendir(&dir, path);
    if (res != FR_OK) return newErrCode(res);

    for (;;) {
        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        if (fno.fname[0] == '.') continue; // skip '.' and '..'

        s_printf(subpath, "%s/%s", path, fno.fname);

        if (fno.fattrib & AM_DIR) {
            ret = _FolderDelete(subpath);
            if (ret.err) break;
        } else {
            res = f_unlink(subpath);
            if (res != FR_OK) {
                ret = newErrCode(res);
                break;
            }
        }
    }
    f_closedir(&dir);

    if (!ret.err) {
        res = f_unlink(path);
        if (res != FR_OK) ret = newErrCode(res);
    }

    return ret;
}

int _StartsWith(const char *a, const char *b)
{
    if (strncmp(a, b, strlen(b)) == 0)
        return 1;
    return 0;
}

int _is_macos_junk(const char *name)
{
    if (name[0] != '.') return 0;

    if (strcmp(name, ".DS_Store") == 0 ||
        strcmp(name, ".Spotlight-V100") == 0 ||
        strcmp(name, ".Trashes") == 0 ||
        strcmp(name, ".Trash") == 0 ||
        strcmp(name, ".apDisk") == 0 ||
        strcmp(name, ".VolumeIcon.icns") == 0 ||
        strcmp(name, ".fseventsd") == 0 ||
        strcmp(name, ".TemporaryItems") == 0 ||
        _StartsWith(name, "._")) 
    {
        return 1;
    }
    return 0;
}

int _traverse_unified(char *path, TraversalStats *stats, u32 check_first_run)
{
    FRESULT res;
    DIR dir;
    u32 dirLength = 0;
    u32 local_scanned = 0;
    FILINFO fno; 

    if (stats->aborted) return FR_OK;

    if (check_first_run)
    {
        res = f_stat(path, &fno);
        if (res == FR_OK) {
             if (fno.fattrib & AM_ARC)
            {
                stats->fixed_bits++;
                f_chmod(path, 0, AM_ARC);
            }
        }
    }

    res = f_opendir(&dir, path);
    if (res != FR_OK)
        return res;

    dirLength = strlen(path);
    for (;;)
    {
        if (stats->aborted) break;

        path[dirLength] = 0; // Clear end

        res = f_readdir(&dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0)
            break;

        if (fno.fname[0] == '.' && (fno.fname[1] == 0 || (fno.fname[1] == '.' && fno.fname[2] == 0)))
            continue; // skip '.' and '..' only

        local_scanned++;
        if (local_scanned > 2000)
            break;

        if (fno.fattrib & AM_DIR) 
        {
            if (strcmp(fno.fname, "roms") == 0 || 
                strcmp(fno.fname, "retroarch") == 0)
                continue;
        }

        memcpy(&path[dirLength], "/", 1);
        memcpy(&path[dirLength + 1], fno.fname, strlen(fno.fname) + 1);

        stats->scanned_count++;
        if ((stats->scanned_count & 511) == 0) {
            u32 current_elapsed = get_tmr_s() - stats->start_time;
            const char spinner[] = {'|', '/', '-', '\\'};
            int spin_idx = (stats->scanned_count >> 9) & 3;

            gfx_con_setpos(0, stats->output_y);
            gfx_printf("[%c] Files: %d | Fixed: %d | Removed: %d | %02d:%02d elapsed   ",
                spinner[spin_idx],
                stats->scanned_count, stats->fixed_bits, stats->deleted_junk,
                current_elapsed / 60, current_elapsed % 60);

            // check timeout
            if (current_elapsed >= 15 * 60) {
                stats->aborted = 1;
                break;
            }
        }

        if (_is_macos_junk(fno.fname))
        {
            if (fno.fattrib & AM_DIR)
            {
                if (!_FolderDelete(path).err) stats->deleted_junk++;
            }
            else
            {
                if (f_unlink(path) == FR_OK) stats->deleted_junk++;
            }
            continue; 
        }

        int should_have_bit = 0;

        if (fno.fattrib & AM_DIR) {
            size_t len = strlen(fno.fname);
            if (len > 4 && strcmp(&fno.fname[len - 4], ".nca") == 0) {
                should_have_bit = 1;
            }
        }

        if (should_have_bit) 
        {
            if (!(fno.fattrib & AM_ARC)) {
                stats->fixed_bits++;
                f_chmod(path, AM_ARC, AM_ARC);
            }
        } 
        else 
        {
            if (fno.fattrib & AM_ARC) {
                stats->fixed_bits++;
                f_chmod(path, 0, AM_ARC);
            }
        }

        if (fno.fattrib & AM_DIR)
        {
            res = _traverse_unified(path, stats, 0);
            if (res != FR_OK)
                break;
        }
    }

    f_closedir(&dir);
    return res;
}

void m_entry_fixAndCleanAll()
{
    char path[512];
    TraversalStats stats = {0};

    if (sd_mount())
    {
        stats.start_time = get_tmr_s();
        strcpy(path, ""); // Root

        gfx_clearscreen();
        gfx_printf("-- Running Maintenance (Fix Archive Bits + Clean Mac Junk)\n");
        gfx_printf("Scanning entire SD card...\n");
        gfx_printf("Skipping roms and retroarch folders.\n\n");

        u32 x, y;
        gfx_con_getpos(&x, &y);
        stats.output_y = y;
        gfx_printf("[-] Files: 0 | Fixed: 0 | Removed: 0 | 00:00 elapsed   \n\n");

        _traverse_unified(path, &stats, 1);

        if (stats.aborted) {
            gfx_printf("\n\n%kOperation aborted! Time limit (15 mins) reached.%k", 0xFF0000FF, 0xFFCCCCCC);
        }

        gfx_printf("\n\n%kDone!\nFixed attributes: %d\nRemoved junk files: %d%k", 
            0xFF96FF00, stats.fixed_bits, stats.deleted_junk, 0xFFCCCCCC);
    }
}

// Takes no arguments. Called independently
void m_entry_deleteBootFlags()
{
    gfx_clearscreen();
    gfx_printf("\n\n-- Disabling automatic sysmodule startup.\n\n");
    
    DIR dir;
    FILINFO fno;
    char targetPath[512];
    
    if (f_opendir(&dir, "sd:/atmosphere/contents") == FR_OK)
    {
        for (;;) {
            if (f_readdir(&dir, &fno) != FR_OK || fno.fname[0] == 0) break;
            if (fno.fname[0] == '.') continue;
            
            if (fno.fattrib & AM_DIR) {
                s_printf(targetPath, "sd:/atmosphere/contents/%s/flags/boot2.flag", fno.fname);
                if (f_unlink(targetPath) == FR_OK) {
                    gfx_printf("Deleted: %s\n", targetPath);
                }
            }
        }
        f_closedir(&dir);
    }
}