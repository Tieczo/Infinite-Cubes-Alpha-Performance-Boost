import socket,struct,sys,time,zlib,json,threading
# Observer: a second player without graphics on the server at 127.0.0.1:12345; logs the other players and counts events.
# Usage: observer.py OUT.json SECONDS
out=sys.argv[1]; secs=float(sys.argv[2]); ES=4456
s=socket.create_connection(('127.0.0.1',12345),timeout=15); s.sendall(struct.pack('<ii',17,3))
buf=bytearray(); lock=threading.Lock(); alive=[True]
def rx():
    s.settimeout(1)
    while alive[0]:
        try:
            d=s.recv(1<<20)
            if not d: alive[0]=False; break
            with lock: buf.extend(d)
        except socket.timeout: pass
        except OSError: alive[0]=False
threading.Thread(target=rx,daemon=True).start()
def need(n):
    t=time.time()
    while True:
        with lock:
            if len(buf)>=n: return True
        if not alive[0] or time.time()-t>3: return False
        time.sleep(0.002)
def take(n):
    with lock:
        b=bytes(buf[:n]); del buf[:n]
    return b
need(4+4+8+ES+8); print('Beginn',struct.unpack('<ii',take(8))); pid=struct.unpack('<q',take(8))[0]; take(ES); take(4); seed=struct.unpack('<i',take(4))[0]; print('Beobachter id',pid,'Seed',seed)
ev={'hits':0,'hits_doubled':0,'kills':0,'items':0,'bullets':0,'sounds':0,'particles':0,'blocks':0,'pickups':0,'aggro':0,'buffs':0}
seen={}; first={}; tl=[]; chat=0
def lst(d,o,sz):
    n=struct.unpack_from('<i',d,o)[0]; return n,o+4+n*sz
def world(d):
    o=0
    for name,sz in (('blocks',20),('hits',72),('particles',72),('sounds',24),('bullets',112)):
        o0=o; n,o=lst(d,o,sz); ev[name]+=n
        # The original Server.exe queues each creature hit on a player twice: identical hits in one packet.
        if name=='hits': ev['hits_doubled']+=n-len({d[o0+4+k*sz:o0+4+(k+1)*sz] for k in range(n)})
    n,o=lst(d,o,88)
    m=struct.unpack_from('<i',d,o)[0]; o+=4
    for _ in range(m):
        o+=8; n,o=lst(d,o,328); ev['items']+=n
    m=struct.unpack_from('<i',d,o)[0]; o+=4
    for _ in range(m):
        o+=8; n,o=lst(d,o,16)
    n,o=lst(d,o,288); ev['pickups']+=n
    n,o=lst(d,o,24); ev['kills']+=n
    n,o=lst(d,o,24); ev['aggro']+=n
    n,o=lst(d,o,40); ev['buffs']+=n
    n,o=lst(d,o,56)
    assert o==len(d)
t0=time.time(); nexts=0
while time.time()-t0<secs and alive[0]:
    if not need(4): continue
    p=struct.unpack('<i',take(4))[0]
    if p==0:
        need(4); n=struct.unpack('<i',take(4))[0]; need(n); d=zlib.decompress(take(n)); i=struct.unpack_from('<q',d,0)[0]; mask=struct.unpack_from('<Q',d,8)[0]
        seen[i]=seen.get(i,0)+1
        if i<=10 and i!=pid and mask&1:
            x,y,z=struct.unpack_from('<qqq',d,16); first.setdefault(i,(x,y,z)); last=(x/65536,y/65536,z/65536)
            if time.time()-t0>nexts: tl.append((round(time.time()-t0),i)+last); nexts=time.time()-t0+10
    elif p==2: pass
    elif p==5: need(8); take(8)
    elif p==4:
        need(4); n=struct.unpack('<i',take(4))[0]; need(n); world(zlib.decompress(take(n)))
    elif p==10:
        need(12); c,l=struct.unpack('<qi',take(12)); need(2*l); take(2*l); chat+=1
    else: print('unbekanntes Paket',p); break
print('Spieler gesehen',{i:seen[i] for i in seen if i<=10},'Lebewesen',len(seen),'Ereignisse',ev,'Chat',chat)
for r in tl: print(r)
print('Verbindung am Ende offen',alive[0])
alive[0]=False; s.close()
