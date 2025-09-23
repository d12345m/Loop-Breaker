#!/bin/bash

# BufferTest - Build and Run Script
# This script builds the JUCE project and runs the application

set -e  # Exit on any error

echo "🔨 Building BufferTest..."
cd "$(dirname "$0")/Builds/MacOSX"

# Build the project
xcodebuild -project BufferTest.xcodeproj -configuration Debug -quiet

# Check if build was successful
if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo "🚀 Launching BufferTest..."
    
    # Run the application
    open "build/Debug/BufferTest.app"
    
    echo "📱 BufferTest is now running!"
else
    echo "❌ Build failed!"
    exit 1
fi
