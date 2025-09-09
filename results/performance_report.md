# Web Server Performance Benchmark Report

Generated on: Tue Sep  9 18:22:03 EAT 2025

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
Requests/sec:  66595.37
Transfer/sec:    181.82MB
```


### ApacheBench Results
#### C++ Multithreaded Server (ab)
```
Requests per second:    8398.43 [#/sec] (mean)
Time per request:       119.070 [ms] (mean)
Time per request:       0.119 [ms] (mean, across all concurrent requests)
Transfer rate:          23078.62 [Kbytes/sec] received
```

