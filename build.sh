#!/usr/bin/env bash

clang++ \
-framework IOKit \
-framework CoreFoundation \
-o ck70 \
hid.c \
main.cpp
