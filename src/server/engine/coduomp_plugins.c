/*
 * NOT_FROM_ORIGINAL_SOURCE: coduox: server plugin host.
 *
 * `loadplugin <name>` loads plugins/<name>.so (or .dll) from fs_homepath,
 * then fs_basepath. The name is restricted to letters, digits, '_' and '-',
 * so a load can never leave the plugins directory. The plugin receives one
 * table of server functions (plugin/coduomp_plugin.h) and is otherwise
 * opaque: the server calls only the entry point and the optional callbacks
 * it exports.
 *
 * Invariants:
 *  - A plugin is called only after its entry point reported this server's
 *    interface version.
 *  - Script function and command names are copied into fixed tables that
 *    are never compacted, so a name pointer handed to the script compiler
 *    stays valid for the life of the process.
 *  - A plugin that fails OnInit has every registration it made during the
 *    load removed before it is unloaded. A plugin that loaded successfully
 *    is never unloaded, so a compiled script can never hold a pointer into
 *    an unmapped library.
 *  - Callbacks are not re-entered: a plugin loading another plugin from
 *    inside OnInit is refused.
 */

#include "coduomp_plugins.h"

#include "plugin/coduomp_plugin.h"

#include "qcommon/net_text.h"
#include "qcommon/q_command.h"
#include "qcommon/q_cvar.h"
#include "qcommon/q_string.h"
#include "qcommon/server_runtime_types.h"
#include "scripting/script_string.h"
#include "scripting/script_value.h"
#include "server_client_message.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define CODUOMP_PLUGIN_SUFFIX ".dll"
#else
#define CODUOMP_PLUGIN_SUFFIX ".so"
#endif

enum {
    CODUOMP_PLUGIN_MAX = 16,
    CODUOMP_PLUGIN_NAME_SIZE = 33,
    CODUOMP_PLUGIN_PATH_SIZE = 512,
    CODUOMP_PLUGIN_SYMBOL_MAX = 256,
    CODUOMP_PLUGIN_SYMBOL_NAME_SIZE = 64,
    CODUOMP_PLUGIN_MESSAGE_SIZE = 1024
};

typedef struct coduomp_plugin_s {
    char name[CODUOMP_PLUGIN_NAME_SIZE];
    void *library;
    pluginInfo_t info;
    void (*onTerminate)(void);
    void (*onExitLevel)(void);
    void (*onFrame)(void);
    void (*onClientConnected)(int32_t clientNum);
    void (*onClientDisconnected)(int32_t clientNum);
} coduomp_plugin_t;

/* One registered console command or script function. */
typedef struct coduomp_plugin_symbol_s {
    char name[CODUOMP_PLUGIN_SYMBOL_NAME_SIZE];
    coduomp_plugin_function_t function;
} coduomp_plugin_symbol_t;

extern serverStatic_t svs;
extern cvar_t *sv_maxclients;
extern cvar_t *sv_running;

void Com_Printf(const char *format, ...);
void Com_DPrintf(const char *format, ...);

static coduomp_plugin_t coduomp_plugins[CODUOMP_PLUGIN_MAX];
static int32_t coduomp_plugin_count;
static coduomp_plugin_symbol_t
    coduomp_plugin_script_functions[CODUOMP_PLUGIN_SYMBOL_MAX];
static int32_t coduomp_plugin_script_function_count;
static coduomp_plugin_symbol_t
    coduomp_plugin_commands[CODUOMP_PLUGIN_SYMBOL_MAX];
static int32_t coduomp_plugin_command_count;
/* Set while a plugin's entry point or OnInit runs. */
static qboolean coduomp_plugin_loading;
static qboolean coduomp_plugin_exit_registered;

/* ------------------------------------------------------------------ */
/* Library access                                                     */
/* ------------------------------------------------------------------ */

static void *coduomp_plugin_library_open(const char *path)
{
#if defined(_WIN32)
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

static void coduomp_plugin_library_close(void *library)
{
#if defined(_WIN32)
    (void)FreeLibrary((HMODULE)library);
#else
    (void)dlclose(library);
#endif
}

/* Copies the address of an exported function into *function, which must be
 * a function pointer object, and leaves it NULL when the symbol is absent.
 * POSIX dlsym yields void * even for functions; the byte copy keeps that
 * conversion out of ISO C's sight under -pedantic. */
static void coduomp_plugin_library_symbol(void *library, const char *name,
                                          void *function,
                                          size_t functionSize)
{
#if defined(_WIN32)
    FARPROC symbol = GetProcAddress((HMODULE)library, name);
#else
    void *symbol = dlsym(library, name);
#endif

    memset(function, 0, functionSize);
    if (functionSize == sizeof(symbol)) {
        memcpy(function, &symbol, functionSize);
    }
}

#define CODUOMP_PLUGIN_SYMBOL(library, name, function) \
    coduomp_plugin_library_symbol((library), (name), &(function), \
                                  sizeof(function))

/* ------------------------------------------------------------------ */
/* Function table handed to plugins                                   */
/* ------------------------------------------------------------------ */

static void coduomp_plugin_api_print_error(const char *format, ...)
{
    char message[CODUOMP_PLUGIN_MESSAGE_SIZE];
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    Com_Printf("^1Plugin error: %s\n", message);
}

static void *coduomp_plugin_api_malloc(size_t size)
{
    return malloc(size);
}

static void coduomp_plugin_api_free(void *memory)
{
    free(memory);
}

static void coduomp_plugin_copy_string(char *buffer, int32_t size,
                                       const char *text)
{
    if (buffer == NULL || size <= 0) {
        return;
    }
    Q_strncpyz(buffer, text != NULL ? text : "", size);
}

static void coduomp_plugin_api_cvar_get_string(const char *name,
                                               char *buffer, int32_t size)
{
    coduomp_plugin_copy_string(
        buffer, size, name != NULL ? Cvar_VariableString(name) : "");
}

static int32_t coduomp_plugin_api_cvar_get_int(const char *name)
{
    return name != NULL ? Cvar_VariableIntegerValue(name) : 0;
}

static float coduomp_plugin_api_cvar_get_float(const char *name)
{
    return name != NULL ? Cvar_VariableValue(name) : 0.0f;
}

static void coduomp_plugin_api_cvar_set(const char *name, const char *value)
{
    if (name != NULL && name[0] != '\0' && value != NULL) {
        Cvar_Set(name, value);
    }
}

/* Stores name, lower-cased when lowerCase is set, in the next table slot.
 * Returns the slot, or NULL when the name is empty, too long, holds a
 * character outside [A-Za-z0-9_], or the table is full or already holds the
 * name. */
static coduomp_plugin_symbol_t *coduomp_plugin_symbol_add(
    coduomp_plugin_symbol_t *table, int32_t *count, const char *name,
    coduomp_plugin_function_t function, qboolean lowerCase)
{
    coduomp_plugin_symbol_t *symbol;
    size_t length;

    if (name == NULL || function == NULL ||
        *count >= CODUOMP_PLUGIN_SYMBOL_MAX) {
        return NULL;
    }
    length = strlen(name);
    if (length == 0 || length >= CODUOMP_PLUGIN_SYMBOL_NAME_SIZE) {
        return NULL;
    }

    symbol = &table[*count];
    for (size_t i = 0; i < length; ++i) {
        char character = name[i];

        if (lowerCase != qfalse && character >= 'A' && character <= 'Z') {
            character = (char)(character - 'A' + 'a');
        }
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_')) {
            return NULL;
        }
        symbol->name[i] = character;
    }
    symbol->name[length] = '\0';

    for (int32_t i = 0; i < *count; ++i) {
        if (Q_stricmp(table[i].name, symbol->name) == 0) {
            return NULL;
        }
    }

    symbol->function = function;
    ++*count;
    return symbol;
}

static int32_t coduomp_plugin_api_add_command(
    const char *name, coduomp_plugin_function_t function)
{
    const coduomp_plugin_symbol_t *command;

    /* A plugin never replaces a command, and the rollback after a failed
     * load therefore never removes one the plugin did not add. */
    if (name == NULL) {
        return 0;
    }
    for (const cmd_function_t *existing = cmd_functions;
         existing != NULL;
         existing = existing->next) {
        if (Q_stricmp(name, existing->name) == 0) {
            return 0;
        }
    }

    command = coduomp_plugin_symbol_add(
        coduomp_plugin_commands, &coduomp_plugin_command_count, name,
        function, qfalse);
    if (command == NULL) {
        return 0;
    }
    Cmd_AddCommand(command->name, command->function);
    return 1;
}

static const char *coduomp_plugin_api_cmd_argv(int32_t index)
{
    return index >= 0 && index < Cmd_Argc() ? Cmd_Argv(index) : "";
}

static void coduomp_plugin_api_execute_command(const char *text)
{
    char line[MAX_STRING_CHARS];
    size_t length;

    if (text == NULL) {
        return;
    }
    length = strlen(text);
    if (length == 0 || length > sizeof(line) - 2u ||
        strpbrk(text, "\r\n") != NULL) {
        return;
    }
    memcpy(line, text, length);
    line[length] = '\n';
    line[length + 1u] = '\0';
    Cbuf_AddText(line);
}

static int32_t coduomp_plugin_api_scr_add_function(
    const char *name, coduomp_plugin_function_t function)
{
    return coduomp_plugin_symbol_add(
               coduomp_plugin_script_functions,
               &coduomp_plugin_script_function_count, name, function,
               qtrue) != NULL
               ? 1
               : 0;
}

static void coduomp_plugin_api_scr_add_string(const char *value)
{
    Scr_AddString(value != NULL ? value : "");
}

static void coduomp_plugin_api_scr_add_array_key(int32_t stringIndex)
{
    if (stringIndex <= 0 || stringIndex > UINT16_MAX) {
        Scr_Error("plugin passed an invalid array key");
        return;
    }
    Scr_AddArrayStringIndexed((uint16_t)stringIndex);
}

static int32_t coduomp_plugin_api_scr_alloc_string(const char *text)
{
    return (int32_t)Scr_AllocString(text != NULL ? text : "");
}

/* The script runtime keeps the message pointer until the error is reported,
 * after the plugin's frames are gone, so the text is copied here first. */
static const char *coduomp_plugin_script_error_message(const char *message)
{
    static char copy[CODUOMP_PLUGIN_MESSAGE_SIZE];

    Q_strncpyz(copy, message != NULL ? message : "", (int32_t)sizeof(copy));
    return copy;
}

static void coduomp_plugin_api_scr_error(const char *message)
{
    Scr_Error(coduomp_plugin_script_error_message(message));
}

static void coduomp_plugin_api_scr_param_error(int32_t index,
                                               const char *message)
{
    Scr_ParamError(index, coduomp_plugin_script_error_message(message));
}

static int32_t coduomp_plugin_api_get_max_clients(void)
{
    return sv_maxclients != NULL ? sv_maxclients->integer : 0;
}

/* The client in slot clientNum, or NULL when the slot is out of range or
 * free. */
static client_t *coduomp_plugin_client(int32_t clientNum)
{
    if (svs.clients == NULL || sv_maxclients == NULL || clientNum < 0 ||
        clientNum >= sv_maxclients->integer ||
        svs.clients[clientNum].state == CS_FREE) {
        return NULL;
    }
    return &svs.clients[clientNum];
}

static int32_t coduomp_plugin_api_is_client_connected(int32_t clientNum)
{
    const client_t *const client = coduomp_plugin_client(clientNum);

    return client != NULL && client->state >= CS_CONNECTED ? 1 : 0;
}

static void coduomp_plugin_api_get_client_name(int32_t clientNum,
                                               char *buffer, int32_t size)
{
    const client_t *const client = coduomp_plugin_client(clientNum);

    coduomp_plugin_copy_string(buffer, size,
                               client != NULL ? client->name : "");
}

static int32_t coduomp_plugin_api_get_client_guid(int32_t clientNum)
{
    const client_t *const client = coduomp_plugin_client(clientNum);

    return client != NULL ? client->guid : 0;
}

static void coduomp_plugin_api_get_client_address(int32_t clientNum,
                                                  char *buffer, int32_t size)
{
    const client_t *const client = coduomp_plugin_client(clientNum);
    const char *address = "";

    if (client != NULL) {
        address = client->netchan.remoteAddress.type == NA_IP
                      ? NET_AdrToString(client->netchan.remoteAddress)
                      : "bot";
    }
    coduomp_plugin_copy_string(buffer, size, address);
}

static void coduomp_plugin_api_drop_client(int32_t clientNum,
                                           const char *reason)
{
    client_t *const client = coduomp_plugin_client(clientNum);

    if (client != NULL) {
        SV_DropClient(client, reason != NULL ? reason : "");
    }
}

static const coduomp_plugin_api_t coduomp_plugin_api_table = {
    CODUOMP_PLUGIN_API_VERSION,
    (uint32_t)sizeof(coduomp_plugin_api_t),
    Com_Printf,
    Com_DPrintf,
    coduomp_plugin_api_print_error,
    coduomp_plugin_api_malloc,
    coduomp_plugin_api_free,
    coduomp_plugin_api_cvar_get_string,
    coduomp_plugin_api_cvar_get_int,
    coduomp_plugin_api_cvar_get_float,
    coduomp_plugin_api_cvar_set,
    coduomp_plugin_api_add_command,
    Cmd_Argc,
    coduomp_plugin_api_cmd_argv,
    coduomp_plugin_api_execute_command,
    coduomp_plugin_api_scr_add_function,
    Scr_GetNumParam,
    Scr_GetInt,
    Scr_GetFloat,
    Scr_GetString,
    Scr_AddInt,
    Scr_AddFloat,
    coduomp_plugin_api_scr_add_string,
    Scr_AddUndefined,
    Scr_MakeArray,
    Scr_AddArray,
    coduomp_plugin_api_scr_add_array_key,
    coduomp_plugin_api_scr_alloc_string,
    coduomp_plugin_api_scr_error,
    coduomp_plugin_api_scr_param_error,
    coduomp_plugin_api_get_max_clients,
    coduomp_plugin_api_is_client_connected,
    coduomp_plugin_api_get_client_name,
    coduomp_plugin_api_get_client_guid,
    coduomp_plugin_api_get_client_address,
    coduomp_plugin_api_drop_client
};

/* ------------------------------------------------------------------ */
/* Loading                                                            */
/* ------------------------------------------------------------------ */

void coduomp_plugins_terminate(void)
{
    /* Reached from the quit command and, for other exit paths, from the
     * process exit handler; plugins hear about it once. */
    static qboolean terminated;

    if (terminated != qfalse) {
        return;
    }
    terminated = qtrue;
    for (int32_t i = 0; i < coduomp_plugin_count; ++i) {
        if (coduomp_plugins[i].onTerminate != NULL) {
            coduomp_plugins[i].onTerminate();
        }
    }
}

static qboolean coduomp_plugin_name_valid(const char *name)
{
    const size_t length = strlen(name);

    if (length == 0 || length >= CODUOMP_PLUGIN_NAME_SIZE) {
        return qfalse;
    }
    for (size_t i = 0; i < length; ++i) {
        const char character = name[i];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '_' ||
              character == '-')) {
            return qfalse;
        }
    }
    return qtrue;
}

/* Opens plugins/<name><suffix> under fs_homepath, then fs_basepath. */
static void *coduomp_plugin_open(const char *name, char *path,
                                 size_t pathSize)
{
    static const char *const roots[] = { "fs_homepath", "fs_basepath" };

    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        const char *const root = Cvar_VariableString(roots[i]);
        void *library;

        if (root[0] == '\0' ||
            snprintf(path, pathSize, "%s/plugins/%s" CODUOMP_PLUGIN_SUFFIX,
                     root, name) >= (int)pathSize) {
            continue;
        }
        library = coduomp_plugin_library_open(path);
        if (library != NULL) {
            return library;
        }
#if !defined(_WIN32)
        /* A file that exists but does not load usually lacks a library it
         * depends on; the loader's own message names it. */
        {
            FILE *const file = fopen(path, "rb");

            if (file != NULL) {
                const char *const reason = dlerror();

                (void)fclose(file);
                Com_Printf("loadplugin: %s\n",
                           reason != NULL ? reason : path);
            }
        }
#endif
    }
    return NULL;
}

void coduomp_plugins_load_f(void)
{
    char path[CODUOMP_PLUGIN_PATH_SIZE];
    int32_t (*attach)(const coduomp_plugin_api_t *api);
    void (*onInfoRequest)(pluginInfo_t *info);
    int (*onInit)(void);
    coduomp_plugin_t *plugin;
    const char *name;
    int32_t builtVersion;
    int32_t scriptFunctionMark;
    int32_t commandMark;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: loadplugin <name>\n");
        return;
    }
    name = Cmd_Argv(1);
    if (coduomp_plugin_name_valid(name) == qfalse) {
        Com_Printf("loadplugin: a plugin name is 1 to %i letters, digits, "
                   "'_' or '-', without a path or file extension\n",
                   CODUOMP_PLUGIN_NAME_SIZE - 1);
        return;
    }
    if (coduomp_plugin_loading != qfalse) {
        Com_Printf("loadplugin: another plugin is still loading\n");
        return;
    }
    for (int32_t i = 0; i < coduomp_plugin_count; ++i) {
        if (Q_stricmp(coduomp_plugins[i].name, name) == 0) {
            Com_Printf("loadplugin: %s is already loaded\n", name);
            return;
        }
    }
    if (coduomp_plugin_count >= CODUOMP_PLUGIN_MAX) {
        Com_Printf("loadplugin: no more than %i plugins can be loaded\n",
                   CODUOMP_PLUGIN_MAX);
        return;
    }

    plugin = &coduomp_plugins[coduomp_plugin_count];
    memset(plugin, 0, sizeof(*plugin));
    plugin->library = coduomp_plugin_open(name, path, sizeof(path));
    if (plugin->library == NULL) {
        Com_Printf("loadplugin: could not load plugins/%s"
                   CODUOMP_PLUGIN_SUFFIX
                   " from fs_homepath or fs_basepath\n", name);
        return;
    }

    CODUOMP_PLUGIN_SYMBOL(plugin->library, "coduomp_plugin_attach", attach);
    if (attach == NULL) {
        Com_Printf("loadplugin: %s is not a plugin for this server\n", path);
        coduomp_plugin_library_close(plugin->library);
        return;
    }

    coduomp_plugin_loading = qtrue;
    builtVersion = attach(&coduomp_plugin_api_table);
    if (builtVersion != CODUOMP_PLUGIN_API_VERSION) {
        coduomp_plugin_loading = qfalse;
        Com_Printf("loadplugin: %s was built for plugin interface %i; this "
                   "server provides %i\n",
                   name, builtVersion, CODUOMP_PLUGIN_API_VERSION);
        coduomp_plugin_library_close(plugin->library);
        return;
    }

    Q_strncpyz(plugin->name, name, (int32_t)sizeof(plugin->name));
    plugin->info.handlerVersion.major = CODUOMP_PLUGIN_API_VERSION;
    CODUOMP_PLUGIN_SYMBOL(plugin->library, "OnInfoRequest", onInfoRequest);
    if (onInfoRequest != NULL) {
        onInfoRequest(&plugin->info);
        plugin->info.fullName[sizeof(plugin->info.fullName) - 1u] = '\0';
        plugin->info.shortDescription[
            sizeof(plugin->info.shortDescription) - 1u] = '\0';
        plugin->info.longDescription[
            sizeof(plugin->info.longDescription) - 1u] = '\0';
    }

    scriptFunctionMark = coduomp_plugin_script_function_count;
    commandMark = coduomp_plugin_command_count;
    CODUOMP_PLUGIN_SYMBOL(plugin->library, "OnInit", onInit);
    if (onInit != NULL && onInit() != 0) {
        /* Undo everything registered during this load; nothing can refer to
         * it yet because the tables only grow while a load is in progress. */
        while (coduomp_plugin_command_count > commandMark) {
            --coduomp_plugin_command_count;
            Cmd_RemoveCommand(
                coduomp_plugin_commands[coduomp_plugin_command_count].name);
        }
        coduomp_plugin_script_function_count = scriptFunctionMark;
        coduomp_plugin_loading = qfalse;
        Com_Printf("loadplugin: %s failed to initialize\n", name);
        coduomp_plugin_library_close(plugin->library);
        return;
    }
    coduomp_plugin_loading = qfalse;

    CODUOMP_PLUGIN_SYMBOL(plugin->library, "OnTerminate",
                          plugin->onTerminate);
    CODUOMP_PLUGIN_SYMBOL(plugin->library, "OnExitLevel",
                          plugin->onExitLevel);
    CODUOMP_PLUGIN_SYMBOL(plugin->library, "OnFrame", plugin->onFrame);
    CODUOMP_PLUGIN_SYMBOL(plugin->library, "OnClientConnected",
                          plugin->onClientConnected);
    CODUOMP_PLUGIN_SYMBOL(plugin->library, "OnClientDisconnected",
                          plugin->onClientDisconnected);
    ++coduomp_plugin_count;

    if (coduomp_plugin_exit_registered == qfalse) {
        coduomp_plugin_exit_registered = qtrue;
        (void)atexit(coduomp_plugins_terminate);
    }

    Com_Printf("Loaded plugin %s (%s)\n", name, path);
    if (coduomp_plugin_script_function_count > scriptFunctionMark &&
        sv_running != NULL && sv_running->integer != 0) {
        Com_Printf("Its script functions become available when the next map "
                   "loads\n");
    }
}

void coduomp_plugins_list_f(void)
{
    if (coduomp_plugin_count == 0) {
        Com_Printf("No plugins loaded\n");
        return;
    }
    for (int32_t i = 0; i < coduomp_plugin_count; ++i) {
        const coduomp_plugin_t *const plugin = &coduomp_plugins[i];

        Com_Printf("%2i %-16s v%i.%i  %s\n", i, plugin->name,
                   plugin->info.pluginVersion.major,
                   plugin->info.pluginVersion.minor,
                   plugin->info.fullName[0] != '\0'
                       ? plugin->info.fullName
                       : plugin->info.shortDescription);
    }
    Com_Printf("%i plugin(s), %i script function(s), %i command(s)\n",
               coduomp_plugin_count, coduomp_plugin_script_function_count,
               coduomp_plugin_command_count);
}

/* ------------------------------------------------------------------ */
/* Server-side entry points                                           */
/* ------------------------------------------------------------------ */

void (*coduomp_plugins_script_function(const char **name))(void)
{
    for (int32_t i = 0; i < coduomp_plugin_script_function_count; ++i) {
        if (strcmp(*name, coduomp_plugin_script_functions[i].name) == 0) {
            *name = coduomp_plugin_script_functions[i].name;
            return coduomp_plugin_script_functions[i].function;
        }
    }
    return NULL;
}

void coduomp_plugins_on_frame(void)
{
    for (int32_t i = 0; i < coduomp_plugin_count; ++i) {
        if (coduomp_plugins[i].onFrame != NULL) {
            coduomp_plugins[i].onFrame();
        }
    }
}

void coduomp_plugins_on_exit_level(void)
{
    for (int32_t i = 0; i < coduomp_plugin_count; ++i) {
        if (coduomp_plugins[i].onExitLevel != NULL) {
            coduomp_plugins[i].onExitLevel();
        }
    }
}

void coduomp_plugins_on_client_connected(int32_t clientNum)
{
    for (int32_t i = 0; i < coduomp_plugin_count; ++i) {
        if (coduomp_plugins[i].onClientConnected != NULL) {
            coduomp_plugins[i].onClientConnected(clientNum);
        }
    }
}

void coduomp_plugins_on_client_disconnected(int32_t clientNum)
{
    /* A plugin that drops a client from inside this callback re-enters the
     * drop path; the nested notification is skipped so it cannot recurse. */
    static qboolean notifying;

    if (notifying != qfalse) {
        return;
    }
    notifying = qtrue;
    for (int32_t i = 0; i < coduomp_plugin_count; ++i) {
        if (coduomp_plugins[i].onClientDisconnected != NULL) {
            coduomp_plugins[i].onClientDisconnected(clientNum);
        }
    }
    notifying = qfalse;
}
