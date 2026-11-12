# XTest input for a window in gamescope's Xwayland (both.sh): XT_DISPLAY=:N [XT_OY=y offset] xt.py where|click X Y|type T|hold KEY MS|mouse BTN MS
import sys,time,os
from Xlib import X,display,XK
from Xlib.ext import xtest
d=display.Display(os.environ['XT_DISPLAY'])
oy=int(os.environ.get('XT_OY','0'))
def mv(x,y): xtest.fake_input(d,X.MotionNotify,x=x,y=y+oy); d.sync()
c=sys.argv[1]
if c=='click':
    x,y=int(sys.argv[2]),int(sys.argv[3]); mv(x-2,y-2); time.sleep(.25); mv(x,y); time.sleep(.3)
    xtest.fake_input(d,X.ButtonPress,1); d.sync(); time.sleep(.12); xtest.fake_input(d,X.ButtonRelease,1); d.sync()
elif c=='mouse':
    b=int(sys.argv[2]); xtest.fake_input(d,X.ButtonPress,b); d.sync(); time.sleep(float(sys.argv[3])/1000); xtest.fake_input(d,X.ButtonRelease,b); d.sync()
elif c=='hold':
    k=d.keysym_to_keycode(XK.string_to_keysym(sys.argv[2])); xtest.fake_input(d,X.KeyPress,k); d.sync(); time.sleep(float(sys.argv[3])/1000); xtest.fake_input(d,X.KeyRelease,k); d.sync()
elif c=='type':
    m={'.':'period'}
    for ch in sys.argv[2]:
        k=d.keysym_to_keycode(XK.string_to_keysym(m.get(ch,ch))); xtest.fake_input(d,X.KeyPress,k); d.sync(); time.sleep(.05); xtest.fake_input(d,X.KeyRelease,k); d.sync(); time.sleep(.08)
elif c=='where':
    print(d.screen().root.query_pointer()._data['root_x'],d.screen().root.query_pointer()._data['root_y'],d.screen().width_in_pixels,d.screen().height_in_pixels)
