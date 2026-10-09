/*
 * NOT_FROM_ORIGINAL_SOURCE: coduox: per-player key/value store for scripts.
 *
 *   self setKey(key, value)   stores an integer, float, or string
 *   self getKey(key)          returns the stored value, or undefined
 *   self deleteKey(key)       removes the key if present
 *
 * Keys are 1..CODUOMP_PLAYER_DATA_KEY_SIZE - 1 characters from [A-Za-z0-9_.-]
 * and are case-sensitive. String values are at most
 * CODUOMP_PLAYER_DATA_VALUE_SIZE - 1 bytes and cannot contain line breaks. A
 * player holds at most CODUOMP_PLAYER_DATA_MAX_KEYS keys.
 *
 * A player with a nonzero GUID is backed by playerdata/<guid>.txt in the
 * writable game directory. The file is read on the player's first access and
 * rewritten after every change, so a server stop loses nothing. Each line is
 * "<key> <type> <value>" with type i (integer), f (float), or s (string);
 * malformed lines are skipped when reading. A player whose GUID is 0 (bots,
 * and players without CD-key authorization) keeps values only in memory.
 *
 * In-memory state is cleared when the client disconnects and when the game
 * shuts down or restarts; persistent values are read again on next access.
 */

#include "coduomp_player_data.h"

#include "recovered_game.h"
#include "game_functions.h"
#include "scr_vm.h"
#include "g_syscalls.h"
#include "qcommon/com_sprintf.h"
#include "qcommon/q_string.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CODUOMP_PLAYER_DATA_MAX_KEYS = 128,
    CODUOMP_PLAYER_DATA_KEY_SIZE = 33,
    CODUOMP_PLAYER_DATA_VALUE_SIZE = 257,
    /* Largest file accepted on load: every key and value at full length. */
    CODUOMP_PLAYER_DATA_FILE_SIZE =
        CODUOMP_PLAYER_DATA_MAX_KEYS *
        (CODUOMP_PLAYER_DATA_KEY_SIZE + CODUOMP_PLAYER_DATA_VALUE_SIZE + 4),
    CODUOMP_PLAYER_DATA_PATH_SIZE = 64
};

typedef enum coduomp_player_data_type_e {
    CODUOMP_PLAYER_DATA_INT = 'i',
    CODUOMP_PLAYER_DATA_FLOAT = 'f',
    CODUOMP_PLAYER_DATA_STRING = 's'
} coduomp_player_data_type_t;

typedef struct coduomp_player_data_entry_s {
    char key[CODUOMP_PLAYER_DATA_KEY_SIZE];
    char type;
    char value[CODUOMP_PLAYER_DATA_VALUE_SIZE];
} coduomp_player_data_entry_t;

typedef struct coduomp_player_data_s {
    qboolean loaded;
    int guid;
    int count;
    coduomp_player_data_entry_t entries[CODUOMP_PLAYER_DATA_MAX_KEYS];
} coduomp_player_data_t;

static coduomp_player_data_t coduomp_player_data[MAX_CLIENTS];
/* Load and save staging buffer; one extra byte for the terminator. */
static char coduomp_player_data_file[CODUOMP_PLAYER_DATA_FILE_SIZE + 1];

/* NOT_FROM_ORIGINAL_SOURCE: coduox: console trace of store activity, printed
 * only while developer_script is nonzero. */
static void coduomp_player_data_debug(const char *format, ...)
{
    char message[1024];
    va_list arguments;

    if (trap_Cvar_VariableIntegerValue("developer_script") == 0) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    Com_Printf("[playerdata] %s\n", message);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: true for a key accepted by setKey. */
static qboolean coduomp_player_data_key_is_valid(const char *key)
{
    size_t length = 0;

    for (; key[length] != '\0'; ++length) {
        const char character = key[length];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '.' || character == '-')) {
            return qfalse;
        }
    }
    return length > 0 && length < CODUOMP_PLAYER_DATA_KEY_SIZE ? qtrue
                                                               : qfalse;
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: index of key in data, or -1. */
static int coduomp_player_data_find(const coduomp_player_data_t *data,
                                    const char *key)
{
    for (int index = 0; index < data->count; ++index) {
        if (strcmp(data->entries[index].key, key) == 0) {
            return index;
        }
    }
    return -1;
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: the persistent file path for guid. */
static void coduomp_player_data_path(int guid, char *path, size_t pathSize)
{
    Com_sprintf(path, pathSize, "playerdata/%d.txt", guid);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: parses one "<key> <type> <value>" line
 * into data, ignoring malformed lines, duplicate keys, and lines beyond the
 * key limit. */
static void coduomp_player_data_parse_line(coduomp_player_data_t *data,
                                           char *line)
{
    char *separator = strchr(line, ' ');
    char *value;
    coduomp_player_data_entry_t *entry;
    size_t valueLength;

    if (separator == NULL || data->count == CODUOMP_PLAYER_DATA_MAX_KEYS) {
        return;
    }
    *separator = '\0';
    if (!coduomp_player_data_key_is_valid(line) ||
        coduomp_player_data_find(data, line) >= 0) {
        return;
    }
    if ((separator[1] != CODUOMP_PLAYER_DATA_INT &&
         separator[1] != CODUOMP_PLAYER_DATA_FLOAT &&
         separator[1] != CODUOMP_PLAYER_DATA_STRING) ||
        separator[2] != ' ') {
        return;
    }
    value = separator + 3;
    valueLength = strlen(value);
    if (valueLength > 0 && value[valueLength - 1] == '\r') {
        value[--valueLength] = '\0';
    }
    if (valueLength >= CODUOMP_PLAYER_DATA_VALUE_SIZE) {
        return;
    }

    entry = &data->entries[data->count++];
    Q_strncpyz(entry->key, line, sizeof(entry->key));
    entry->type = separator[1];
    Q_strncpyz(entry->value, value, sizeof(entry->value));
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: reads the persistent file for data->guid.
 * A missing, empty, or oversized file leaves the store empty. */
static void coduomp_player_data_load(coduomp_player_data_t *data)
{
    char path[CODUOMP_PLAYER_DATA_PATH_SIZE];
    int handle = 0;
    int length;
    char *line;

    coduomp_player_data_path(data->guid, path, sizeof(path));
    length = trap_FS_FOpenFile(path, &handle, FS_READ);
    if (handle == 0) {
        coduomp_player_data_debug("load %s: no file yet", path);
        return;
    }
    if (length <= 0 || length > CODUOMP_PLAYER_DATA_FILE_SIZE) {
        if (length > CODUOMP_PLAYER_DATA_FILE_SIZE) {
            Com_Printf("WARNING: %s is too large and was not loaded\n", path);
        }
        trap_FS_FCloseFile(handle);
        return;
    }
    trap_FS_Read(coduomp_player_data_file, length, handle);
    trap_FS_FCloseFile(handle);
    coduomp_player_data_file[length] = '\0';

    line = coduomp_player_data_file;
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        coduomp_player_data_parse_line(data, line);
        line = next;
    }
    coduomp_player_data_debug("load %s: %d bytes, %d keys", path, length,
                              data->count);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: rewrites the persistent file for
 * data->guid from the in-memory store. Stores with GUID 0 are not written. */
static void coduomp_player_data_save(const coduomp_player_data_t *data)
{
    char path[CODUOMP_PLAYER_DATA_PATH_SIZE];
    int handle = 0;
    size_t length = 0;

    if (data->guid == 0) {
        coduomp_player_data_debug("save skipped: GUID is 0, values are kept "
                                  "for this session only");
        return;
    }
    for (int index = 0; index < data->count; ++index) {
        const coduomp_player_data_entry_t *entry = &data->entries[index];
        length += (size_t)Com_sprintf(
            coduomp_player_data_file + length,
            sizeof(coduomp_player_data_file) - length, "%s %c %s\n",
            entry->key, entry->type, entry->value);
    }

    coduomp_player_data_path(data->guid, path, sizeof(path));
    (void)trap_FS_FOpenFile(path, &handle, FS_WRITE);
    if (handle == 0) {
        Com_Printf("WARNING: could not write %s\n", path);
        return;
    }
    trap_FS_Write(coduomp_player_data_file, (int)length, handle);
    trap_FS_FCloseFile(handle);
    coduomp_player_data_debug("save %s: %d keys, %d bytes", path,
                              data->count, (int)length);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: the store for a player entity, loading it
 * on first access. A non-player entity is a script error. */
static coduomp_player_data_t *coduomp_player_data_for_entity(
    uint32_t entityNum, const char *methodName)
{
    coduomp_player_data_t *data;
    int guid;

    if (entityNum >= (uint32_t)level.maxclients ||
        g_entities[entityNum].client == NULL) {
        Scr_Error(va("%s: entity %i is not a player", methodName,
                     (int)entityNum));
        return NULL;
    }

    data = &coduomp_player_data[entityNum];
    guid = trap_GetGuid((int)entityNum);
    if (!data->loaded || data->guid != guid) {
        coduomp_player_data_debug("client %d: first access, GUID %d",
                                  (int)entityNum, guid);
        memset(data, 0, sizeof(*data));
        data->loaded = qtrue;
        data->guid = guid;
        if (guid != 0) {
            coduomp_player_data_load(data);
        }
    }
    return data;
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: reads and validates the key parameter. */
static const char *coduomp_player_data_key_param(const char *methodName)
{
    const char *key = Scr_GetString(0);

    if (!coduomp_player_data_key_is_valid(key)) {
        Scr_Error(va("%s: key must be 1-%d characters of letters, digits, "
                     "'_', '.', or '-'",
                     methodName, CODUOMP_PLAYER_DATA_KEY_SIZE - 1));
        return NULL;
    }
    return key;
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: self setKey(key, value) */
void coduomp_player_cmd_setkey(uint32_t entityNum)
{
    coduomp_player_data_t *data;
    coduomp_player_data_entry_t *entry;
    const char *key;
    char value[CODUOMP_PLAYER_DATA_VALUE_SIZE];
    char type;
    int index;

    if (Scr_GetNumParam() != 2) {
        Scr_Error("USAGE: self setKey(<key>, <value>)");
        return;
    }
    data = coduomp_player_data_for_entity(entityNum, "setKey");
    key = coduomp_player_data_key_param("setKey");
    if (data == NULL || key == NULL) {
        return;
    }

    switch (Scr_GetType(1)) {
    case SCRIPT_VAR_INT:
        type = CODUOMP_PLAYER_DATA_INT;
        Com_sprintf(value, sizeof(value), "%d", Scr_GetInt(1));
        break;
    case SCRIPT_VAR_FLOAT:
        type = CODUOMP_PLAYER_DATA_FLOAT;
        Com_sprintf(value, sizeof(value), "%.9g", (double)Scr_GetFloat(1));
        break;
    case SCRIPT_VAR_STRING: {
        const char *text = Scr_GetString(1);
        if (strlen(text) >= sizeof(value)) {
            Scr_Error(va("setKey: string values are limited to %d bytes",
                         CODUOMP_PLAYER_DATA_VALUE_SIZE - 1));
            return;
        }
        if (strchr(text, '\n') != NULL || strchr(text, '\r') != NULL) {
            Scr_Error("setKey: string values cannot contain line breaks");
            return;
        }
        type = CODUOMP_PLAYER_DATA_STRING;
        Q_strncpyz(value, text, sizeof(value));
        break;
    }
    default:
        Scr_Error("setKey: value must be an integer, float, or string");
        return;
    }

    index = coduomp_player_data_find(data, key);
    if (index < 0) {
        if (data->count == CODUOMP_PLAYER_DATA_MAX_KEYS) {
            Scr_Error(va("setKey: a player can hold at most %d keys",
                         CODUOMP_PLAYER_DATA_MAX_KEYS));
            return;
        }
        index = data->count++;
        Q_strncpyz(data->entries[index].key, key,
                   sizeof(data->entries[index].key));
    } else if (data->entries[index].type == type &&
               strcmp(data->entries[index].value, value) == 0) {
        coduomp_player_data_debug("client %d setKey(%s) = %c \"%s\" "
                                  "(unchanged)",
                                  (int)entityNum, key, type, value);
        return;
    }

    entry = &data->entries[index];
    entry->type = type;
    Q_strncpyz(entry->value, value, sizeof(entry->value));
    coduomp_player_data_debug("client %d setKey(%s) = %c \"%s\"",
                              (int)entityNum, key, type, value);
    coduomp_player_data_save(data);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: self getKey(key) */
void coduomp_player_cmd_getkey(uint32_t entityNum)
{
    coduomp_player_data_t *data;
    const coduomp_player_data_entry_t *entry;
    const char *key;
    int index;

    if (Scr_GetNumParam() != 1) {
        Scr_Error("USAGE: self getKey(<key>)");
        return;
    }
    data = coduomp_player_data_for_entity(entityNum, "getKey");
    key = coduomp_player_data_key_param("getKey");
    if (data == NULL || key == NULL) {
        return;
    }

    index = coduomp_player_data_find(data, key);
    if (index < 0) {
        coduomp_player_data_debug("client %d getKey(%s) = undefined",
                                  (int)entityNum, key);
        Scr_AddUndefined();
        return;
    }
    entry = &data->entries[index];
    coduomp_player_data_debug("client %d getKey(%s) = %c \"%s\"",
                              (int)entityNum, key, entry->type, entry->value);
    switch (entry->type) {
    case CODUOMP_PLAYER_DATA_INT:
        Scr_AddInt((int)strtol(entry->value, NULL, 10));
        break;
    case CODUOMP_PLAYER_DATA_FLOAT:
        Scr_AddFloat(strtof(entry->value, NULL));
        break;
    default:
        Scr_AddString(entry->value);
        break;
    }
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: self deleteKey(key) */
void coduomp_player_cmd_deletekey(uint32_t entityNum)
{
    coduomp_player_data_t *data;
    const char *key;
    int index;

    if (Scr_GetNumParam() != 1) {
        Scr_Error("USAGE: self deleteKey(<key>)");
        return;
    }
    data = coduomp_player_data_for_entity(entityNum, "deleteKey");
    key = coduomp_player_data_key_param("deleteKey");
    if (data == NULL || key == NULL) {
        return;
    }

    index = coduomp_player_data_find(data, key);
    coduomp_player_data_debug("client %d deleteKey(%s)%s", (int)entityNum,
                              key, index < 0 ? " (not set)" : "");
    if (index < 0) {
        return;
    }
    memmove(&data->entries[index], &data->entries[index + 1],
            (size_t)(data->count - index - 1) * sizeof(data->entries[0]));
    --data->count;
    coduomp_player_data_save(data);
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: see coduomp_player_data.h. */
void coduomp_player_data_clear(int clientNum)
{
    if (clientNum >= 0 && clientNum < MAX_CLIENTS) {
        memset(&coduomp_player_data[clientNum], 0,
               sizeof(coduomp_player_data[clientNum]));
    }
}

/* NOT_FROM_ORIGINAL_SOURCE: coduox: see coduomp_player_data.h. */
void coduomp_player_data_clear_all(void)
{
    memset(coduomp_player_data, 0, sizeof(coduomp_player_data));
}
