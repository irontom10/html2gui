#!/bin/sh
set -eu
HTML=${H2G_DOOMSDAY:-examples/doomsday.html}
./h2g-reflow-test
# Use a small, explicit mobile-first test first; no GUI dependencies.
./h2g-layout-check tests/responsive.html 500 side content metrics > tests/tmp/rt-500.txt
./h2g-layout-check tests/responsive.html 1100 side content metrics > tests/tmp/rt-1100.txt
./h2g-layout-check tests/responsive.html 1600 side content metrics > tests/tmp/rt-1600.txt
python3 - <<'PY'
from pathlib import Path
for size, visible, x, cols in ((500,0,0,1),(1100,1,270,2),(1600,1,300,4)):
    rows=Path(f'tests/tmp/rt-{size}.txt').read_text().splitlines()
    m={row.split()[0].split('=')[1]:row for row in rows[1:]}
    assert f'visible={visible}' in m['side'], (size,m['side'])
    assert f'x={x}' in m['content'], (size,m['content'])
    assert f'cols={cols}' in m['metrics'], (size,m['metrics'])
    assert int(rows[0].split('document_height=')[1]) > 0
print('PASS: responsive HTML -> DOM -> runtime layout: 500/1100/1600px, sidebar, main column, metric columns')
PY
# The full Doomsday document is intentionally a larger regression fixture.
if [ -f "$HTML" ]; then
  ./h2g-layout-check "$HTML" 650 main layout class-count > tests/tmp/doomsday-650.txt
  ./h2g-layout-check "$HTML" 1600 main layout class-count > tests/tmp/doomsday-1600.txt
  python3 - <<'PY'
from pathlib import Path
m=Path('tests/tmp/doomsday-650.txt').read_text()
d=Path('tests/tmp/doomsday-1600.txt').read_text()
assert 'x=0 y=' in next(x for x in m.splitlines() if x.startswith('id=main'))
assert 'x=300 y=' in next(x for x in d.splitlines() if x.startswith('id=main'))
for text in (m,d):
    assert 'nodes=15223' in text
    assert 'id=class-count visible=1' in text
print('PASS: complete Doomsday DOM at 650/1600px; sidebar and metrics reflow')
PY
else
  echo 'SKIP: Doomsday fixture not present outside package' >&2
fi
