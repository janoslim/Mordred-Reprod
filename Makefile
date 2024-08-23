# CUDA_PATH       ?= /usr/local/cuda
# CUDA_INC_PATH   ?= $(CUDA_PATH)/include
# CUDA_BIN_PATH   ?= $(CUDA_PATH)/bin

# NVCC = nvcc

# #SM_TARGETS   = -gencode=arch=compute_52,code=\"sm_52,compute_52\" 
# # SM_DEF     = -DSM520

# SM_TARGETS   = -gencode=arch=compute_70,code=\"sm_70,compute_70\" 
# SM_DEF     = -DSM700

# #GENCODE_SM50    := -gencode arch=compute_52,code=sm_52
# GENCODE_SM70    := -gencode arch=compute_70,code=sm_70
# GENCODE_FLAGS   := $(GENCODE_SM70)

# #NVCCFLAGS += --std=c++11 $(SM_DEF) -Xptxas="-dlcm=cg -v" -lineinfo -Xcudafe -\# 
# NVCCFLAGS += --std=c++14 $(SM_DEF) -Xptxas="-dlcm=cg -v" -lineinfo -Xcudafe -\# 
# OPENMPFLAGS = -Xcompiler -fopenmp -lgomp

# SRC = src
# BIN = bin
# OBJ = obj
# INC = includes

# CUB_DIR = cub/
# INCLUDES = -I$(CUB_DIR) -I$(CUB_DIR)test -I. -I$(INC)

# CFLAGS = -O3 -march=native -std=c++14 -ffast-math
# LDFLAGS = -ltbb -L/usr/local/lib/
# CINCLUDES = -I$(INC)
# CXX = clang++

# $(OBJ)/%.o: $(SRC)/%.cu
# 	$(NVCC) -lcurand -lcuda $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# $(BIN)/%: $(OBJ)/%.o
# 	$(NVCC) -ltbb -L/usr/local/lib/-lcuda $(SM_TARGETS) -lcurand $^ -o $@

# $(OBJ)/cpu/%.o: $(SRC)/cpu/%.cpp
# 	$(NVCC) -lcurand $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# #$(CXX) $(CFLAGS) $(CINCLUDES) -c $< -o $@

# $(BIN)/cpu/%: $(OBJ)/cpu/%.o
# 	$(NVCC) -ltbb -L/usr/local/lib/$(SM_TARGETS) -lcurand $^ -o $@
	
# #$(CXX) -ltbb -L/usr/local/lib/$^ -o $@

# $(OBJ)/%.o: $(SRC)/%.cpp
# 	$(NVCC) -lcurand -lcuda $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# $(OBJ)/gpudb/CostModel.o: $(SRC)/gpudb/CostModel.cu
# 	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/$(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# $(OBJ)/gpudb/QueryOptimizer.o: $(SRC)/gpudb/QueryOptimizer.cu
# 	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/$(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# $(OBJ)/gpudb/QueryProcessing.o: $(SRC)/gpudb/QueryProcessing.cu
# 	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/$(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# $(OBJ)/gpudb/CPUGPUProcessing.o: $(SRC)/gpudb/CPUGPUProcessing.cu
# 	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/$(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# $(OBJ)/gpudb/CPUProcessing.o: $(SRC)/gpudb/CPUProcessing.cu
# 	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/$(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# $(OBJ)/gpudb/main.o: $(SRC)/gpudb/main.cu
# 	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/$(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@

# $(BIN)/gpudb/main: $(OBJ)/gpudb/main.o $(OBJ)/gpudb/CacheManager.o $(OBJ)/gpudb/QueryOptimizer.o $(OBJ)/gpudb/CPUProcessing.o $(OBJ)/gpudb/CPUGPUProcessing.o $(OBJ)/gpudb/QueryProcessing.o $(OBJ)/gpudb/CostModel.o
# 	$(NVCC) $(SM_TARGETS) -lcuda -ltbb -L/usr/local/lib/-lcurand $^ -o $@

# setup:
# 	mkdir -p bin/ssb obj/ssb
# 	mkdir -p bin/ops obj/ops
# 	mkdir -p bin/cpu/ssb obj/cpu/ssb
# 	mkdir -p bin/gpudb obj/gpudb

# clean:
# 	rm -rf bin/* obj/*


CUDA_PATH       ?= /usr/local/cuda
CUDA_INC_PATH   ?= $(CUDA_PATH)/include
CUDA_BIN_PATH   ?= $(CUDA_PATH)/bin

NVCC = nvcc

#SM_TARGETS   = -gencode=arch=compute_52,code=\"sm_52,compute_52\" 
# SM_DEF     = -DSM520

SM_TARGETS   = -gencode=arch=compute_75,code=\"sm_75,compute_75\" 
SM_DEF     = -DSM700

#GENCODE_SM50    := -gencode arch=compute_52,code=sm_52
GENCODE_SM70    := -gencode arch=compute_75,code=sm_75
GENCODE_FLAGS   := $(GENCODE_SM70)

#NVCCFLAGS += --std=c++11 $(SM_DEF) -Xptxas="-dlcm=cg -v" -lineinfo -Xcudafe -\# 
NVCCFLAGS += --std=c++14 $(SM_DEF) -Xptxas="-dlcm=cg -v" -lineinfo -Xcudafe -\# 
OPENMPFLAGS = -Xcompiler -fopenmp -lgomp

SRC = src
BIN = bin
OBJ = obj
INC = includes

# get argument of make
SF=${SF}

CUB_DIR = cub/
INCLUDES = -I$(CUB_DIR) -I$(CUB_DIR)test -I. -I$(INC)

CFLAGS = -O3 -march=native -std=c++14 -ffast-math -g
LDFLAGS = -ltbb -L/usr/local/lib/
CINCLUDES = -I$(INC)
CXX = clang++
#define CUB_STDERR
$(OBJ)/%.o: $(SRC)/%.cu
	$(NVCC) -lcurand -lcuda  -ltbb -L/usr/local/lib/ $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -g -dc $< -o $@ -DCUB_STDERR -DSF=${SF}

$(BIN)/%: $(OBJ)/%.o
	$(NVCC) -ltbb -L/usr/local/lib/ -lcuda $(SM_TARGETS) -lcurand $^  -g -o $@ -DCUB_STDERR -DSF=${SF}

$(OBJ)/cpu/%.o: $(SRC)/cpu/%.cpp
	$(NVCC) -lcurand $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3  -g -dc $< -o $@ -DCUB_STDERR -DSF=${SF}

#$(CXX) $(CFLAGS) $(CINCLUDES) -c $< -o $@

$(BIN)/cpu/%: $(OBJ)/cpu/%.o
	$(NVCC) -ltbb -L/usr/local/lib/ $(SM_TARGETS) -lcurand $^  -g -o $@ -DCUB_STDERR -DSF=${SF}
	
#$(CXX) -ltbb -L/usr/local/lib/ $^ -o $@

$(OBJ)/%.o: $(SRC)/%.cpp
	$(NVCC) -lcurand -lcuda $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS)  -g -dc $< -o $@ #-O3 -DCUB_STDERR -DSF=${SF}

$(OBJ)/gpudb/CostModel.o: $(SRC)/gpudb/CostModel.cu
	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/ $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@ -DCUB_STDERR -DSF=${SF}

$(OBJ)/gpudb/QueryOptimizer.o: $(SRC)/gpudb/QueryOptimizer.cu
	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/ $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@ -DCUB_STDERR -DSF=${SF}

$(OBJ)/gpudb/QueryProcessing.o: $(SRC)/gpudb/QueryProcessing.cu
	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/ $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@ -DCUB_STDERR -DSF=${SF}

$(OBJ)/gpudb/CPUGPUProcessing.o: $(SRC)/gpudb/CPUGPUProcessing.cu
	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/ $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@ -DCUB_STDERR -DSF=${SF}

$(OBJ)/gpudb/CPUProcessing.o: $(SRC)/gpudb/CPUProcessing.cu
	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/ $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@ -DCUB_STDERR -DSF=${SF}

$(OBJ)/gpudb/main.o: $(SRC)/gpudb/main.cu
	$(NVCC) -lcurand -lcuda -ltbb -L/usr/local/lib/ $(SM_TARGETS) $(NVCCFLAGS) $(CPU_ARCH) $(INCLUDES) $(LIBS) -O3 -dc $< -o $@ -DCUB_STDERR -DSF=${SF}

$(BIN)/gpudb/main.bin: $(OBJ)/gpudb/main.o $(OBJ)/gpudb/CacheManager.o $(OBJ)/gpudb/QueryOptimizer.o $(OBJ)/gpudb/CPUProcessing.o $(OBJ)/gpudb/CPUGPUProcessing.o $(OBJ)/gpudb/QueryProcessing.o $(OBJ)/gpudb/CostModel.o
	$(NVCC) $(SM_TARGETS) -lcuda -ltbb -L/usr/local/lib/ -lcurand $^ -o $@ -DCUB_STDERR -DSF=${SF}

setup:
	mkdir -p bin/ssb obj/ssb
	mkdir -p bin/ops obj/ops
	mkdir -p bin/cpu/ssb obj/cpu/ssb
	mkdir -p bin/gpudb obj/gpudb

clean:
	rm -rf bin/* obj/*
