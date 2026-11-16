# Host "simulator": compiles the real display UI against LVGL and renders
# PPM screenshots of every page with demo data, at the firmware's panel
# resolution (1024x600). RES="-DDISPLAY_HOR_RES=800 -DDISPLAY_VER_RES=480"
# renders another size (give it its own OUT=, e.g. OUT=out-800).
#   git clone --depth 1 -b v8.3.11 https://github.com/lvgl/lvgl
#   git clone --depth 1 -b v7.2.1  https://github.com/bblanchon/ArduinoJson
#   make -C tests/sim LVGL=/path/lvgl ARDUINOJSON=/path/ArduinoJson
LVGL ?= ../../../lvgl
ARDUINOJSON ?= ../../../ArduinoJson
DISP := ../../firmware/display
COMMON := ../../firmware/lib/CaravanCommon/src
OUT ?= out
INC := $(RES) -Ishim -I$(DISP)/include -I$(DISP)/src -I$(LVGL) -I$(LVGL)/.. -I$(COMMON) -I$(ARDUINOJSON)/src -DLV_CONF_INCLUDE_SIMPLE
LVGL_SRC := $(shell find $(LVGL)/src -name '*.c')
LVGL_OBJ := $(patsubst $(LVGL)/%.c,obj/lvgl/%.o,$(LVGL_SRC))
APP_C := $(wildcard $(DISP)/src/ui/*.c)
APP_C_OBJ := $(patsubst $(DISP)/src/ui/%.c,obj/app/%.o,$(APP_C))
APP_SRC := sim_main.cpp $(DISP)/src/link.cpp $(wildcard $(DISP)/src/ui/*.cpp) $(wildcard $(COMMON)/*.cpp)

all: $(OUT)/sim
	@mkdir -p $(OUT)
	cd $(OUT) && OUT=. ./sim

obj/lvgl/%.o: $(LVGL)/%.c
	@mkdir -p $(dir $@)
	@$(CC) -O1 -w $(INC) -c $< -o $@

obj/app/%.o: $(DISP)/src/ui/%.c
	@mkdir -p $(dir $@)
	@$(CC) -O1 -w $(INC) -c $< -o $@

$(OUT)/sim: $(LVGL_OBJ) $(APP_C_OBJ) $(APP_SRC)
	@mkdir -p $(OUT)
	$(CXX) -std=c++17 -O1 -g -Wall -Wno-unused-function -Wno-maybe-uninitialized $(INC) $(APP_SRC) $(APP_C_OBJ) $(LVGL_OBJ) -lm -o $@

clean:
	rm -rf obj $(OUT)
.PHONY: all clean
