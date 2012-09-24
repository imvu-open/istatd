
splitd TCP splitter daemon
==========================

Copyright 2012 IMVU, Inc. All Rights Reserved.
Author: jwatte@imvu.com

splitd is an experimental tool that allows you to split TCP sessions 
for doing things like parallel load testing and protocol regression 
testing.

usage: splitd <listen-port> <forward-1> [<forward-2> ...]

splitd will listen to the given port for incoming TCP v4 connections. 
When a connection comes in, it will attempt to connect to each of the 
specified forward addresses (which are specified as host:port). If 
all the connections succeed, it will enter a simple read-and-forward 
loop going each way between the incoming connection and the first 
forward target. For each other forward target, splitd will still 
forward input from the incoming connection, and it will receive data 
from the forwarded-to connection, but that data will be ignored.

When any one connection drops (incoming, or any forwarded connection,) 
splitd drops all the related connections.

splitd uses fork(), so it probably won't scale well beyond 1,000 or 
so active connections.
