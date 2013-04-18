knot
====

- Knot is a lightweight and simple TCP network C++11 library with no dependencies.
- Knot is tiny. One header and one source file.
- Knot is cross-platform.
- Knot is MIT licensed.

public API
----------
- `connect` connects to a network address.
- `send` sends data bytes thru a connection.
- `receive` receives data bytes from a connection.
- `receive` receives data bytes from a http connection.
- `close` closes an established connection.
- `listen` creates a listening thread.
- `sleep` puts a thread to sleep.
- `reset_counters` reset transmission stats.
- `get_bytes_received` get number of bytes received since last reset.
- `get_bytes_sent` get number of bytes received since last reset.
- `get_interface_address` get address of current interface address (requires an established connection)

sample
------
<pre>
#include &lt;iostream&gt;
#include "knot.hpp"

int main( int argc, const char **argv )
{
    int socket;
    std::string answer;
    std::cout &lt;&lt; "answer from NTP server: ";

    if( knot::connect( socket, "time-C.timefreq.bldrdoc.gov", "13" ) )
        if( knot::send( socket, "dummy" ) )
            if( knot::receive( socket, answer ) )
                std::cout &lt;&lt; answer &lt;&lt; std::endl;

    knot::close( socket );

    return 0;
}
</pre>

possible output
---------------
<pre>
D:\prj\knot&lt;cl sample.client.ntp.cc knot.cpp
D:\prj\knot&lt;sample.client.ntp.exe
answer from NTP server:
56400 13-04-18 13:34:19 50 0 0  52.0 UTC(NIST) *
D:\prj\knot&lt;
</pre>
