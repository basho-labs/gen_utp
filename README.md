# gen_utp: an API and driver for the uTP protocol

## Description

`gen_utp` provides an API and driver for the
[Micro Transport Protocol](http://en.wikipedia.org/wiki/Micro_Transport_Protocol)
(uTP), similar to `gen_tcp` and `gen_udp`. It attempts to provide a
TCP-like API with `listen`, `accept`, and `connect` calls, but due to the
nature of the underlying
[libutp library](https://github.com/bittorrent/libutp) the semantics are
not identical.

`gen_utp` provides the following support:

 * server `listen` and `accept`
 * client `connect`
 * both `list` and `binary` modes for incoming messages
 * `active` settings of `true`, `false`, and `once`
 * controlling processes
 * `setopts` and `getopts` calls
 * IPv4 and IPv6
 * server `accept` can be async, or blocking with optional timeout
 * `recv` with optional timeout

Currently, the server `listen` call differs from TCP in that it doesn't
store a backlog; if async accept is not enabled and there are no waiting
acceptors, the `listen` socket just drops incoming connection
attempts. Hopefully this shortcoming will be fixed in the future.

Look at the tests under `test/gen_utp_tests.erl` for usage examples. More
documentation to follow, and more tests are needed as well.
