#pragma once
#include <string>
#include <map>

namespace knot
{
    // http request methods
    enum method_mask : uint
    {
        RM_NONE = 0,
        RM_GET = 1,
        RM_POST = 1 << 1,
        RM_HEAD = 1 << 2,
        RM_PUT = 1 << 3,
        RM_DELETE = 1 << 4,
        RM_TRACE = 1 << 5,
        RM_OPTIONS = 1 << 6,
        RM_ALL = (uint) ~0,
        RM_GETPOST = RM_GET | RM_POST,
        RM_COMMON = RM_GETPOST | RM_HEAD | RM_PUT | RM_DELETE
    };
    // api
    bool connect( int &sockfd, const std::string &ip, const std::string &port, double timeout_secs = 600 );
    bool is_connected( int &sockfd, double timeout_secs = 600 );
    bool send( int &sockfd, const std::string &output, double timeout_secs = 600 );
    bool receive( int &sockfd, std::string &input, double timeout_secs = 600 );
    bool receive_www( int &sockfd, std::string &input, double timeout_sec = 600, uint valid_method_mask=RM_ALL );
    bool receive_www( int &sockfd, std::string &request_method, std::string &raw_location, std::string &input, std::string &data, std::map<std::string, std::string> &headers, double timeout_sec=600, uint valid_method_mask=RM_ALL);
    bool disconnect( int &sockfd, double timeout_secs = 600 );
    bool close_r( int &sockfd );
    bool close_w( int &sockfd );
    void sleep( double secs );

    // api, server side
    bool listen( int &sockfd, const std::string &bindip, const std::string &port, void (*delegate_callback)( int master_fd, int child_fd, std::string client_addr_ip, std::string client_addr_port ), unsigned backlog_queue = 1024 ); // @todo: if mask
    bool shutdown( int &sockfd );
    bool shutdown();
    // bool ban( ip/mask, true/false ); // @todo
    // bool limit( ip/mask, instances_per_ip, downspeed, upspeed ); // @todo

    // stats
    //size_t get_hits();     // @todo, number of requests
    //size_t get_visitors(); // @todo, number of unique visitors (per IP)
    //size_t get_watchers(); // @todo, sizeof set active connections filtered per IP
    size_t get_bytes_received();
    size_t get_bytes_sent();
      void reset_counters();

    // tools
    struct uri
    {
        struct {
            std::string url, protocol, hostname, address;
            std::string user, pass, host, ip, port;
            std::string full, path;
        } pretty;

        struct {
            int ip[6];
            int port;
        } inet;

        int ok;

        std::string print() const;
    };

    // tools, get ip (once connected)
    bool get_interface_address( int &sockfd, std::string &ip, std::string &port );

    // tools, resolve ip/url
    uri lookup( const std::string &url );
    uri lookup( const std::string &addr, unsigned port );
    uri lookup( const std::string &addr, const std::string &port );
    std::string encode( const std::string &url );
    std::string decode( const std::string &url );
}
