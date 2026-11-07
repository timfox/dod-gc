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

	clients: List[socket.socket] = []
	bufs: Dict[socket.socket, bytearray] = {}
	frames = 0
	last_report = 0.0

	try:
		import time

		while True:
			rlist = [srv] + clients
			readable, _, _ = select.select(rlist, [], [], 1.0)
			now = time.time()
			if now - last_report >= 5.0:
				print(f"stats clients={len(clients)} frames={frames}", flush=True)
				last_report = now
			for sock in readable:
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
					dead = []
					for peer in clients:
						if peer is sock:
							continue
						try:
							peer.sendall(struct.pack("<H", size) + frame)
						except OSError:
							dead.append(peer)
					for peer in dead:
						if peer in clients:
							clients.remove(peer)
						bufs.pop(peer, None)
						try:
							peer.close()
						except OSError:
							pass
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
