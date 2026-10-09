#ifndef CODUOMP_PLUGINS_H
#define CODUOMP_PLUGINS_H

#include <stdint.h>

/* NOT_FROM_ORIGINAL_SOURCE: coduox: server plugin host. Plugins are shared
 * libraries built against plugin/coduomp_plugin.h and loaded on demand by the
 * console command `loadplugin <name>`. Every function here runs on the
 * server's main thread, and every function except the two command handlers is
 * a no-op when no plugin is loaded. */

/* Console commands: `loadplugin <name>` and `plugins`. */
void coduomp_plugins_load_f(void);
void coduomp_plugins_list_f(void);

/* Returns the plugin script function registered under name, or NULL. On a
 * match *name is replaced by storage that lives as long as the process,
 * which the script compiler keeps. name must already be lower-case, as the
 * compiler supplies it. */
void (*coduomp_plugins_script_function(const char **name))(void);

/* Event notifications; each calls the matching optional plugin callback in
 * load order. */
void coduomp_plugins_on_frame(void);
void coduomp_plugins_on_exit_level(void);
void coduomp_plugins_on_client_connected(int32_t clientNum);
void coduomp_plugins_on_client_disconnected(int32_t clientNum);

/* The server process is exiting: calls each plugin's optional OnTerminate
 * once, however often this is reached. */
void coduomp_plugins_terminate(void);

#endif
