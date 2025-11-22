#!/usr/bin/env bash
#Run this script using the bash shell, found via env
#This makes the script portable, as bash might not be in the same place on every system

set -e
#exit immediately if any command returns a non-zero (error) status

# Simple topology:
#   r1: 10.0.0.1/24
#   r2: 10.0.0.2/24
#   r3: 10.0.0.3/24
#
# All connected to bridge br0 (a virtual switch).
 
#Reference: https://hackmd.io/@ranmJBMnTBajwNlW9v73TA/HyL2Y5Mwh
#Reference: https://youtu.be/iN2RnYaFn-0
#Reference: https://youtu.be/j_UUnlVC2Ss


echo "Creating routers"
#adding a router 
sudo ip netns add r1 || true
sudo ip netns add r2 || true
sudo ip netns add r3 || true

echo "Creating veth pairs"

sudo ip link add veth-r1 type veth peer name veth-r1-br

#ip link add: create a new network interface
#type veth: a virtual Ethernet pair
#veth-r1: one end of the virtual cable
#peer name veth-r1-br: the other end of the cable
#So you create a pair: veth-r1 and veth-r1-br
#Anything sent into one end comes out the other, like a wire

sudo ip link set veth-r1 netns r1
#Moves the interface veth-r1 into network namespace r1.
#Inside namespace r1: there is an interface called veth-r1.
#On the host namespace: there is veth-r1-br.

sudo ip link add veth-r2 type veth peer name veth-r2-br 
sudo ip link set veth-r2 netns r2

sudo ip link add veth-r3 type veth peer name veth-r3-br
sudo ip link set veth-r3 netns r3

# r1’s namespace has veth-r1, host has veth-r1-br
# r2’s namespace has veth-r2, host has veth-r2-br
# r3’s namespace has veth-r3, host has veth-r3-br

echo "Creating bridge"

sudo ip link add name br0 type bridge
# Creates a Linux bridge called br0.
# A bridge works at Layer 2 like an Ethernet switch
# It has ports added to it to forward frames between them

sudo ip link set br0 up
# Activates the bridge interface.
# Interfaces default to DOWN they only start passing packets when set UP

#            +------------+
#            |   bridge   |  ← (software switch)
#            +------------+
#              /    |     \
#             /     |      \
#    veth-r1-br  veth-r2-br  veth-r3-br
#       |           |           |
#    r1/ns       r2/ns       r3/ns

# r1 = a physical router
# veth-r1 = its Ethernet port
# veth-r1-br = the other end of the cable
# br0 = an actual network switch

echo "Attaching router links to bridge"
sudo ip link set veth-r1-br master br0
sudo ip link set veth-r2-br master br0
sudo ip link set veth-r3-br master br0

# Makes veth-r1-br a port of the bridge br0
# So everything that comes from r1’s veth will go through br0
#r1 <-> veth-r1 <-> veth-r1-br <-> br0 <-> veth-r2-br <-> veth-r2 <-> r2
sudo ip link set veth-r1-br up
sudo ip link set veth-r2-br up
sudo ip link set veth-r3-br up

# These bring the host-side interfaces up so the bridge can actually forward packets on them
# At this point, at L2 you have a functioning “LAN switch” with three cables, one to each namespace

echo "Configuring IP addresses inside namespaces"

sudo ip netns exec r1 ip link set lo up
sudo ip netns exec r1 ip link set veth-r1 up
sudo ip netns exec r1 ip addr add 10.0.0.1/24 dev veth-r1


# lo is the loopback interface (127.0.0.1)
# ip link set veth-r1 up: Activates r1’s interface, so it can send/receive packets
# ip addr add 10.0.0.1/24 dev veth-r1: Assigns an IP address to veth-r1
# Address: 10.0.0.1 and Netmask: /24 (255.255.255.0)
#Now r1 is a host in 10.0.0.0/24.

# The /24 defines the subnet mask, which is 255.255.255.0 in dotted decimal.
# It means the first 24 bits of the address identify the network, and the last 8 bits identify the host.
# So 10.0.0.1/24, 10.0.0.2/24, and 10.0.0.3/24 are all in the same local network and can communicate through the virtual bridge.
#The 10.0.0.0 block is reserved for private networks

sudo ip netns exec r2 ip link set lo up
sudo ip netns exec r2 ip link set veth-r2 up
sudo ip netns exec r2 ip addr add 10.0.0.2/24 dev veth-r2

sudo ip netns exec r3 ip link set lo up
sudo ip netns exec r3 ip link set veth-r3 up
sudo ip netns exec r3 ip addr add 10.0.0.3/24 dev veth-r3

echo "Routers and IPs:"
echo "  r1 (10.0.0.1/24)"
echo "  r2 (10.0.0.2/24)"
echo "  r3 (10.0.0.3/24)"
