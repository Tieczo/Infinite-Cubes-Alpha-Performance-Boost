# Test-only: builds a test Save from a scene copy: only WORLD in worlds.db, only character slot SLOT in
# characters.db (both become entry 0, so "first character, first world" picks them), world clock set
# to TIME_MS (day kept). Usage: prep_scene.py SRC_SAVE DEST_SAVE WORLD SLOT TIME_MS
import sys, os, shutil, sqlite3, struct
src, dst, world, slot, time_ms = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
if os.path.exists(dst):
    shutil.rmtree(dst)
os.makedirs(dst)
for f in ('characters.db', 'worlds.db', 'world_%s.db' % world, 'map_%s.db' % world):
    shutil.copy2(os.path.join(src, f), os.path.join(dst, f))
if os.path.isdir(os.path.join(src, 'CharacterSave')):
    shutil.copytree(os.path.join(src, 'CharacterSave'), os.path.join(dst, 'CharacterSave'))
def single(path, keep):
    c = sqlite3.connect(path)
    v = c.execute('select value from blobs where key=?', (keep,)).fetchone()[0]
    c.execute('delete from blobs')
    c.execute('insert into blobs values (?, ?)', ('num', struct.pack('<i', 1)))
    c.execute('insert into blobs values (?, ?)', ('0', v))
    c.commit(); c.execute('vacuum'); c.close()
    return v
c = sqlite3.connect('file:%s?mode=ro' % os.path.join(dst, 'worlds.db'), uri=True)
idx = None
for k, v in c.execute('select key, value from blobs'):
    if k != 'num':
        l = struct.unpack('<i', v[:4])[0]
        if v[4:4 + l].decode('latin1') == world:
            idx = k
c.close()
assert idx is not None, 'Welt nicht gefunden: ' + world
single(os.path.join(dst, 'worlds.db'), idx)
single(os.path.join(dst, 'characters.db'), slot)
c = sqlite3.connect(os.path.join(dst, 'world_%s.db' % world))
v = c.execute("select value from blobs where key='time'").fetchone()[0]
day = struct.unpack('<i', v[:4])[0]
c.execute("update blobs set value=? where key='time'", (struct.pack('<ii', day, time_ms),))
c.commit(); c.close()
print('Szene: Welt %s (war Eintrag %s), Charakter-Slot %s, Tag %d, Uhrzeit %.2f h' % (world, idx, slot, day, time_ms / 3.6e6))
