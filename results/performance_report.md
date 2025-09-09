# Web Server Performance Benchmark Report

Generated on: Tue Sep  9 16:21:12 EAT 2025

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
Requests/sec:  33438.80
Transfer/sec:     12.63MB
```


### ApacheBench Results
#### C++ Multithreaded Server (ab)
```
