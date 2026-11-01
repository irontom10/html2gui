#!/bin/sh
set -eu
mkdir -p tests/tmp
./html2gtk examples/errs.html tests/tmp/errs.ui tests/tmp/errs.css 'ERRS Diag'
./html2gtk tests/fixtures.html tests/tmp/fixtures.ui tests/tmp/fixtures.css 'Fixtures'
./html2gtk tests/vertical.html tests/tmp/vertical.ui tests/tmp/vertical.css 'Grow test'
python3 - <<'PY'
import xml.etree.ElementTree as ET
from pathlib import Path
ui = Path('tests/tmp/errs.ui')
css = Path('tests/tmp/errs.css').read_text()
root = ET.parse(ui).getroot()
objects = root.findall('.//object')
assert root.tag == 'interface'
assert len(objects) > 30, len(objects)
assert any(o.get('id') == 'connect_button' and o.get('class') == 'GtkButton' for o in objects)
assert any(o.get('id') == 'calibration_id' and o.get('class') == 'GtkEntry' for o in objects)
assert any(o.get('class') == 'GtkGrid' for o in objects)
assert '.tw-' in css and 'background-color:' in css
fixture = ET.parse('tests/tmp/fixtures.ui')
ob = {o.get('id'): o for o in fixture.findall('.//object') if o.get('id')}
assert ob['adapt'].get('class') == 'GtkDropDown'
assert ob['secret'].get('class') == 'GtkEntry'
assert ob['fire'].get('class') == 'GtkButton'
label = ob['special'].find("property[@name='label']")
assert label is not None and label.text == 'ECM & GPS → "OK"', label.text
textarea = ob['notes'].find('.//object[@class="GtkTextBuffer"]/property[@name="text"]')
assert textarea is not None and textarea.text == 'Hot < Cold'
assert any(o.get('class') == 'GtkStringList' for o in fixture.findall('.//object'))
assert any(o.find("layout/property[@name='column-span']") is not None for o in fixture.findall('.//object'))
assert any(o.get('class') == 'GtkBox' and o.find("property[@name='hexpand']") is not None for o in fixture.findall('.//object'))
vertical = ET.parse('tests/tmp/vertical.ui')
vo = {o.get('id'): o for o in vertical.findall('.//object') if o.get('id')}
def has_true(o, prop):
    el = o.find(f"property[@name='{prop}']")
    return el is not None and el.text == 'true'
assert has_true(vo['root_box'], 'vexpand')
body = next(o for o in vo.values() if o.get('class') == 'GtkBox' and any(c.get('name', '').startswith('tw-') for style in o.findall('style') for c in style.findall('class')) and o.find("property[@name='vexpand']") is not None)
assert has_true(body, 'vexpand')
assert has_true(vo['grow_main'], 'vexpand')
assert has_true(vo['grow_section'], 'vexpand')
errs = {o.get('id'): o for o in ET.parse(ui).findall('.//object') if o.get('id')}
assert has_true(errs['root_box'], 'vexpand')
print('PASS: XML parsed, IDs/native widgets, Tailwind CSS, entities, dropdown, text area, colspan, spacers, vertical expansion')
PY
if ./html2gtk tests/invalid.html tests/tmp/invalid.ui tests/tmp/invalid.css 2>/dev/null; then
    echo 'FAIL: expected malformed document error' >&2
    exit 1
fi
# ERRS warning-class regression at mobile, desktop, wide desktop.
for width in 500 1100 1600; do
    ./html2gtk tests/errs-tailwind.html "tests/tmp/errs-tailwind-$width.ui" "tests/tmp/errs-tailwind-$width.css" ERRS "$width" 2>"tests/tmp/errs-tailwind-$width.log"
    if grep -q 'warning:' "tests/tmp/errs-tailwind-$width.log"; then
        cat "tests/tmp/errs-tailwind-$width.log" >&2
        exit 1
    fi
done
python3 - <<'PY'
import xml.etree.ElementTree as ET
from pathlib import Path

def obj(roots, id):
    return next(e for e in roots.findall('.//object') if e.get('id') == id)

def prop(o, name):
    e = o.find(f"property[@name='{name}']")
    return e.text if e is not None else None

for width, orient, ncols, siden in [(500,'vertical',1,None),(1100,'horizontal',2,'256'),(1600,'horizontal',4,'256')]:
    u=ET.parse(f'tests/tmp/errs-tailwind-{width}.ui')
    c=Path(f'tests/tmp/errs-tailwind-{width}.css').read_text()
    assert prop(obj(u,'outer'),'orientation') == orient
    assert prop(obj(u,'sidebar'),'width-request') == siden
    children=obj(u,'metrics').findall('./child/object')
    columns=[int(e.find("layout/property[@name='column']").text) for e in children]
    assert max(columns)+1 == ncols, (width,columns)
    assert obj(u,'scroll').get('class') == 'GtkScrolledWindow'
    assert 'text placeholder' in c and ':hover' in c and ':focus' in c
    assert 'alpha(#0f172a, 0.70)' in c
    assert 'tw-divider-' in c
    assert 'letter-spacing:' in c and 'text-transform:' in c
    assert 'background-color: #22d3ee;' in c
print('PASS: ERRS warning-class coverage (0 warnings), viewport-specific native layout, hover/focus, alpha colors, dividers, scroll')
PY

# HTML5 optional end tags: options in datalist/select, list items, paragraphs,
# definition terms and table cells.  The parser must not treat HTML as XML.
./html2gtk tests/optional-end-tags.html tests/tmp/optional-end-tags.ui tests/tmp/optional-end-tags.css 'Optional tags' 1100 2>tests/tmp/optional-end-tags.log
python3 - <<'PYTEST'
import xml.etree.ElementTree as ET
r=ET.parse('tests/tmp/optional-end-tags.ui').getroot()
objs=r.findall('.//object')
assert any(o.get('id')=='powertrain' and o.get('class')=='GtkDropDown' for o in objs)
assert any(o.get('id')=='records' and o.get('class')=='GtkGrid' for o in objs)
assert any(o.get('id')=='shortlist' and o.get('class')=='GtkBox' for o in objs)
print('PASS: HTML optional end tags (option, optgroup, li, dt/dd, p, tr/td)')
PYTEST

# Widget-specific GObject properties must only be emitted on compatible widgets.
# In particular, GTK4 has GtkLabel:ellipsize/xalign, not GtkBox:ellipsize or
# GtkScrolledWindow:xalign.
./html2gtk tests/widget-properties.html tests/tmp/widget-properties.ui tests/tmp/widget-properties.css 'Properties' > /dev/null 2>tests/tmp/widget-properties.log
python3 - <<'PYTEST'
import xml.etree.ElementTree as ET
root = ET.parse('tests/tmp/widget-properties.ui').getroot()
for widget in root.findall('.//object'):
    kind = widget.get('class')
    names = {prop.get('name') for prop in widget.findall('property')}
    if kind != 'GtkLabel':
        assert not ({'ellipsize', 'xalign'} & names), (kind, widget.get('id'), names)
labels = [w for w in root.findall('.//object') if w.get('class') == 'GtkLabel']
assert any(w.find("property[@name='ellipsize']") is not None for w in labels)
pics = [w for w in root.findall('.//object') if w.get('class') == 'GtkPicture']
assert pics and pics[0].find("property[@name='file']") is not None
assert pics[0].find("property[@name='filename']") is None
print('PASS: GtkLabel properties restricted to labels; GtkPicture:file property')
PYTEST

# Every unique CSS declaration should be emitted just once. Layout-only
# Tailwind utility sets must not install empty GtkCssProvider rules.
./html2gtk tests/style-interning.html tests/tmp/style-interning.ui tests/tmp/style-interning.css 'CSS intern' 1600 2>tests/tmp/style-interning.log
python3 - <<'PYTEST'
import re
import xml.etree.ElementTree as ET
from pathlib import Path
r = ET.parse('tests/tmp/style-interning.ui').getroot()
css = Path('tests/tmp/style-interning.css').read_text()
a = r.find('.//object[@id="repeat_one"]/style/class')
b = r.find('.//object[@id="repeat_two"]/style/class')
assert a is not None and b is not None and a.get('name') == b.get('name')
cls = a.get('name')
assert len(re.findall(r'^\.' + re.escape(cls) + r'\s*\{', css, re.M)) == 1
assert cls + ':hover' in css
assert css.count('background-color: #f87171;') == 1  # lg overrides base/sm
assert not re.search(r'\{\s*\}', css)
assert r.find('.//object[@id="unstyled"]/style/class') is None
print('PASS: shared style class, single breakpoint-collapsed rule, no empty CSS selectors')
PYTEST

# v0.2.5: viewport-aware visibility, fixed+fluid arbitrary grids, and
# native maximum-size clamp boxes (GTK layout helper, NOT scroll windows).
for width in 500 1100 1600; do
    ./html2gtk tests/max-sizing.html "tests/tmp/max-$width.ui" "tests/tmp/max-$width.css" "Max test" "$width" 2>"tests/tmp/max-$width.log"
done
python3 - <<'PYTESTMAX'
import xml.etree.ElementTree as ET

def prop(o,key):
    e=o.find("property[@name='%s']"%key)
    return e.text if e is not None else None
for width in (500,1100,1600):
    r=ET.parse('tests/tmp/max-%d.ui'%width).getroot()
    clamps=[o for o in r.iter('object') if (o.get('id') or '').startswith('h2g_clamp_')]
    assert any('_1700_-1_1' in o.get('id') for o in clamps)
    assert all(o.get('class')=='GtkBox' for o in clamps)
    assert all(prop(o,'overflow')=='hidden' for o in clamps)
    assert any('_768_-1_0' in o.get('id') for o in clamps)
    assert any('_-1_44_0' in o.get('id') for o in clamps)
    assert not any(prop(o,'max-content-width') is not None for o in clamps)
    inner={o.get('id'):o for o in r.iter('object') if o.get('id')}
    assert inner['full'].get('class')=='GtkBox'
print('PASS: max constraints use runtime GtkCustomLayout instead of scrollbars')
PYTESTMAX

cat > tests/tmp/torture-layout.html <<'HTML'
<!doctype html><html><body class="min-h-screen flex flex-col">
<div id="shell" class="lg:grid lg:grid-cols-[270px_minmax(0,1fr)] xl:grid-cols-[300px_minmax(0,1fr)]">
<aside id="sidebar" class="hidden lg:block">Navigation</aside>
<div id="page" class="min-w-0"><header id="top">Header</header>
<main id="page_main" class="mx-auto w-full max-w-[1700px]">Body</main>
</div></div></body></html>
HTML
for width in 500 1100 1600; do
    ./html2gtk tests/tmp/torture-layout.html "tests/tmp/torture-$width.ui" "tests/tmp/torture-$width.css" T "$width" 2>"tests/tmp/torture-$width.log"
done
python3 - <<'PYLAYOUT'
import xml.etree.ElementTree as ET

def p(o, key):
    x=o.find("property[@name='%s']"%key)
    return x.text if x is not None else None
for width, sidebar, first, cols in [(500,False,None,1),(1100,True,'270',2),(1600,True,'300',2)]:
    r=ET.parse('tests/tmp/torture-%d.ui'%width).getroot()
    o={x.get('id'):x for x in r.iter('object') if x.get('id')}
    assert o['shell'].get('class') == ('GtkGrid' if sidebar else 'GtkBox')
    assert (p(o['sidebar'],'visible') != 'false') == sidebar
    assert p(o['sidebar'],'width-request') == first
    if sidebar:
        assert p(o['shell'],'column-homogeneous') == 'false'
        assert p(o['page'],'hexpand') == 'true'
        assert p(o['sidebar'],'hexpand') is None
    assert any(x.get('id','').startswith('h2g_clamp_') and x.find('./child/object[@id="page_main"]') is not None for x in r.iter('object'))
print('PASS: hidden/lg:block visibility, 270/300px sidebar grid, fluid main, max-w clamp')
PYLAYOUT
