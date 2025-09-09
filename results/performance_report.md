# Web Server Performance Benchmark Report

Generated on: Tue Sep  9 17:54:59 EAT 2025

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
Requests/sec:  71514.61
Transfer/sec:    195.37MB
```


### ApacheBench Results
#### C++ Multithreaded Server (ab)
```
Requests per second:    8243.20 [#/sec] (mean)
Time per request:       121.312 [ms] (mean)
Time per request:       0.121 [ms] (mean, across all concurrent requests)
Transfer rate:          22682.99 [Kbytes/sec] received
```

