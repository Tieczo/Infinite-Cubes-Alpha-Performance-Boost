# Version check and chat round trip against a server at 127.0.0.1:12345: a wrong client version is refused,
# a chat line with non-ASCII text reaches a second client.
import socket,struct,sys,time
ES=4456
# Falsche Version
s=socket.create_connection(('127.0.0.1',12345),timeout=5); s.sendall(struct.pack('<ii',17,99)); s.settimeout(3)
try:
    d=b''
    while True:
        x=s.recv(64)
        if not x: break
        d+=x
except socket.timeout: pass
print('Falsche Version: Antwort',d.hex(),'dann geschlossen')
s.close(); time.sleep(1)
def join():
    s=socket.create_connection(('127.0.0.1',12345),timeout=10); s.sendall(struct.pack('<ii',17,3)); return s
a=join(); b=join(); time.sleep(3)
text='Grüße aus Köln ÄÖÜß €'
u=text.encode('utf-16-le'); a.sendall(struct.pack('<ii',10,len(text))+u)
b.settimeout(0.5); buf=b''; t=time.time()
while time.time()-t<6:
    try:
        x=b.recv(1<<20)
        if x: buf+=x
    except socket.timeout: pass
k=buf.find(u); print('Chat kommt an:',k>=0, 'Absender/Länge davor',struct.unpack('<iqi',buf[k-16:k]) if k>=16 else '', buf[k:k+len(u)].decode('utf-16-le') if k>=0 else '')
a.close(); b.close()
