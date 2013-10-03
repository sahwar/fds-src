Users of Unix platforms should read "Unix Note" at the end of this
file before continuing.

To run this demo, open two terminal windows. In the first window,
start the IceBox server:

$ icebox --Ice.Config=config.icebox

Note that for debug Windows builds you will need to use iceboxd rather
than icebox as the executable name.

In the second window, run the client:

$ client

To shut down IceBox, use iceboxadmin:

$ iceboxadmin --Ice.Config=config.admin shutdown

Unix Note:

For icebox to be able to find the library containing the service, you
must add the current directory to your shared library search path
(e.g., LD_LIBRARY_PATH on Linux, SHLIB_PATH on HP-UX, etc.).
Alternatively, you can copy libHelloService.so to a directory that is
already in the search path.
