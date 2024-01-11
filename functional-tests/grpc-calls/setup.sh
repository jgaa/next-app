#!/bin/sh

if [ ! -d ".venv" ]; then
    rm -rf .venv
fi

python3 -m venv .venv

. .venv/bin/activate

pip install -r requirements.txt

python -m grpc_tools.protoc -I ../../src/proto --python_out=. --pyi_out=. --grpc_python_out=. ../../src/proto/nextapp.proto
