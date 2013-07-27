This is a PHP version of the Ice "value" demo client.

First, start the value demo server, using any of the supported language
mappings, on this host.

Then, if you are using an RPM installation of IcePHP, run the PHP
client:

$ php -f Client.php

Otherwise, review the comments in the php.ini file to determine whether
any changes are needed for your environment, and then run the PHP
client:

$ php -c php.ini -f Client.php

Note that this client requires user input, therefore you must run this
example using the CLI version of the PHP interpreter.

An alternate version of the client that uses PHP namespaces is
provided as Client_ns.php.
