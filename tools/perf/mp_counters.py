#!/usr/bin/env python3
"""Throwaway Linux builds only: per-tick monotonic timestamps and socket/lock costs.
Use MEASURE_TARGETS='Cube Server' build-measure.sh COMMIT DEST this-script.
MP lines go to stderr; server socket identifies each stream. Pair the nth server
snapshot with the nth client completion for data age (same host monotonic clock).
"""
import sys
from pathlib import Path
root = Path(sys.argv[1])
header = r'''
#include <chrono>
#include <cstdio>
#include <algorithm>
namespace {
long long mpNow() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct MPStats {
 long long start=0, wait=0, io=0, longest=0, bytes=0, calls=0, entities=0, seq=0;
 void report(const char *side, int socket) {
  fprintf(stderr,"MP %s %d %lld %lld %lld %lld %lld %lld %lld %lld %lld\n",side,socket,++seq,start,mpNow(),wait,io,longest,bytes,calls,entities);
  start=mpNow(); wait=io=longest=bytes=calls=entities=0;
 }
};
thread_local MPStats mp;
int mpSend(int s,const char *p,int n,int f) { auto t=mpNow(); int r=send(s,p,n,f); auto d=mpNow()-t; mp.io+=d; mp.longest=std::max(mp.longest,d); mp.bytes+=std::max(r,0); ++mp.calls; return r; }
int mpReceive(int s,char *p,int n,int f) { auto t=mpNow(); int r=cube::receiveAll(s,p,n,f); mp.io+=mpNow()-t; ++mp.calls; mp.bytes+=n; return r; }
}
'''
for filename,start,end,server in [('Server.cpp','            sendThread = StartThread(', '                timeEndPeriod(1);',True),('GameController.cpp','        m_receiveThread = StartThread(', '        m_sendThread = StartThread(',False)]:
 if len(sys.argv)>2 and filename!=sys.argv[2]: continue
 p=root/'src/cube'/filename;s=p.read_text();a=s.index(start);b=s.index(end,a);part=s[a:b]
 if server:
  part=part.replace('send(', 'mpSend(').replace('                    visible.clear();','                    mp.start=mpNow();\n                    visible.clear();').replace('                    // At most one tick', '                    mp.entities=visible.size(); mp.report("server", connections[slot]->socket);\n                    // At most one tick')
 else:
  part=part.replace('receiveAll(', 'mpReceive(').replace('                switch (packetId)', '                if(packetId==PacketEntitiesEnd) mp.report("client",m_socket);\n                if(packetId==PacketEntityData) ++mp.entities;\n                switch (packetId)')
 lock='world->enterRegions();' if server else 'm_world.enterRegions();'
 part=part.replace(lock,'{ auto mpLockStart=mpNow(); '+lock+' mp.wait+=mpNow()-mpLockStart; }')
 s=s[:a]+part+s[b:];pos=s.index('namespace cube {');s=s[:pos]+header+s[pos:];p.write_text(s)
