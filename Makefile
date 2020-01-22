PROJECT := redshow

.PHONY: clean all objects install

CC := g++

GPU_PATCH_DIR := /home/kz21/Codes/hpctoolkit-gpu-patch/include

LIB_DIR := lib/
INC_DIR := include/
BUILD_DIR := build/

LIB := $(LIB_DIR)$(PROJECT).so

ifdef DEBUG
OFLAGS += -g -DDEBUG
endif

CFLAGS := -std=c++11 $(OFLAGS)
LDFLAGS := -lstdc++ -lboost
SRCS := $(shell find src -maxdepth 3 -name "*.cpp")
OBJECTS := $(addprefix $(BUILD_DIR), $(patsubst %.cpp, %.o, $(SRCS)))
OBJECTS_DIR := $(sort $(addprefix $(BUILD_DIR), $(dir $(SRCS))))

all: dirs objects lib
dirs: $(OBJECTS_DIR) $(LIB_DIR) $(BIN_DIR)
objects: $(OBJECTS)
lib: $(LIB)

$(OBJECTS_DIR):
	mkdir -p $@

$(LIB_DIR):
	mkdir -p $@

$(LIB): $(OBJECTS)
	ar crf $@ $^ 

$(OBJECTS): $(BUILD_DIR)%.o : %.cpp
	$(CC) $(CFLAGS) -I$(INC_DIR) -I$(GPU_PATCH_DIR) -o $@ -c $<

clean:
	-rm -rf $(BUILD_DIR) $(LIB_DIR)

ifdef PREFIX
install:
	mkdir -p $(PREFIX)/$(BUILD_DIR)
	mkdir -p $(PREFIX)/$(INC_DIR)
	cp -rf $(LIB_DIR) $(PREFIX)
	cp -rf $(INC_DIR)$(PROJECT).h $(PREFIX)
endif

#utils
print-% : ; $(info $* is $(flavor $*) variable set to [$($*)]) @true
