#!/bin/bash
# Download Bazel dependency archives for offline builds.
#
# Usage:
#   bash tools/download_bazel_deps.sh
#
# What this script does:
#   1. Downloads required Bazel dependency archives to thirdparty/runtime_deps/
#   2. Verifies sha256 checksums
#   3. Populates Bazel repository_cache so builds work fully offline
#      (only when BAZEL_REPO_CACHE dir is writable, e.g. inside compile container)
#
# After running this script, `bazel build` works without any network access.

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT="${SCRIPT_DIR}/.."
DISTDIR="${REPO_ROOT}/thirdparty/runtime_deps"
REPO_CACHE="${BAZEL_REPO_CACHE:-/root/.cache/bazel/_bazel_root/cache/repos/v1/content_addressable/sha256}"

# Primary source: openEuler build cache (same server as yuanrong project)
OPENEULER_BASE="${OPENEULER_CACHE:-https://build-logs.openeuler.openatom.cn:38080/temp-archived/openeuler/openYuanrong/runtime_deps}"

mkdir -p "${DISTDIR}"

# download_one <filename> <expected_sha256> <fallback_url>
download_one() {
    local filename="$1"
    local expected_sha="$2"
    local fallback="$3"
    local dest="${DISTDIR}/${filename}"

    # Already exists with correct checksum — skip
    if [ -f "${dest}" ]; then
        actual_sha=$(sha256sum "${dest}" | awk '{print $1}')
        if [ "${actual_sha}" = "${expected_sha}" ]; then
            echo "[skip] ${filename}"
            return 0
        fi
        echo "[warn] ${filename}: checksum mismatch, re-downloading..."
        rm -f "${dest}"
    fi

    # Try openEuler mirror first
    echo "[download] ${filename} ..."
    if wget -q --timeout=30 -O "${dest}.tmp" "${OPENEULER_BASE}/${filename}" 2>/dev/null; then
        actual_sha=$(sha256sum "${dest}.tmp" | awk '{print $1}')
        if [ "${actual_sha}" = "${expected_sha}" ]; then
            mv "${dest}.tmp" "${dest}"
            echo "[ok] ${filename} (openEuler)"
            return 0
        fi
        echo "[warn] ${filename}: openEuler checksum mismatch, trying fallback..."
        rm -f "${dest}.tmp"
    fi

    # Fallback URL
    if [ -n "${fallback}" ]; then
        echo "[download] ${filename} from ${fallback} ..."
        if wget -q --timeout=60 -O "${dest}.tmp" "${fallback}" 2>/dev/null; then
            actual_sha=$(sha256sum "${dest}.tmp" | awk '{print $1}')
            if [ "${actual_sha}" = "${expected_sha}" ]; then
                mv "${dest}.tmp" "${dest}"
                echo "[ok] ${filename} (fallback)"
                return 0
            fi
            echo "[error] ${filename}: fallback checksum mismatch (got ${actual_sha}, expected ${expected_sha})"
            rm -f "${dest}.tmp"
        else
            echo "[error] ${filename}: fallback download failed"
        fi
    fi

    echo "[error] Failed to download ${filename}"
    return 1
}

# Populate Bazel repository_cache with distdir files (sha256-addressed)
populate_repo_cache() {
    if [ ! -w "$(dirname "${REPO_CACHE}")" ] && [ ! -d "${REPO_CACHE}" ]; then
        echo "[skip] repository_cache not writable (${REPO_CACHE}), skipping population."
        echo "       Run this script inside the compile container to populate the cache."
        return 0
    fi
    echo ""
    echo "Populating Bazel repository_cache: ${REPO_CACHE}"
    local count=0
    for f in "${DISTDIR}"/*.zip "${DISTDIR}"/*.tar.gz; do
        [ -f "$f" ] || continue
        local sha
        sha=$(sha256sum "$f" | awk '{print $1}')
        local cache_entry="${REPO_CACHE}/${sha}"
        if [ ! -f "${cache_entry}/file" ]; then
            mkdir -p "${cache_entry}"
            cp "$f" "${cache_entry}/file"
            count=$((count + 1))
            echo "  cached: $(basename $f)"
        fi
    done
    echo "  Added ${count} new entries to repository_cache."
}

echo "=== Downloading Bazel dependency archives ==="
echo "Distdir: ${DISTDIR}"
echo ""

failed=0

# Each entry: download_one <filename> <sha256> <fallback_url>
download_one "20240722.0.zip" \
    "104dead3edd7b67ddeb70c37578245130d6118efad5dad4b618d7e26a5331f55" \
    "https://gitee.com/mirrors/abseil-cpp/repository/archive/20240722.0.zip" \
    || failed=$((failed+1))

download_one "v3.25.5.zip" \
    "747e7477cd959878998145626b49d6f1b9d46065f2fe805622ff5702334f7cb7" \
    "https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.25.5.zip" \
    || failed=$((failed+1))

download_one "d863bc33e15cba6d873c878dcca9e6fe52b2f8cb.zip" \
    "568988b5f7261ca181468dba38849fabf59dd9200fb2ed4b2823da187ef84d8c" \
    "https://github.com/protocolbuffers/utf8_range/archive/d863bc33e15cba6d873c878dcca9e6fe52b2f8cb.zip" \
    || failed=$((failed+1))

download_one "v1.3.1.zip" \
    "7c31009abc4e76ddc32e1448b6051bafe5f606aac158bb36166100a21ec170c6" \
    "https://gitee.com/mirrors/zlib/repository/archive/v1.3.1.zip" \
    || failed=$((failed+1))

download_one "2024-02-01.zip" \
    "54bff0e995b101e1865dcea5d052ec10b3aadb6f8c57b5c03c9eeccddb00a08a" \
    "https://gitee.com/mirrors/re2/repository/archive/2024-02-01.zip" \
    || failed=$((failed+1))

download_one "541b1ded4abadcc38e8178680b0677f65594ea6f.zip" \
    "7ebab01b06c555f4b6514453dc3e1667f810ef91d1d4d2d3aa29bb9fcb40a900" \
    "https://github.com/googleapis/googleapis/archive/541b1ded4abadcc38e8178680b0677f65594ea6f.zip" \
    || failed=$((failed+1))

download_one "cares-1_19_1.zip" \
    "edcaac184aff0e6b6eb7b9ede7a55f36c7fc04085d67fecff2434779155dd8ce" \
    "https://gitee.com/mirrors/c-ares/repository/archive/cares-1_19_1.zip" \
    || failed=$((failed+1))

download_one "v3.11.3.zip" \
    "0deac294b2c96c593d0b7c0fb2385a2f4594e8053a36c52b11445ef4b9defebb" \
    "https://gitee.com/mirrors/nlohmann-json/repository/archive/v3.11.3.zip" \
    || failed=$((failed+1))

download_one "v1.13.0.zip" \
    "647924848ca7cb91ba5e34260132902886e1bd140428bd3bd7b4e8fa6c6c8904" \
    "https://gitee.com/mirrors/googletest/repository/archive/v1.13.0.zip" \
    || failed=$((failed+1))

download_one "0.8.0.zip" \
    "6a05c681872d9465b8e2040b5211b1aa5cf30151dc4f3d7ed23ac75ce0fd9944" \
    "https://gitee.com/mirrors/yaml-cpp/repository/archive/0.8.0.zip" \
    || failed=$((failed+1))

download_one "v1.13.0.tar.gz" \
    "7735cc56507149686e6019e06f588317099d4522480be5f38a2a09ec69af1706" \
    "https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.13.0.tar.gz" \
    || failed=$((failed+1))

download_one "bazel-skylib-1.3.0.tar.gz" \
    "74d544d96f4a5bb630d465ca8bbcfe231e3594e5aae57e1edbf17a6eb3ca2506" \
    "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.3.0/bazel-skylib-1.3.0.tar.gz" \
    || failed=$((failed+1))

download_one "rules_cc-0.0.9.tar.gz" \
    "2037875b9a4456dce4a79d112a8ae885bbc4aad968e6587dca6e64f3a0900cdf" \
    "https://github.com/bazelbuild/rules_cc/releases/download/0.0.9/rules_cc-0.0.9.tar.gz" \
    || failed=$((failed+1))

download_one "5.3.0-21.7.tar.gz" \
    "dc3fb206a2cb3441b485eb1e423165b231235a1ea9b031b4433cf7bc1fa460dd" \
    "https://github.com/bazelbuild/rules_proto/archive/refs/tags/5.3.0-21.7.tar.gz" \
    || failed=$((failed+1))

download_one "0.19.0.tar.gz" \
    "ffc7b877c95413c82bfd5482c017edcf759a6250d8b24e82f41f3c8b8d9e287e" \
    "https://github.com/bazelbuild/rules_python/archive/refs/tags/0.19.0.tar.gz" \
    || failed=$((failed+1))

download_one "0.9.0.tar.gz" \
    "2a4d07cd64b0719b39a7c12218a3e507672b82a97b98c6a89d38565894cf7c51" \
    "https://github.com/bazel-contrib/rules_foreign_cc/archive/refs/tags/0.9.0.tar.gz" \
    || failed=$((failed+1))

download_one "0.31.3.tar.gz" \
    "d6735ed25754dbcb4fce38e6d72c55b55f6afa91408e0b72f1357640b88bb49c" \
    "https://github.com/bazelbuild/rules_apple/archive/refs/tags/0.31.3.tar.gz" \
    || failed=$((failed+1))

download_one "0.21.0.tar.gz" \
    "802c094df1642909833b59a9507ed5f118209cf96d13306219461827a00992da" \
    "https://github.com/bazelbuild/rules_swift/archive/refs/tags/0.21.0.tar.gz" \
    || failed=$((failed+1))

download_one "0.10.0.tar.gz" \
    "c02a8c902f405e5ea12b815f426fbe429bc39a2628b290e50703d956d40f5542" \
    "https://github.com/bazelbuild/apple_support/archive/refs/tags/0.10.0.tar.gz" \
    || failed=$((failed+1))

echo ""
if [ "${failed}" -gt 0 ]; then
    echo "[error] ${failed} file(s) failed to download."
    exit 1
fi

echo "All ${#CHECKSUMS[@]} files downloaded and verified."
populate_repo_cache
echo ""
echo "Done. Run 'bazel build' to build offline."
