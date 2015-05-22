import os
import sys
import cmd
import shlex
import pipes
from argparse import ArgumentParser
import pkgutil
from services.fds_auth import FdsAuth
from fdscli import FDSShell

def main():
    cmdargs=sys.argv[1:]

    auth = FdsAuth()
    token = auth.login()

    #login failures will return empty tokens
    if ( token is None ):
        print 'Authentication failed.'
        sys.exit( 1 )

    shell = FDSShell(auth, cmdargs)

    # now we check argv[0] to see if its a shortcut scripts or not
    if ( len( cmdargs ) > 0 ):

        for plugin in shell.plugins:
            detectMethod = getattr( plugin, "detect_shortcut", None )

            # the plugin does not support shortcut argv[0] stuff
            if ( detectMethod is None or not callable( plugin.detect_shortcut ) ):
                continue

            tempArgs = plugin.detect_shortcut( cmdargs )

            # we got a new argument set
            if ( tempArgs is not None ):
                cmdargs = tempArgs
                break
        # end of for loop

    # now actually run the command
    shell.run ( cmdargs )

if __name__ == '__main__':
    main()
