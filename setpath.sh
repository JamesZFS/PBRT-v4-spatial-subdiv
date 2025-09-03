# This script sets the path to the installed binaries and libraries.

PBRT_DIR=$( realpath $( dirname "${BASH_SOURCE[0]}" ))

if [[ "$(uname)" == 'Darwin' ]]; then
	export PATH="$PBRT_DIR/cmake-build-release:$PATH"
else
	export LD_LIBRARY_PATH="$PBRT_DIR/cmake-build-release"
	export PATH="$PBRT_DIR/cmake-build-release:$PATH"
fi
