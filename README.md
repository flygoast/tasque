tasque
======

A simple task memory queue.

tasque (task queue) is very simple task queue. It is just a rewrite of
beanstalkd, so I directly copy its protocol documentation. Please 
refer doc/protocol.txt.

At present, the job id management implementation has some problems
-- just increase by one. Someday, it will get the limit of integer.
Leave it as TODO.
