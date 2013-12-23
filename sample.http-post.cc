#include "knot.hpp"

#include <iostream>
#include <sstream>
#include <map>
#include <string>

bool send_www_post( int sockfd, const std::string &host, const std::string &page, const std::map< std::string, std::string > &vars )
{
    std::stringstream header, content;

    for( auto &in : vars )
        content << knot::encode( in.first ) << "=" << knot::encode( in.second ) << "&";

    header
        << "POST " << page << " HTTP/1.0\r\n"
        << "Host: " << host << "\r\n"
        << "Content-type: application/x-www-form-urlencoded\r\n"
        << "Content-length: " << content.str().size() << "\r\n\r\n"
        << content.str();

    return knot::send( sockfd, header.str() );
}

int main( int argc, const char **argv ) {

    std::map< std::string, std::string > post;
    post["api_dev_key"] = "7442ea185e8571f5c76f0f71c40a1a61";
    post["api_option"] = "paste";
    post["api_paste_code"] = "hello pastebin.com from <knot c++>";
    post["api_paste_private"] = "1";
    post["api_paste_expire_date"] = "N";

    knot::uri resolved = knot::lookup("http://pastebin.com/api/api_post.php");
    if( !resolved.ok )
        return std::cerr << "cannot lookup uri" << std::endl, -1;

    std::cout << "connection details: " << resolved.print() << std::endl;

    int sockfd;
    std::string answer;

    if( knot::connect( sockfd, resolved.pretty.ip, resolved.pretty.port ) )
        if( send_www_post( sockfd, resolved.pretty.hostname, resolved.pretty.path, post ) )
            if( knot::receive_www( sockfd, answer ) )
                ;

    knot::disconnect( sockfd );

    printf("answer: %s \n", answer.c_str() );

    return 0;
}
