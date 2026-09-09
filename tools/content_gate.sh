#!/bin/bash
# Content gate: enforces DESIGN.md's hard rules
# Prevents commits that contain references to: child, children, kid, kids, minor, epstein, maxwell, little st
# Searches recursively under assets/ and src/, excluding binary files and .git
# Excludes technical false positives: JSON fields, Python attributes, vendor code, type names

exit_code=0

# Search in assets/ and src/ directories
for dir in assets src; do
  if [ -d "$dir" ]; then
    # Use grep -r to search recursively, -I to exclude binary files, -i for case-insensitive
    # grep -v excludes known false positives
    if grep -r -i -I --exclude-dir=.git \
      -e "child" -e "children" -e "kid" -e "kids" -e "minor" \
      -e "epstein" -e "maxwell" -e "little st" \
      "$dir" 2>/dev/null | \
      grep -v ".gltf.*\"children\"" | \
      grep -v "\.children" | \
      grep -v "vendor/" | \
      grep -v "JoystickID" | \
      grep -v "Binary"; then
      exit_code=1
    fi
  fi
done

exit $exit_code
