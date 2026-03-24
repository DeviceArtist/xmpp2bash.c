#!/bin/bash
prompt="$1"

if [ -z "$prompt" ]; then
  prompt="hello"
fi

curl -s http://localhost:8000/completion \
  -H "Content-Type: application/json" \
  -d "{\"prompt\": \"$prompt\"}" | jq -r '.content'