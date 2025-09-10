# Stress Test Report

Generated on: Wed Sep 10 18:33:20 EAT 2025

## Test Summary

This report contains the results of comprehensive stress testing for the
C++ Multithreaded Web Server under extreme load conditions.

## Tests Performed

1. **High Connection Count Test**: Testing scalability with increasing concurrent connections
2. **Extended Duration Test**: Validating stability over long periods
3. **Memory Leak Detection**: Monitoring memory usage under sustained load
4. **Error Handling Test**: Verifying server resilience against malformed requests

## Results

### Connection Count Test Results
```
=== stress_100 ===
Requests/sec:  39565.85
Transfer/sec:    155.80MB
=== stress_1000 ===
Requests/sec:  78091.55
Transfer/sec:    305.45MB
=== stress_10m ===
Requests/sec:  48858.91
Transfer/sec:    192.77MB
=== stress_1m ===
Requests/sec:  46706.94
Transfer/sec:    183.92MB
=== stress_2000 ===
Requests/sec:  73075.56
Transfer/sec:    285.81MB
=== stress_500 ===
Requests/sec:  55934.55
Transfer/sec:    219.24MB
=== stress_5000 ===
Requests/sec:  71508.89
Transfer/sec:    279.82MB
=== stress_5m ===
Requests/sec:  47831.58
Transfer/sec:    188.71MB
```

