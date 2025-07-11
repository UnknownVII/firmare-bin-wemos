#!/bin/bash

# Optional: Print the current directory
echo "📂 Working in $(pwd)"

# Add all changes
git add .

# Commit with a standard message
git commit -m "🚀 New firmware update"

# Fetch latest changes from all remotes
git fetch --all

# Pull latest from the current branch
git pull

# Push your commit
git push

echo "✅ Firmware pushed to GitHub!"
