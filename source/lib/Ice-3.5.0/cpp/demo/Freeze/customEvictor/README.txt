This demo shows how to write a custom evictor using IceUtil::Cache,
and compares the performance of this evictor to the performance of
a simple evictor implemented using EvictorBase (described in the
Ice manual).

This demo uses a Freeze map for storage, and does not use the Freeze
evictor. It also illustrates how to access a Freeze map from multiple
threads concurrently, using a Freeze map object per thread.

IceUtil::Cache can be used to implement an evictor for objects stored
in any database, not just Freeze; as a result, you may find this demo
useful even if you don't plan to use Freeze.


Server
------

The server provides 10,000 items (Ice objects) stored in a Freeze
dictionary. These items are provided to Ice through a servant locator,
with two possible servant locator implementations:

 - Evictor, a servant locator implemented using IceUtil::Cache

   With this evictor, a cached object can be retrieved while another
   object (or several other objects) are being loaded from the
   database. As a result, long cache-misses do not affect cache hits.

 - SimpleEvictor, a servant locator implemented using EvictorBase

   With this evictor, a single mutex protects all access to its
   underlying map. In particular, any lookup will block while an
   object is being retrieved from the database.

To use the Evictor, run the server with no arguments:

$ server

To use the SimpleEvictor, run the server with "simple" as the argument:

$ server simple

In both configurations, the evictor size (the number of objects cached
by the evictor) is set to 8,000 to provide a good cache-hit ratio when
the access pattern is random.


Client
------

The client starts six threads: five threads that, in a loop, randomly
read items provided by the server, and one thread that writes these
items at random, also in a loop. After a large number of iterations,
the client displays the average number of requests per second for each
thread.

With the default configuration, cache misses are reasonably costly,
so you should see a performance advantage when using the
IceUtil::Cache-based evictor. You could also add a sleep in
EvictorCache::load (Evictor.cpp) and SimpleEvictor::add
(SimpleEvictor.cpp) to make cache misses even more expensive and
further increase the advantage of the IceUtil::Cache-based evictor.


Berkeley DB cache tuning
------------------------

This demo also provides a good opportunity to experiment with the
Berkeley DB cache size. Evictor cache misses are expensive when the
server needs to retrieve the data from disk, and not so expensive
when the data is cached by the database. With a properly sized
Berkeley DB cache, the entire database is in memory, and evictor
cache misses become fairly cheap.

Use db/DB_CONFIG to set the Berkeley DB environment cache size.

The default cache size is 256KB; with a cache size of 128KB, the
performance difference between Evictor and EvictorBase increases,
since the larger number of cache misses requires more data to be read
from disk. With a cache size of 100MB, the performance difference
between the two evictor implementations becomes very small.
