# Web Server Performance Benchmark Report

Generated on: Tue Sep  9 18:14:37 EAT 2025

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
Requests/sec:  16082.89
Transfer/sec:     31.56MB
```


### ApacheBench Results
#### C++ Multithreaded Server (ab)
```
Requests per second:    4911.08 [#/sec] (mean)
Time per request:       203.621 [ms] (mean)
Time per request:       0.204 [ms] (mean, across all concurrent requests)
Transfer rate:          13380.31 [Kbytes/sec] received
```

