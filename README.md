# CS118 Project 2
Leslie Lam, 804-302-387
Dominic Mortel, 904-287-174
Kevin Huynh, 704-283-105

## Makefile

This provides a couple make targets for things.
By default (all target), it makes the `server` and `client` executables.

It provides a `clean` target, and `tarball` target to create the submission file as well.

## Provided Files

server.cpp : code for server that serves one file through a TCP connection

client.cpp : code for client that downloads a file through a TCP connection

tcp.h : Declaration of the class for the TCP header
