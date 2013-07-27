This example demonstrates the use of Freeze to manage contacts in a
phone book.

To run the demo, first start the server:

$ server

In another window, populate the server's database by starting the
client and redirecting its input from the file "contacts":

$ client < contacts

Then start the client again to use the demo interactively:

$ client

Type "help" to get a list of valid commands.

The collocated executable combines the client and server into one
program. In this case you do not need to start a separate server.
If you have not already populated the database, you can do so using
the collocated executable as shown below:

$ collocated < contacts

Then start the interactive demo like this:

$ collocated
