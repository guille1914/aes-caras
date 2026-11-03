# Lector/escritor mínimo de .pex de Skyrim (big-endian), con ida y vuelta exacta.
import struct
NARGS={0:0,1:3,2:3,3:3,4:3,5:3,6:3,7:3,8:3,9:3,10:2,11:2,12:2,13:2,14:2,15:3,16:3,17:3,18:3,19:3,20:1,21:2,22:2,
       23:3,24:2,25:3,26:1,27:3,28:3,29:3,30:2,31:2,32:3,33:3,34:4,35:4}
VAR={23,24,25}
class R:
    def __init__(s,b): s.b=b; s.i=0
    def u(s,f):
        v=struct.unpack_from('>'+f,s.b,s.i)[0]; s.i+=struct.calcsize('>'+f); return v
    def ws(s):
        n=s.u('H'); v=s.b[s.i:s.i+n]; s.i+=n; return v
class W:
    def __init__(s): s.p=[]
    def u(s,f,v): s.p.append(struct.pack('>'+f,v))
    def ws(s,b): s.u('H',len(b)); s.p.append(b)
    def bytes(s): return b''.join(s.p)
def rvar(r):
    t=r.u('B'); v={0:None,1:'H',2:'H',3:'i',4:'f',5:'B'}[t]
    return (t, r.u(v) if v else None)
def wvar(w,x):
    t,v=x; w.u('B',t)
    f={0:None,1:'H',2:'H',3:'i',4:'f',5:'B'}[t]
    if f: w.u(f,v)
def rfun(r):
    f=dict(ret=r.u('H'),doc=r.u('H'),uf=r.u('I'),fl=r.u('B'))
    f['params']=[(r.u('H'),r.u('H')) for _ in range(r.u('H'))]
    f['locals']=[(r.u('H'),r.u('H')) for _ in range(r.u('H'))]
    ins=[]
    for _ in range(r.u('H')):
        op=r.u('B'); a=[rvar(r) for _ in range(NARGS[op])]
        if op in VAR:
            n=rvar(r); a.append(n); a+= [rvar(r) for _ in range(n[1])]
        ins.append((op,a))
    f['ins']=ins; return f
def wfun(w,f):
    w.u('H',f['ret']);w.u('H',f['doc']);w.u('I',f['uf']);w.u('B',f['fl'])
    for k in ('params','locals'):
        w.u('H',len(f[k]))
        for a,b in f[k]: w.u('H',a);w.u('H',b)
    w.u('H',len(f['ins']))
    for op,a in f['ins']:
        w.u('B',op); [wvar(w,x) for x in a]
def leer(b):
    r=R(b); p={}
    p['hdr']=(r.u('I'),r.u('B'),r.u('B'),r.u('H'),r.u('Q')); p['src']=r.ws();p['user']=r.ws();p['mach']=r.ws()
    p['st']=[r.ws() for _ in range(r.u('H'))]
    d=r.u('B'); p['dbg']=None
    if d:
        mt=r.u('Q'); fs=[]
        for _ in range(r.u('H')):
            a=(r.u('H'),r.u('H'),r.u('H'),r.u('B')); c=r.u('H'); fs.append((a,[r.u('H') for _ in range(c)]))
        p['dbg']=(mt,fs)
    p['uflags']=[(r.u('H'),r.u('B')) for _ in range(r.u('H'))]
    objs=[]
    for _ in range(r.u('H')):
        o=dict(name=r.u('H')); size=r.u('I')
        o.update(parent=r.u('H'),doc=r.u('H'),uf=r.u('I'),auto=r.u('H'))
        nv=r.u('H'); assert nv==0; npr=r.u('H'); assert npr==0
        sts=[]
        for _ in range(r.u('H')):
            sn=r.u('H'); fs=[]
            for _ in range(r.u('H')):
                fs.append((r.u('H'),rfun(r)))
            sts.append((sn,fs))
        o['states']=sts; objs.append(o)
    p['objs']=objs; assert r.i==len(b),(r.i,len(b)); return p
def escribir(p):
    w=W(); m,a,b,g,t=p['hdr']; w.u('I',m);w.u('B',a);w.u('B',b);w.u('H',g);w.u('Q',t)
    w.ws(p['src']);w.ws(p['user']);w.ws(p['mach'])
    w.u('H',len(p['st'])); [w.ws(s) for s in p['st']]
    if p['dbg']:
        w.u('B',1); mt,fs=p['dbg']; w.u('Q',mt); w.u('H',len(fs))
        for (x,y,z,k),ls in fs:
            w.u('H',x);w.u('H',y);w.u('H',z);w.u('B',k);w.u('H',len(ls)); [w.u('H',l) for l in ls]
    else: w.u('B',0)
    w.u('H',len(p['uflags']))
    for x,y in p['uflags']: w.u('H',x);w.u('B',y)
    w.u('H',len(p['objs']))
    for o in p['objs']:
        w.u('H',o['name']); ow=W()
        ow.u('H',o['parent']);ow.u('H',o['doc']);ow.u('I',o['uf']);ow.u('H',o['auto']);ow.u('H',0);ow.u('H',0)
        ow.u('H',len(o['states']))
        for sn,fs in o['states']:
            ow.u('H',sn); ow.u('H',len(fs))
            for fn,f in fs: ow.u('H',fn); wfun(ow,f)
        body=ow.bytes(); w.u('I',len(body)+4); w.p.append(body)
    return w.bytes()
