#!/usr/bin/env bash
# Fetch pinned third-party sources into third_party/ (not committed to git).
# Pinned versions keep every build reproducible, locally and in CI.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TP="${ROOT}/third_party"
mkdir -p "${TP}"

fetch() { # name url tag
    local name="$1" url="$2" tag="$3"
    if [ -d "${TP}/${name}/.git" ]; then
        echo "✓ ${name} already present"
        return
    fi
    echo "→ ${name} @ ${tag}"
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch "${tag}" "${url}" "${TP}/${name}"
}

fetch FreeRTOS-Kernel        https://github.com/FreeRTOS/FreeRTOS-Kernel.git                     V11.1.0
fetch stm32h7rsxx_hal_driver https://github.com/STMicroelectronics/stm32h7rsxx_hal_driver.git   v1.2.1
fetch cmsis_device_h7rs      https://github.com/STMicroelectronics/cmsis_device_h7rs.git        v1.2.1

# CMSIS core headers only (the full CMSIS_5 repository is large).
if [ ! -d "${TP}/CMSIS_5/.git" ]; then
    echo "→ CMSIS_5 (Core/Include) @ 5.9.0"
    git -c advice.detachedHead=false clone --quiet --depth 1 --branch 5.9.0 --filter=blob:none --sparse \
        https://github.com/ARM-software/CMSIS_5.git "${TP}/CMSIS_5"
    git -C "${TP}/CMSIS_5" sparse-checkout set CMSIS/Core/Include
else
    echo "✓ CMSIS_5 already present"
fi

echo "Done. Third-party sources are in ${TP}"
