#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CART_DIR="${ROOT_DIR}/Source/cart"

if [[ ! -d "${CART_DIR}" ]]; then
  echo "cart directory not found: ${CART_DIR}" >&2
  exit 1
fi

WAMRC_BIN="${WAMRC_BIN:-}"
if [[ -z "${WAMRC_BIN}" ]]; then
  if command -v wamrc >/dev/null 2>&1; then
    WAMRC_BIN="$(command -v wamrc)"
  elif [[ -x "${ROOT_DIR}/third_party/wasm-micro-runtime/wamr-compiler/build/wamrc" ]]; then
    WAMRC_BIN="${ROOT_DIR}/third_party/wasm-micro-runtime/wamr-compiler/build/wamrc"
  fi
fi

if [[ -z "${WAMRC_BIN}" ]]; then
  echo "wamrc not found. Set WAMRC_BIN=/absolute/path/to/wamrc." >&2
  exit 1
fi

# Playdate-focused defaults: Cortex-M7 + Thumb (ARMv7E-M).
WAMRC_TARGET="${WAMRC_TARGET:-thumbv7em}"
WAMRC_CPU="${WAMRC_CPU:-cortex-m7}"

shopt -s nullglob
wasm_files=("${CART_DIR}"/*.wasm)
if [[ ${#wasm_files[@]} -eq 0 ]]; then
  echo "no .wasm files found under ${CART_DIR}" >&2
  exit 1
fi

for wasm in "${wasm_files[@]}"; do
  aot="${wasm%.wasm}.aot"
  cmd=("${WAMRC_BIN}")
  cmd+=("--target=${WAMRC_TARGET}")
  cmd+=("--cpu=${WAMRC_CPU}")

  cmd+=("-o" "${aot}" "${wasm}")

  echo "[aot] ${wasm##*/} -> ${aot##*/} (target=${WAMRC_TARGET}, cpu=${WAMRC_CPU})"
  "${cmd[@]}"
done

echo "AOT build complete."
