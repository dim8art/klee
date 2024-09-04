# This file is part of TestCov,
# a robust test executor with reliable coverage measurement:
# https://gitlab.com/sosy-lab/software/test-suite-validator/
#
# SPDX-FileCopyrightText: 2021 Dirk Beyer <https://www.sosy-lab.org>
#
# SPDX-License-Identifier: Apache-2.0

BUILD_DIR := build
PREFIX := .

build: ${BUILD_DIR}/label-adder/bin/label-adder

install: ${BUILD_DIR}/label-adder/bin/label-adder
	mkdir -p ${PREFIX}/bin
	mv $< ${PREFIX}/bin/

${BUILD_DIR}/label-adder/bin/label-adder: ${BUILD_DIR}/label-adder/CMakeCache.txt src/*
	cd ${BUILD_DIR}/label-adder \
		&& ninja label-adder

${BUILD_DIR}/label-adder/CMakeCache.txt: src/CMakeLists.txt
	mkdir -p ${BUILD_DIR}/label-adder \
		&& cd ${BUILD_DIR}/label-adder \
		&& cmake -G Ninja ../llvm -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD="X86" -DLLVM_BUILD_TOOLS=OFF

clean:
	${RM} -rf ${BUILD_DIR}
