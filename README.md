nlmon
=====

Simple example of how to use libnl and libev to monitor kernel netlink
events.  Beware, there's a lot of boiler plate code you might not need.


Building
--------

To build and use nlmon, you need the libnl and libev libraries.  On
Debian/Ubuntu and Linux Mint, the following command installs the
required development libraries and their runtime equivalents:

    sudo apt install libnl-route-3-dev libnl-3-dev libev-dev

Provided the code of this project is cloned to `~/nlmon`, do:

    cd ~/nlmon
    gcc -o nlmon  nlmon.c -I/usr/include/libnl3 -lnl-3 -lnl-route-3 -lev

or type `make` to let it call GCC for you.


Testing
-------

In one terminal window, run nlmon:

    ./nlmon -v

In another terminal window, create and delete a VETH pair:

    sudo ip link add veth1 type veth peer name veth1-peer
    sudo ip link del veth1

There should be output from nlmon in the first window:

    ./nlmon -v
    nlmon: veth iface veth1 deleted
    nlmon: veth iface veth1-peer deleted
    nlmon: veth iface veth1-peer added
    nlmon: veth iface veth1 added


Origin & References
-------------------

This project is based on a deprecated external netlink plugin for the
[finit][] init system.  It was developed by various people at Westermo
over the years.  It has since been replaced by a native plugin.

Brought back to life by [Joachim Wiberg][] for use as an example of how
to utilize libnl and libev.

[finit]:          https://github.com/troglobit/finit
[Joachim Wiberg]: http://troglobit.com
