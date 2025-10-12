#!/bin/bash
set -e

# Dependency build script for OrderBook Simulator
# This script downloads, builds, and installs all external dependencies
# to ~/.cache/orderbook-deps so they persist across build directory deletions

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CACHE_DIR="$HOME/.cache/orderbook-deps"
DOWNLOAD_DIR="$CACHE_DIR/downloads"
BUILD_DIR="$CACHE_DIR/build"
INSTALL_DIR="$CACHE_DIR/install"

# Dependency versions
NLOHMANN_JSON_VERSION="3.11.3"
OPENSSL_VERSION="3.0.15"
CURL_VERSION="8.4.0"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}OrderBook Simulator - Dependency Builder${NC}"
echo "================================================"
echo ""

# Check for required system dependencies
echo "Checking system dependencies..."

# Check for unzip
if ! command -v unzip &> /dev/null; then
    echo -e "${YELLOW}WARNING: unzip not found${NC}"
    echo "Please install unzip: sudo apt-get install unzip"
    exit 1
fi
echo "✓ unzip found"

# Check for perl (needed for OpenSSL build)
if ! command -v perl &> /dev/null; then
    echo -e "${YELLOW}WARNING: perl not found${NC}"
    echo "Please install perl: sudo apt-get install perl"
    exit 1
fi
echo "✓ perl found"

echo ""

# Create cache directories
mkdir -p "$DOWNLOAD_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$INSTALL_DIR"

# Check if dependencies are already built
if [ -f "$INSTALL_DIR/.deps_built" ]; then
    echo -e "${YELLOW}Dependencies already built in $INSTALL_DIR${NC}"
    echo "To rebuild, run: rm -rf $CACHE_DIR"
    echo ""
    echo "Export path for CMake:"
    echo "export ORDERBOOK_DEPS_DIR=\"$INSTALL_DIR\""
    exit 0
fi

echo "Cache directory: $CACHE_DIR"
echo "Install directory: $INSTALL_DIR"
echo ""

# Function to download file if not exists
download_if_needed() {
    local url=$1
    local filename=$2

    if [ -f "$DOWNLOAD_DIR/$filename" ]; then
        # Check if file is valid (not empty or too small)
        local filesize=$(stat -c%s "$DOWNLOAD_DIR/$filename" 2>/dev/null || stat -f%z "$DOWNLOAD_DIR/$filename" 2>/dev/null)
        if [ "$filesize" -gt 1000 ]; then
            echo -e "${YELLOW}Using cached $filename${NC}"
            return 0
        else
            echo "Cached file is invalid, re-downloading..."
            rm -f "$DOWNLOAD_DIR/$filename"
        fi
    fi

    echo "Downloading $filename..."
    curl -L -f -o "$DOWNLOAD_DIR/$filename" "$url"
    if [ $? -ne 0 ]; then
        echo "Download failed!"
        rm -f "$DOWNLOAD_DIR/$filename"
        return 1
    fi
}

#
# 1. Build nlohmann/json (header-only library)
#
echo -e "${GREEN}Building nlohmann/json ${NLOHMANN_JSON_VERSION}${NC}"
echo "------------------------------------------------"

# Use the include.zip which contains just the headers
JSON_ZIP="include.zip"
JSON_URL="https://github.com/nlohmann/json/releases/download/v${NLOHMANN_JSON_VERSION}/include.zip"

download_if_needed "$JSON_URL" "json-${NLOHMANN_JSON_VERSION}-${JSON_ZIP}"

# Extract and install (header-only, just copy headers)
echo "Installing nlohmann/json headers..."
cd "$BUILD_DIR"
rm -rf json-build
mkdir -p json-build
cd json-build
unzip -q "$DOWNLOAD_DIR/json-${NLOHMANN_JSON_VERSION}-${JSON_ZIP}"
mkdir -p "$INSTALL_DIR/include"
cp -r include/nlohmann "$INSTALL_DIR/include/"
echo -e "${GREEN}✓ nlohmann/json installed${NC}"
echo ""

#
# 2. Build OpenSSL
#
echo -e "${GREEN}Building OpenSSL ${OPENSSL_VERSION}${NC}"
echo "------------------------------------------------"

OPENSSL_TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
OPENSSL_URL="https://www.openssl.org/source/${OPENSSL_TARBALL}"

download_if_needed "$OPENSSL_URL" "$OPENSSL_TARBALL"

# Extract and build
echo "Building OpenSSL (this may take several minutes)..."
cd "$BUILD_DIR"
rm -rf openssl-build
mkdir -p openssl-build
cd openssl-build
tar -xzf "$DOWNLOAD_DIR/$OPENSSL_TARBALL"
cd "openssl-${OPENSSL_VERSION}"

# Configure OpenSSL
./config \
    --prefix="$INSTALL_DIR" \
    --openssldir="$INSTALL_DIR/ssl" \
    no-shared \
    no-tests \
    2>&1 | tee configure.log

# Build and install
make -j$(nproc) 2>&1 | tee build.log
make install_sw 2>&1 | tee install.log

echo -e "${GREEN}✓ OpenSSL installed${NC}"
echo ""

#
# 3. Build libcurl
#
echo -e "${GREEN}Building libcurl ${CURL_VERSION}${NC}"
echo "------------------------------------------------"

CURL_TARBALL="curl-${CURL_VERSION}.tar.gz"
CURL_URL="https://github.com/curl/curl/releases/download/curl-${CURL_VERSION//./_}/${CURL_TARBALL}"

download_if_needed "$CURL_URL" "$CURL_TARBALL"

# Extract and build
echo "Building libcurl (this may take a few minutes)..."
cd "$BUILD_DIR"
rm -rf curl-build
mkdir -p curl-build
cd curl-build
tar -xzf "$DOWNLOAD_DIR/$CURL_TARBALL"
cd "curl-${CURL_VERSION}"

# Configure curl with minimal options (HTTP only, with locally-built OpenSSL)
PKG_CONFIG_PATH="$INSTALL_DIR/lib64/pkgconfig:$INSTALL_DIR/lib/pkgconfig:$PKG_CONFIG_PATH" \
./configure \
    --prefix="$INSTALL_DIR" \
    --enable-static \
    --disable-shared \
    --without-zlib \
    --without-brotli \
    --without-zstd \
    --without-libpsl \
    --without-libssh2 \
    --without-librtmp \
    --without-nghttp2 \
    --without-ngtcp2 \
    --without-nghttp3 \
    --without-quiche \
    --disable-ldap \
    --disable-ldaps \
    --disable-rtsp \
    --disable-dict \
    --disable-telnet \
    --disable-tftp \
    --disable-pop3 \
    --disable-imap \
    --disable-smb \
    --disable-smtp \
    --disable-gopher \
    --disable-mqtt \
    --disable-manual \
    --disable-docs \
    --with-openssl="$INSTALL_DIR" \
    --enable-optimize \
    --disable-debug \
    2>&1 | tee configure.log

# Build and install
make -j$(nproc) 2>&1 | tee build.log
make install 2>&1 | tee install.log

echo -e "${GREEN}✓ libcurl installed${NC}"
echo ""

#
# Create marker file to indicate successful build
#
echo "$(date)" > "$INSTALL_DIR/.deps_built"
echo "nlohmann_json=${NLOHMANN_JSON_VERSION}" >> "$INSTALL_DIR/.deps_built"
echo "openssl=${OPENSSL_VERSION}" >> "$INSTALL_DIR/.deps_built"
echo "curl=${CURL_VERSION}" >> "$INSTALL_DIR/.deps_built"

echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}All dependencies built successfully!${NC}"
echo "================================================"
echo ""
echo "Install location: $INSTALL_DIR"
echo ""
echo "To use these dependencies, set the environment variable:"
echo -e "${YELLOW}export ORDERBOOK_DEPS_DIR=\"$INSTALL_DIR\"${NC}"
echo ""
echo "Then run CMake as usual:"
echo "  cmake -B build -S ."
echo "  cmake --build build"
echo ""
