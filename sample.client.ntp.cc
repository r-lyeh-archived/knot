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
