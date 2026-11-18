#!/usr/bin/env python3
"""Minimal L2 hub for Dolphin EthernetTapServer (unix socket frame fan-out).

Protocol (TAPServerConnection): each frame is uint16 LE size + ethernet payload.
Default path: /tmp/dolphin-tap
"""
from __future__ import annotations

import argparse
import os
import select
import socket
import struct
import sys
from typing import Dict, List


def main() -> int:
	ap = argparse.ArgumentParser()
	ap.add_argument("--path", default="/tmp/dolphin-tap")
	ap.add_argument("--verbose", action="store_true")
	ap.add_argument("--tap", help="also bridge this Linux tap device (create it first, e.g. in `unshare -Urn`)")
	args = ap.parse_args()

	path = args.path
	if os.path.exists(path):
		os.unlink(path)

	srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
	srv.bind(path)
	os.chmod(path, 0o666)
	srv.listen(8)
	srv.setblocking(False)
	print(f"tap-hub listening on {path}", flush=True)

	tapfd = -1
	if args.tap:
		import fcntl
		tapfd = os.open("/dev/net/tun", os.O_RDWR)
		# TUNSETIFF, IFF_TAP | IFF_NO_PI
		fcntl.ioctl(tapfd, 0x400454CA, struct.pack("16sH", args.tap.encode(), 0x0002 | 0x1000))
		os.set_blocking(tapfd, False)
		print(f"bridging tap {args.tap}", flush=True)

	def fan_out(frame: bytes, origin) -> None:
		dead = []
		for peer in clients:
			if peer is origin:
				continue
			try:
				peer.sendall(struct.pack("<H", len(frame)) + frame)
			except OSError:
				dead.append(peer)
		for peer in dead:
			clients.remove(peer)
			bufs.pop(peer, None)
			try:
				peer.close()
			except OSError:
				pass
		if tapfd >= 0 and origin is not None:
			try:
				os.write(tapfd, frame)
			except OSError:
				pass

	clients: List[socket.socket] = []
	bufs: Dict[socket.socket, bytearray] = {}
	frames = 0
	bytes_in = 0
	last_report = 0.0

	try:
		import time

		while True:
			rlist = [srv] + clients + ([tapfd] if tapfd >= 0 else [])
			readable, _, _ = select.select(rlist, [], [], 1.0)
			now = time.time()
			if now - last_report >= 5.0:
				print(
					f"stats clients={len(clients)} frames={frames} bytes_in={bytes_in}",
					flush=True,
				)
				last_report = now
			for sock in readable:
				if sock == tapfd and tapfd >= 0:
					try:
						frame = os.read(tapfd, 65536)
					except OSError:
						continue
					if len(frame) >= 14:
						frames += 1
						fan_out(frame, None)
					continue
				if sock is srv:
					conn, _ = srv.accept()
					conn.setblocking(False)
					clients.append(conn)
					bufs[conn] = bytearray()
					print(f"client connected n={len(clients)}", flush=True)
					continue
				try:
					chunk = sock.recv(65536)
				except OSError:
					chunk = b""
				if not chunk:
					print("client disconnected", flush=True)
					clients.remove(sock)
					bufs.pop(sock, None)
					try:
						sock.close()
					except OSError:
						pass
					continue
				bytes_in += len(chunk)
				if args.verbose and len(chunk) <= 64:
					print(f"recv {len(chunk)}B raw={chunk[:32].hex()}", flush=True)
				elif args.verbose:
					print(f"recv {len(chunk)}B", flush=True)
				bufs[sock].extend(chunk)
				buf = bufs[sock]
				while True:
					if len(buf) < 2:
						break
					(size,) = struct.unpack_from("<H", buf, 0)
					if size < 14 or size > 0x4000:
						# resync: drop one byte
						del buf[0:1]
						continue
					if len(buf) < 2 + size:
						break
					frame = bytes(buf[2 : 2 + size])
					del buf[0 : 2 + size]
					frames += 1
					if args.verbose:
						ethertype = (frame[12] << 8) | frame[13] if size >= 14 else 0
						print(f"frame {size}B ethertype=0x{ethertype:04x}", flush=True)
					fan_out(frame, sock)
	finally:
		for c in clients:
			try:
				c.close()
			except OSError:
				pass
		srv.close()
		if os.path.exists(path):
			os.unlink(path)
	return 0


if __name__ == "__main__":
	sys.exit(main())
