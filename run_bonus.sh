#!/usr/bin/env bash
set -e

echo "=== Setting up router namespaces for BONUS run (host included) ==="

# Clean up old setup if it exists
for ns in r1 r2 r3; do
    sudo ip netns del "$ns" 2>/dev/null || true
done

sudo ip link del br0 2>/dev/null || true
sudo ip link del veth-r1 2>/dev/null || true
sudo ip link del veth-r2 2>/dev/null || true
sudo ip link del veth-r3 2>/dev/null || true
sudo ip link del veth-h 2>/dev/null || true
sudo ip link del veth-h-br 2>/dev/null || true

echo "Creating namespaces"
sudo ip netns add r1
sudo ip netns add r2
sudo ip netns add r3

echo "Creating bridge br0"
sudo ip link add name br0 type bridge
sudo ip link set br0 up

echo "Creating veth pairs for routers"

# r1 <-> br0
sudo ip link add veth-r1 type veth peer name veth-r1-br
sudo ip link set veth-r1 netns r1
sudo ip link set veth-r1-br master br0
sudo ip link set veth-r1-br up

# r2 <-> br0
sudo ip link add veth-r2 type veth peer name veth-r2-br
sudo ip link set veth-r2 netns r2
sudo ip link set veth-r2-br master br0
sudo ip link set veth-r2-br up

# r3 <-> br0
sudo ip link add veth-r3 type veth peer name veth-r3-br
sudo ip link set veth-r3 netns r3
sudo ip link set veth-r3-br master br0
sudo ip link set veth-r3-br up

echo "Configuring IP addresses inside namespaces"

sudo ip netns exec r1 ip addr add 10.0.0.1/24 dev veth-r1
sudo ip netns exec r1 ip link set veth-r1 up
sudo ip netns exec r1 ip link set lo up

sudo ip netns exec r2 ip addr add 10.0.0.2/24 dev veth-r2
sudo ip netns exec r2 ip link set veth-r2 up
sudo ip netns exec r2 ip link set lo up

sudo ip netns exec r3 ip addr add 10.0.0.3/24 dev veth-r3
sudo ip netns exec r3 ip link set veth-r3 up
sudo ip netns exec r3 ip link set lo up

echo "Creating host veth and attaching host to br0"

# host <-> br0
sudo ip link add veth-h type veth peer name veth-h-br
sudo ip link set veth-h-br master br0
sudo ip link set veth-h-br up

# give host an IP in the same subnet
sudo ip addr add 10.0.0.254/24 dev veth-h 2>/dev/null || true
sudo ip link set veth-h up

echo "Routers and IPs:"
echo "  host (10.0.0.254/24)"
echo "  r1   (10.0.0.1/24)"
echo "  r2   (10.0.0.2/24)"
echo "  r3   (10.0.0.3/24)"

echo "=== Building router_bonus (Part2 BONUS + Part3) ==="
make router_bonus

echo "=== Starting routers in tmux session 'routers' ==="

# kill old session if exists
sudo tmux kill-session -t routers 2>/dev/null || true

# start r1 in a new session
sudo tmux new-session -d -s routers "ip netns exec r1 ./router_bonus"

# split pane for r2
sudo tmux split-window -h -t routers "ip netns exec r2 ./router_bonus"

# split pane for r3
sudo tmux split-window -v -t routers:0.0 "ip netns exec r3 ./router_bonus"

# split pane for HOST router (uses veth-h / 10.0.0.254)
sudo tmux split-window -v -t routers:0.1 "./router_bonus"

# arrange panes nicely
sudo tmux select-layout -t routers tiled

echo "=== Attaching to tmux session 'routers' ==="
sudo tmux attach -t routers
