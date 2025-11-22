#!/usr/bin/env bash
set -e

echo "Removing router namespace"
sudo ip netns del r1 2>/dev/null || true
sudo ip netns del r2 2>/dev/null || true
sudo ip netns del r3 2>/dev/null || true

echo "Deleting bridge"
sudo ip link set br0 down 2>/dev/null || true
sudo ip link del br0 2>/dev/null || true

echo "Removing veth interfaces"
sudo ip link del veth-r1-br 2>/dev/null || true
sudo ip link del veth-r2-br 2>/dev/null || true
sudo ip link del veth-r3-br 2>/dev/null || true

sudo ip link del veth-r1 2>/dev/null || true
sudo ip link del veth-r2 2>/dev/null || true
sudo ip link del veth-r3 2>/dev/null || true

echo "Killing tmux session 'routers' if it exists"
sudo tmux has-session -t routers 2>/dev/null && sudo tmux kill-session -t routers 2>/dev/null || true

echo "Cleanup complete."
