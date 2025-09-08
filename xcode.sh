#!/bin/sh
cmake -S . -G "Xcode" -S . -B xcode -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
