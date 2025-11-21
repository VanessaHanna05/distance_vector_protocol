#!/usr/bin/env bash
set -e

# Simple topology:
#   r1: 10.0.0.1/24
#   r2: 10.0.0.2/24
#   r3: 10.0.0.3/24
#
# All connected to bridge br0 (a virtual switch).

echo "[+] Creating network namespaces (routers)..."
sudo ip netns add r1 || true
sudo ip netns add r2 || true
sudo ip netns add r3 || true

echo "[+] Creating veth pairs..."
# r1 <--> br0
sudo ip link add veth-r1 type veth peer name veth-r1-br || true
sudo ip link set veth-r1 netns r1

# r2 <--> br0
sudo ip link add veth-r2 type veth peer name veth-r2-br || true
sudo ip link set veth-r2 netns r2

# r3 <--> br0
sudo ip link add veth-r3 type veth peer name veth-r3-br || true
sudo ip link set veth-r3 netns r3

echo "[+] Creating bridge br0..."
sudo ip link add name br0 type bridge || true
sudo ip link set br0 up

echo "[+] Attaching router links to bridge..."
sudo ip link set veth-r1-br master br0
sudo ip link set veth-r2-br master br0
sudo ip link set veth-r3-br master br0

sudo ip link set veth-r1-br up
sudo ip link set veth-r2-br up
sudo ip link set veth-r3-br up

echo "[+] Configuring IP addresses inside namespaces..."
# Router r1
sudo ip netns exec r1 ip link set lo up
sudo ip netns exec r1 ip link set veth-r1 up
sudo ip netns exec r1 ip addr add 10.0.0.1/24 dev veth-r1

# Router r2
sudo ip netns exec r2 ip link set lo up
sudo ip netns exec r2 ip link set veth-r2 up
sudo ip netns exec r2 ip addr add 10.0.0.2/24 dev veth-r2

# Router r3
sudo ip netns exec r3 ip link set lo up
sudo ip netns exec r3 ip link set veth-r3 up
sudo ip netns exec r3 ip addr add 10.0.0.3/24 dev veth-r3

echo "[+] Topology created."
echo
echo "Namespaces:"
echo "  r1 (10.0.0.1/24)"
echo "  r2 (10.0.0.2/24)"
echo "  r3 (10.0.0.3/24)"
echo
echo "Test ping (optional):"
echo "  sudo ip netns exec r1 ping 10.0.0.2 -c 2"
echo "  sudo ip netns exec r2 ping 10.0.0.3 -c 2"
echo
echo "Then run your program in each namespace, e.g.:"
echo "  sudo ip netns exec r1 ./neighbor"
echo "  sudo ip netns exec r2 ./neighbor"
echo "  sudo ip netns exec r3 ./neighbor"
