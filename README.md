# CSE-130: Principles of Complex Computer System Design

- This repo contains the most significant work I've completed during this course

- I designed a multithreaded web server based on HTTP/1.1 protocol, which was able to process concurrent GET, PUT and HEAD requests. This server had the additional feature of logging hex-dumps, and support for health-checks using GET requests. After compiling, type "./httpserver [-l logfilename] [-N numthreads] (portnumber)" to run

- A load balancer was also created to accept concurrent requests from one port (listenport), and distribute the requests amongst a set of web servers (serverportx). After compiling, type "./loadbalancer [-R numintervals] [-N numthreads] (listenport) (serverport1 [... serverportn])" to run

## Files

- httpserver.c - source code for multithreaded httpserver
- loadbalancer.c - source code for multithreaded load balancer
- queue.c - source code for queue implementation (from zentut.com)
- queue.h - header file for queue implementation (also from zentut.com)
- Makefile - run 'make' to compile the file
- DESIGN-HB.pdf - Design document for the general structure of the 'httpserver' source code
- DESIGN-LB.pdf - Design document for the general structure of the 'loadbalancer' source code
- README.md - this file
