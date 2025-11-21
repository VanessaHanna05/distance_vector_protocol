#!/usr/bin/env bash
set -e

echo "Removing router namespaces..."
sudo ip netns del r1 2>/dev/null || true
sudo ip netns del r2 2>/dev/null || true
sudo ip netns del r3 2>/dev/null || true

echo "Deleting bridge br0 if it exists..."
sudo ip link set br0 down 2>/dev/null || true
sudo ip link del br0 2>/dev/null || true

echo "Removing veth interfaces if they exist..."
sudo ip link del veth-r1-br 2>/dev/null || true
sudo ip link del veth-r2-br 2>/dev/null || true
sudo ip link del veth-r3-br 2>/dev/null || true

# Sometimes the namespace side of the veth persists, so try these as well:
sudo ip link del veth-r1 2>/dev/null || true
sudo ip link del veth-r2 2>/dev/null || true
sudo ip link del veth-r3 2>/dev/null || true

echo "Cleanup complete."
