PROJECT := redshow
PROJECT_PARSER := redshow_parser
PROJECT_GRAPHVIZ := redshow_graphviz
CONFIGS := Makefile.config

include $(CONFIGS)

.PHONY: clean all objects install

CC := g++

LIB_DIR := lib/
INC_DIR := include/
BIN_DIR := bin/
SRC_DIR := src/
BOOST_INC_DIR := $(BOOST_DIR)/include
BUILD_DIR := build/
CUR_DIR = $(shell pwd)/

LIB := $(LIB_DIR)lib$(PROJECT).so

ifdef DEBUG
OFLAGS += -g -DDEBUG
else
OFLAGS += -g -O3
endif

ifdef AVX
OFLAGS += -m$(AVX)
else
OFLAGS += -march=native
endif

# CFLAGS := -fPIC -std=c++17 $(OFLAGS)
# LDFLAGS := -fPIC -shared -L$(BOOST_DIR)/lib -lboost_graph -lboost_regex
CFLAGS := -fPIC -std=c++17 $(OFLAGS) -I$(TORCH_MONITOR_DIR)/include
LDFLAGS := -fPIC -shared -L$(BOOST_DIR)/lib -lboost_graph -lboost_regex -L$(TORCH_MONITOR_DIR)/lib -Wl,-rpath=$(TORCH_MONITOR_DIR)/lib

ifdef OPENMP
CFLAGS += -DOPENMP -fopenmp
LDFLAGS += -fopenmp
endif

ifdef STATIC_CPP
LDFLAGS += -static-libstdc++
endif

BINS := $(PROJECT_PARSER)
BIN_SRCS := $(addsuffix .cpp, $(addprefix src/, $(BINS)))

SRCS := $(shell find $(SRC_DIR) -maxdepth 3 -name "*.cpp")
SRCS := $(filter-out $(BIN_SRCS), $(SRCS))
OBJECTS := $(addprefix $(BUILD_DIR), $(patsubst %.cpp, %.o, $(SRCS)))
OBJECTS_DIR := $(sort $(addprefix $(BUILD_DIR), $(dir $(SRCS))))

all: dirs objects lib bins

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

$(BINS): % : $(SRC_DIR)%.cpp $(OBJECTS)
	$(CC) $(CFLAGS) -I$(INC_DIR) -I$(BOOST_DIR)/include -I$(GPU_PATCH_DIR)/include -L$(TORCH_MONITOR_DIR)/lib -Wl,-rpath=$(TORCH_MONITOR_DIR)/lib -o $@ $^ -ltorch_monitor

$(LIB): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ -ltorch_monitor

$(OBJECTS): $(BUILD_DIR)%.o : %.cpp
	$(CC) $(CFLAGS) -I$(INC_DIR) -I$(BOOST_DIR)/include -I$(GPU_PATCH_DIR)/include -o $@ -c $< -ltorch_monitor

clean:
	-rm -rf $(BUILD_DIR) $(LIB_DIR) $(BINS)

ifdef PREFIX
# Do not install main binary
install:
	mkdir -p $(PREFIX)/$(LIB_DIR)
	mkdir -p $(PREFIX)/$(INC_DIR)
	mkdir -p $(PREFIX)/$(BIN_DIR)
	cp -rf $(LIB_DIR) $(PREFIX)
	cp -rf $(INC_DIR)$(PROJECT).h $(PREFIX)/$(INC_DIR)
	cp -rf $(INC_DIR)$(PROJECT_GRAPHVIZ).h $(PREFIX)/$(INC_DIR)
	cp -rf $(BINS) $(PREFIX)/$(BIN_DIR)
endif

#utils
print-% : ; $(info $* is $(flavor $*) variable set to [$($*)]) @true
