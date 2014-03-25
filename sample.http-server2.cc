#include <cstdlib>
#include <iostream>
#include "knot.hpp"
#include <climits>
#include <map>

void die( const std::string &message )
{
    std::cerr << message.c_str() << std::endl;
    std::exit( 1 );
}

void echo_www( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port )
{
    std::string input;
    std::string method;
    std::string data;
    std::string location;
    std::map<std::string, std::string> headers;

    if( !knot::receive_www( child_fd, method, location, input, data, headers, 600, knot::RM_GET | knot::RM_POST | knot::RM_PUT  ) )
        die( "server error: cant recv" );

    if( !knot::send( child_fd, "HTTP 200 OK\r\n\r\n" + input ) )
        die( "server error: cant send" );

    if( !knot::disconnect( child_fd ) )
        die( "server error: cant close" );
    
    std::cout << "hit ("<<method<<") to " << location << " from " << client_addr_ip << ':' << client_addr_port << ". Data-length: " << data.length() << std::endl;
    std::cout << "Headers: " << std::endl;
    for (auto i = headers.begin(); i != headers.end(); ++i)
    {
	std::cout << i->first << ": " << i->second << std::endl;
    }
}

int main( int argc, const char **argv )
{
    int server_socket;
    if( !knot::listen( server_socket, "0.0.0.0", "8080", echo_www, 1024 ) )
        die( "server error: cant listen at port 8080" );

    std::cout << "server says: ready at port 8080" << std::endl;

    for(;;)
        knot::sleep( 1.0 );

    knot::shutdown();

    return 0;
}
