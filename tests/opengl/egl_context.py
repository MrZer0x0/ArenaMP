"""Small surfaceless desktop GL harness. Mesa software GL is sufficient."""
import ctypes as C
import ctypes.util
import os
os.environ.setdefault('EGL_PLATFORM','surfaceless')
os.environ.setdefault('LIBGL_ALWAYS_SOFTWARE','1')
E=C.CDLL(ctypes.util.find_library('EGL'))
G=C.CDLL(ctypes.util.find_library('GL'))
def api(lib,name,restype,args):
 f=getattr(lib,name);f.restype=restype;f.argtypes=args;return f
P=C.c_void_p;I=C.c_int;U=C.c_uint;F=C.c_float
get=api(E,'eglGetDisplay',P,[P]);d=get(None)
assert api(E,'eglInitialize',U,[P,C.POINTER(I),C.POINTER(I)])(d,None,None)
assert api(E,'eglBindAPI',U,[U])(0x30A2)
attrs=(I*11)(0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3038)
config=P();n=I()
assert api(E,'eglChooseConfig',U,[P,C.POINTER(I),C.POINTER(P),I,C.POINTER(I)])(d,attrs,C.byref(config),1,C.byref(n)) and n.value
surface=api(E,'eglCreatePbufferSurface',P,[P,P,C.POINTER(I)])(d,config,(I*5)(0x3057,64,0x3056,64,0x3038))
context=api(E,'eglCreateContext',P,[P,P,P,C.POINTER(I)])(d,config,None,(I*1)(0x3038))
assert context and surface
assert api(E,'eglMakeCurrent',U,[P,P,P,P])(d,surface,surface,context)
gl_string=api(G,'glGetString',C.c_char_p,[U]);print('GL:',gl_string(0x1F02).decode(),gl_string(0x1F01).decode())
def shader(source,kind):
 h=api(G,'glCreateShader',U,[U])(kind)
 b=source.encode();s=C.c_char_p(b)
 api(G,'glShaderSource',None,[U,I,C.POINTER(C.c_char_p),C.POINTER(I)])(h,1,C.byref(s),None)
 api(G,'glCompileShader',None,[U])(h)
 ok=I();api(G,'glGetShaderiv',None,[U,U,C.POINTER(I)])(h,0x8B81,C.byref(ok))
 log=C.create_string_buffer(16000);api(G,'glGetShaderInfoLog',None,[U,I,C.POINTER(I),C.c_char_p])(h,len(log),None,log)
 if not ok.value:raise AssertionError(log.value.decode())
 return h
def program(vertex,fragment,feedback=()):
 h=api(G,'glCreateProgram',U,[])()
 for source,kind in ((vertex,0x8B31),(fragment,0x8B30)):
  sh=shader(source,kind);api(G,'glAttachShader',None,[U,U])(h,sh)
 for name,index in [('aOffset',6),('aRotation',7)]:
  api(G,'glBindAttribLocation',None,[U,U,C.c_char_p])(h,index,name.encode())
 if feedback:
  varyings=(C.c_char_p*len(feedback))(*(x.encode() for x in feedback))
  api(G,'glTransformFeedbackVaryings',None,[U,I,C.POINTER(C.c_char_p),U])(h,len(feedback),varyings,0x8C8C)
 api(G,'glLinkProgram',None,[U])(h)
 ok=I();api(G,'glGetProgramiv',None,[U,U,C.POINTER(I)])(h,0x8B82,C.byref(ok))
 log=C.create_string_buffer(16000);api(G,'glGetProgramInfoLog',None,[U,I,C.POINTER(I),C.c_char_p])(h,len(log),None,log)
 if not ok.value:raise AssertionError(log.value.decode())
 return h
if __name__=='__main__':program('#version 120\nvoid main(){gl_Position=gl_Vertex;}','#version 120\nvoid main(){gl_FragColor=vec4(1.0);}')
