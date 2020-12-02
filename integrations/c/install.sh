#!/bin/sh

set -o xtrace
set -o errexit

# Helper function to locate cmake.
find_cmake ()
{
  if [ ! -z "$CMAKE" ]; then
    return 0
  elif [ -f "/Applications/cmake-3.2.2-Darwin-x86_64/CMake.app/Contents/bin/cmake" ]; then
    CMAKE="/Applications/cmake-3.2.2-Darwin-x86_64/CMake.app/Contents/bin/cmake"
  elif [ -f "/Applications/Cmake.app/Contents/bin/cmake" ]; then
    CMAKE="/Applications/Cmake.app/Contents/bin/cmake"
  elif [ -f "/opt/cmake/bin/cmake" ]; then
    CMAKE="/opt/cmake/bin/cmake"
  elif command -v cmake 2>/dev/null; then
     CMAKE=cmake
  elif uname -a | grep -iq 'x86_64 GNU/Linux'; then
     if [ -f "$(pwd)/cmake-3.11.0/bin/cmake" ]; then
      CMAKE="$(pwd)/cmake-3.11.0/bin/cmake"
      return 0
     fi
     curl --retry 5 https://cmake.org/files/v3.11/cmake-3.11.0-Linux-x86_64.tar.gz -sS --max-time 120 --fail --output cmake.tar.gz
     mkdir cmake-3.11.0
     tar xzf cmake.tar.gz -C cmake-3.11.0 --strip-components=1
     CMAKE=$(pwd)/cmake-3.11.0/bin/cmake
  elif [ -f "/cygdrive/c/cmake/bin/cmake" ]; then
     CMAKE="/cygdrive/c/cmake/bin/cmake"
  fi
  if [ -z "$CMAKE" -o -z "$( $CMAKE --version 2>/dev/null )" ]; then
     # Some images have no cmake yet, or a broken cmake (see: BUILD-8570)
     echo "-- MAKE CMAKE --"
     CMAKE_INSTALL_DIR=$(readlink -f cmake-install)
     curl --retry 5 https://cmake.org/files/v3.11/cmake-3.11.0.tar.gz -sS --max-time 120 --fail --output cmake.tar.gz
     tar xzf cmake.tar.gz
     cd cmake-3.11.0
     ./bootstrap --prefix="${CMAKE_INSTALL_DIR}"
     make -j8
     make install
     cd ..
     CMAKE="${CMAKE_INSTALL_DIR}/bin/cmake"
     echo "-- DONE MAKING CMAKE --"
  fi
}

# Install the C driver.
cd mongo-c-driver || exit

FLAGS="-DCMAKE_BUILD_TYPE=Debug"

# TODO: install on UNIX differently from Windows.
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
case "$OS" in
    cygwin*)
	# Windows
	CMAKE="/cygdrive/c/cmake/bin/cmake"
	"$CMAKE" -G "$CC" "-DCMAKE_PREFIX_PATH=${INSTALL_DIR}/lib/cmake" $FLAGS
	"$CMAKE" --build . --target ALL_BUILD
	"$CMAKE" --build . --target INSTALL
	;;
    
    *)
	# UNIX
	find_cmake

	mkdir cmake_build
	cd cmake_build

	cmake $FLAGS ../

	make -j 8 all
	make install
	;;
esac
