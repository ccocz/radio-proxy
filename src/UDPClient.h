#ifndef RADIO_PROXY_UDPCLIENT_H
#define RADIO_PROXY_UDPCLIENT_H


#include <netinet/in.h>
#include <chrono>

class UDPClient {
private:
    struct sockaddr_in address;
    time_t last_request;
    bool banned;
public:
    UDPClient(sockaddr_in);
    void set_last_request(time_t);
    bool is_equal(const UDPClient&);
    bool is_banned() {
        return banned;
    }
    time_t get_last_request() {
        return last_request;
    }
    void ban() {
        banned = 1;
    }
    sockaddr_in get_addr() {
        return address;
    }
};


#endif //RADIO_PROXY_UDPCLIENT_H
