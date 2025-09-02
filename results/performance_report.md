# Web Server Performance Benchmark Report

Generated on: Wed Aug 27 03:45:15 EAT 2025

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
Requests/sec:  41258.31
Transfer/sec:     15.54MB
```


### ApacheBench Results
#### C++ Multithreaded Server (ab)
```
