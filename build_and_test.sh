#!/bin/bash
echo "==================================="
echo "Building and Testing RPC DSL"
echo "==================================="
mkdir -p build
cd build || exit 1
echo ""
echo "--- Configuring CMake ---"
if ! cmake ..; then
echo ""
echo "==================================="
echo "FAILURE: CMake configuration failed."
echo "==================================="
exit 1
fi
echo ""
echo "--- Building Project ---"
if ! cmake --build .; then
echo ""
echo "==================================="
echo "FAILURE: Build failed."
echo "==================================="
exit 1
fi
echo ""
echo "--- Running Tests ---"
if ! ctest --output-on-failure; then
echo ""
echo "==================================="
echo "FAILURE: Tests failed."
echo "==================================="
exit 1
fi
echo ""
echo "==================================="
echo "SUCCESS: Build and tests passed!"
echo "==================================="
