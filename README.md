knot
====

- Knot is a lightweight and simple TCP network C++11 library with no dependencies.
- Knot is tiny. One header and one source file.
- Knot is cross-platform. Compiles under MSVC/GCC. Works on Windows/Linux.
- Knot is MIT licensed.

public API
----------
- `knot::connect()` connects to a network address.
- `knot::send()` sends data bytes thru a connection.
- `knot::receive()` receives data bytes from a connection.
- `knot::receive()` receives data bytes from a http connection.
- `knot::close()` closes an established connection.
- `knot::listen()` creates a listening thread.
- `knot::sleep()` puts a thread to sleep.
- `knot::reset_counters()` reset transmission stats.
- `knot::get_bytes_received()` get number of bytes received since last reset.
- `knot::get_bytes_sent()` get number of bytes received since last reset.
- `knot::get_interface_address()` get address of current interface address (requires an established connection)

sample
------
```
#include <iostream>
#include "knot.hpp"

int main( int argc, const char **argv )
{
    int socket;
    std::string answer;
    std::cout << "answer from NTP server: ";

    if( knot::connect( socket, "time-C.timefreq.bldrdoc.gov", "13" ) )
        if( knot::send( socket, "dummy" ) )
            if( knot::receive( socket, answer ) )
                std::cout << answer << std::endl;

    knot::close( socket );

    return 0;
}
```

possible output
---------------
```
D:\prj\knot>cl sample.client.ntp.cc knot.cpp
D:\prj\knot>sample.client.ntp.exe
answer from NTP server:
56400 13-04-18 13:34:19 50 0 0  52.0 UTC(NIST) *
D:\prj\knot>
```

special notes
-------------
- g++ users: both `-std=c++11` and `-lpthread` may be required when compiling `knot.cpp`