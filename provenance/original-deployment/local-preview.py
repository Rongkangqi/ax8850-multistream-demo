#!/usr/bin/env python3
"""Loopback-only, on-demand RTSP -> MPEG-TS remux for the board's VLC build."""
import argparse
import json
import logging
import signal
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT=Path(__file__).resolve().parent
logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s')
children=set()
lock=threading.Lock()
slots=threading.BoundedSemaphore(8)

def stop_process(p):
    if p.poll() is None:
        p.terminate()
        try:p.wait(timeout=3)
        except subprocess.TimeoutExpired:p.kill();p.wait(timeout=3)

class Handler(BaseHTTPRequestHandler):
    protocol_version='HTTP/1.0'
    def do_GET(self):
        name=urlsplit(self.path).path.strip('/')
        config=json.loads((ROOT/'config.json').read_text())
        names={c['name'] for c in config['channels']}|{'overview'}
        if not name.endswith('.ts') or name[:-3] not in names:
            self.send_error(404,'Use /overview.ts or /driving.ts');return
        if not slots.acquire(blocking=False):
            self.send_error(503,'Preview client limit reached');return
        name=name[:-3];p=None
        try:
            # No video decoding/encoding. The existing card-encoded bitstream
            # is repackaged only while a local player is connected.
            args=['ffmpeg','-hide_banner','-nostdin','-loglevel','warning',
                  '-rtsp_transport','tcp','-timeout','8000000','-analyzeduration','1000000','-probesize','1000000',
                  '-i','rtsp://127.0.0.1:8554/'+name,'-map','0:v:0','-an','-c:v','copy',
                  '-f','mpegts','-mpegts_flags','+resend_headers','-muxdelay','0','-muxpreload','0',
                  '-flush_packets','1','pipe:1']
            p=subprocess.Popen(args,stdout=subprocess.PIPE,bufsize=0)
            with lock:children.add(p)
            chunk=p.stdout.read(188*32)
            if not chunk:self.send_error(503,'RTSP source is not ready');return
            self.send_response(200)
            self.send_header('Content-Type','video/mp2t')
            self.send_header('Cache-Control','no-store')
            self.send_header('Connection','close')
            self.end_headers()
            self.connection.settimeout(8)
            logging.info('Preview opened: %s',name)
            while chunk:
                self.wfile.write(chunk)
                chunk=p.stdout.read(188*32)
        except (BrokenPipeError,ConnectionResetError,TimeoutError):
            pass
        finally:
            if p is not None:
                stop_process(p)
                if p.stdout:p.stdout.close()
                with lock:children.discard(p)
            slots.release()
            logging.info('Preview closed: %s',name)
    def log_message(self,format,*args):
        logging.info(format,*args)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--port',type=int,default=8850);args=parser.parse_args()
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    server.daemon_threads=True
    def shutdown(*_):
        threading.Thread(target=server.shutdown,daemon=True).start()
    signal.signal(signal.SIGTERM,shutdown);signal.signal(signal.SIGINT,shutdown)
    logging.info('Local VLC preview: http://127.0.0.1:%s/overview.ts',args.port)
    try:server.serve_forever(poll_interval=.2)
    finally:
        server.server_close()
        with lock:active=list(children)
        for p in active:stop_process(p)
if __name__=='__main__':main()
