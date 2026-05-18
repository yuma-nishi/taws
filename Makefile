#--------------------------------------------------
# Copyright (c) 2026 SECOM CO., LTD. All Rights reserved.
#
# SPDX-License-Identifier: BSD-2-Clause
#--------------------------------------------------

## TEEP Application

ROOT_DIR ?= $(CURDIR)

SYS_INC = \
	-isystem $(ROOT_DIR)/third_party/QCBOR/inc \
	-isystem $(ROOT_DIR)/third_party/t_cose/inc \
	-isystem $(ROOT_DIR)/third_party/libteep/inc \
	-isystem $(ROOT_DIR)/third_party/libcsuit/inc \
	-isystem $(ROOT_DIR)/third_party/intel-sgx-ssl/Linux/package/include

WAMR_SYS_INC = \
	-isystem $(ROOT_DIR)/third_party/wasm-micro-runtime/core/iwasm/include \
	-isystem $(ROOT_DIR)/third_party/wasm-micro-runtime/core/shared/utils \
	-isystem $(ROOT_DIR)/third_party/wasm-micro-runtime/core/shared/platform/linux-sgx

######## SGX SDK Settings ########

SGX_SDK ?= /opt/intel/sgxsdk
SGX_MODE ?= HW
SGX_ARCH ?= x64
SGX_DEBUG ?= 0

ifeq ($(shell getconf LONG_BIT), 32)
    SGX_ARCH := x86
else ifeq ($(findstring -m32, $(CXXFLAGS)), -m32)
    SGX_ARCH := x86
endif

ifeq ($(SGX_ARCH), x86)
    SGX_COMMON_FLAGS := -m32
    SGX_LIBRARY_PATH := $(SGX_SDK)/lib
    SGX_ENCLAVE_SIGNER := $(SGX_SDK)/bin/x86/sgx_sign
    SGX_EDGER8R := $(SGX_SDK)/bin/x86/sgx_edger8r
else
    SGX_COMMON_FLAGS := -m64
    SGX_LIBRARY_PATH := $(SGX_SDK)/lib64
    SGX_ENCLAVE_SIGNER := $(SGX_SDK)/bin/x64/sgx_sign
    SGX_EDGER8R := $(SGX_SDK)/bin/x64/sgx_edger8r
endif

ifeq ($(SGX_DEBUG), 1)
        SGX_COMMON_FLAGS += -O0 -g -DDEBUG
else
        SGX_COMMON_FLAGS += -O2
endif

SGX_COMMON_FLAGS += -Wall -Wextra -Winit-self -Wpointer-arith -Wreturn-type \
                    -Waddress -Wsequence-point -Wformat-security \
                    -Wmissing-include-dirs -Wfloat-equal -Wundef -Wshadow \
                    -Wcast-align -Wcast-qual -Wconversion -Wredundant-decls
SGX_COMMON_CFLAGS := $(SGX_COMMON_FLAGS) -Wjump-misses-init -Wstrict-prototypes -Wunsuffixed-float-constants
SGX_COMMON_CXXFLAGS := $(SGX_COMMON_FLAGS) -Wnon-virtual-dtor -std=c++11

######## App Settings ########
ifneq ($(SGX_MODE), HW)
    Urts_Library_Name := sgx_urts_sim
    Uae_Service_Library_Name := sgx_uae_service_sim
else
    Urts_Library_Name := sgx_urts
    Uae_Service_Library_Name := sgx_uae_service
endif

App_Cpp_Files := App/src/sgx_teep_session.cpp App/src/teep_http_client.cpp App/src/dcap_quote_ocalls.cpp
App_Include_Paths := -IApp -IApp/inc -Icommon -I$(SGX_SDK)/include $(SYS_INC)

App_C_Flags := -fPIC -Wno-attributes $(App_Include_Paths)

App_Cpp_Flags := $(App_C_Flags)
App_Link_Flags := -L$(SGX_LIBRARY_PATH) -l$(Urts_Library_Name) -l$(Uae_Service_Library_Name) -lpthread -lsgx_usgxssl -lsgx_dcap_ql -L./lib -lteep -lqcbor -lcurl $(ROOT_DIR)/third_party/wasm-micro-runtime/product-mini/platforms/linux-sgx/build/libvmlib_untrusted.a


App_Cpp_Objects := $(App_Cpp_Files:.cpp=.o)

App_Name := teep_wasm_get
BUILD_APP ?= 0
APP_TARGET := $(if $(filter 1,$(BUILD_APP)),$(App_Name),)

GO_BUILD_DIR := $(ROOT_DIR)/build/go
GO_BIN := taws
GO_LIB := $(GO_BUILD_DIR)/libattester.a
GO_APP_CPP_OBJS := $(GO_BUILD_DIR)/sgx_teep_session.o $(GO_BUILD_DIR)/teep_http_client.o $(GO_BUILD_DIR)/dcap_quote_ocalls.o $(GO_BUILD_DIR)/attester_api.o
GO_APP_C_OBJS := $(GO_BUILD_DIR)/Enclave_u.o

######## Enclave Settings ########
LIBTEEP := ./lib/libteep.a
LIBCSUIT := ./lib/libcsuit.a
LIBT_COSE := ./lib/libt_cose.a
LIBQCBOR := ./lib/libqcbor.a
LIBSGXSSL := ./lib/libsgx_tsgxssl.a ./lib/libsgx_tsgxssl_crypto.a
SGXSSL_EDL_PATH := $(CURDIR)/third_party/intel-sgx-ssl/Linux/package/include
SGXSSL_EDL_PATHS := --search-path $(SGXSSL_EDL_PATH) --search-path $(SGXSSL_EDL_PATH)/filefunc --search-path $(SGXSSL_EDL_PATH)/nofilefunc


Enclave_Version_Script := Enclave/Enclave_debug.lds
ifeq ($(SGX_MODE), HW)
ifneq ($(SGX_DEBUG), 1)
    # Choose to use 'Enclave.lds' for HW release mode
    Enclave_Version_Script = Enclave/Enclave.lds 
endif
endif

ifneq ($(SGX_MODE), HW)
    Trts_Library_Name := sgx_trts_sim
    Service_Library_Name := sgx_tservice_sim
else
    Trts_Library_Name := sgx_trts
    Service_Library_Name := sgx_tservice
endif
Crypto_Library_Name := sgx_tcrypto

Enclave_Cpp_Files := Enclave/src/Enclave.cpp Enclave/src/Enclave_wasm.cpp \
					 Enclave/src/Enclave_generate_keypair.cpp Enclave/src/Enclave_process_message.cpp \
					 Enclave/src/Enclave_generate_evidence.cpp\
					 Enclave/src/suit_processor_wrapper.cpp \
					 Enclave/src/tc_manager.cpp
Enclave_Include_Paths := -IEnclave -IEnclave/inc -Icommon -I$(SGX_SDK)/include -I$(SGX_SDK)/include/libcxx -I$(SGX_SDK)/include/tlibc $(SYS_INC) \
						$(WAMR_SYS_INC)
						 

Enclave_C_Flags := -nostdinc -fvisibility=hidden -fpie -fstack-protector -fno-builtin-printf $(Enclave_Include_Paths)
Enclave_Cpp_Flags := $(Enclave_C_Flags) -nostdinc++
# Enable the security flags
Enclave_Security_Link_Flags := -Wl,-z,relro,-z,now,-z,noexecstack

# To generate a proper enclave, it is recommended to follow below guideline to link the trusted libraries:
#    1. Link sgx_trts with the `--whole-archive' and `--no-whole-archive' options,
#       so that the whole content of trts is included in the enclave.
#    2. For other libraries, you just need to pull the required symbols.
#       Use `--start-group' and `--end-group' to link these libraries.
# Do NOT move the libraries linked with `--start-group' and `--end-group' within `--whole-archive' and `--no-whole-archive' options.
# Otherwise, you may get some undesirable errors.
Enclave_Link_Flags := $(Enclave_Security_Link_Flags) \
	-Wl,--no-undefined -nostdlib -nodefaultlibs -nostartfiles -L$(SGX_LIBRARY_PATH) -L./lib\
	-Wl,--whole-archive -l$(Trts_Library_Name) $(LIBCSUIT) $(LIBTEEP) $(LIBQCBOR) -Wl,--no-whole-archive \
	-Wl,--start-group -lsgx_pthread -lsgx_tstdc -lsgx_tcxx -l$(Crypto_Library_Name) -l$(Service_Library_Name) $(LIBSGXSSL) $(LIBT_COSE) $(ROOT_DIR)/third_party/wasm-micro-runtime/product-mini/platforms/linux-sgx/build/libvmlib.a -Wl,--end-group \
	-Wl,-Bstatic -Wl,-Bsymbolic -Wl,--no-undefined \
	-Wl,-pie,-eenclave_entry -Wl,--export-dynamic  \
	-Wl,--defsym,__ImageBase=0 \
	-Wl,--version-script=$(Enclave_Version_Script) \
	-Wl,--wrap=suit_store_callback \
	-Wl,--wrap=suit_invoke_callback \
	-Wl,--wrap=suit_report_callback \
	-Wl,--wrap=suit_condition_callback

Enclave_Cpp_Objects := $(Enclave_Cpp_Files:.cpp=.o)

Enclave_Name := enclave.so
Signed_Enclave_Name := enclave.signed.so
Enclave_Config_File := Enclave/Enclave.config.xml
Enclave_Test_Key := Enclave/Enclave_private_test.pem

ifeq ($(SGX_MODE), HW)
ifeq ($(SGX_DEBUG), 1)
    Build_Mode = HW_DEBUG
else
    Build_Mode = HW_RELEASE
endif
else
ifeq ($(SGX_DEBUG), 1)
    Build_Mode = SIM_DEBUG
else
    Build_Mode = SIM_RELEASE
endif
endif


.PHONY: all run target go-front run-go run-web
all: .config_$(Build_Mode)_$(SGX_ARCH)
	@$(MAKE) target

ifeq ($(Build_Mode), HW_RELEASE)
target: $(APP_TARGET) $(Enclave_Name) go-front
	@echo "The project has been built in release hardware mode."
ifneq ($(APP_TARGET),)
	@echo "Please sign the $(Enclave_Name) first with your signing key before you run the $(App_Name) to launch and access the enclave."
endif
	@echo "To sign the enclave use the command:"
	@echo "   $(SGX_ENCLAVE_SIGNER) sign -key <your key> -enclave $(Enclave_Name) -out <$(Signed_Enclave_Name)> -config $(Enclave_Config_File)"
	@echo "You can also sign the enclave using an external signing tool."
	@echo "To build the project in simulation mode set SGX_MODE=SIM."
else
target: $(APP_TARGET) $(Signed_Enclave_Name) go-front
ifeq ($(Build_Mode), HW_DEBUG)
	@echo "The project has been built in debug hardware mode."
else ifeq ($(Build_Mode), SIM_DEBUG)
	@echo "The project has been built in debug simulation mode."
else
	@echo "The project has been built in release simulation mode."
endif
endif

run: all
ifneq ($(Build_Mode), HW_RELEASE)
ifneq ($(APP_TARGET),)
	@$(CURDIR)/$(App_Name)
	@echo "RUN  =>  $(App_Name) [$(SGX_MODE)|$(SGX_ARCH), OK]"
endif
endif

.config_$(Build_Mode)_$(SGX_ARCH):
	@rm -f .config_* $(App_Name) $(Enclave_Name) $(Signed_Enclave_Name) $(App_Cpp_Objects) App/Enclave_u.* $(Enclave_Cpp_Objects) Enclave/Enclave_t.*
	@touch .config_$(Build_Mode)_$(SGX_ARCH)

######## App Objects ########

App/Enclave_u.h: $(SGX_EDGER8R) 
	@cd App && $(SGX_EDGER8R) --untrusted ../Enclave/Enclave.edl --search-path ../Enclave --search-path $(ROOT_DIR)/common --search-path $(SGX_SDK)/include $(SGXSSL_EDL_PATHS) --search-path $(ROOT_DIR)/third_party/wasm-micro-runtime/core/shared/platform/linux-sgx
	@echo "GEN  =>  $@"

App/Enclave_u.c: App/Enclave_u.h

App/Enclave_u.o: App/Enclave_u.c
	$(CC) $(SGX_COMMON_CFLAGS) $(App_C_Flags) -c $< -o $@
	@echo "CC   <=  $<"

App/%.o: App/%.cpp App/Enclave_u.h
	$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Cpp_Flags) -c $< -o $@
	@echo "CXX  <=  $<"

App/src/sgx_teep_session.o: common/teep_buffer_sizes.h

$(GO_BUILD_DIR):
	@mkdir -p $(GO_BUILD_DIR)

$(GO_BUILD_DIR)/sgx_teep_session.o: App/src/sgx_teep_session.cpp App/Enclave_u.h common/teep_buffer_sizes.h | $(GO_BUILD_DIR)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Cpp_Flags) -DATTESTER_NO_MAIN -c $< -o $@
	@echo "CXX  <=  $< (go)"

$(GO_BUILD_DIR)/teep_http_client.o: App/src/teep_http_client.cpp | $(GO_BUILD_DIR)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Cpp_Flags) -DATTESTER_NO_MAIN -c $< -o $@
	@echo "CXX  <=  $< (go)"

$(GO_BUILD_DIR)/dcap_quote_ocalls.o: App/src/dcap_quote_ocalls.cpp App/inc/dcap_quote_ocalls.h | $(GO_BUILD_DIR)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Cpp_Flags) -DATTESTER_NO_MAIN -c $< -o $@
	@echo "CXX  <=  $< (go)"

$(GO_BUILD_DIR)/attester_api.o: App/src/attester_api.cpp App/inc/attester_api.h | $(GO_BUILD_DIR)
	@$(CXX) $(SGX_COMMON_CXXFLAGS) $(App_Cpp_Flags) -DATTESTER_NO_MAIN -c $< -o $@
	@echo "CXX  <=  $< (go)"

$(GO_BUILD_DIR)/Enclave_u.o: App/Enclave_u.c App/Enclave_u.h | $(GO_BUILD_DIR)
	@$(CC) $(SGX_COMMON_CFLAGS) $(App_C_Flags) -c $< -o $@
	@echo "CC   <=  $< (go)"

$(GO_LIB): $(GO_APP_CPP_OBJS) $(GO_APP_C_OBJS)
	@ar rcs $@ $^
	@echo "AR   =>  $@"

go-front: $(GO_LIB)
	@CGO_ENABLED=1 \
	CGO_CFLAGS="$(App_Include_Paths)" \
	CGO_LDFLAGS="-L$(GO_BUILD_DIR) -lattester $(App_Link_Flags) -lstdc++" \
	go build -o $(GO_BUILD_DIR)/$(GO_BIN) ./yolov8-frontend
	@echo "GO   =>  $(GO_BUILD_DIR)/$(GO_BIN)"

run-go: go-front
	@$(GO_BUILD_DIR)/$(GO_BIN)

run-web: go-front
	@$(GO_BUILD_DIR)/$(GO_BIN) web

$(App_Name): App/Enclave_u.o $(App_Cpp_Objects)
	$(CXX) $^ -o $@ $(App_Link_Flags)
	@echo "LINK =>  $@"

######## Enclave Objects ########

Enclave/Enclave_t.h: $(SGX_EDGER8R) Enclave/Enclave.edl
	@cd Enclave && $(SGX_EDGER8R) --trusted ../Enclave/Enclave.edl --search-path ../Enclave --search-path $(ROOT_DIR)/common --search-path $(SGX_SDK)/include $(SGXSSL_EDL_PATHS) --search-path $(ROOT_DIR)/third_party/libteep/inc --search-path $(ROOT_DIR)/third_party/wasm-micro-runtime/core/shared/platform/linux-sgx
	@echo "GEN  =>  $@"

Enclave/Enclave_t.c: Enclave/Enclave_t.h

Enclave/Enclave_t.o: Enclave/Enclave_t.c
	$(CC) $(SGX_COMMON_CFLAGS) $(Enclave_C_Flags) -c $< -o $@
	@echo "CC   <=  $<"

Enclave/%.o: Enclave/%.cpp
	$(CXX) $(SGX_COMMON_CXXFLAGS) $(Enclave_Cpp_Flags) -c $< -o $@
	@echo "CXX  <=  $<"

$(Enclave_Cpp_Objects): Enclave/Enclave_t.h
Enclave/src/Enclave_process_message.o: common/teep_buffer_sizes.h

$(Enclave_Name): Enclave/Enclave_t.o $(Enclave_Cpp_Objects)
	$(CXX) $^ -o $@ $(Enclave_Link_Flags) 
	@echo "LINK =>  $@"

$(Signed_Enclave_Name): $(Enclave_Name)
ifeq ($(wildcard $(Enclave_Test_Key)),)
	@echo "There is no enclave test key<Enclave_private_test.pem>."
	@echo "The project will generate a key<Enclave_private_test.pem> for test."
	@openssl genrsa -out $(Enclave_Test_Key) -3 3072
endif
	@$(SGX_ENCLAVE_SIGNER) sign -key $(Enclave_Test_Key) -enclave $(Enclave_Name) -out $@ -config $(Enclave_Config_File)
	@echo "SIGN =>  $@"

.PHONY: clean

clean:
	@rm -f .config_* $(App_Name) $(Enclave_Name) $(Signed_Enclave_Name) $(App_Cpp_Objects) App/Enclave_u.* $(Enclave_Cpp_Objects) Enclave/Enclave_t.*
	@rm -rf $(GO_BUILD_DIR)
