#include "fscopy.h"
#include <libs/fatfs/ff.h>
#include <utils/btn.h>
#include "../tegraexplorer/tconf.h"
#include "../gfx/gfx.h"
#include <mem/heap.h>
#include <string.h>
#include "../gfx/gfxutils.h"
#include "fsutils.h"
#include "readers/folderReader.h"
#include <utils/util.h>

ErrCode_t FileCopy(const char *locin, const char *locout, u8 options){
    FIL in, out;
    FILINFO in_info;
    u64 sizeRemaining, toCopy;
    u8 *buff;
    u32 x, y;
    ErrCode_t err = newErrCode(0);
    int res = 0;

    gfx_con_getpos(&x, &y);

    if (!strcmp(locin, locout)){
        return newErrCode(TE_ERR_SAME_LOC);
    }

    if ((res = f_open(&in, locin, FA_READ | FA_OPEN_EXISTING))){
        return newErrCode(res);
    }

    if ((res = f_stat(locin, &in_info))){
        return newErrCode(res);
    }
       
    if ((res = f_open(&out, locout, FA_CREATE_ALWAYS | FA_WRITE))){
        return newErrCode(res);
    }

    if (options & COPY_MODE_PRINT){
        gfx_printf("[  0%%]");
        x += 16;
    }

    buff = malloc(TConf.FSBuffSize);
    sizeRemaining = f_size(&in);
    const u64 totalsize = sizeRemaining;

    while (sizeRemaining > 0){
        toCopy = MIN(sizeRemaining, TConf.FSBuffSize);

        if ((res = f_read(&in, buff, toCopy, NULL))){
            err = newErrCode(res);
            break;
        }
            
        if ((res = f_write(&out, buff, toCopy, NULL))){
            err = newErrCode(res);
            break;
        }

        sizeRemaining -= toCopy;

        if (options & COPY_MODE_PRINT){
            gfx_con_setpos(x, y);
            gfx_printf("%3d%%",  (u32)(((totalsize - sizeRemaining) * 100) / totalsize));
        }

        if (options & COPY_MODE_CANCEL && btn_read() & (BTN_VOL_DOWN | BTN_VOL_UP)){
            f_unlink(locout);
            break;
        }
    }

    f_close(&in);
    f_close(&out);
    free(buff);

    f_chmod(locout, in_info.fattrib, 0x3A);

    if (options & COPY_MODE_PRINT){
        gfx_con_setpos(x - 16, y);
    }
    
    //f_stat(locin, &in_info); //somehow stops fatfs from being weird
    return err;
}

void BoxRestOfScreen(){
    u32 tempX, tempY;
    gfx_con_getpos(&tempX, &tempY);
    gfx_boxGrey(tempX, tempY, YLEFT, tempY + 16, 0x1B);
}

ErrCode_t FolderCopy(const char *locin, const char *locout){
    if (TConf.explorerCopyMode >= CMODE_CopyFolder){
        if (strstr(locout, locin) != NULL)
             return newErrCode(TE_ERR_PATH_IN_PATH);
    }

    if (!strcmp(locin, locout)){
        return newErrCode(TE_ERR_SAME_LOC);
    }

    char *dstPath = CombinePaths(locout, strrchr(locin, '/') + 1);
    int res = 0;
    ErrCode_t ret = newErrCode(0);
    u32 x, y;
    gfx_con_getpos(&x, &y);
    u32 last_update_ms = 0;

    Vector_t fileVec = ReadFolder(locin, &res);
    if (res){
        ret = newErrCode(res);
    }
    else {
        vecDefArray(FSEntry_t *, fs, fileVec);
        f_mkdir(dstPath);

        for (int i = 0; i < fileVec.count && !ret.err; i++){
            char *temp = CombinePaths(locin, fs[i].name);
            if (fs[i].isDir){
                ret = FolderCopy(temp, dstPath);
            }
            else {
                u32 now = get_tmr_ms();
                if (now - last_update_ms >= 256) {
                    last_update_ms = now;
                    gfx_con_setpos(x, y);

                    u32 limit = (YLEFT - x) / 16 - 10;
                    if (limit > 127) limit = 127;
                    char fname_line[128];
                    u32 flen = strlen(fs[i].name);
                    if (flen >= limit) {
                        memcpy(fname_line, fs[i].name, limit - 3);
                        fname_line[limit - 3] = '.'; fname_line[limit - 2] = '.'; fname_line[limit - 1] = '.';
                    } else {
                        memcpy(fname_line, fs[i].name, flen);
                        memset(fname_line + flen, ' ', limit - flen);
                    }
                    fname_line[limit] = 0;
                    gfx_puts(fname_line);
                }

                char *tempDst = CombinePaths(dstPath, fs[i].name);
                u32 limit = (YLEFT - x) / 16 - 10;
                gfx_con_setpos(x + limit * 16, y);
                ret = FileCopy(temp, tempDst, COPY_MODE_PRINT);
                free(tempDst);
                gfx_con_setpos(x, y);
            }
            free(temp);
        }
    }

    FILINFO fno;

    if (!ret.err){
        res = f_stat(locin, &fno);
        if (res)
            ret = newErrCode(res);
        else
            ret = newErrCode(f_chmod(dstPath, fno.fattrib, 0x3A));
    }

    free(dstPath);
    clearFileVector(&fileVec);
    return ret;
}

ErrCode_t FolderDelete(const char *path){
    int res = 0;
    ErrCode_t ret = newErrCode(0);
    u32 x, y;
    gfx_con_getpos(&x, &y);
    u32 last_update_ms = 0;

    Vector_t fileVec = ReadFolder(path, &res);
    if (res){
        ret = newErrCode(res);
    }
    else {
        vecDefArray(FSEntry_t *, fs, fileVec);

        for (int i = 0; i < fileVec.count && !ret.err; i++){
            char *temp = CombinePaths(path, fs[i].name);
            if (fs[i].isDir){
                ret = FolderDelete(temp);
            }
            else {
                u32 now = get_tmr_ms();
                if (now - last_update_ms >= 256) {
                    last_update_ms = now;
                    gfx_con_setpos(x, y);

                    u32 limit = (YLEFT - x) / 16 - 1;
                    if (limit > 127) limit = 127;
                    char fname_line[128];
                    u32 flen = strlen(fs[i].name);
                    if (flen >= limit) {
                        memcpy(fname_line, fs[i].name, limit - 3);
                        fname_line[limit - 3] = '.'; fname_line[limit - 2] = '.'; fname_line[limit - 1] = '.';
                    } else {
                        memcpy(fname_line, fs[i].name, flen);
                        memset(fname_line + flen, ' ', limit - flen);
                    }
                    fname_line[limit] = 0;
                    gfx_puts(fname_line);
                }

                res = f_unlink(temp);
                if (res){
                    ret = newErrCode(res);
                }
            }
            free(temp);
        }
    }

    if (!ret.err){
        res = f_unlink(path);
        if (res)
            ret = newErrCode(res);
    }

    clearFileVector(&fileVec);
    return ret;
}