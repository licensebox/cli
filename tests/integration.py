#!/usr/bin/env python3
"""Production CLI tests: real native identity, synthetic keys and loopback only."""
import base64
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import json
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey

def enc(data): return base64.urlsafe_b64encode(data).decode().rstrip('=')
def dec(data): return base64.urlsafe_b64decode(data+'='*(-len(data)%4))
def stamp(now): return datetime.fromtimestamp(now, timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
signer=Ed25519PrivateKey.from_private_bytes(bytes(range(32)))
public=enc(signer.public_key().public_bytes_raw())
class State:
    machine=''
    binding=''
    mode=''
    notes=[]
    counts=[]
    seen=[]
state=State()
def lease():
    now=int(time.time())
    payload=json.dumps(dict(v=2,kid='synthetic',product='demo',user='synthetic-user',email='test@example.invalid',machine=state.machine,binding=state.binding,modules=['core'],issued=stamp(now),expires=stamp(now+86400),terms=1),separators=(',',':')).encode()
    return enc(payload)+'.'+enc(signer.sign(b'licensebox lease v2\n'+payload))
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def reply(self,status,value):
        raw=json.dumps(value,ensure_ascii=False).encode();self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
    def do_POST(self):
        try:
            data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            op=self.path.rsplit('/',1)[-1];state.seen.append(op)
            if op=='device':
                assert data['product']=='demo' and data['binding'].startswith('m1:linux-')
                state.machine=data['machine'];state.binding=data['binding']
                message=f"licensebox device v2\ndemo\n{state.binding}\n{data['time']}"
            elif op=='token':
                assert data['device_code']=='synthetic-device-code'
                message=f"licensebox token v2\nsynthetic-device-code\n{state.binding}\n{data['time']}"
            else:
                assert data['binding']==state.binding
                token=data['lease'];payload,signature=token.split('.')
                signer.public_key().verify(dec(signature),b'licensebox lease v2\n'+dec(payload))
                message=f"licensebox {op} v2\n{token}\n{state.binding}\n{data['time']}\n{hashlib.sha256(data['payload'].encode()).hexdigest()}"
            Ed25519PublicKey.from_public_bytes(dec(state.machine)).verify(dec(data['signature']),message.encode())
            if op=='device': return self.reply(200,dict(device_code='synthetic-device-code',user_code='SYNTH-CLI',verification_uri='https://example.invalid/activate',verification_uri_complete='https://example.invalid/activate?code=SYNTH-CLI',expires_in=60,interval=0,interval_ms=1))
            if op=='token': return self.reply(200,dict(lease=lease(),usage=True))
            if state.mode=='offline' and op=='renew':return self.reply(503,dict(error='temporarily_unavailable'))
            if state.mode=='deny':return self.reply(403,dict(error='location_denied',error_description='Synthetic refusal'))
            payload=json.loads(data['payload'])
            if op=='renew':state.counts.extend(payload.get('usage',[]));return self.reply(200,dict(lease=lease(),usage=True))
            if op=='modules':return self.reply(200,[dict(name='core',description='Synthetic',free=False,have=True,requested=False)])
            if op=='request':
                state.notes.append(payload)
                return self.reply(200,dict(id='synthetic-request',modules=payload['modules'],note=payload['note'],status='pending'))
            if op=='requests':return self.reply(200,[])
            if op=='access-link':return self.reply(200,dict(url='https://example.invalid/private-access'))
            self.reply(400,dict(error='invalid_argument'))
        except Exception:
            self.reply(400,dict(error='bad_signature'))
            raise

def main():
    executable=str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='licensebox-cli-synthetic-') as folder:
        root=Path(folder);keys=root/'keys.json';keys.write_text(json.dumps({'synthetic':public}))
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
        options=['--product','demo','--keys',str(keys),'--directory',str(root/'state'),'--server',f'http://127.0.0.1:{server.server_port}','--version','integration']
        def run(*args,code=0,error=None):
            p=subprocess.run([executable,*options,*args],text=True,capture_output=True,timeout=15)
            assert p.returncode==code,(args,p.returncode,p.stdout,p.stderr)
            if error:assert p.stdout=='' and p.stderr==f'licensebox: {error}\n',(args,p.stdout,p.stderr)
            assert 'synthetic-device-code' not in p.stdout+p.stderr
            assert not any(len(word)>200 and '.' in word for word in p.stdout.split()),'lease leaked to stdout'
            return p
        try:
            run('status',code=1,error='not_signed_in')
            run('heartbeat','core',code=1,error='not_signed_in');assert state.seen==[]
            result=run('sign-in','--timeout','3');assert result.stdout=='authorized\n'
            assert 'https://example.invalid/activate' in result.stderr and 'SYNTH-CLI' in result.stderr
            assert run('require','core').stdout=='authorized\n'
            before=state.seen.count('renew')
            for _ in range(2):
                heartbeat=run('heartbeat','core');assert heartbeat.stdout=='authorized\n' and heartbeat.stderr==''
            assert state.seen.count('renew')==before+2
            run('heartbeat','missing',code=1,error='module_denied')
            run('heartbeat','',code=2,error='invalid_argument')
            saved=(root/'state/lease').read_text();state.mode='offline'
            assert run('heartbeat','core').stdout=='authorized\n'
            assert (root/'state/lease').read_text()==saved
            state.mode=''

            status=json.loads(run('status').stdout);assert set(status)=={'product','modules','expires'} and status['modules']==['core']
            assert json.loads(run('modules').stdout)[0]['name']=='core'
            assert json.loads(run('requests').stdout)==[]
            submitted=json.loads(run('request','export','--note','café – shell test').stdout)
            assert submitted['note']=='café – shell test' and state.notes[-1]['modules']==['export']
            assert run('access-link').stdout=='https://example.invalid/private-access\n'
            assert run('record','export','--success','--elapsed','0.125').stdout=='recorded\n'
            run('record','export','--failed','--elapsed','0.025')
            run('require','core');assert any(count['command']=='export' and count['ok']==1 and count['failed']==1 for count in state.counts)
            run('require','missing',code=1,error='module_denied')
            run('require','core','unexpected',code=2,error='invalid_argument')
            run('record','export','--success','--elapsed','nan',code=2,error='invalid_argument')
            run('--binding','m1:forbidden','require','core',code=2,error='invalid_argument')
            state.mode='deny';run('heartbeat','core',code=1,error='location_denied')
            server.shutdown();server.server_close();worker.join()
            run('heartbeat','core',code=1,error='location_denied')
            run('require','core',code=1,error='location_denied')
            run('status',code=1,error='location_denied')
            assert run('logout').stdout=='logged_out\n'
            run('status',code=1,error='not_signed_in')
            assert (root/'state/install.key').exists() and not (root/'state/lease').exists()
            assert (root/'state').stat().st_mode&0o777==0o700
            assert (root/'state/install.key').stat().st_mode&0o777==0o600
            # A synthetic self-signed TLS server requires the explicit CA file;
            # no flag disables verification.
            cert=root/'test-ca.pem'; private=root/'test-ca-key.pem'
            subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1','-keyout',str(private),'-out',str(cert)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            state.mode=''
            tls=ThreadingHTTPServer(('127.0.0.1',0),Handler)
            context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,private)
            tls.socket=context.wrap_socket(tls.socket,server_side=True)
            tls_worker=threading.Thread(target=tls.serve_forever,daemon=True);tls_worker.start()
            try:
                tls_options=['--product','demo','--keys',str(keys),'--directory',str(root/'tls-state'),'--server',f'https://127.0.0.1:{tls.server_port}']
                untrusted=subprocess.run([executable,*tls_options,'sign-in','--timeout','0.1'],capture_output=True,text=True,timeout=10)
                assert untrusted.returncode==1 and untrusted.stdout=='' and untrusted.stderr.startswith('licensebox: ')
                trusted=subprocess.run([executable,*tls_options,'--ca-bundle',str(cert),'sign-in','--timeout','3'],capture_output=True,text=True,timeout=10)
                assert trusted.returncode==0 and trusted.stdout=='authorized\n',trusted.stderr
            finally:
                tls.shutdown();tls.server_close();tls_worker.join()
        finally:
            server.shutdown();server.server_close()
    print('PASS: production CLI flags, native identity, signed operations, UTF-8, usage, policy persistence, output redaction, verified TLS/CA and installed consumer')
if __name__=='__main__':main()
