#!/usr/bin/env bash
set -euo pipefail

# SPACE_URL="http://your-space.hf.space"
SPACE_URL="https://awesomesauce10-pothole.hf.space"
AUTH_TOKEN="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"
SPACE_URL="https://awesomesauce10-pothole.hf.space"
AUTH_TOKEN="accesstokenhere"

auth_header=()
if [[ -n "$AUTH_TOKEN" ]]; then
  auth_header=(-H "Authorization: Bearer $AUTH_TOKEN")
fi

curl -X POST "$SPACE_URL/upload" \
  "${auth_header[@]}" \
  -H "Content-Type: application/json" \
  -d '{"session_id":"CAR001","type":"gps","timestamp":1697358001,"lat":40.7128,"lon":-74.0060}'

curl -X POST "$SPACE_URL/upload" \
  "${auth_header[@]}" \
  -F "session_id=CAR001" \
  -F "type=image" \
  -F "timestamp=1697358002" \
  -F "image=@test-images/real.jpg"
