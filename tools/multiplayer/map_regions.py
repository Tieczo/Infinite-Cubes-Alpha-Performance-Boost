# Compares the region entries of two map databases. Usage: map_regions.py A.db B.db
import sqlite3,sys,collections
def rd(f):
    c=sqlite3.connect('file:%s?mode=ro'%f,uri=True); return dict(c.execute('select key,value from blobs'))
a=rd(sys.argv[1]); b=rd(sys.argv[2]); T=14; Rr=44
c=collections.Counter(); ex=[]
for k in a:
    if not k.startswith('reg') or k not in b: continue
    p,q=a[k],b[k]
    for t in range(4096):
        x=p[4+t*T:4+(t+1)*T]; y=q[4+t*T:4+(t+1)*T]
        if x[0]!=y[0]: c['Kachel-Flags anders']+=1
        if x[1:]!=y[1:]:
            e=any(x[1:]) and any(y[1:])
            c['Zelle anders, beide gefüllt' if e else 'Zelle nur auf einer Seite']+=1
            d=x[1:]==b'\0'*8+b'\1'+b'\0'*4 or y[1:]==b'\0'*8+b'\1'+b'\0'*4
            c['davon eine Seite noch leer' if d else 'ECHT anders']+=e
            if e and not d and len(ex)<4: ex.append((k,t,x.hex(),y.hex()))
        elif any(x[1:]): c['Zelle gleich gefüllt']+=1
    o=4+4096*T
    for r in range(64):
        x=p[o+r*Rr:o+(r+1)*Rr]; y=q[o+r*Rr:o+(r+1)*Rr]
        c['Datensatz gleich' if x==y else 'Datensatz anders']+=1
        d=x[:13]==b'\0'*12+b'\1' or y[:13]==b'\0'*12+b'\1'
        if x!=y: c['Datensatz: eine Seite leer' if d else 'Datensatz ECHT anders']+=1
        if x!=y and not d and len(ex)<8: ex.append((k,'rec',r,x.hex(),y.hex()))
print(c); 
for e in ex: print(e)
