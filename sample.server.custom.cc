#include <cstdlib>
#include <iostream>
#include "knot.hpp"

void die( const std::string &message )
{
    std::cerr << message.c_str() << std::endl;
    std::exit( 1 );
}

void echo( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port )
{
    std::string input;

    if( !knot::receive( child_fd, input ) )
        die("server error: cant recv");

    if( !knot::send( child_fd, input ) )
        die("server error: cant send");

    if( !knot::close( child_fd ) )
        die("server error: cant close");

    std::cout << "server says: hit from " << client_addr_ip << ':' << client_addr_port << std::endl;
}

int main( int argc, char **argv )
{
    // server

    int server_socket;

    if( !knot::listen( server_socket, "8080", echo, 1024 ) )
        die("server error: cant listen at port 8080");

    // client

    int client_socket;
    std::string answer;

    if( !knot::connect( client_socket, "127.0.0.1", "8080" ) )
        die("client error: cant connect");

    if( !knot::send( client_socket, "Hello world" ) )
        die("client error: cant send");

    if( !knot::receive( client_socket, answer ) )
        die("client error: cant receive");

    if( !knot::close( client_socket ) )
        die("client error: cant close");

    std::cout << "client says: answer='" << answer << "'" << std::endl;

    return 0;
}
