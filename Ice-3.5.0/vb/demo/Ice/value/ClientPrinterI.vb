' **********************************************************************
'
' Copyright (c) 2003-2013 ZeroC, Inc. All rights reserved.
'
' This copy of Ice is licensed to you under the terms described in the
' ICE_LICENSE file included in this distribution.
'
' **********************************************************************

Imports Demo

Public Class ClientPrinterI
    Inherits ClientPrinter

    Public Overloads Overrides Sub printBackwards(ByVal current As Ice.Current)
        Dim arr() As Char = message.ToCharArray()
        For i As Integer = 0 To arr.Length / 2 - 1
            Dim tmp As Char = arr(arr.Length - i - 1)
            arr(arr.Length - i - 1) = arr(i)
            arr(i) = tmp
        Next
        System.Console.Out.WriteLine(New String(arr))
    End Sub

End Class
