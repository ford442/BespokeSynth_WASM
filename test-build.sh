#!/bin/bash

# Simple test script to verify the build works

echo "🧪 Testing BespokeSynth WASM Build..."
echo ""

# Check if node_modules exists
if [ ! -d "node_modules" ]; then
    echo "❌ node_modules not found. Run 'npm install' first."
    exit 1
fi

# Run the build
echo "📦 Building project..."
npm run build

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Build successful!"
    echo ""
    echo "📁 Output files in dist/:"
    ls -lh dist/
    echo ""
    echo "🎉 All tests passed!"
    echo ""
    echo "To start the dev server, run: npm run dev"
    exit 0
else
    echo ""
    echo "❌ Build failed!"
    exit 1
fi
