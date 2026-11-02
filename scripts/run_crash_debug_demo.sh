#!/usr/bin/env bash

set -uo pipefail

script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_directory="$(cd -- "${script_directory}/.." && pwd)"
build_directory="${project_directory}/build-debug"
binary_path="${build_directory}/edgelink_crash_demo"
artifact_directory="${build_directory}/debug-artifacts"
core_path="${artifact_directory}/edgelink.core"

if [[ ! -x "${binary_path}" ]]; then
    echo "[FAIL] Debug demo is not built. Run:"
    echo "cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DEDGELINK_BUILD_DEBUG_TOOLS=ON"
    echo "cmake --build build-debug -j4"
    exit 1
fi

mkdir -p "${artifact_directory}"

echo "[1/3] GDB运行程序、捕获SIGSEGV并生成Core Dump"
gdb -q -batch \
    -ex "set pagination off" \
    -ex "set confirm off" \
    -ex "run" \
    -ex "generate-core-file ${core_path}" \
    -ex "backtrace" \
    --args "${binary_path}" --segfault \
    >"${artifact_directory}/live-gdb-backtrace.txt" 2>&1

if [[ ! -f "${core_path}" ]]; then
    echo "[FAIL] GDB did not generate ${core_path}"
    exit 1
fi

echo "[2/3] 在新GDB会话中仅通过可执行文件和Core Dump恢复调用栈"
gdb -q -batch \
    -ex "set pagination off" \
    -ex "backtrace" \
    "${binary_path}" "${core_path}" \
    >"${artifact_directory}/core-backtrace.txt" 2>&1

echo "[3/3] strace记录SIGABRT前的系统调用与信号"
set +e
strace -ff -tt -T -s 128 \
    -o "${artifact_directory}/strace.log" \
    "${binary_path}" --abort \
    >/dev/null 2>&1
strace_status=$?
set -e

if [[ ${strace_status} -eq 0 ]]; then
    echo "[FAIL] Abort demo unexpectedly exited normally"
    exit 1
fi

echo "[PASS] Debug artifacts: ${artifact_directory}"
echo "       live-gdb-backtrace.txt  GDB现场调用栈"
echo "       core-backtrace.txt      Core Dump离线调用栈"
echo "       strace.log.*             系统调用与信号记录"
