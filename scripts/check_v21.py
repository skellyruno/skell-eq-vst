"""Static contract for the 1070x529 Skelly EQ compact GUI (no JUCE SDK needed)."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
ui=(root/'Source/RestoredEditor.cpp').read_text()
cmake=(root/'CMakeLists.txt').read_text()
processor=(root/'Source/PluginProcessor.cpp').read_text()
dsp=(root/'Source/dsp/BiquadCascade.h').read_text()
assert 'VERSION 2.1.0' in cmake
assert 'constexpr float baseW = 1070.0f;' in ui
assert 'constexpr float baseH = 529.0f;' in ui
assert 'setSize(1070, 529)' in ui
assert 'setResizeLimits(' in ui
assert 'setFixedAspectRatio' in ui
assert 'stopTimer();' in ui and 'graph.onSelection = nullptr;' in ui
assert 'setLookAndFeel(nullptr)' in ui
assert 'fft.performFrequencyOnlyForwardTransform' in ui
assert 'formatFrequency(frequency);' in ui
assert 'formatDecibels(gain);' in ui
assert 'formatQ(q);' in ui
assert 'setSize(1338, 661)' not in ui
assert 'put(graph, 50, 120, 836, 274)' in ui
assert 'drawMeters(g);' in ui
assert 'rampSamples' in dsp and 'tiltSection' in dsp and 'anySolo' in processor
for asset in ('frame.png', 'skull_emblem.png', 'graph_bg.png', 'knob_cap.png'):
    assert (root/'Assets'/asset).stat().st_size > 1024,asset
for path in (root/'Source').rglob('*.cpp'):
    s=path.read_text()
    clean=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"','',s,flags=re.S)
    level=0
    for ch in clean:
        if ch=='{':level+=1
        elif ch=='}':level-=1
        assert level>=0,f'Brace underflow: {path}'
    assert level==0,f'Brace imbalance: {path}'
# Validate actual setBounds statements in resized (other than the tab loop).
layout=ui.split('void SkellyEQRestoredEditor::resized()')[1].split('void SkellyEQRestoredEditor::timerCallback()')[0]
rectangles={}
for n,x,y,w,h in re.findall(r'put\((\w+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\)',layout):
    rectangles[n]=tuple(map(int,(x,y,w,h)))
for k,(x,y,w,h) in rectangles.items():
    assert x>=0 and y>=0 and x+w<=1070 and y+h<=529,(k,x,y,w,h)
    assert w>=19 and h>=19,(k,x,y,w,h)
# Distinct graph and sidebar; bounded within their respective frames.
for name in ('typeSelect','channelSelect','phaseSelect','slopeSelect','typeButtons'):
    if name in rectangles:
        x,y,w,h=rectangles[name]
        assert x>=910 and x+w<=1052,(name,rectangles[name])

def overlap(a,b):
    x,y,w,h=a; X,Y,W,H=b
    return max(0,min(x+w,X+W)-max(x,X))*max(0,min(y+h,Y+H)-max(y,Y))
for group in [
    ['about','options','saveA','loadA','saveB','loadB'],
    ['enabled','solo','resetCurve'],
    ['typeSelect','channelSelect','phaseSelect','slopeSelect'],
    ['modeSelect','analyzeSelect','analyzeAmount'],
    ['bandSelect','previous','next'],
    ['frequency','gain','q','output'],
]:
    for i,a in enumerate(group):
        for b in group[i+1:]:
            assert overlap(rectangles[a],rectangles[b])==0,(a,b)
assert 'setResizable(true, true)' in ui
assert 'SKELLY_RESTORED_EDITOR' in cmake and 'SKELLY_SAFE_EDITOR' in cmake
print('PASS: V2.1 compact geometry, control separation, assets, safety, DSP invariants')
