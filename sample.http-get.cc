#include <iostream>
#include "knot.hpp"

int main( int argc, const char **argv )
{
    int socket;
    std::string answer;

    if( knot::connect( socket, "www.google.com", "80" ) )
        if( knot::send( socket, "GET /\r\n\r\n" ) )
            if( knot::receive( socket, answer ) )
                std::cout << "ok, answer from google website: " << answer << std::endl;

    knot::disconnect( socket );

    return 0;
}
