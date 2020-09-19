PROJECT := redshow
CONFIGS := Makefile.config

include $(CONFIGS)

.PHONY: clean all objects install

CC := g++

LIB_DIR := lib/
INC_DIR := include/
BUILD_DIR := build/
CUR_DIR = $(shell pwd)/

LIB := $(LIB_DIR)lib$(PROJECT).so

ifdef DEBUG
OFLAGS += -g -DDEBUG
else
OFLAGS += -O3
endif

CFLAGS := -fPIC -std=c++11 $(OFLAGS)
LDFLAGS := -fPIC -shared -static-libstdc++
SRCS := $(shell find src -maxdepth 3 -name "*.cpp")
OBJECTS := $(addprefix $(BUILD_DIR), $(patsubst %.cpp, %.o, $(SRCS)))
OBJECTS_DIR := $(sort $(addprefix $(BUILD_DIR), $(dir $(SRCS))))
BINS := main

# all: dirs objects lib bins
# Do not compile bin now
all: dirs objects lib

ifdef PREFIX
install: all
endif

dirs: $(OBJECTS_DIR) $(LIB_DIR)
objects: $(OBJECTS)
lib: $(LIB)
bins: $(BINS)

$(OBJECTS_DIR):
	mkdir -p $@

$(LIB_DIR):
	mkdir -p $@

$(BINS): % : %.cpp $(OBJECTS)
	$(CC) $(CFLAGS) -I$(INC_DIR) -I$(GPU_PATCH_DIR)/include -o $@ $^

$(LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ 

$(OBJECTS): $(BUILD_DIR)%.o : %.cpp
	$(CC) $(CFLAGS) -I$(INC_DIR) -I$(GPU_PATCH_DIR)/include -o $@ -c $<

clean:
	-rm -rf $(BUILD_DIR) $(LIB_DIR) $(BINS)

ifdef PREFIX
# Do not install main binary
install:
	mkdir -p $(PREFIX)/$(LIB_DIR)
	mkdir -p $(PREFIX)/$(INC_DIR)
	cp -rf $(LIB_DIR) $(PREFIX)
	cp -rf $(INC_DIR)$(PROJECT).h $(PREFIX)/$(INC_DIR)
endif

#utils
print-% : ; $(info $* is $(flavor $*) variable set to [$($*)]) @true
