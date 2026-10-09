/*
 * NOT_FROM_ORIGINAL_SOURCE: coduox: script file functions compatible with the
 * CoD4x fs_* script API.
 *
 *   fs_testfile(path)          true when path opens for reading
 *   fs_fopen(path, mode)       mode "read", "write", or "append"; returns a
 *                              handle 1..CODUOMP_SCRIPT_FILE_MAX, or 0
 *   fs_readline(handle)        next line without its terminator, or undefined
 *                              at end of file or for a handle not open for
 *                              reading
 *   fs_writeline(handle, text) writes text and "\n"; true when the handle is
 *                              open for writing or appending, otherwise false
 *   fs_remove(path)            true when the file was removed
 *   fs_fclose(handle)          closes one handle; unopened handles are ignored
 *   fs_fcloseall()             closes every script handle
 *
 * Paths are resolved by the engine's game filesystem, which rejects ".." and
 * "::" components. Reads come from the active search paths (including pk3
 * files); writes, appends, and removals use the writable fs_homepath copy of
 * the current game directory. Package and native-module names (.pk3, .so,
 * .dll, .dylib, .exe) cannot be opened for writing or removed. Script handles
 * index a table owned by this module; they are never engine handles, so a
 * script cannot reach files the game module or engine opened for other
 * purposes. The table is closed when the game shuts down or restarts.
 */

#include "coduomp_script_files.h"

#include "recovered_game.h"
#include "game_functions.h"
#include "scr_vm.h"
#include "g_syscalls.h"
#include "qcommon/q_string.h"

#include <string.h>

enum {
    CODUOMP_SCRIPT_FILE_MAX = 10,
    CODUOMP_SCRIPT_FILE_CHUNK_SIZE = 4096,
    /* Longest line returned by one fs_readline call; the remainder of a
     * longer line is returned by following calls. */
    CODUOMP_SCRIPT_FILE_LINE_SIZE = 8192,
    CODUOMP_SCRIPT_FILE_PATH_SIZE = 256
};

typedef enum coduomp_script_file_mode_e {
    CODUOMP_SCRIPT_FILE_READ = 0,
    CODUOMP_SCRIPT_FILE_WRITE = 1
} coduomp_script_file_mode_t;

typedef struct coduomp_script_file_s {
    qboolean open;
    coduomp_script_file_mode_t mode;
    int engineHandle;
    /* Path as passed to fs_fopen; fs_remove refuses paths still open. */
    char path[CODUOMP_SCRIPT_FILE_PATH_SIZE];
    /* Bytes not yet transferred from the engine handle (read mode). */
    int remaining;
    /* chunk[chunkPos..chunkLength) holds read-ahead bytes not yet returned. */
    int chunkPos;
    int chunkLength;
    char chunk[CODUOMP_SCRIPT_FILE_CHUNK_SIZE];
} coduomp_script_file_t;

static coduomp_script_file_t coduomp_script_files[CODUOMP_SCRIPT_FILE_MAX];

/* NOT_FROM_ORIGINAL_SOURCE: coduox: maps a script handle to its open slot, or
 * NULL for any value outside 1..CODUOMP_SCRIPT_FILE_MAX or a closed slot. */
static coduomp_script_file_t *coduomp_script_file_from_handle(int handle)
{
    if (handle < 1 || handle > CODUOMP_SCRIPT_FILE_MAX) {
        return NULL;
    }
    if (!coduomp_script_files[handle - 1].open) {
        return NULL;
    }
    return &coduomp_script_files[handle - 1];
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: releases the engine handle and clears the
 * slot. */
static void coduomp_script_file_close(coduomp_script_file_t *file)
{
    if (file->engineHandle != 0) {
        trap_FS_FCloseFile(file->engineHandle);
    }
    memset(file, 0, sizeof(*file));
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: true when path names a package or native
 * module, which scripts may not create, truncate, or remove. The engine
 * applies the same rule to removals. */
static qboolean coduomp_script_file_path_is_protected(const char *path)
{
    static const char *const protectedSuffixes[] = {
        ".pk3", ".so", ".dll", ".dylib", ".exe"
    };
    const size_t pathLength = strlen(path);

    for (size_t index = 0;
         index < sizeof(protectedSuffixes) / sizeof(protectedSuffixes[0]);
         ++index) {
        const size_t suffixLength = strlen(protectedSuffixes[index]);
        if (pathLength >= suffixLength &&
            Q_stricmp(path + pathLength - suffixLength,
                      protectedSuffixes[index]) == 0) {
            return qtrue;
        }
    }
    return qfalse;
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: refills the read-ahead buffer. The
 * engine's read syscall reports no count, so reads never request more than the
 * size returned by the open call minus bytes already transferred. Returns
 * qfalse at end of file. */
static qboolean coduomp_script_file_fill(coduomp_script_file_t *file)
{
    int count = file->remaining;

    if (count <= 0) {
        return qfalse;
    }
    if (count > CODUOMP_SCRIPT_FILE_CHUNK_SIZE) {
        count = CODUOMP_SCRIPT_FILE_CHUNK_SIZE;
    }
    trap_FS_Read(file->chunk, count, file->engineHandle);
    file->remaining -= count;
    file->chunkPos = 0;
    file->chunkLength = count;
    return qtrue;
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: fs_testfile(path) */
void coduomp_script_func_fs_testfile(void)
{
    int handle = 0;
    int length;

    if (Scr_GetNumParam() != 1) {
        Scr_Error("Usage: fs_testfile(<filename>)");
        return;
    }

    length = trap_FS_FOpenFile(Scr_GetString(0), &handle, FS_READ);
    if (handle != 0) {
        trap_FS_FCloseFile(handle);
    }
    Scr_AddBool(length >= 0 && handle != 0 ? qtrue : qfalse);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: fs_fopen(path, mode). An unknown mode, a
 * path longer than CODUOMP_SCRIPT_FILE_PATH_SIZE - 1, or a protected name
 * opened for writing is a script error; other failures return 0. */
void coduomp_script_func_fs_fopen(void)
{
    const char *path;
    const char *modeName;
    coduomp_script_file_mode_t mode;
    fsMode_t engineMode;
    int slot;
    int engineHandle = 0;
    int length;

    if (Scr_GetNumParam() != 2) {
        Scr_Error("Usage: fs_fopen(<filename>, <mode>)");
        return;
    }
    path = Scr_GetString(0);
    modeName = Scr_GetString(1);
    if (Q_stricmp(modeName, "read") == 0) {
        mode = CODUOMP_SCRIPT_FILE_READ;
        engineMode = FS_READ;
    } else if (Q_stricmp(modeName, "write") == 0) {
        mode = CODUOMP_SCRIPT_FILE_WRITE;
        engineMode = FS_WRITE;
    } else if (Q_stricmp(modeName, "append") == 0) {
        mode = CODUOMP_SCRIPT_FILE_WRITE;
        engineMode = FS_APPEND;
    } else {
        Scr_Error("fs_fopen: invalid mode; valid modes are read, write, "
                  "append");
        return;
    }
    if (strlen(path) >= CODUOMP_SCRIPT_FILE_PATH_SIZE) {
        Scr_Error("fs_fopen: path is too long");
        return;
    }
    if (mode == CODUOMP_SCRIPT_FILE_WRITE &&
        coduomp_script_file_path_is_protected(path)) {
        Scr_Error("fs_fopen: package and program files cannot be written");
        return;
    }

    for (slot = 0; slot < CODUOMP_SCRIPT_FILE_MAX; ++slot) {
        if (!coduomp_script_files[slot].open) {
            break;
        }
    }
    if (slot == CODUOMP_SCRIPT_FILE_MAX) {
        Com_DPrintf("fs_fopen: all %d script file handles are open\n",
                    CODUOMP_SCRIPT_FILE_MAX);
        Scr_AddInt(0);
        return;
    }

    length = trap_FS_FOpenFile(path, &engineHandle, engineMode);
    if (length < 0 || engineHandle == 0) {
        if (engineHandle != 0) {
            trap_FS_FCloseFile(engineHandle);
        }
        Com_DPrintf("fs_fopen: could not open %s\n", path);
        Scr_AddInt(0);
        return;
    }

    memset(&coduomp_script_files[slot], 0, sizeof(coduomp_script_files[slot]));
    coduomp_script_files[slot].open = qtrue;
    coduomp_script_files[slot].mode = mode;
    coduomp_script_files[slot].engineHandle = engineHandle;
    Q_strncpyz(coduomp_script_files[slot].path, path,
               sizeof(coduomp_script_files[slot].path));
    if (mode == CODUOMP_SCRIPT_FILE_READ) {
        coduomp_script_files[slot].remaining = length;
    }
    Scr_AddInt(slot + 1);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: fs_readline(handle). Lines end at "\n"; a
 * preceding "\r" is removed. A final line without a terminator is still
 * returned. */
void coduomp_script_func_fs_readline(void)
{
    static char line[CODUOMP_SCRIPT_FILE_LINE_SIZE];
    coduomp_script_file_t *file;
    int length = 0;
    qboolean readAny = qfalse;

    if (Scr_GetNumParam() != 1) {
        Scr_Error("Usage: fs_readline(<filehandle>)");
        return;
    }
    file = coduomp_script_file_from_handle(Scr_GetInt(0));
    if (file == NULL || file->mode != CODUOMP_SCRIPT_FILE_READ) {
        Com_DPrintf("fs_readline: file handle is not open for reading\n");
        Scr_AddUndefined();
        return;
    }

    for (;;) {
        char character;

        if (file->chunkPos == file->chunkLength &&
            !coduomp_script_file_fill(file)) {
            break;
        }
        character = file->chunk[file->chunkPos++];
        readAny = qtrue;
        if (character == '\n') {
            break;
        }
        line[length++] = character;
        if (length == CODUOMP_SCRIPT_FILE_LINE_SIZE - 1) {
            break;
        }
    }

    if (!readAny) {
        Scr_AddUndefined();
        return;
    }
    if (length > 0 && line[length - 1] == '\r') {
        --length;
    }
    line[length] = '\0';
    Scr_AddString(line);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: fs_writeline(handle, text). The engine
 * write syscall reports no count, so the result only reflects whether the
 * handle was open for writing or appending. */
void coduomp_script_func_fs_writeline(void)
{
    coduomp_script_file_t *file;
    const char *text;

    if (Scr_GetNumParam() != 2) {
        Scr_Error("Usage: fs_writeline(<filehandle>, <text>)");
        return;
    }
    file = coduomp_script_file_from_handle(Scr_GetInt(0));
    text = Scr_GetString(1);
    if (file == NULL || file->mode != CODUOMP_SCRIPT_FILE_WRITE) {
        Com_DPrintf("fs_writeline: file handle is not open for writing\n");
        Scr_AddBool(qfalse);
        return;
    }

    trap_FS_Write(text, (int)strlen(text), file->engineHandle);
    trap_FS_Write("\n", 1, file->engineHandle);
    Scr_AddBool(qtrue);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: fs_remove(path). Removing a path that a
 * script handle still has open, or a protected name, is a script error. */
void coduomp_script_func_fs_remove(void)
{
    const char *path;
    int slot;

    if (Scr_GetNumParam() != 1) {
        Scr_Error("Usage: fs_remove(<filename>)");
        return;
    }
    path = Scr_GetString(0);
    if (coduomp_script_file_path_is_protected(path)) {
        Scr_Error("fs_remove: package and program files cannot be removed");
        return;
    }
    for (slot = 0; slot < CODUOMP_SCRIPT_FILE_MAX; ++slot) {
        if (coduomp_script_files[slot].open &&
            Q_stricmp(coduomp_script_files[slot].path, path) == 0) {
            Scr_Error("fs_remove: the file is still open");
            return;
        }
    }

    Scr_AddBool(coduomp_trap_FS_Remove(path));
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: fs_fclose(handle) */
void coduomp_script_func_fs_fclose(void)
{
    coduomp_script_file_t *file;

    if (Scr_GetNumParam() != 1) {
        Scr_Error("Usage: fs_fclose(<filehandle>)");
        return;
    }
    file = coduomp_script_file_from_handle(Scr_GetInt(0));
    if (file != NULL) {
        coduomp_script_file_close(file);
    }
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: fs_fcloseall() */
void coduomp_script_func_fs_fcloseall(void)
{
    coduomp_script_files_close_all();
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: see coduomp_script_files.h. */
void coduomp_script_files_close_all(void)
{
    int slot;

    for (slot = 0; slot < CODUOMP_SCRIPT_FILE_MAX; ++slot) {
        if (coduomp_script_files[slot].open) {
            coduomp_script_file_close(&coduomp_script_files[slot]);
        }
    }
}
