# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (c) 2020 Olivier Dion <olivier.dion@polymtl.ca>


all: ctest
	./ctest

ctest: ctest.c ctest.h
	gcc  -DCTEST_SELF_TEST -O0 -ggdb3  -Wall -Wextra -fsanitize=address $< -o $@ -lpthread

.PHONY: all
