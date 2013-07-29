' **********************************************************************
'
' Copyright (c) 2003-2013 ZeroC, Inc. All rights reserved.
'
' This copy of Ice is licensed to you under the terms described in the
' ICE_LICENSE file included in this distribution.
'
' **********************************************************************
Imports Demo

Module CallbackS
    Class Server
        Inherits Ice.Application

        Public Overloads Overrides Function run(ByVal args() As String) As Integer
            If args.Length > 0 Then
                Console.Error.WriteLine(appName() & ": too many arguments")
                Return 1
            End If

            Dim adapter As Ice.ObjectAdapter = communicator().createObjectAdapter("Callback.Server")
            adapter.add(New CallbackSenderI, communicator().stringToIdentity("callbackSender"))
            adapter.activate()
            communicator().waitForShutdown()
            Return 0
        End Function

    End Class
    Sub Main(ByVal args() As String)
        Dim app As Server = New Server
        Dim status As Integer = app.main(args, "config.server")
        System.Environment.Exit(status)
    End Sub

End Module
