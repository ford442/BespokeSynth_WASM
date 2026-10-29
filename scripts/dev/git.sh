#!/bin/sh
# Quick "commit everything and push" helper. Runs from the repo root so `git add .` covers the whole tree.
set -e
cd "$(git rev-parse --show-toplevel)"
git add .
git commit -m "${1:-codespace}"
git push
