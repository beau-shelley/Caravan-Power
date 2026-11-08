# Host unit tests for firmware/lib/CaravanCommon.
# Needs ArduinoJson 7 headers:  git clone --depth 1 -b v7.2.1 https://github.com/bblanchon/ArduinoJson
ARDUINOJSON ?= ../../ArduinoJson
COMMON := ../firmware/lib/CaravanCommon/src
CXXFLAGS := -std=c++17 -Wall -Wextra -Wno-maybe-uninitialized -O1 -g -I$(COMMON) -I$(ARDUINOJSON)/src

bin/test_common: test_common.cpp $(wildcard $(COMMON)/*.cpp) $(wildcard $(COMMON)/*.h)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined test_common.cpp $(wildcard $(COMMON)/*.cpp) -o $@

test: bin/test_common
	./bin/test_common

.PHONY: test
.DEFAULT_GOAL := test
