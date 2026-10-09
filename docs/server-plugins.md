# Server plugins

A plugin is a shared library that adds console commands and script functions
to a running server without rebuilding it. Plugins are loaded on demand; the
server loads none by itself.

## Loading

Put the plugin in a `plugins` folder inside the server's `fs_homepath` or
`fs_basepath` folder, then load it from the server console, the command
line, or a config file:

```text
loadplugin mysql
```

That loads `plugins/mysql.so` (`plugins/mysql.dll` on Windows). The name is
given without a path or file extension and may hold only letters, digits,
`_`, and `-`, so nothing outside the `plugins` folder can be loaded.

`plugins` lists the loaded plugins with their versions and the number of
script functions and commands they added.

Load a plugin that adds script functions before the first map starts, for
example with `+loadplugin mysql` on the command line or a `loadplugin` line
in the server config. Scripts are compiled when a map loads, so functions
from a plugin loaded while a map is running become callable from the next
map on.

A loaded plugin stays loaded until the server exits. Up to 16 plugins can be
loaded.

A plugin is native code with the same rights as the server itself. Install
only plugins you trust. Any console command source can run `loadplugin`,
including the `execCommand` script function, but only for a file that is
already in the `plugins` folder.

The server refuses a file that is not a plugin, a plugin built for a
different plugin interface version, and a plugin whose setup fails; in the
last case everything it registered is removed again.

## Available plugins

| Plugin | What it adds |
|---|---|
| [coduox-mysql](https://github.com/CyBeR1337x/coduox-mysql) | MySQL and MariaDB access for game scripts: `mysql_real_connect`, `mysql_query`, `mysql_fetch_row`, `mysql_fetch_rows`, `mysql_real_escape_string`, and four more. A port of the [CoD4X MySQL plugin](https://github.com/callofduty4x/mysql). |

A built `mysql.so` for 64-bit Linux is on that project's
[releases page](https://github.com/CyBeR1337x/coduox-mysql/releases). The
server machine needs the MariaDB client library, for example
`apt install libmariadb3`.

## Writing a plugin

A plugin compiles against one header, `src/plugin/coduomp_plugin.h`, which
documents every function and callback. Plugins do not link against the
server and never see its data structures.

```c
#include "coduomp_plugin.h"

#include <string.h>

CODUOMP_PLUGIN_ENTRY()

static void Scr_Double(void)
{
    if (Plugin_Scr_GetNumParam() != 1) {
        Plugin_Scr_Error("Usage: double(<integer>)");
    }
    Plugin_Scr_AddInt(Plugin_Scr_GetInt(0) * 2);
}

static void Hello_f(void)
{
    Plugin_Printf("hello %s\n", Plugin_Cmd_Argv(1));
}

PCL void OnInfoRequest(pluginInfo_t *info)
{
    info->pluginVersion.major = 1;
    info->pluginVersion.minor = 0;
    strcpy(info->fullName, "Example plugin");
}

PCL int OnInit(void)
{
    Plugin_ScrAddFunction("double", Scr_Double);
    Plugin_AddCommand("hello", Hello_f);
    return 0;
}
```

Build it as a shared library for the same architecture as the server:

```sh
cc -fPIC -shared -Ipath/to/src/plugin example.c -o plugins/example.so
```

C++ works the same way with `g++`.

### Callbacks

Every callback is optional. Define the ones you need with `PCL`:

| Callback | Called when |
|---|---|
| `void OnInfoRequest(pluginInfo_t *info)` | Once, before `OnInit`, to describe the plugin |
| `int OnInit(void)` | The plugin is loaded; return `0` on success |
| `void OnTerminate(void)` | The server exits through the `quit` command |
| `void OnExitLevel(void)` | The map's scripts end: map change, restart, or shutdown |
| `void OnFrame(void)` | Once per server frame while a map is running |
| `void OnClientConnected(int32_t clientNum)` | A client was accepted into a slot |
| `void OnClientDisconnected(int32_t clientNum)` | A client is being dropped |

### Server functions

The `Plugin_*` names follow the [CoD4x](https://github.com/callofduty4x/CoD4x_Server) plugin interface where the two
overlap, so code written for it needs few changes:

| Area | Functions |
|---|---|
| Output | `Plugin_Printf`, `Plugin_DPrintf`, `Plugin_PrintError` |
| Memory | `Plugin_Malloc`, `Plugin_Free` |
| Cvars | `Plugin_Cvar_GetString`, `Plugin_Cvar_GetInt`, `Plugin_Cvar_GetFloat`, `Plugin_Cvar_Set` |
| Commands | `Plugin_AddCommand`, `Plugin_Cmd_Argc`, `Plugin_Cmd_Argv`, `Plugin_ExecuteCommand` |
| Script functions | `Plugin_ScrAddFunction` |
| Script arguments | `Plugin_Scr_GetNumParam`, `Plugin_Scr_GetInt`, `Plugin_Scr_GetFloat`, `Plugin_Scr_GetString` |
| Script results | `Plugin_Scr_AddInt`, `Plugin_Scr_AddFloat`, `Plugin_Scr_AddString`, `Plugin_Scr_AddUndefined`, `Plugin_Scr_MakeArray`, `Plugin_Scr_AddArray`, `Plugin_Scr_AddArrayKey`, `Plugin_Scr_AllocString` |
| Script errors | `Plugin_Scr_Error`, `Plugin_Scr_ParamError` |
| Clients | `Plugin_GetMaxClients`, `Plugin_IsClientConnected`, `Plugin_GetClientName`, `Plugin_GetClientGuid`, `Plugin_GetClientAddress`, `Plugin_DropClient` |

### Rules

- Everything runs on the server's main thread. Do not call server functions
  from another thread, and keep callbacks short: a slow callback stalls the
  game for every player.
- A string returned by the server is valid only until the next server
  function call. Copy it to keep it.
- `Plugin_Scr_Error` and `Plugin_Scr_ParamError` do not return. Release
  anything you hold before calling them; C++ objects on the stack are
  destroyed normally.
- The `Plugin_Scr_*` functions work only inside a script function while a
  script is calling it.
- A plugin cannot replace an existing console command, and a game script
  function of the same name takes precedence over a plugin's.
- Plugins built for CoD4x do not load as they are. They have to be rebuilt
  from source against this header, and anything they use beyond the
  functions above has to be changed.
