// #include <utils/util.h>
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

#include <unistd.h>
#include <sys/types.h>
// #include <dirent.h>
// #include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void _DeleteFileSimple(char *thing)
{
    //char *thing = CombinePaths(path, entry.name);
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

int listdir(char *path, u32 hos_folder)
{
    FRESULT res;
    DIR dir;
    u32 dirLength = 0;
    static FILINFO fno;

    // Open directory.
    res = f_opendir(&dir, path);
    if (res != FR_OK)
        return res;

    dirLength = strlen(path);
    for (;;)
    {
        // Clear file or folder path.
        path[dirLength] = 0;
        // Read a directory item.
        res = f_readdir(&dir, &fno);
        // Break on error or end of dir.
        if (res != FR_OK || fno.fname[0] == 0)
            break;
        // Skip official Nintendo dir if started from root.
        if (!hos_folder && !strcmp(fno.fname, "Nintendo"))
            continue;

        // // Set new directory or file.
        memcpy(&path[dirLength], "/", 1);
        memcpy(&path[dirLength + 1], fno.fname, strlen(fno.fname) + 1);
        // gfx_printf("THING: %s\n", fno.fname);
        // gfx_printf("Path: %s\n", dir);
        // Is it a directory?
        if (fno.fattrib & AM_DIR)
        {
            if (
                strcmp(fno.fname, ".Trash") == 0 ||
                strcmp(fno.fname, ".Trashes") == 0 ||
                strcmp(fno.fname, ".DS_Store") == 0 ||
                strcmp(fno.fname, ".Spotlight-V100") == 0 ||
                strcmp(fno.fname, ".apDisk") == 0 ||
                strcmp(fno.fname, ".VolumeIcon.icns") == 0 ||
                strcmp(fno.fname, ".fseventsd") == 0 ||
                strcmp(fno.fname, ".TemporaryItems") == 0)
            {
                _FolderDelete(path);
            }

            // Enter the directory.
            listdir(path, 0);
            if (res != FR_OK)
                break;
        }
        else
        {
            if (
                strcmp(fno.fname, ".DS_Store") == 0 ||
                strcmp(fno.fname, ".Spotlight-V100") == 0 ||
                strcmp(fno.fname, ".apDisk") == 0 ||
                strcmp(fno.fname, ".VolumeIcon.icns") == 0 ||
                strcmp(fno.fname, ".fseventsd") == 0 ||
                strcmp(fno.fname, ".TemporaryItems") == 0 ||
                _StartsWith(fno.fname, "._"))
            {
                _DeleteFileSimple(path);
            }
        }
    }
    f_closedir(&dir);
    free(path);
    return res;
}

int _fix_attributes(char *path, u32 *total, u32 hos_folder, u32 check_first_run, u32 output_y)
{
    FRESULT res;
    DIR dir;
    u32 dirLength = 0;
    static FILINFO fno;

    if (check_first_run)
    {
        // Read file attributes.
        res = f_stat(path, &fno);
        if (res != FR_OK)
            return res;

        // Check if archive bit is set.
        if (fno.fattrib & AM_ARC)
        {
            *(u32 *)total = *(u32 *)total + 1;
            f_chmod(path, 0, AM_ARC);
        }
    }

    // Open directory.
    res = f_opendir(&dir, path);
    if (res != FR_OK)
        return res;

    dirLength = strlen(path);
    for (;;)
    {
        // Clear file or folder path.
        path[dirLength] = 0;

        // Read a directory item.
        res = f_readdir(&dir, &fno);

        // Break on error or end of dir.
        if (res != FR_OK || fno.fname[0] == 0)
            break;

        // Skip official Nintendo dir if started from root.
        if (!hos_folder && !strcmp(fno.fname, "Nintendo"))
            continue;

        // Set new directory or file.
        memcpy(&path[dirLength], "/", 1);
        memcpy(&path[dirLength + 1], fno.fname, strlen(fno.fname) + 1);

        // --- ВІЗУАЛІЗАЦІЯ ---
        // 1. Ставимо курсор на початок рядка output_y
        gfx_con_setpos(0, output_y);
        
        // 2. Друкуємо шлях. Додаємо пробіли в кінці, щоб затерти попередній довший текст.
        // %-80s означає "надрукувати рядок, і якщо він коротший за 80 символів, добити пробілами".
        // Якщо екран сміттєвий, можна збільшити кількість пробілів або просто вивести довгий рядок пробілів після path.
        gfx_printf("Checking: %s                                        ", path); 
        // -------------------

        // Check if archive bit is set.
        if (fno.fattrib & AM_ARC)
        {
            *total = *total + 1;
            f_chmod(path, 0, AM_ARC);
        }

        // Is it a directory?
        if (fno.fattrib & AM_DIR)
        {
            // Set archive bit to NCA folders.
            if (hos_folder && !strcmp(fno.fname + strlen(fno.fname) - 4, ".nca"))
            {
                *total = *total + 1;
                f_chmod(path, AM_ARC, AM_ARC);
            }

            // Enter the directory. 
            // Передаємо output_y далі в рекурсію
            res = _fix_attributes(path, total, hos_folder, 0, output_y);
            if (res != FR_OK)
                break;
        }
    }

    f_closedir(&dir);

    return res;
}

void m_entry_fixArchiveBit(u32 type)
{
    char path[256];
    char label[16];

    u32 total = 0;
    if (sd_mount())
    {
        switch (type)
        {
        case 0:
            strcpy(path, "/");
            strcpy(label, "SD Card");
            break;
        case 1:
        default:
            strcpy(path, "/Nintendo");
            strcpy(label, "Nintendo folder");
            break;
        }

        gfx_printf("Traversing all %s files!\nThis may take some time...\n\n", label);
        
        // Отримуємо поточну позицію курсору (Y), щоб знати, де малювати статус
        u32 x, y;
        gfx_con_getpos(&x, &y);
        
        // Передаємо 'y' у функцію
        _fix_attributes(path, &total, type, type, y);
        
        // Після завершення переходимо на новий рядок, щоб фінальний текст не затер останній файл
        gfx_printf("\n\n%kTotal archive bits cleared: %d!%k", 0xFF96FF00, total, 0xFFCCCCCC);
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

// Рекурсивна функція видалення (на основі _fix_attributes)
int _clean_macos_recursive(char *path, u32 *deleted_count, u32 output_y)
{
    FRESULT res;
    DIR dir;
    u32 dirLength = 0;
    static FILINFO fno;

    // Відкриваємо директорію
    res = f_opendir(&dir, path);
    if (res != FR_OK)
        return res;

    dirLength = strlen(path);
    for (;;)
    {
        // Очищаємо кінець шляху для нового файлу
        path[dirLength] = 0;

        // Читаємо елемент
        res = f_readdir(&dir, &fno);

        // Вихід, якщо помилка або кінець списку
        if (res != FR_OK || fno.fname[0] == 0)
            break;

        // Формуємо повний шлях
        memcpy(&path[dirLength], "/", 1);
        memcpy(&path[dirLength + 1], fno.fname, strlen(fno.fname) + 1);

        // --- ВІЗУАЛІЗАЦІЯ ---
        gfx_con_setpos(0, output_y);
        gfx_printf("Scanning: %-80s", path); 
        // -------------------

        // Перевіряємо, чи це сміття
        if (_is_macos_junk(fno.fname))
        {
            // Якщо це директорія (наприклад .Trash)
            if (fno.fattrib & AM_DIR)
            {
                // Використовуємо вашу функцію _FolderDelete, вона безпечна для const char*
                if (!_FolderDelete(path).err) {
                    *deleted_count = *deleted_count + 1;
                }
            }
            else
            {
                // Якщо файл - видаляємо. 
                // УВАГА: не використовуємо _DeleteFileSimple, бо вона робить free(), 
                // а path у нас статичний/на стеку.
                if (f_unlink(path) == FR_OK) {
                    *deleted_count = *deleted_count + 1;
                }
            }
            
            // Якщо ми видалили папку/файл, немає сенсу заходити всередину, йдемо далі
            continue; 
        }

        // Якщо це звичайна папка - заходимо рекурсивно
        if (fno.fattrib & AM_DIR)
        {
            // Пропускаємо системну папку Nintendo, щоб прискорити процес (опціонально)
            // if (strcmp(fno.fname, "Nintendo") == 0) continue;

            res = _clean_macos_recursive(path, deleted_count, output_y);
            if (res != FR_OK)
                break;
        }
    }

    f_closedir(&dir);
    return res;
}

void m_entry_removeMacJunk()
{
    char path[512]; // Збільшив буфер про всяк випадок
    u32 deleted = 0;

    if (sd_mount())
    {
        // Починаємо з кореня
        strcpy(path, ""); // FatFs розуміє порожній рядок як корінь, або можна "/"

        gfx_clearscreen();
        gfx_printf("\n\n-- Cleaning macOS junk files (.DS_Store, ._*, etc)\n");
        gfx_printf("This keeps your SD card clean and fixes some errors.\n\n");

        u32 x, y;
        gfx_con_getpos(&x, &y);

        // Запускаємо рекурсію
        _clean_macos_recursive(path, &deleted, y);

        gfx_printf("\n\n%kDone! Removed %d junk files/folders.%k", 0xFF96FF00, deleted, 0xFFCCCCCC);
    }
}

void m_entry_fixAll()
{
    gfx_clearscreen();
    m_entry_deleteBootFlags();
    m_entry_deleteInstalledThemes();

}
