# Web Server Performance Benchmark Report

Generated on: Tue Sep  9 16:37:56 EAT 2025

## Test Configuration
- **Duration**: 30s
- **Concurrent Connections**: 1000
- **Worker Threads**: 10
- **Total Requests (AB)**: 100000

## Servers Tested
1. **C++ Multithreaded Server** - Custom epoll + thread pool implementation
2. **Nginx** - Industry standard web server
3. **Apache HTTP Server** - Popular web server

## Results Summary

### wrk Benchmark Results
#### C++ Multithreaded Server (wrk)
```
Requests/sec:  82125.64
Transfer/sec:    167.28MB
```


### ApacheBench Results
#### C++ Multithreaded Server (ab)
```
Requests per second:    9759.30 [#/sec] (mean)
Time per request:       102.466 [ms] (mean)
Time per request:       0.102 [ms] (mean, across all concurrent requests)
Transfer rate:          21084.80 [Kbytes/sec] received
```

