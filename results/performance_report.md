# Web Server Performance Benchmark Report

Generated on: Tue Sep  9 18:35:24 EAT 2025

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
Requests/sec:  71085.10
Transfer/sec:    135.39MB
```


### ApacheBench Results
#### C++ Multithreaded Server (ab)
```
Requests per second:    8177.39 [#/sec] (mean)
Time per request:       122.288 [ms] (mean)
Time per request:       0.122 [ms] (mean, across all concurrent requests)
Transfer rate:          17706.66 [Kbytes/sec] received
```

