SHELL := /bin/bash

# Disable built-in rules and variables
MAKEFLAGS += --no-builtin-rules
MAKEFLAGS += --no-builtin-variables

NETWORK := local

###########################################################################
# OS we're running on
ifeq ($(OS),Windows_NT)
	detected_OS := Windows
else
	detected_OS := $(shell sh -c 'uname 2>/dev/null || echo Unknown')
endif

ifeq ($(detected_OS),Darwin)	  # Mac OS X  (Intel)
	OS += macos
	DIDC += didc-macos
endif
ifeq ($(detected_OS),Linux)		  # Ubuntu
	OS += linux
	DIDC += didc-linux64 
endif

ifeq ($(detected_OS),Windows_NT)  # Windows (icpp supports it but you cannot run this Makefile)
	OS += windows_cannot_run_make
endif
ifeq ($(detected_OS),Unknown)     # Unknown
	OS += unknown
endif

###########################################################################
# latest release of didc
VERSION_DIDC := $(shell curl --silent "https://api.github.com/repos/dfinity/candid/releases/latest" | grep -e '"tag_name"' | cut -c 16-25)
# version to install for clang
VERSION_CLANG := $(shell cat version_clang.txt)

###########################################################################
# Use some clang tools that come with wasi-sdk
ICPP_COMPILER_ROOT := $(HOME)/.icpp/wasi-sdk/wasi-sdk-25.0
CLANG_FORMAT = $(ICPP_COMPILER_ROOT)/bin/clang-format
CLANG_TIDY = $(ICPP_COMPILER_ROOT)/bin/clang-tidy


###########################################################################
# CI/CD - Phony Makefile targets
#
.PHONY: all-tests
all-tests: all-static test-all-llms 

.PHONY: summary
summary:
	@echo "-------------------------------------------------------------"
	@echo OS=$(OS)
	@echo VERSION_DIDC=$(VERSION_DIDC)
	@echo VERSION_CLANG=$(VERSION_CLANG)
	@echo CLANG_FORMAT=$(CLANG_FORMAT)
	@echo CLANG_TIDY=$(CLANG_TIDY)
	@echo ICPP_COMPILER_ROOT=$(ICPP_COMPILER_ROOT)
	@echo "-------------------------------------------------------------"

.PHONY: test-all-llms
test-all-llms:
	dfx identity use default
	@echo "#########################################"
	@echo "####### testing llama2_c #############"
	@echo "#########################################"
	cd llama2_c && \
		icpp build-native && \
		./build-native/mockic.exe && \
		./demo.sh && \
		pytest && \
		dfx stop
	
.PHONY: all-static
all-static: \
	cpp-format cpp-lint \
	python-format python-lint python-type
	
CPP_AND_H_FILES = $(shell ls \
llama2_c/src/*.cpp llama2_c/src/*.h \
llama2_c/native/*.cpp llama2_c/native/*.h)

.PHONY: cpp-format
cpp-format:
	@echo "---"
	@echo "cpp-format"
	$(CLANG_FORMAT) --style=file --verbose -i $(CPP_AND_H_FILES)

.PHONY: cpp-lint
cpp-lint:
	@echo "---"
	@echo "cpp-lint"
	@echo "TO IMPLEMENT with clang-tidy"

PYTHON_DIRS ?= llama2_c

.PHONY: python-format
python-format:
	@echo "---"
	@echo "python-format"
	python -m black $(PYTHON_DIRS)

.PHONY: python-lint
python-lint:
	@echo "---"
	@echo "python-lint"
	python -m pylint --jobs=0 --rcfile=.pylintrc $(PYTHON_DIRS)

.PHONY: python-type
python-type:
	@echo "---"
	@echo "python-type"
	python -m mypy --config-file .mypy.ini --show-column-numbers --strict --explicit-package-bases $(PYTHON_DIRS)


###########################################################################
# Toolchain installation for .github/workflows

.PHONY: install-clang-ubuntu
install-clang-ubuntu:
	@echo "Installing clang-$(VERSION_CLANG) compiler"
	sudo apt-get remove python3-lldb-14
	wget https://apt.llvm.org/llvm.sh
	chmod +x llvm.sh
	echo | sudo ./llvm.sh $(VERSION_CLANG)
	rm llvm.sh

	@echo "Creating soft links for compiler executables"
	sudo ln --force -s /usr/bin/clang-$(VERSION_CLANG) /usr/bin/clang
	sudo ln --force -s /usr/bin/clang++-$(VERSION_CLANG) /usr/bin/clang++

# This installs ~/bin/dfx
# Make sure to source ~/.profile afterwards -> it adds ~/bin to the path if it exists
.PHONY: install-dfx
install-dfx:
	DFXVM_INIT_YES=true sh -ci "$$(curl -fsSL https://sdk.dfinity.org/install.sh)"

.PHONY: install-didc
install-didc:
	@echo "Installing didc $(VERSION_DIDC) ..."
	sudo rm -rf /usr/local/bin/didc
	wget https://github.com/dfinity/candid/releases/download/${VERSION_DIDC}/$(DIDC)
	sudo mv $(DIDC) /usr/local/bin/didc
	chmod +x /usr/local/bin/didc
	@echo " "
	@echo "Installed successfully in:"
	@echo /usr/local/bin/didc

.PHONY: install-jp
install-jp:
	sudo apt-get update && sudo apt-get install jp

.PHONY: install-python
install-python:
	pip install --upgrade pip
	pip install -r requirements.txt