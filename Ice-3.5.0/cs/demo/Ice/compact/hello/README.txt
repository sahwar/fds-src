This demo shows how to write a simple C# client application using Ice
for .NET Compact Framework (CF).

This demo requires a hello server. You can use the C# server located
in ..\..\hello, or you can use a server from any other language mapping.

You must use Visual Studio 2008 to build this client (Visual Studio
2010 does not support Compact Framework development). Follow these
instructions to build and run the client:

1) Open the project in Visual Studio 2008. This is a Smart Device
   project and therefore Visual Studio will compile it against the
   Compact Framework libraries. Furthermore, the Ice Visual Studio
   Add-in detects a Smart Device project and automatically
   configures the project to compile against the Ice for .NET CF
   DLL in <Ice installation directory>\Assembles\cf\Ice.dll.

2) Build the client by right-clicking on the project and choosing
   "Build".

3) Start a device emulator. Select Tools -> Device Emulator Manager,
   highlight "USA Windows Mobile 5.0 Pocket PC R2 Emulator", and
   select Actions -> Connect.

4) In the emulator window, select File -> Configure, open the Network
   tab, and check "Enable NE2000 PCMCIA network adapter". Open the
   drop-down menu and select the appropriate network adapter, then
   press OK.

   If you get an error dialog that mentions Virtual PC 2007, you will
   need to install this product to enable network access for the
   emulator. You can download Virtual PC 2007 at the link below:

   http://www.microsoft.com/downloads/en/details.aspx?FamilyID=04d26402-3199-48a3-afa2-2dc0b40a73b6

   Close the emulator, install Virtual PC 2007, and restart the
   emulator.

5) After successfully enabling the network adapter, you may need to
   activate the network connection. Click on the symbol resembling an
   antenna in the device's status bar and choose Connect to start the
   connection. If you wish, start Internet Explorer and verify that
   you can successfully access the Internet.

6) In Visual Studio, right-click on the project, choose Deploy, select
   the "USA Windows Mobile 5.0 Pocket PC R2 Emulator" device, and
   click Deploy. This action causes Visual Studio to install the .NET
   CF run time, the client executable, and the Ice for .NET CF run
   time in the emulator.

7) In a command window, start the hello server.

8) In the emulator, choose Start -> Programs and select File Explorer.
   Click on My Documents and choose My Device. Select Program Files,
   then select "client".

9) Finally, select "client" to start the program. In the Hostname
   field, enter the IP address or host name of the machine on which
   the hello server is running. Click the Say Hello button to send a
   request to the server.

   Note that the initial request takes a little longer to complete
   while the connection is being established.
