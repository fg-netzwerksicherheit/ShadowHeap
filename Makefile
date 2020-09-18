SHELL := /bin/bash

############################################
# Compiler flags
############################################
CC = gcc
CFLAGS = -ggdb -Wl,--no-as-needed -ldl -lm -std=gnu11
CXX = g++
# TODO: fix the -W* and -fpermissive warnings
CXXFLAGS = -Wl,--no-as-needed -ldl -std=gnu++14 -Wno-return-type -Wno-subobject-linkage -fpermissive

############################################
# Dynamic vars
############################################
ifeq ($(v),1)
CFLAGS := $(CFLAGS) -DVERBOSE=1
CXXFLAGS := $(CXXFLAGS) -DVERBOSE=1
endif

############################################
# Features
############################################
FEATURE_FLAGS_ALL = -DUSB_CHECK=1 -DPTR_CHECK=1 -DTOP_CHECK=1 -DTCA_CHECK=1 -DLEAK_CHECK=1
FEATURE_FLAGS_TESTING = -DINFO_MSG=1 -DTRACE_MSG=1
FEATURE_FLAGS_HOOKED =
FEATURE_FLAGS_SHADOW = $(FEATURE_FLAGS_ALL) $(FEATURE_FLAGS_TESTING) -DSHADOW=1
FEATURE_FLAGS_SHADOW_DEBUG = $(FEATURE_FLAGS_SHADOW) -ggdb
FEATURE_FLAGS_SHADOW_DEBUG_VERBOSE = $(FEATURE_FLAGS_SHADOW_DEBUG) -DVERBOSE=1
FEATURE_FLAGS_LIB = $(FEATURE_FLAGS_SHADOW) 
FEATURE_FLAGS_LIB_TESTING = $(FEATURE_FLAGS_SHADOW_DEBUG_VERBOSE) $(FEATURE_FLAGS_TESTING)
FEATURE_FLAGS_MIT_LEVEL_1 = -DSHADOW=1 -DLEAK_CHECK=1 -DPTR_CHECK=1
FEATURE_FLAGS_MIT_LEVEL_2 = -DSHADOW=1 -DLEAK_CHECK=1 -DPTR_CHECK=1 -DTOP_CHECK=1
FEATURE_FLAGS_MIT_LEVEL_3 = -DSHADOW=1 -DLEAK_CHECK=1 -DPTR_CHECK=1 -DTOP_CHECK=1 -DUSB_CHECK=1 
FEATURE_FLAGS_MIT_LEVEL_4 = -DSHADOW=1 -DLEAK_CHECK=1 -DPTR_CHECK=1 -DTOP_CHECK=1 -DUSB_CHECK=1  -DTCA_CHECK=1

FLAG_STORE_CACHE = -DMETA_STORE='CachedMetaStore<HashMetaStore>'
FLAG_STORE_VECTOR = -DMETA_STORE='VectorMetaStore<>'
FLAG_STORE_MAP = -DMETA_STORE='MapMetaStore<>'
FLAG_STORE_HASH = -DMETA_STORE='HashMetaStore<>'
FLAG_STORE_DEFAULT = $(FLAG_STORE_CACHE)

############################################
# Folders and filenames
############################################
ROOT_DIR=$(shell pwd)
SRC_FOLDER=src
BIN_FOLDER=bin
FG_FOLDER=../FlameGraph
SCRIPT_FOLDER=scripts
PERF_FOLDER=perf-test

DEFAULT_MITIGATION_LIB = malloc-shadow-prod
SOURCES := $(shell find src/ -name '*.h' -o -name '*.c' -o -name '*.cxx')
# all .cxx files except runners, tests, and perf thingies.
# These need to be linked into runners and shadowheap itself.
# For example, this is leak.cxx.
SHADOWHEAP_SUPPORT_SOURCES := $(filter-out \
	src/runners/% src/tests/% src/perf/%, \
	$(filter %.cxx, $(SOURCES)))
	
############################################
# Experiment targets
############################################
TYPICAL_EXPERIMENTS_NONE = \
	--preload none "" \
	--preload none-hooked 	"$(realpath $(BIN_FOLDER)/malloc-hooked.so)"

TYPICAL_EXPERIMENTS_PROD = \
	--preload prod-onlyptr "$(realpath $(BIN_FOLDER)/malloc-shadow-prod.so) $(ENVIRONMENT_PTR_ONLY)" \
	--preload prod-onlyusb "$(realpath $(BIN_FOLDER)/malloc-shadow-prod.so) $(ENVIRONMENT_USB_ONLY)" \
	--preload prod-onlytop "$(realpath $(BIN_FOLDER)/malloc-shadow-prod.so) $(ENVIRONMENT_TOP_ONLY)" \
	--preload prod-onlytca "$(realpath $(BIN_FOLDER)/malloc-shadow-prod.so) $(ENVIRONMENT_TCA_ONLY)"
	
TYPICAL_EXPERIMENTS_PROD_LEVELS = \
	--preload prod-level-1 "$(realpath $(BIN_FOLDER)/malloc-shadow-prod-level-1.so) " \
	--preload prod-level-2 "$(realpath $(BIN_FOLDER)/malloc-shadow-prod-level-2.so) " \
	--preload prod-level-3 "$(realpath $(BIN_FOLDER)/malloc-shadow-prod-level-3.so) " \
	--preload prod-level-4 "$(realpath $(BIN_FOLDER)/malloc-shadow-prod-level-4.so) " 

TYPICAL_EXPERIMENTS_STORES = \
	--preload stores-hash-onlyptr "$(realpath $(BIN_FOLDER)/malloc-shadow-prod-hash.so) $(ENVIRONMENT_PTR_ONLY)" \
	--preload stores-tree-onlyptr "$(realpath $(BIN_FOLDER)/malloc-shadow-prod-tree.so) $(ENVIRONMENT_PTR_ONLY)"

TYPICAL_EXPERIMENTS_DEBUG = \
	--preload debug-ptronly "$(realpath $(BIN_FOLDER)/malloc-shadow-debug.so) $(ENVIRONMENT_PTR_ONLY)" \
	--preload debug-usbonly "$(realpath $(BIN_FOLDER)/malloc-shadow-debug.so) $(ENVIRONMENT_USB_ONLY)" \
	--preload debug-toponly "$(realpath $(BIN_FOLDER)/malloc-shadow-debug.so) $(ENVIRONMENT_TOP_ONLY)" \
	--preload debug-tcaonly "$(realpath $(BIN_FOLDER)/malloc-shadow-debug.so) $(ENVIRONMENT_TCA_ONLY)" \
	--preload debug-all "$(realpath $(BIN_FOLDER)/malloc-shadow-debug.so)" \
	--preload debug-none "$(realpath $(BIN_FOLDER)/malloc-shadow-debug.so) $(ENVIRONMENT_NONE)" 

############################################
# Target variables
############################################

LIBRARIES = \
	$(BIN_FOLDER)/malloc-hooked.so \
	$(BIN_FOLDER)/malloc-shadow-debug-vec.so \
	$(BIN_FOLDER)/malloc-shadow-debug.so \
	$(BIN_FOLDER)/malloc-shadow-prod-lib.so \
	$(BIN_FOLDER)/malloc-shadow-prod-hash.so \
	$(BIN_FOLDER)/malloc-shadow-prod-tree.so \
	$(BIN_FOLDER)/malloc-shadow-prod-vec.so \
	$(BIN_FOLDER)/malloc-shadow-prod.so \
	$(BIN_FOLDER)/malloc-shadow-prod-level-1.so \
	$(BIN_FOLDER)/malloc-shadow-prod-level-2.so \
	$(BIN_FOLDER)/malloc-shadow-prod-level-3.so \
	$(BIN_FOLDER)/malloc-shadow-prod-level-4.so \
	$(BIN_FOLDER)/malloc-shadow-verbose.so \
	$(BIN_FOLDER)/malloc-shadow-verbose-hash.so \
	$(BIN_FOLDER)/malloc-shadow.so

TESTS = \
	$(BIN_FOLDER)/dlsymtest.t \
	$(BIN_FOLDER)/realloc.t \
	$(BIN_FOLDER)/store.t

RUNNERS = \
	$(BIN_FOLDER)/facade-runner.x \
	$(BIN_FOLDER)/runner.x \
	$(BIN_FOLDER)/shadow-runner.x \
	$(BIN_FOLDER)/simple-meta-store.x \
	$(BIN_FOLDER)/wrapper-runner.x

DEBUGRUNNERS = \
	$(BIN_FOLDER)/static-runner.x

############################################
# Common targets
############################################
.PHONY: all libraries tests dbg-runners
all: libraries tests $(BIN_FOLDER)/perf.x
libraries: $(LIBRARIES)
tests: $(TESTS)
runners: $(RUNNERS)
dbg-runners: $(DEBUGRUNNERS)

############################################
# Clean targets
############################################
clean:
	@rm -rf $(BIN_FOLDER)

rebuild: clean all

############################################
# Mitigation libs
############################################

$(BIN_FOLDER)/malloc-%.so : $(SRC_FOLDER)/wrapper/malloc_hooks.cxx $(SOURCES)
	@mkdir -p $(BIN_FOLDER)
	$(CXX) -o $@ $(CXXFLAGS) -shared -fPIC $(SHADOWHEAP_SUPPORT_SOURCES)
	
$(BIN_FOLDER)/malloc-hooked.so              : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) $(FEATURE_FLAGS_HOOKED)
$(BIN_FOLDER)/malloc-shadow.so              : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) -DSHADOW=1
$(BIN_FOLDER)/malloc-shadow-debug.so        : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) $(FEATURE_FLAGS_SHADOW_DEBUG)
$(BIN_FOLDER)/malloc-shadow-debug-vec.so    : CXXFLAGS += -O3 $(FLAG_STORE_VECTOR)  $(FEATURE_FLAGS_SHADOW_DEBUG)
$(BIN_FOLDER)/malloc-shadow-verbose.so      : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) $(FEATURE_FLAGS_SHADOW_DEBUG_VERBOSE)
$(BIN_FOLDER)/malloc-shadow-verbose-hash.so : CXXFLAGS += -O3 $(FLAG_STORE_HASH)    $(FEATURE_FLAGS_SHADOW_DEBUG_VERBOSE)
$(BIN_FOLDER)/malloc-shadow-prod.so         : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) $(FEATURE_FLAGS_SHADOW)
$(BIN_FOLDER)/malloc-shadow-prod-vec.so     : CXXFLAGS += -O3 $(FLAG_STORE_VECTOR)  $(FEATURE_FLAGS_SHADOW)
$(BIN_FOLDER)/malloc-shadow-prod-tree.so    : CXXFLAGS += -O3 $(FLAG_STORE_MAP)     $(FEATURE_FLAGS_SHADOW)
$(BIN_FOLDER)/malloc-shadow-prod-hash.so    : CXXFLAGS += -O3 $(FLAG_STORE_HASH)    $(FEATURE_FLAGS_SHADOW)
$(BIN_FOLDER)/malloc-shadow-prod-level-1.so : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) $(FEATURE_FLAGS_MIT_LEVEL_1)
$(BIN_FOLDER)/malloc-shadow-prod-level-2.so : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) $(FEATURE_FLAGS_MIT_LEVEL_2)
$(BIN_FOLDER)/malloc-shadow-prod-level-3.so : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) $(FEATURE_FLAGS_MIT_LEVEL_3)
$(BIN_FOLDER)/malloc-shadow-prod-level-4.so : CXXFLAGS += -O3 $(FLAG_STORE_DEFAULT) $(FEATURE_FLAGS_MIT_LEVEL_4)

############################################
# Tests and runners
############################################

$(BIN_FOLDER)/perf.x: $(SRC_FOLDER)/perf/perf.cxx
	@mkdir -p $(BIN_FOLDER)
	$(CXX) -o $@ $(CXXFLAGS) -ggdb $<

$(BIN_FOLDER)/%.x : $(SRC_FOLDER)/runners/%.cxx
	@mkdir -p $(BIN_FOLDER)
	$(CXX) -o $@ $(CXXFLAGS) $(FLAG_STORE_VECTOR) $(FEATURE_FLAGS_SHADOW_DEBUG_VERBOSE) $< $(SHADOWHEAP_SUPPORT_SOURCES)

$(BIN_FOLDER)/%.x : $(SRC_FOLDER)/runners/%.c
	@mkdir -p $(BIN_FOLDER)
	@$(CC) -o $@ $(CFLAGS) $<

$(BIN_FOLDER)/%.t : $(SRC_FOLDER)/tests/%.cxx $(SOURCES)
	@mkdir -p $(BIN_FOLDER)
	$(CXX) -o $@ $(CXXFLAGS) $<

$(BIN_FOLDER)/%.t : $(SRC_FOLDER)/tests/%.c
	@mkdir -p $(BIN_FOLDER)
	$(CC) -o $@ $(CFLAGS) $<

############################################
# Run targets
############################################
run: $(BIN_FOLDER)/$(DEFAULT_MITIGATION_LIB).so $(BIN_FOLDER)/runner.x
	@echo ""
	@echo "------------------------------"
	@echo "Running default target: "
	@echo "------------------------------"
	@LD_PRELOAD=${ROOT_DIR}/$(BIN_FOLDER)/$(DEFAULT_MITIGATION_LIB).so $(BIN_FOLDER)/runner.x
	@echo "------------------------------"

run-ls: $(BIN_FOLDER)/$(DEFAULT_MITIGATION_LIB).so
	@echo ""
	@echo "------------------------------"
	@echo "Running ls test: "
	@echo "------------------------------"
	@LD_PRELOAD=${ROOT_DIR}/$(BIN_FOLDER)/$(DEFAULT_MITIGATION_LIB).so ls -la .
	@echo "------------------------------"

run-tests: tests $(BIN_FOLDER)/$(DEFAULT_MITIGATION_LIB).so
	$(BIN_FOLDER)/dlsymtest.t
	$(BIN_FOLDER)/store.t
	# This doesn't quite work with the native glibc, should use 2.26
	# LD_PRELOAD=$(BIN_FOLDER)/$(DEFAULT_MITIGATION_LIB).so $(BIN_FOLDER)/realloc.t

graph-malloc: $(BIN_FOLDER)/perf.x $(LIBRARIES)
	@mkdir -p $(PERF_FOLDER)
	perf record  -F 3000 -a -g -R ./run-experiment.py run --verbose --repetitions 1 \
		--database $(PERF_FOLDER)/$@.json \
		$(TYPICAL_EXPERIMENTS_DEBUG) \
		-- ./scripts/run_patched.sh --base libc_patched/build/2.26 --name $(BIN_FOLDER)/perf.x "Data"
	perf script > $(PERF_FOLDER)/$@.perf
	@$(FG_FOLDER)/stackcollapse-perf.pl $(PERF_FOLDER)/$@.perf > $(PERF_FOLDER)/$@.folded
	@$(FG_FOLDER)/flamegraph.pl $(PERF_FOLDER)/$@.folded > $(PERF_FOLDER)/$@.svg

perf-malloc: $(BIN_FOLDER)/perf.x $(LIBRARIES) 
	@mkdir -p $(PERF_FOLDER)
	./run-experiment.py run --verbose --repetitions 100 \
		--database $(PERF_FOLDER)/$@.json \
		$(TYPICAL_EXPERIMENTS_NONE) \
		$(TYPICAL_EXPERIMENTS_PROD) \
		$(TYPICAL_EXPERIMENTS_PROD_LEVELS) \
		$(TYPICAL_EXPERIMENTS_STORES) \
		-- ./$(BIN_FOLDER)/perf.x "Data"

$(PERF_BENCHMARKS:%=$(PERF_FOLDER)/%.pdf): $(PERF_FOLDER)/%.pdf : %
	./run-experiment.py plot -o $@ --database $(@:.pdf=.json) real mem_max 

format:
	find src -type f -exec clang-format -i --style=file {} +
