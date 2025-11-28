#include <utils/util.h>
// #include "tools.h"
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
// #include "utils.h"
#include "../utils/utils.h"
#include "../fs/fsutils.h"

#include <sys/types.h>
// #include <dirent.h>
// #include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// Структура для підрахунку результатів
typedef struct {
    u32 fixed_bits;
    u32 deleted_junk;
} TraversalStats;

void _DeleteFileSimple(char *thing)
{
    int res = f_unlink(thing);
    if (res)
        DrawError(newErrCode(res));
    free(thing);
}

void _RenameFileSimple(char *sourcePath, char *destPath)
{
    int res = f_rename(sourcePath, destPath);
    if (res)
    {
        DrawError(newErrCode(res));
    }
}

ErrCode_t _FolderDelete(const char *path)
{
    int res = 0;
    ErrCode_t ret = newErrCode(0);
    u32 x, y;
    gfx_con_getpos(&x, &y);
    Vector_t fileVec = ReadFolder(path, &res);
    if (res)
    {
        ret = newErrCode(res);
    }
    else
    {
        vecDefArray(FSEntry_t *, fs, fileVec);
        for (int i = 0; i < fileVec.count && !ret.err; i++)
        {
            char *temp = CombinePaths(path, fs[i].name);
            if (fs[i].isDir)
            {
                ret = _FolderDelete(temp);
            }
            else
            {
                res = f_unlink(temp);
                if (res)
                {
                    ret = newErrCode(res);
                }
            }
            free(temp);
        }
    }
    if (!ret.err)
    {
        res = f_unlink(path);
        if (res)
            ret = newErrCode(res);
    }
    clearFileVector(&fileVec);
    return ret;
}

int _StartsWith(const char *a, const char *b)
{
    if (strncmp(a, b, strlen(b)) == 0)
        return 1;
    return 0;
}

// Перевірка, чи є файл сміттям macOS
int _is_macos_junk(const char *name)
{
    if (strcmp(name, ".DS_Store") == 0 ||
        strcmp(name, ".Spotlight-V100") == 0 ||
        strcmp(name, ".Trashes") == 0 ||
        strcmp(name, ".Trash") == 0 ||
        strcmp(name, ".apDisk") == 0 ||
        strcmp(name, ".VolumeIcon.icns") == 0 ||
        strcmp(name, ".fseventsd") == 0 ||
        strcmp(name, ".TemporaryItems") == 0 ||
        _StartsWith(name, "._")) // Файли ресурсів macOS (._file)
    {
        return 1;
    }
    return 0;
}

// УНІВЕРСАЛЬНА функція обходу (завжди робить все)
int _traverse_unified(char *path, TraversalStats *stats, u32 hos_folder_mode, u32 check_first_run, u32 output_y)
{
    FRESULT res;
    DIR dir;
    u32 dirLength = 0;
    static FILINFO fno;

    // Перевірка кореневого елемента
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

    // Open directory.
    res = f_opendir(&dir, path);
    if (res != FR_OK)
        return res;

    dirLength = strlen(path);
    for (;;)
    {
        path[dirLength] = 0; // Clear end
        res = f_readdir(&dir, &fno);

        if (res != FR_OK || fno.fname[0] == 0)
            break;
        
        memcpy(&path[dirLength], "/", 1);
        memcpy(&path[dirLength + 1], fno.fname, strlen(fno.fname) + 1);

        // --- ВІЗУАЛІЗАЦІЯ ---
        gfx_con_setpos(0, output_y);
        gfx_printf("Scan: %s                                                  ", path); 
        // -------------------

        // 1. ВИДАЛЕННЯ СМІТТЯ (Безумовне)
        if (_is_macos_junk(fno.fname))
        {
            if (fno.fattrib & AM_DIR)
            {
                if (!_FolderDelete(path).err) {
                    stats->deleted_junk++;
                }
            }
            else
            {
                if (f_unlink(path) == FR_OK) {
                    stats->deleted_junk++;
                }
            }
            continue; // Видалили - йдемо далі
        }

        // 2. ФІКС АТРИБУТІВ (Безумовний)
        if (fno.fattrib & AM_ARC)
        {
            stats->fixed_bits++;
            f_chmod(path, 0, AM_ARC);
        }

        // 3. РЕКУРСІЯ
        if (fno.fattrib & AM_DIR)
        {
            // Спеціальна логіка для .nca файлів (потребує архівного біта)
            if (hos_folder_mode && !strcmp(fno.fname + strlen(fno.fname) - 4, ".nca"))
            {
                stats->fixed_bits++;
                f_chmod(path, AM_ARC, AM_ARC);
            }

            // Йдемо в глибину
            res = _traverse_unified(path, stats, 0, 0, output_y);
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
        strcpy(path, ""); // Root

        gfx_clearscreen();
        gfx_printf("\n\n-- Running Maintenance (Fix Archive Bits + Clean Mac Junk)\n");
        gfx_printf("Scanning entire SD card...\n\n");

        u32 x, y;
        gfx_con_getpos(&x, &y);

        // hos_folder_mode = 1 (вмикає правильну обробку .nca файлів, якщо вони трапляться)
        _traverse_unified(path, &stats, 1, 1, y);

        gfx_printf("\n\n%kDone!\nFixed attributes: %d\nRemoved junk files: %d%k", 
            0xFF96FF00, stats.fixed_bits, stats.deleted_junk, 0xFFCCCCCC);
    }
}

void _deleteTheme(char *basePath, char *folderId)
{
    char *path = CombinePaths(basePath, folderId);
    if (FileExists(path))
    {
        gfx_printf("-- Theme found: %s\n", path);
        FolderDelete(path);
    }
    free(path);
}

void m_entry_deleteInstalledThemes()
{
    gfx_clearscreen();
    gfx_printf("\n\n-- Deleting installed themes.\n\n");
    _deleteTheme("sd:/atmosphere/contents", "0100000000001000");
    _deleteTheme("sd:/atmosphere/contents", "0100000000001007");
    _deleteTheme("sd:/atmosphere/contents", "0100000000001013");
}

void m_entry_deleteBootFlags()
{
    gfx_clearscreen();
    gfx_printf("\n\n-- Disabling automatic sysmodule startup.\n\n");
    char *storedPath = CpyStr("sd:/atmosphere/contents");
    int readRes = 0;
    Vector_t fileVec = ReadFolder(storedPath, &readRes);
    if (readRes)
    {
        clearFileVector(&fileVec);
        DrawError(newErrCode(readRes));
    }
    else
    {
        vecDefArray(FSEntry_t *, fsEntries, fileVec);
        for (int i = 0; i < fileVec.count; i++)
        {
            char *suf = "/flags/boot2.flag";
            char *flagPath = CombinePaths(storedPath, fsEntries[i].name);
            flagPath = CombinePaths(flagPath, suf);

            if (FileExists(flagPath))
            {
                gfx_printf("Deleting: %s\n", flagPath);
                _DeleteFileSimple(flagPath);
            }
            free(flagPath);
        }
    }
}
