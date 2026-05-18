#!/usr/bin/env bash

clang++ \
-std=c++11 \
-framework IOKit \
-framework CoreFoundation \
-o ck70 \
hid.c \
main.cpp
