#!/bin/bash
# resolve_stack_demangle.sh
#
# Usage:
#   ./resolve_stack_demangle.sh <binary> <symbols_file> <stack_dump_file> [offset]
#
#   <binary>           : Path to the executable binary.
#   <symbols_file>     : Path to the separate symbols file.
#   <stack_dump_file>  : File containing the raw stack trace.
#   [offset]           : (Optional) Hexadecimal offset (e.g. 0x400000) to subtract from addresses
#
# This script creates a temporary copy of the binary, attaches the debug link,
# then processes the stack dump. For each address found that comes from the binary,
# it subtracts the offset (if provided) and uses addr2line to resolve the symbol.
# The function name returned by addr2line is then demangled with c++filt.
#
# Requirements: objcopy, addr2line, c++filt, bash

if [ "$#" -lt 3 ]; then
    echo "Usage: $0 <binary> <symbols_file> <stack_dump_file> [offset]"
    exit 1
fi

binary="$1"
symbols="$2"
stack_dump="$3"
offset="$4"  # Optional offset in hex (e.g. 0x400000)

# Get the basename of the binary (for matching in the stack trace)
binary_base=$(basename "$binary")

# Create a temporary copy of the binary
tmp_binary=$(mktemp /tmp/tmp_binary.XXXXXX) || { echo "Failed to create temporary file"; exit 1; }
cp "$binary" "$tmp_binary" || { echo "Failed to copy binary"; exit 1; }

# Attach the separate symbols file via a GNU debuglink.
if ! objcopy --add-gnu-debuglink="$symbols" "$tmp_binary"; then
    echo "Failed to add debuglink from symbols file"
    rm "$tmp_binary"
    exit 1
fi

echo "=========================================="
echo "Resolved Stack Trace:"
echo "=========================================="

# Process the stack dump file line-by-line.
while IFS= read -r line; do
    # Look for a hexadecimal address in the line.
    if [[ $line =~ (0x[0-9A-Fa-f]+) ]]; then
        orig_addr="${BASH_REMATCH[1]}"
        resolved_addr="$orig_addr"
        # If an offset is provided and this line refers to our binary, adjust the address.
        if [ -n "$offset" ]; then
            if [[ $line == *"$binary"* || $line == *"$binary_base"* ]]; then
                # Bash arithmetic with hex numbers.
                adjusted=$(( ${orig_addr} - ${offset} ))
                resolved_addr=$(printf "0x%x" "$adjusted")
            fi
        fi
        # Use addr2line to get function and source information.
        # addr2line returns two lines: first the mangled function name, second the file:line.
        symbol_info=$(addr2line -f -e "$tmp_binary" "$resolved_addr")
        # Extract the first line (function name) and the rest (source info)
        func_name=$(echo "$symbol_info" | head -n1)
        src_info=$(echo "$symbol_info" | tail -n +2)
        # Demangle the function name using c++filt.
        demangled=$(echo "$func_name" | c++filt)
        echo "$line"
        echo "    => $demangled"
        echo "       $src_info"
    else
        # Print any lines that do not contain an address as is.
        echo "$line"
    fi
done < "$stack_dump"

# Clean up temporary binary.
rm "$tmp_binary"
