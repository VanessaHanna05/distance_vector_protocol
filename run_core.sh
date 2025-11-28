#!/usr/bin/env bash
set -e

echo "=== CLEAN UP OLD SETUP ==="
sudo ip netns del r1 2>/dev/null || true
sudo ip netns del r2 2>/dev/null || true
sudo ip netns del r3 2>/dev/null || true
sudo ip link del br0 2>/dev/null || true

echo "=== CREATE NAMESPACES ==="
sudo ip netns add r1
sudo ip netns add r2
sudo ip netns add r3

echo "=== CREATE BRIDGE FIRST (WSL2 REQUIREMENT) ==="
sudo ip link add br0 type bridge
sudo ip link set br0 up

echo "=== CREATE VETH PAIRS ==="

sudo ip link add veth-r1 type veth peer name veth-r1-br
sudo ip link add veth-r2 type veth peer name veth-r2-br
sudo ip link add veth-r3 type veth peer name veth-r3-br

echo "=== ATTACH BRIDGE ENDS ==="
sudo ip link set veth-r1-br master br0
sudo ip link set veth-r2-br master br0
sudo ip link set veth-r3-br master br0

sudo ip link set veth-r1-br up
sudo ip link set veth-r2-br up
sudo ip link set veth-r3-br up

echo "=== MOVE OTHER ENDS TO NAMESPACES ==="

sudo ip link set veth-r1 netns r1
sudo ip link set veth-r2 netns r2
sudo ip link set veth-r3 netns r3

echo "=== CONFIGURE INTERFACES WITH FORCED BROADCAST ==="

sudo ip netns exec r1 ip link set lo up
sudo ip netns exec r1 ip link set veth-r1 up
sudo ip netns exec r1 ip addr flush dev veth-r1
sudo ip netns exec r1 ip addr add 10.0.0.1/24 brd 10.0.0.255 dev veth-r1

sudo ip netns exec r2 ip link set lo up
sudo ip netns exec r2 ip link set veth-r2 up
sudo ip netns exec r2 ip addr flush dev veth-r2
sudo ip netns exec r2 ip addr add 10.0.0.2/24 brd 10.0.0.255 dev veth-r2

sudo ip netns exec r3 ip link set lo up
sudo ip netns exec r3 ip link set veth-r3 up
sudo ip netns exec r3 ip addr flush dev veth-r3
sudo ip netns exec r3 ip addr add 10.0.0.3/24 brd 10.0.0.255 dev veth-r3

echo "=== VERIFY BROADCAST FLAG ==="
sudo ip netns exec r1 ip addr show veth-r1 | grep brd
sudo ip netns exec r2 ip addr show veth-r2 | grep brd
sudo ip netns exec r3 ip addr show veth-r3 | grep brd

echo "=== BUILD ROUTER ==="
make router_core

echo "=== RUN TMUX ==="
sudo tmux kill-session -t routers 2>/dev/null || true

sudo tmux new-session -d -s routers "ip netns exec r1 ./router_core"
sudo tmux split-window -h -t routers "ip netns exec r2 ./router_core"
sudo tmux split-window -v -t routers:0.0 "ip netns exec r3 ./router_core"

sudo tmux select-layout -t routers tiled
sudo tmux attach -t routers
