# Compares the map tiles (shape and colour) of two map databases, e.g. Save/map_online_SEED.db of two clients.
# Usage: map_shape.py A.db B.db
import sqlite3,sys,zlib,struct,collections
def rd(f):
    c=sqlite3.connect('file:%s?mode=ro'%f,uri=True); return dict(c.execute('select key,value from blobs'))
def tile(v):
    ver,bz,sx,sy,sz,cs=struct.unpack_from('<iiiiii',v,0)
    return ver,bz,(sx,sy,sz),zlib.decompress(v[24:24+cs])
a=rd(sys.argv[1]); b=rd(sys.argv[2])
ks=sorted(k for k in a if k.startswith('tile') and k in b)
skip=0;vox=0;occa=0;occb=0;only=0;colordiff=0;delta=collections.Counter();per=[]
for k in ks:
    A=tile(a[k]);B=tile(b[k]); p,q=A[3],B[3]; n=len(p)//3; o=0;c=0
    if A[:3]!=B[:3]: skip+=1; continue
    for i in range(n):
        x=p[3*i:3*i+3];y=q[3*i:3*i+3]; ea=x==b'\0\0\0'; eb=y==b'\0\0\0'
        vox+=1; occa+=not ea; occb+=not eb
        if ea!=eb: only+=1;o+=1
        elif x!=y:
            colordiff+=1;c+=1; m=max(abs(x[j]-y[j]) for j in range(3)); delta[min(m,64) if m<16 else (m//16)*16]+=1
    per.append((k,o,c))
print('Zonen',len(ks),'Kopf anders',skip,'Voxel',vox,'belegt orig',occa,'belegt neu',occb,'Form anders',only,'Farbe anders',colordiff)
print('größte Farbabweichung je Voxel',sorted(delta.items()))
print(sorted(per,key=lambda r:-r[1])[:6]); print(sorted(per,key=lambda r:-r[2])[:3])
