# This script sets the path to the installed binaries and libraries.

PBRT_DIR=$( realpath $( dirname "${BASH_SOURCE[0]}" ))

alias pbrt="$PBRT_DIR/build/pbrt"
