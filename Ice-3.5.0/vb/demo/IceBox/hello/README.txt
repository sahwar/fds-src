To run this demo, open two terminal windows. In the first window,
start the IceBox server:

$ iceboxnet --Ice.Config=config.icebox

In the second window, run the client:

$ client

To shut down IceBox, use iceboxadmin:

$ iceboxadmin --Ice.Config=config.admin shutdown
