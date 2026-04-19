BUILD_DIR ?= build
ROM       ?= $(firstword $(wildcard roms/*.gba))

.PHONY: build run clean

build:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>/dev/null || true
	@cmake --build $(BUILD_DIR) -j$$(nproc)

run: clean build
	@if [ -z "$(ROM)" ]; then echo "No .gba ROM found in roms/"; exit 1; fi
	@echo "Running: $(ROM)"
	@./$(BUILD_DIR)/platform/linux_sdl/kairocore "$(ROM)"

clean:
	@rm -rf $(BUILD_DIR)
