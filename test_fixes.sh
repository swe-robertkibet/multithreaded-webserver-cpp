#!/bin/bash

# Quick test to validate the critical fixes
# Tests file serving, basic concurrency, and connection handling

set -e

echo "Testing critical fixes..."

# Start server in background
cd build
./bin/webserver 8080 &
SERVER_PID=$!
echo "Started server with PID: $SERVER_PID"

# Wait for server to start
sleep 2

# Test 1: Basic file serving (should return 200)
echo "Test 1: Basic file serving"
HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/)
if [ "$HTTP_STATUS" = "200" ]; then
    echo "✅ File serving working (HTTP $HTTP_STATUS)"
else
    echo "❌ File serving failed (HTTP $HTTP_STATUS)"
fi

# Test 2: API endpoint
echo "Test 2: API endpoint"
HTTP_STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/api/info)
if [ "$HTTP_STATUS" = "200" ]; then
    echo "✅ API endpoint working (HTTP $HTTP_STATUS)"
else
    echo "❌ API endpoint failed (HTTP $HTTP_STATUS)"
fi

# Test 3: Multiple concurrent connections
echo "Test 3: Concurrent connections"
for i in {1..10}; do
    curl -s http://127.0.0.1:8080/ > /dev/null &
done
wait
echo "✅ Concurrent requests completed"

# Test 4: Small load test
echo "Test 4: Small load test with ab"
if command -v ab &> /dev/null; then
    ab -n 1000 -c 10 http://127.0.0.1:8080/ > ab_test.log 2>&1 || true
    
    # Check if server is still running
    if kill -0 $SERVER_PID 2>/dev/null; then
        echo "✅ Server survived load test"
    else
        echo "❌ Server crashed during load test"
    fi
    
    # Check for successful requests
    SUCCESS_COUNT=$(grep -o "Complete requests:.*[0-9]" ab_test.log | grep -o "[0-9]*" | tail -1 || echo "0")
    if [ "$SUCCESS_COUNT" -gt 990 ]; then
        echo "✅ Load test successful ($SUCCESS_COUNT/1000 requests)"
    else
        echo "⚠️  Load test had issues ($SUCCESS_COUNT/1000 requests)"
    fi
else
    echo "⚠️  ab not available, skipping load test"
fi

# Cleanup
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true
echo "✅ Test completed"