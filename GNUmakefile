SHELL := /bin/sh

CMAKE ?= cmake
SIM_BUILD_DIR ?= build
DEVICE_BUILD_DIR ?= build-device

SDK_PATH ?= $(if $(PLAYDATE_SDK_PATH),$(PLAYDATE_SDK_PATH),$(shell awk -F '\t' '$$1 == "SDKRoot" { print $$2; exit }' $$HOME/.Playdate/config 2>/dev/null))
ARM_TOOLCHAIN_FILE := $(SDK_PATH)/C_API/buildsupport/arm.cmake

.PHONY: help all simulator device sim-config sim-build device-config device-build clean distclean check-sdk check-arm

help:
	@echo "Targets:"
	@echo "  make simulator   Build simulator package (.pdx)"
	@echo "  make device      Build device package (.pdx)"
	@echo "  make all         Build simulator + device"
	@echo "  make clean       Remove build outputs"
	@echo "  make distclean   Remove all generated build files (including in-source CMake cache)"

all: simulator device

simulator: sim-build

device: device-build

sim-config: check-sdk
	$(CMAKE) -S . -B $(SIM_BUILD_DIR)

sim-build: sim-config
	$(CMAKE) --build $(SIM_BUILD_DIR)

device-config: check-sdk check-arm
	$(CMAKE) -S . -B $(DEVICE_BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE="$(ARM_TOOLCHAIN_FILE)"

device-build: device-config
	$(CMAKE) --build $(DEVICE_BUILD_DIR) --config Release

check-sdk:
	@if [ -z "$(SDK_PATH)" ]; then \
		echo "SDK path not found. Set PLAYDATE_SDK_PATH or ~/.Playdate/config"; \
		exit 1; \
	fi

check-arm:
	@if [ ! -f "$(ARM_TOOLCHAIN_FILE)" ]; then \
		echo "ARM toolchain file not found: $(ARM_TOOLCHAIN_FILE)"; \
		exit 1; \
	fi

clean:
	rm -rf $(SIM_BUILD_DIR) $(DEVICE_BUILD_DIR) \
		playdate-wasm-4.pdx playdate-wasm-4-device.pdx

distclean: clean
	rm -rf CMakeFiles CMakeCache.txt cmake_install.cmake compile_commands.json Makefile
