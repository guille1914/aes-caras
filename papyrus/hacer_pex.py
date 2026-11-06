# Genera AesCaras.pex (equivalente a papyrus/AesCaras.psc) a partir de la estructura de TESModPlatform.pex.
import sys, copy, pex2
src, dst = sys.argv[1], sys.argv[2]
p = pex2.leer(open(src,'rb').read()); st = p['st']
def idx(s):
    s = s.encode()
    if s not in st: st.append(s)
    return st.index(s)
o = p['objs'][0]
nativa = [f for fn,f in o['states'][0][1] if f['fl']==3][0]
print('ejemplo nativa', st[[fn for fn,f in o['states'][0][1] if f['fl']==3][0]], nativa['fl'])
def fun(ret, params):
    return dict(ret=idx(ret), doc=idx(''), uf=0, fl=3, params=[(idx(a),idx(b)) for a,b in params], locals=[], ins=[])
nuevas = [(idx('PintarCara'), fun('Int', [('akActor','Actor'),('asCapas','String')])),
          (idx("Version"), fun("Int", [])),
          (idx("SoltarTeclas"), fun("Int", []))]
guardar = [(fn,f) for fn,f in o['states'][0][1] if st[fn] in (b'GetState', b'GotoState')]
assert len(guardar)==2
o['name'] = idx('AesCaras')
o['states'] = [(o['states'][0][0], guardar + nuevas)] + o['states'][1:]
p['src'] = b'AesCaras.psc'; p['user'] = b'aestherius'; p['mach'] = b'aestherius'; p['dbg'] = None
b = pex2.escribir(p); open(dst,'wb').write(b)
q = pex2.leer(b); assert pex2.escribir(q)==b
for fn,f in q['objs'][0]['states'][0][1]: print(q['st'][fn], q['st'][f['ret']], f['fl'], [(q['st'][a],q['st'][c]) for a,c in f['params']])
print(len(q['objs'][0]['states']), 'estados;', len(b), 'bytes')
