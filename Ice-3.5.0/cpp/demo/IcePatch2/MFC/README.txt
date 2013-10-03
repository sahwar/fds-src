This demo illustrates how to use MFC to create a simple IcePatch2
client application.

In order to use this demo, you must first prepare a sample data
directory for the IcePatch2 server. The contents of the data
directory are what the client will receive during the patch process.
For convenience, this example uses its own source files as the data
to be patched.

Assuming that your current working directory is demo/IcePatch2/MFC,
you can prepare the data files with the following command:

> icepatch2calc .

Next, start the IcePatch2 server. Note that the trailing "." argument
in the following command is required and directs the server to use the
current directory as its data directory:

> icepatch2server --IcePatch2.Endpoints="tcp -h 127.0.0.1 -p 10000" .

Before we start the patch client, we must first create an empty
directory where the downloaded files will be placed. For example, open
a new command window and execute this command:

> mkdir C:\icepatch_download

Now you can start the IcePatch2 client (You will also need to pass an
argument that defines IcePatch2Client.Proxy if you use an endpoint other
than the one shown above when you start icepatch2server):

> client

Click the "..." button to the right of the Patch Directory field and
select the empty directory you just created. Then click the "Patch"
button to begin the patch process. The first time you click this
button you will be prompted to confirm that you want to perform a
thorough patch (because the target download directory is empty).

If you add files or delete files from the data directory or make
changes to existing files, you must stop the server, run icepatch2calc
again to update the data files, and then restart the IcePatch2 server.
