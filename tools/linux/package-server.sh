#!/usr/bin/env bash

# Packages a dedicated-server engine and its game module as
# coduox-server-linux-<arch>[-auth]-<label>.tar.gz, in the layout of the
# upstream server packages: the engine at the top level and the game module
# under uo/. Archive members carry numeric owner 0 and no host account names.

set -eu

if test "$#" -ne 7; then
    echo "usage: $0 engine game output.tar.gz package-arch auth(0|1) label source-commit" >&2
    exit 2
fi

engine=$1
game=$2
output=$3
package_arch=$4
auth=$5
label=$6
source_commit=$7

for input in "$engine" "$game"; do
    if test ! -f "$input"; then
        echo "error: required package input does not exist: $input" >&2
        exit 2
    fi
done

if test -e "$output"; then
    echo "error: refusing to overwrite existing package: $output" >&2
    exit 2
fi

case "$output" in
    /*) output_absolute=$output ;;
    *) output_absolute=$(pwd)/$output ;;
esac

case "$package_arch" in
    x86_64)
        architecture='Linux ELF64 / x86-64'
        build_target=server64
        game_module=game.mp.uo.x86_64.so
        strip_tool=${LINUX64_STRIP:-strip}
        ;;
    *)
        echo "error: unsupported Linux server package architecture: $package_arch" >&2
        exit 2
        ;;
esac

case "$auth" in
    0)
        variant=
        authorization=disabled
        authorization_note='CD-key authorization is disabled, matching the default server build.'
        ;;
    1)
        variant=-auth
        authorization=enabled
        authorization_note='CD-key authorization is enabled. Set sv_cracked 1 to also admit players
whose key the authorize server denies.'
        ;;
    *)
        echo "error: auth must be 0 or 1: $auth" >&2
        exit 2
        ;;
esac

case "$label" in
    *[!A-Za-z0-9._-]* | '')
        echo "error: label may hold only letters, digits, '.', '_' and '-': $label" >&2
        exit 2
        ;;
esac

if ! command -v "$strip_tool" >/dev/null 2>&1; then
    echo "error: Linux strip tool not found: $strip_tool" >&2
    exit 2
fi

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
project_root=$(cd "$script_dir/../.." && pwd -P)
output_directory=$(dirname "$output_absolute")
mkdir -p "$output_directory"

package_name="coduox-server-linux-$package_arch$variant-$label"
staging_parent=$(mktemp -d "${TMPDIR:-/tmp}/coduox-server-package.XXXXXX")
trap 'rm -r "$staging_parent"' EXIT HUP INT TERM
package_root="$staging_parent/$package_name"

mkdir -p "$package_root/uo"
cp "$engine" "$package_root/coduo_lnxded_recovered"
cp "$game" "$package_root/uo/$game_module"
chmod 755 "$package_root/coduo_lnxded_recovered" "$package_root/uo/$game_module"

"$strip_tool" --strip-all \
    "$package_root/coduo_lnxded_recovered" \
    "$package_root/uo/$game_module"

{
    printf '%s\n' "coduox dedicated server linux-$package_arch build"
    printf '\n%s\n' 'Copy the contents into the root of an existing Call of Duty: United'
    printf '%s\n' 'Offensive installation. Preserve the uo/ directory and keep the server'
    printf '%s\n' 'executable with its matching game module. Retail game data is not included.'
    printf '\n%s\n' "$authorization_note"
    printf '%s\n' 'Supply your server configuration and map commands when launching the executable.'
    printf '\n%s\n' 'Plugins go in a plugins/ directory in the installation root and are loaded'
    printf '%s\n' 'with the loadplugin console command.'
    printf '\n%s\n' 'coduox is a fork of https://github.com/opencoduo/coduomp'
    printf '\n%s\n' 'Packaged components:'
    printf '%s\n' '  coduo_lnxded_recovered'
    printf '%s\n' "  uo/$game_module"
} > "$package_root/README.txt"

{
    printf 'source_commit=%s\n' "$source_commit"
    printf 'architecture=%s\n' "$architecture"
    printf 'build_target=%s\n' "$build_target"
    printf 'authorization=%s\n' "$authorization"
} > "$package_root/BUILDINFO.txt"

"$project_root/tools/audit-package.sh" "$package_root"

(
    cd "$package_root"
    sha256sum coduo_lnxded_recovered "uo/$game_module" > SHA256SUMS.txt
)

(
    cd "$staging_parent"
    tar --owner=0 --group=0 --numeric-owner --sort=name \
        -czf "$output_absolute" "$package_name"
)

printf '%s\n' "$output_absolute"
