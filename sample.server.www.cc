#include <cstdlib>
#include <iostream>
#include "knot.hpp"

void die( const std::string &message )
{
    std::cerr << message.c_str() << std::endl;
    std::exit( 1 );
}

void echo_www( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port )
{
    std::string input;

    if( !knot::receive_www( child_fd, input ) )
        die( "server error: cant recv" );

    if( !knot::send( child_fd, "HTTP 200 OK\r\n\r\n" + input ) )
        die( "server error: cant send" );

    if( !knot::disconnect( child_fd ) )
        die( "server error: cant close" );

    std::cout << "hit from " << client_addr_ip << ':' << client_addr_port << std::endl;
}

int main( int argc, const char **argv )
{
    int server_socket;

    if( !knot::listen( server_socket, "8080", echo_www, 1024 ) )
        die( "server error: cant listen at port 8080" );

    std::cout << "server says: ready at port 8080" << std::endl;

    for(;;)
        knot::sleep( 1.0 );

    knot::shutdown();

    return 0;
}
