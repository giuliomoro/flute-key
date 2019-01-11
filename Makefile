DSP_FILE=vfl.dsp
BELA_LDLIBS=-lHTTPDFaust -lkeys -L/root/spi-pru
BELA_CPPFLAGS=-I/root/spi-pru -std=c++14
ARCHFILE=faust/architecture/bela.cpp 
STATIC_DIR=static

BUILD_DIR=$(DSP_FILE:%.dsp=%)

all: $(BUILD_DIR)/render.cpp
	rsync -a $(STATIC_DIR)/* $(BUILD_DIR)/
	BELA_EXPERT_MODE=1 ../Bela/scripts/build_project.sh --force -m "'LDLIBS=$(BELA_LDLIBS)' 'CPPFLAGS=$(BELA_CPPFLAGS)'" $(BUILD_DIR) 

$(BUILD_DIR)/render.cpp: $(DSP_FILE) faust/architecture/bela.cpp
	@echo RUNNING FAUST
	ARCHFILE="$(ARCHFILE)" ./dsp_to_cpp.sh $(DSP_FILE)
	
