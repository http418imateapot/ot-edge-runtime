# Linux-based Robust-Binary-Config
# ==================================
# 適用 Linux 邊緣裝置的輕量嵌入式 Key-Value 設定引擎
# 填補「SQLite 太重、純文字檔太脆」之間的空隙
#
# Wraps CMake for convenience. All real build logic is in CMakeLists.txt.

BUILD_DIR  ?= build
CMAKE_ARGS ?=

.PHONY: all clean test install

all:
	cmake -B $(BUILD_DIR) -DBUILD_TESTING=ON $(CMAKE_ARGS)
	cmake --build $(BUILD_DIR) --parallel

test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	rm -rf $(BUILD_DIR)

install: all
	cmake --install $(BUILD_DIR)

