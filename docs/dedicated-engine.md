# CoD:UO Dedicated Engine

This directory contains maintained source for the Call of Duty: United Offensive
dedicated-server engine. It covers server-side runtime systems such as
commands, cvars, filesystem access, networking, collision, animation/model
services, scripting support, and game-module host calls.

The exported tree is organized as normal buildable C/C++ source:

- `src/server/standalone/`: process and dedicated-platform code.
- `src/server/engine/`: server engine code shared with an embedded listen
  server.
- common subsystems live directly below `src/`.
- `build-mk/server.mk`: compile and link targets for the maintained engine.

## Build

```sh
make -f build-mk/server.mk objects CODUO_FP_FAITHFUL=relaxed
make -f build-mk/server.mk shared-check-link CODUO_FP_FAITHFUL=relaxed
make windows-i386 WINDOWS_DEP_PREFIX=/opt/mingw-i686
make windows-i686 WINDOWS_DEP_PREFIX=/opt/mingw-i686
```

The default `check` target compiles the maintained source into a static archive.
The shared link check permits unresolved engine-internal symbols while more
subsystems are completed.

The original Linux server is an i386 program. Build targets that use `-m32`
require a Linux toolchain with 32-bit compiler and library support installed.

The Windows targets use 32-bit MinGW and produce the engine under
`build/server/windows-i386/` or `build/server/windows-i686/`. They require a matching
MinGW zlib; use `WINDOWS_DEP_PREFIX` when it is outside the compiler's default
search path. The executable is a console application and loads
`uo/uo_game_mp_x86.dll`.

## Game-type voting

The improved game module accepts a whitespace-separated `g_voteGameTypes`
allowlist. For example:

```text
set g_voteGameTypes "tdm dm ctf dom"
```

The list applies to both `callvote g_gametype` and `callvote typemap`, with
case-insensitive, whole-name matching. An empty value (the default) allows
every installed game type. Changes take effect without restarting the map.
The ordinary `g_allowVoteGameType` and `g_allowVoteTypeMap` switches still
control whether those vote commands are enabled. Direct administrator
commands and configured rotations are unaffected. Client menus can still
display installed game types that the server will reject for voting.

## Named test clients

The script function `addtestclient()` accepts an optional name:

```text
bot = addtestclient();            // generated name: bot0, bot1, ...
bot = addtestclient("Sgt. Rock"); // connects with the supplied name
```

Both forms return the new player entity, or `undefined` when every client
slot is in use. An empty string is a script error. Control characters,
backslashes, double quotes, and semicolons are removed from the name, and it
is limited to 31 bytes; a name with nothing left after that uses the
generated name. The game then cleans the name the same way it cleans player
names, so color codes are kept.

The named form needs both this project's engine (dedicated or listen server)
and its game module. A stock engine rejects the named call and drops the map
with a "Bad game system trap" error. Unnamed calls work with either engine.

Every bot is removed when a map loads or restarts, including through `map`,
`map_rotate`, and `map_restart`, and its slot is freed straight away.
Gametype scripts add their bots again each time. The server console reports
each removed bot and the total.

## Script files

The game module provides file functions with the same names and results as
the [CoD4x](https://github.com/callofduty4x/CoD4x_Server) `fs_*` script functions:

| Function | Result |
| --- | --- |
| `fs_testfile(path)` | `true` if the file can be opened for reading |
| `fs_fopen(path, mode)` | mode `"read"`, `"write"` (replace), or `"append"`; a handle from 1 to 10, or `0` on failure |
| `fs_readline(handle)` | the next line without its line ending, or `undefined` at the end of the file or for a handle not open for reading |
| `fs_writeline(handle, text)` | writes `text` and a line ending; `true` if the handle is open for writing, otherwise `false` |
| `fs_remove(path)` | `true` if the file was deleted |
| `fs_fclose(handle)` | closes the handle |
| `fs_fcloseall()` | closes every open script file |

```text
f = fs_fopen("bots.txt", "read");
if (f)
{
    for (line = fs_readline(f); isdefined(line); line = fs_readline(f))
        addtestclient(line);
    fs_fclose(f);
}

f = fs_fopen("logs/kills.txt", "append");
fs_writeline(f, attacker.name + " killed " + self.name);
fs_fclose(f);
```

Reads use the game's normal search paths, including the active mod folder
and `.pk3` files. Writing, appending, and deleting use the server's writable
folder: `<fs_homepath>/<mod or main>/`. Missing folders are created when a
file is written. Paths containing `..` or `::` are rejected, and `.pk3`,
`.so`, `.dll`, `.dylib`, and `.exe` files cannot be written or deleted.
Deleting a file a script still has open is a script error.

Both `\n` and `\r\n` line endings are accepted when reading. A line longer
than 8191 bytes is returned in pieces by successive `fs_readline` calls. At
most ten files can be open at once, and every open file is closed when the
map ends or restarts. `fs_remove` needs this project's engine; a stock engine
drops the map with a "Bad game system trap" error.

## Player data

Player methods store values for a player on the server:

```text
kills = self getKey("kills");
if (!isdefined(kills))
    kills = 0;
self setKey("kills", kills + 1);
self setKey("rank", "Sergeant");
self deleteKey("rank");
```

| Method | Result |
| --- | --- |
| `self setKey(key, value)` | stores an integer, float, or string |
| `self getKey(key)` | the stored value with its original type, or `undefined` |
| `self deleteKey(key)` | removes the key |

Keys are 1 to 32 letters, digits, `_`, `.`, or `-`, and are case-sensitive.
String values are limited to 256 bytes and cannot contain line breaks. A
player can hold up to 128 keys. Breaking these rules is a script error.

Values for a player with a GUID are saved to
`<fs_homepath>/<mod or main>/playerdata/<guid>.txt` as soon as they change,
and are loaded again on the player's next visit. Players with GUID `0`, which
includes bots and players on servers without CD-key checks, keep values only
until they disconnect or the map changes. See [Local test GUIDs](#local-test-guids)
to test saved values on a LAN. If the same GUID is connected twice, the last
change from either copy is the one saved.

## Script kick

`kick(clientnum, reason)` drops the client in that slot, like the [CoD4x](https://github.com/callofduty4x/CoD4x_Server) script
function of the same name. The reason is optional and defaults to the
localized "was kicked" message used by the console `kick` command:

```text
kick(bot getEntityNumber());
kick(3, "EXE_PLAYERKICKED");
```

A client number outside `0` to `sv_maxclients - 1` is a script error. An
empty slot is ignored, and the host of a listen server cannot be kicked.

A kicked bot's slot becomes free again after `sv_zombietime`, like a
player's, so `addtestclient` can reuse it. This also applies to bots removed
with the console `kick` and `clientkick` commands.

## Script console commands

`execCommand(command)` runs a server console command from a script:

```text
execCommand("say Round starts in 10 seconds");
execCommand("tell " + player getEntityNumber() + " Welcome back");
execCommand("set scr_friendlyfire 1; map_restart");
```

The command runs with full console rights after the current server frame,
and `;` separates several commands as it does on the console. Its output goes
to the server console and is not returned to the script. An empty command, a
command containing a line break, or one longer than 1022 characters is a
script error.

The text is passed to the console unchanged. Do not build it from a player's
name or chat text without first removing `;`, `"`, and line breaks, or that
player can run console commands of their own.

## Player view angles

`self getPlayerAngles()` returns the direction a player is looking as a
vector of pitch, yaw, and roll in degrees, like the [CoD4x](https://github.com/callofduty4x/CoD4x_Server) player method of
the same name. It is the read counterpart of `setPlayerAngles`:

```text
angles = player getPlayerAngles();
forward = anglesToForward(angles);
```

## Player country lookup

`self getGeoLocation(type)` reports the country a player's IPv4 address is
registered in, like the [CoD4x](https://github.com/callofduty4x/CoD4x_Server) player method of the same name:

| `type` | Result | Example |
|---|---|---|
| `0` | Two-letter country code | `DE` |
| `1` | Three-letter country code | `DEU` |
| `2` | Country name | `Germany` |
| `3` | Two-letter continent code | `EU` |
| other | Country index as an integer | `56` |

```text
iprintln(player.name + " connected from " + player getGeoLocation(2));
```

The server needs a `GeoIP.dat` file in the GeoIP legacy country format,
placed in `main` or in the active `fs_game` folder. The file is not included.
It is read on each call, so a replaced file takes effect without a restart.
City, IPv6, and other editions are not supported.

When the country is unknown, the results are `--`, `--`, `N/A`, `--`, and
index `0`. That covers a missing or unsupported file, bots, and addresses
the file does not list, which includes LAN and `127.0.0.1` addresses. The
server console reports a missing or unsupported file once.

## Local test GUIDs

Players on a LAN or on the same machine never receive a CD-key GUID, so their
GUID is `0`. For local testing, set:

```text
set sv_localGuid 1
```

Each LAN or local player without a GUID then receives a fixed GUID made from
their name with color codes removed. The same name always produces the same
GUID, so a test player keeps it across reconnects and server restarts, and
using several names lets one machine test as several players. The server
console prints `Assigned local GUID <guid> to <name>`, and `status` shows it.

The GUID is chosen when the player connects; renaming later does not change
it. Bots and internet players are unaffected, and anyone on the LAN can
take another player's GUID by using their name, so leave this at `0` (the
default) on public servers.

## IP-based GUIDs

Players without a CD-key GUID, such as everyone on a server built without
authorization or players admitted by `sv_cracked`, have GUID `0`. To give
each of them a fixed GUID made from their IPv4 address, set:

```text
set sv_ipGuid 1
```

The server console prints `Assigned IP GUID <guid> to <name>`, and `status`
shows it. GUID bans and saved per-player values then work for these players.
Players with a CD-key GUID keep it. LAN and local players get a name-based
GUID when `sv_localGuid` is `1`; otherwise they get an IP-based GUID too, so
every player on the same machine, such as `127.0.0.1`, shares one.

The GUID follows the address, not the player: it changes when a player's IP
address changes, and players sharing one public address, such as a household
or an internet café, share one GUID. A GUID can be traced back to the address
it came from. The setting takes effect on the next connection and defaults
to `0`.

## Clients without a valid CD key

A server built with CD-key authorization rejects players whose key the
authorize server denies. To let them join anyway, set:

```text
set sv_cracked 1
```

Players with a valid key are unaffected and keep their GUID. A denied player
is admitted with GUID `0`, the same as a LAN player, so GUID bans and saved
per-player values do not apply to them unless `sv_ipGuid` gives them one; IP
bans still do. The server console prints `admitting client denied by
authorize server (<reason>): sv_cracked is set` for each one. The setting
takes effect on the next connection attempt and defaults to `0`.

Servers built without CD-key authorization already admit every player, so
`sv_cracked` changes nothing there.

## Server browser colors

The game type and map name shown in server browsers can be colored, using
the same settings as the [iw1x](https://github.com/cod1raph/iw1x-server-r) server:

```text
set g_gametype_color "2"
set mapname_color "5"
```

Each setting takes one digit from `0` to `7`, the standard color codes
(`1` red, `2` green, `3` yellow, `4` blue, `5` cyan, `6` magenta, `7` white,
`0` black). It adds that color code to the start of the value the server
sends to browsers. Empty, the default, leaves the value unchanged, as does
any other value.

Server browsers that filter by game type compare the exact text, so a
colored game type such as `^2tdm` does not match a `tdm` filter, and the
server is hidden from players using that filter. The browser stores at most
15 characters of the game type, including the two-character color code.
