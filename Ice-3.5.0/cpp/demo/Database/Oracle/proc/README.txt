Oracle Pro*C/C++ demo
=====================

This demo shows how to implement an Ice server that uses Oracle
through its Embedded SQL (Pro*C/C++) API.

It is a fairly simple demo that illustrates how to:

 - Map relational data to Ice objects, in particular convert 
   between Ice and Oracle Pro*C/C++ types.
 - Associate an Oracle Pro*C/C++ context and database connection
   to each thread in the Ice server thread pool.
 - Use Ice default servants.


Building the demo
-----------------

On Linux or Unix, set ORACLE_HOME to point to your Oracle installation
home directory. Then build as usual.

On Windows using Visual Studio Project files, you need to add the 
following directories to your Visual C++ environment: 

 - Include files: <oracle-home>\precomp\public
 - Library files: <oracle-home>\precomp\lib
 - Executable files: <oracle-home>\bin

On Windows with nmake Makefiles, please review Makefile.mak.

Then build as usual.


Running the demo
----------------

- Setup an Oracle database with the traditional EMP/DEPT schema. 
  With Oracle server 10.2 or 11.1, the corresponding SQL script is 
  $ORACLE_HOME/rdbms/admin/utlsampl.sql.

- Review the Oracle.ConnectInfo property in the config.server file.
  You may need to change it to connect to your Oracle instance.

- Start the server:

  $ server

- Start the client in a separate window:

  $ client
