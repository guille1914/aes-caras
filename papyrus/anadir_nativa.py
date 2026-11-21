# giro2 · Añade una función nativa global a un .pex ya hecho (sin Creation Kit), copiando la forma de las que ya tiene.
#   python3 papyrus/anadir_nativa.py papyrus/AesCaras.pex GirarJugador Int afGrados:Float
import sys, pex2
ruta, nombre, ret = sys.argv[1], sys.argv[2], sys.argv[3]
params = [a.split(':') for a in sys.argv[4:]]
p = pex2.leer(open(ruta, 'rb').read()); st = p['st']
def idx(s):
    s = s.encode()
    if s not in st: st.append(s)
    return st.index(s)
funs = p['objs'][0]['states'][0][1]
if any(st[fn] == nombre.encode() for fn, f in funs):
    sys.exit(f'{nombre} ya está en {ruta}')
funs.append((idx(nombre), dict(ret=idx(ret), doc=idx(''), uf=0, fl=3, params=[(idx(a), idx(b)) for a, b in params], locals=[], ins=[])))
b = pex2.escribir(p); open(ruta, 'wb').write(b)
q = pex2.leer(b); assert pex2.escribir(q) == b
for fn, f in q['objs'][0]['states'][0][1]: print(q['st'][fn], q['st'][f['ret']], f['fl'], [(q['st'][a], q['st'][c]) for a, c in f['params']])
