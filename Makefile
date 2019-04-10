DSP_FILE=vfl.dsp
BELA_COMMAND_ARGS=-H 6 $(CL)
BELA_LDLIBS=-lHTTPDFaust -lkeys -L/root/spi-pru
BELA_CPPFLAGS=-I/root/spi-pru -std=c++14
ARCHFILE=faust/architecture/bela.cpp 
STATIC_DIR=static

BUILD_DIR=$(DSP_FILE:%.dsp=%)

all: $(BUILD_DIR)/render.cpp
	rsync -a $(STATIC_DIR)/* $(BUILD_DIR)/
	BELA_EXPERT_MODE=1 ../Bela/scripts/build_project.sh --force -m "'LDLIBS=$(BELA_LDLIBS)' 'CPPFLAGS=$(BELA_CPPFLAGS)'" -c "$(BELA_COMMAND_ARGS)" $(BUILD_DIR) 

build: $(BUILD_DIR)/render.cpp
	rsync -a $(STATIC_DIR)/* $(BUILD_DIR)/
	BELA_EXPERT_MODE=1 ../Bela/scripts/build_project.sh --force -m "'LDLIBS=$(BELA_LDLIBS)' 'CPPFLAGS=$(BELA_CPPFLAGS)'" -c "$(BELA_COMMAND_ARGS)" $(BUILD_DIR) -n

$(BUILD_DIR)/render.cpp: $(DSP_FILE) faust/architecture/bela.cpp
	@echo RUNNING FAUST
	ARCHFILE="$(ARCHFILE)" faust2bela -gui $(DSP_FILE)

clean:
	ssh root@192.168.7.2 make -C Bela PROJECT=$(BUILD_DIR) clean
	rm -rf $(BUILD_DIR)

