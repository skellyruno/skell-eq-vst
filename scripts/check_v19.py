from pathlib import Path
import re, zipfile
root=Path(__file__).resolve().parents[1]
c=(root/'Source/RestoredEditor.cpp').read_text()
h=(root/'Source/RestoredEditor.h').read_text()
p=(root/'Source/PluginProcessor.cpp').read_text()
assert 'VERSION 1.9.0' in (root/'CMakeLists.txt').read_text()
for tag in ('drawButtonBackground', 'drawButtonText', 'drawComboBox', 'getComboBoxFont',
            'drawToggleButton', 'drawLinearSlider', 'drawRotarySlider', 'bandTabs',
            'SkellyEQRestoredEditor::resized', 'restoreReferenceCurve',
            'fft.performFrequencyOnlyForwardTransform', 'stopTimer()',
            'setLookAndFeel(nullptr)', 'convertFrom0to1(p->getDefaultValue())'):
    assert tag in c, tag
for tag in ('drawButtonText', 'drawComboBox', 'getComboBoxFont', 'bandTabs'):
    assert tag in h, tag
for f in ('frame.png','skull_emblem.png','graph_bg.png'):
    assert (root/'Assets'/f).is_file(), f
assert 'put(phaseSelect, 1604, 560' in c
assert 'put(slopeSelect, 1605, 593' in c
assert 'put(graph, 85, 169' in c
assert 'state.replaceState(clean)' in p
for f in (root/'Source').rglob('*.cpp'):
    src=f.read_text()
    # Check balanced braces, stripping comments and quoted literals.
    clean=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '',src,flags=re.S)
    level=0
    for ch in clean:
        if ch=='{':level+=1
        elif ch=='}':level-=1
        assert level>=0, f'Brace underflow {f}'
    assert level==0, f'Unbalanced brace {f}'
# UI rectangles selected from resized(): no obvious panel control overlap.
areas={
    'type1':(1540,438,57,29), 'type2':(1601,438,57,29),'type3':(1662,438,57,29),
    'type4':(1541,472,86,26),'type5':(1632,472,86,26),
    'typeSelect':(1608,503,110,25),'modeSelect':(1625,531,93,26),
    'phaseSelect':(1604,560,114,28),'slopeSelect':(1605,593,113,27),
    'analyzeSelect':(1543,651,176,26),'analyzeAmount':(1545,708,174,20)}
for name,(x,y,w,h) in areas.items():
    assert x>=1530 and x+w<=1725 and y>=135 and y+h<=730, name
names=list(areas)
for a in range(len(names)):
    for b in range(a+1,len(names)):
        x1,y1,w1,h1=areas[names[a]];x2,y2,w2,h2=areas[names[b]]
        xOverlap=max(0, min(x1+w1,x2+w2)-max(x1,x2))
        yOverlap=max(0, min(y1+h1,y2+h2)-max(y1,y2))
        assert xOverlap*yOverlap==0, (names[a], names[b])
print('PASS: v1.9 GUI declarations, source invariants, layout rectangles, lifecycle, CMake, assets')
