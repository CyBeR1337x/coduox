#ifndef CODUOMP_PLUGIN_H
#define CODUOMP_PLUGIN_H

/*
 * NOT_FROM_ORIGINAL_SOURCE: coduox: server plugin interface.
 *
 * This is the only header a plugin compiles against, and the only contract
 * between a plugin and the server. A plugin is a shared library that the
 * server operator loads with the console command `loadplugin <name>`. The
 * server and the plugin exchange nothing but the plain C types and function
 * pointers declared here; a plugin never sees server data structures.
 *
 * A plugin source file must contain CODUOMP_PLUGIN_ENTRY() exactly once. It
 * may then define any of the optional callbacks listed at the end of this
 * header and call the server through the Plugin_* names.
 *
 * Rules a plugin relies on:
 *  - Every callback and every registered function runs on the server's main
 *    thread. Server functions must not be called from another thread.
 *  - A string returned by the server stays valid only until the next server
 *    function call; copy it to keep it.
 *  - Plugin_Scr_Error and Plugin_Scr_ParamError do not return. They unwind
 *    the stack to the script interpreter, so a plugin must be built with
 *    unwind tables (the compiler default on x86-64) and must not hold a
 *    resource across the call that only later code in the same function
 *    releases. C++ destructors of live objects do run.
 *  - Plugin_Scr_* functions are valid only inside a function registered
 *    with Plugin_ScrAddFunction while a script calls it.
 *  - A loaded plugin stays loaded until the server process exits.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version of this interface. The server loads a plugin only when the plugin
 * was built against the same version. */
#define CODUOMP_PLUGIN_API_VERSION 1

/* Console commands and script functions registered by a plugin. */
typedef void (*coduomp_plugin_function_t)(void);

typedef struct coduomp_plugin_api_s {
    /* CODUOMP_PLUGIN_API_VERSION and sizeof(coduomp_plugin_api_t) of the
     * running server. */
    uint32_t version;
    uint32_t size;

    /* Console output; printf-style. DPrintf prints only when the server's
     * developer cvar is set. PrintError marks the line as an error. */
    void (*Printf)(const char *format, ...);
    void (*DPrintf)(const char *format, ...);
    void (*PrintError)(const char *format, ...);

    /* General-purpose memory owned by the plugin. Malloc returns NULL when
     * the request cannot be met. */
    void *(*Malloc)(size_t size);
    void (*Free)(void *memory);

    /* Cvars. GetString always NUL-terminates buffer when size > 0 and
     * yields an empty string for an unknown cvar. Set creates the cvar when
     * it does not exist. */
    void (*Cvar_GetString)(const char *name, char *buffer, int32_t size);
    int32_t (*Cvar_GetInt)(const char *name);
    float (*Cvar_GetFloat)(const char *name);
    void (*Cvar_Set)(const char *name, const char *value);

    /* Console commands. AddCommand returns 1 when the command was added and
     * 0 when the name is invalid, already taken, or no slot is free.
     * Cmd_Argc and Cmd_Argv describe the command line being executed and
     * are valid inside a command function; Cmd_Argv(0) is the command name
     * and an out-of-range index yields an empty string. ExecuteCommand
     * queues one line of console text to run after the current frame. */
    int32_t (*AddCommand)(const char *name,
                          coduomp_plugin_function_t function);
    int32_t (*Cmd_Argc)(void);
    const char *(*Cmd_Argv)(int32_t index);
    void (*ExecuteCommand)(const char *text);

    /* Script functions. ScrAddFunction makes function callable from game
     * scripts under name (case-insensitive) and returns 1, or returns 0 when
     * the name is invalid, already registered by a plugin, or no slot is
     * free. Scripts are compiled when a map loads, so a function registered
     * while a map is running becomes callable from the next map on. A game
     * built-in of the same name takes precedence. */
    int32_t (*ScrAddFunction)(const char *name,
                              coduomp_plugin_function_t function);

    /* Script call arguments; index 0 is the first argument. A wrong type or
     * an index past the last argument raises a script error. */
    uint32_t (*Scr_GetNumParam)(void);
    int32_t (*Scr_GetInt)(uint32_t index);
    float (*Scr_GetFloat)(uint32_t index);
    const char *(*Scr_GetString)(uint32_t index);

    /* Script return value. A function returns at most one value; calling
     * none of these returns undefined. To return an array, call
     * Scr_MakeArray once, then for each element push its value with one
     * Scr_Add* call followed by Scr_AddArray (next integer index) or
     * Scr_AddArrayKey (string key from Scr_AllocString). */
    void (*Scr_AddInt)(int32_t value);
    void (*Scr_AddFloat)(float value);
    void (*Scr_AddString)(const char *value);
    void (*Scr_AddUndefined)(void);
    void (*Scr_MakeArray)(void);
    void (*Scr_AddArray)(void);
    void (*Scr_AddArrayKey)(int32_t stringIndex);
    int32_t (*Scr_AllocString)(const char *text);

    /* Script errors; see the unwinding rule above. The message is copied.
     * ParamError names the offending argument by its 0-based index. */
    void (*Scr_Error)(const char *message);
    void (*Scr_ParamError)(int32_t index, const char *message);

    /* Clients. Slots run from 0 to GetMaxClients() - 1. For a slot that is
     * out of range or not in use, IsClientConnected and GetClientGuid
     * return 0 and the buffer functions yield an empty string.
     * GetClientAddress yields "a.b.c.d:port", or "bot" for a test client.
     * DropClient disconnects the client with reason shown to it. */
    int32_t (*GetMaxClients)(void);
    int32_t (*IsClientConnected)(int32_t clientNum);
    void (*GetClientName)(int32_t clientNum, char *buffer, int32_t size);
    int32_t (*GetClientGuid)(int32_t clientNum);
    void (*GetClientAddress)(int32_t clientNum, char *buffer, int32_t size);
    void (*DropClient)(int32_t clientNum, const char *reason);
} coduomp_plugin_api_t;

/* Filled by the optional OnInfoRequest callback and shown by the `plugins`
 * console command. The server presets handlerVersion to its interface
 * version and zeroes the rest; strings must be NUL-terminated. */
typedef struct coduomp_plugin_version_s {
    int32_t major;
    int32_t minor;
} version_t;

typedef struct pluginInfo_s {
    version_t handlerVersion;
    version_t pluginVersion;
    char fullName[64];
    char shortDescription[128];
    char longDescription[1024];
} pluginInfo_t;

#if defined(_WIN32)
#define CODUOMP_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CODUOMP_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* Marks a plugin callback; expands to C linkage and export. */
#ifdef __cplusplus
#define PCL extern "C" CODUOMP_PLUGIN_EXPORT
#else
#define PCL CODUOMP_PLUGIN_EXPORT
#endif

/* The server's function table; set before any callback runs. */
extern const coduomp_plugin_api_t *coduomp_plugin_api;

/* Called by the server right after loading. Returns the interface version
 * the plugin was built against; the server unloads the plugin, calling
 * nothing else in it, unless that equals its own version. */
#define CODUOMP_PLUGIN_ENTRY()                                               \
    const coduomp_plugin_api_t *coduomp_plugin_api;                          \
    PCL int32_t coduomp_plugin_attach(const coduomp_plugin_api_t *api)       \
    {                                                                        \
        if (api != NULL && api->version == CODUOMP_PLUGIN_API_VERSION &&     \
            api->size >= sizeof(coduomp_plugin_api_t)) {                     \
            coduomp_plugin_api = api;                                        \
        }                                                                    \
        return CODUOMP_PLUGIN_API_VERSION;                                   \
    }

/* CoD4x-style names for the function table. */
#define Plugin_Printf coduomp_plugin_api->Printf
#define Plugin_DPrintf coduomp_plugin_api->DPrintf
#define Plugin_PrintError coduomp_plugin_api->PrintError
#define Plugin_Malloc coduomp_plugin_api->Malloc
#define Plugin_Free coduomp_plugin_api->Free
#define Plugin_Cvar_GetString coduomp_plugin_api->Cvar_GetString
#define Plugin_Cvar_GetInt coduomp_plugin_api->Cvar_GetInt
#define Plugin_Cvar_GetFloat coduomp_plugin_api->Cvar_GetFloat
#define Plugin_Cvar_Set coduomp_plugin_api->Cvar_Set
#define Plugin_AddCommand coduomp_plugin_api->AddCommand
#define Plugin_Cmd_Argc coduomp_plugin_api->Cmd_Argc
#define Plugin_Cmd_Argv coduomp_plugin_api->Cmd_Argv
#define Plugin_ExecuteCommand coduomp_plugin_api->ExecuteCommand
#define Plugin_ScrAddFunction coduomp_plugin_api->ScrAddFunction
#define Plugin_Scr_GetNumParam coduomp_plugin_api->Scr_GetNumParam
#define Plugin_Scr_GetInt coduomp_plugin_api->Scr_GetInt
#define Plugin_Scr_GetFloat coduomp_plugin_api->Scr_GetFloat
#define Plugin_Scr_GetString coduomp_plugin_api->Scr_GetString
#define Plugin_Scr_AddInt coduomp_plugin_api->Scr_AddInt
#define Plugin_Scr_AddFloat coduomp_plugin_api->Scr_AddFloat
#define Plugin_Scr_AddString coduomp_plugin_api->Scr_AddString
#define Plugin_Scr_AddUndefined coduomp_plugin_api->Scr_AddUndefined
#define Plugin_Scr_MakeArray coduomp_plugin_api->Scr_MakeArray
#define Plugin_Scr_AddArray coduomp_plugin_api->Scr_AddArray
#define Plugin_Scr_AddArrayKey coduomp_plugin_api->Scr_AddArrayKey
#define Plugin_Scr_AllocString coduomp_plugin_api->Scr_AllocString
#define Plugin_Scr_Error coduomp_plugin_api->Scr_Error
#define Plugin_Scr_ParamError coduomp_plugin_api->Scr_ParamError
#define Plugin_GetMaxClients coduomp_plugin_api->GetMaxClients
#define Plugin_IsClientConnected coduomp_plugin_api->IsClientConnected
#define Plugin_GetClientName coduomp_plugin_api->GetClientName
#define Plugin_GetClientGuid coduomp_plugin_api->GetClientGuid
#define Plugin_GetClientAddress coduomp_plugin_api->GetClientAddress
#define Plugin_DropClient coduomp_plugin_api->DropClient

/*
 * Optional callbacks. The server calls each one the plugin defines with PCL
 * and exactly this signature:
 *
 *   PCL void OnInfoRequest(pluginInfo_t *info);
 *       Describe the plugin. Called once, before OnInit.
 *   PCL int OnInit(void);
 *       Set up and register commands and script functions. Return 0 on
 *       success; any other value makes the server undo the plugin's
 *       registrations and unload it.
 *   PCL void OnTerminate(void);
 *       The server process is exiting.
 *   PCL void OnExitLevel(void);
 *       The game scripts of the current map are shutting down, on a map
 *       change, map restart, or server shutdown.
 *   PCL void OnFrame(void);
 *       Once per server frame while a map is running.
 *   PCL void OnClientConnected(int32_t clientNum);
 *       A client was accepted into slot clientNum.
 *   PCL void OnClientDisconnected(int32_t clientNum);
 *       The client in slot clientNum is being dropped; its name, GUID, and
 *       address are still readable.
 */

#ifdef __cplusplus
}
#endif

#endif
