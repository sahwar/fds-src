#!/usr/bin/python

import os, re, sys, time
import SocketServer

class FdsUDPHandler(SocketServer.BaseRequestHandler):
    """
    This class works similar to the TCP handler class, except that
    self.request consists of a pair of data and client socket, and since
    there is no connection the client address must be given explicitly
    when sending data back via sendto().
    """
    def handle(self):
        data = self.request[0].strip()
        socket = self.request[1]
        os.write(self.server.datafile, "{} wrote:".format(self.client_address[0]) + " clock:" + str(time.time()) + "\n")
        os.write(self.server.datafile, data + "\n------- End of sample ------\n")
        socket.sendto(data.upper(), self.client_address)

if __name__ == "__main__":
    HOST, PORT = "10.1.10.102", 2003
    server = SocketServer.UDPServer((HOST, PORT), FdsUDPHandler)
    server.serve_forever()


