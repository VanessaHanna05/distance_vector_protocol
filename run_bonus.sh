#!/usr/bin/env bash
set -e

echo "=== Setting up router namespaces for BONUS run ==="

# --- Create namespaces (ignore if already exist) ---
sudo ip netns add r1 2>/dev/null || true
sudo ip netns add r2 2>/dev/null || true
sudo ip netns add r3 2>/dev/null || true

echo "Creating veth pairs"

# r1 <-> bridge
sudo ip link add veth-r1 type veth peer name veth-r1-br 2>/dev/null || true
sudo ip link set veth-r1 netns r1 2>/dev/null || true

# r2 <-> bridge
sudo ip link add veth-r2 type veth peer name veth-r2-br 2>/dev/null || true
sudo ip link set veth-r2 netns r2 2>/dev/null || true

# r3 <-> bridge
sudo ip link add veth-r3 type veth peer name veth-r3-br 2>/dev/null || true
sudo ip link set veth-r3 netns r3 2>/dev/null || true

echo "Creating bridge br0"

sudo ip link add name br0 type bridge 2>/dev/null || true
sudo ip link set br0 up

echo "Attaching router links to bridge"
sudo ip link set veth-r1-br master br0 2>/dev/null || true
sudo ip link set veth-r2-br master br0 2>/dev/null || true
sudo ip link set veth-r3-br master br0 2>/dev/null || true

sudo ip link set veth-r1-br up 2>/dev/null || true
sudo ip link set veth-r2-br up 2>/dev/null || true
sudo ip link set veth-r3-br up 2>/dev/null || true

echo "Configuring IP addresses inside namespaces"

# r1: 10.0.0.1/24
sudo ip netns exec r1 ip link set lo up
sudo ip netns exec r1 ip link set veth-r1 up
sudo ip netns exec r1 ip addr add 10.0.0.1/24 dev veth-r1 2>/dev/null || true

# r2: 10.0.0.2/24
sudo ip netns exec r2 ip link set lo up
sudo ip netns exec r2 ip link set veth-r2 up
sudo ip netns exec r2 ip addr add 10.0.0.2/24 dev veth-r2 2>/dev/null || true

# r3: 10.0.0.3/24
sudo ip netns exec r3 ip link set lo up
sudo ip netns exec r3 ip link set veth-r3 up
sudo ip netns exec r3 ip addr add 10.0.0.3/24 dev veth-r3 2>/dev/null || true

echo "Routers and IPs:"
echo "  r1 (10.0.0.1/24)"
echo "  r2 (10.0.0.2/24)"
echo "  r3 (10.0.0.3/24)"

echo "=== Building router_bonus (Part2 BONUS + Part3, WITH kernel routes) ==="
make router_bonus

echo "=== Starting tmux session 'routers' for BONUS run ==="

# Kill any old session if it exists
sudo tmux has-session -t routers 2>/dev/null && sudo tmux kill-session -t routers || true

# new tmux session for r1
sudo tmux new-session -d -s routers "ip netns exec r1 ./router_bonus"

# split pane for r2
sudo tmux split-window -h -t routers "ip netns exec r2 ./router_bonus"

# split pane for r3
sudo tmux split-window -v -t routers:0.0 "ip netns exec r3 ./router_bonus"

# arrange nicely
sudo tmux select-layout -t routers tiled

echo "=== Attaching to tmux session 'routers' ==="
sudo tmux attach -t routers
