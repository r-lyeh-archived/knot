#include <iostream>
#include "knot.hpp"

int main() {
    knot::uri u1 = knot::lookup("http://www.google.com/hello/world?oh=\"yes\"");
    knot::uri u2 = knot::lookup("ftp://myuser@localhost:21/hello/world?oh=\"yes\"");

    std::cout << u1.print() << std::endl;
    std::cout << u2.print() << std::endl;

    return 0;
}
