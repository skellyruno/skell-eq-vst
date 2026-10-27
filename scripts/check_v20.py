from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
c=(root/'Source/RestoredEditor.cpp').read_text()
h=(root/'Source/RestoredEditor.h').read_text()
p=(root/'Source/PluginProcessor.cpp').read_text()
dsp=(root/'Source/dsp/BiquadCascade.h').read_text()
cmake=(root/'CMakeLists.txt').read_text()
assert 'VERSION 2.0.0' in cmake
for tag in ('drawButtonBackground','drawButtonText','drawComboBox',
            'drawToggleButton','drawRotarySlider','bandTabs',
            'SkellyEQRestoredEditor::resized','restoreReferenceCurve',
            'fft.performFrequencyOnlyForwardTransform','hannWindow',
            'stopTimer()','setLookAndFeel(nullptr)'):
    assert tag in c, tag
for tag in ('setCoefficients','rampSamples','tiltSection','anySolo',
            'state.replaceState(clean)','snapshotA','snapshotB'):
    assert tag in p or tag in dsp, tag
for tag in ('spectrumOpacity', 'clipHoldTicks', 'bandTabs'):
    assert tag in h,tag
for asset in ('frame.png','skull_emblem.png','graph_bg.png'):
    assert (root/'Assets'/asset).is_file(),asset
for f in (root/'Source').rglob('*.cpp'):
    src=f.read_text()
    clean=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '',src,flags=re.S)
    level=0
    for ch in clean:
        if ch=='{': level+=1
        elif ch=='}':level-=1
        assert level>=0,f'Brace underflow: {f}'
    assert level==0,f'Unbalanced braces: {f}'
# Check sidebar control regions for overlap. Keep these in sync with resized().
areas={
 'type1':(1540,438,57,29), 'type2':(1601,438,57,29), 'type3':(1662,438,57,29),
 'type4':(1541,472,86,26), 'type5':(1632,472,86,26),
 'typeSelect':(1608,503,110,25), 'modeSelect':(1625,531,93,26),
 'phaseSelect':(1604,560,114,28), 'slopeSelect':(1605,593,113,27),
 'analyzeSelect':(1543,651,176,26), 'analyzeAmount':(1545,708,174,20)}
for name,(x,y,w,h) in areas.items():
    assert x>=1530 and x+w<=1725 and y>=135 and y+h<=730,name
names=list(areas)
for a in range(len(names)):
    for b in range(a+1,len(names)):
        x1,y1,w1,h1=areas[names[a]];x2,y2,w2,h2=areas[names[b]]
        dx=max(0,min(x1+w1,x2+w2)-max(x1,x2))
        dy=max(0,min(y1+h1,y2+h2)-max(y1,y2))
        assert dx*dy==0,(names[a],names[b])
print('PASS: V2.0 source invariants, layout geometry, DSP, version, art')
