"""GPU transform-feedback regression using the actual object/shadow shaders."""
import math
from shaders import *
import numpy as np
A=api
verts=np.array([[.1,.2,.3],[.3,.2,.1],[-.1,.1,.4]],dtype=np.float32)
normals=np.tile([1.,0.,0.],(3,1)).astype(np.float32)
tangents=np.tile([0.,1.,0.,1.],(3,1)).astype(np.float32)
offsets=np.array([[.2,.3,-.1,2.],[-.4,.1,.5,.5]],dtype=np.float32)
axis=np.array([1.,2.,3.]);axis/=np.linalg.norm(axis)
q=np.r_[axis*math.sin(.65),math.cos(.65)]
rotations=np.array([[0.,0.,0.,1.],q],dtype=np.float32)
def buffer(target,data):
 name=U();A(G,'glGenBuffers',None,[I,C.POINTER(U)])(1,C.byref(name))
 A(G,'glBindBuffer',None,[U,U])(target,name)
 A(G,'glBufferData',None,[U,C.c_ssize_t,P,U])(target,data.nbytes,data.ctypes.data,0x88E4)
 return name.value
for mode in (0x1700,0x1701):
 A(G,'glMatrixMode',None,[U])(mode);A(G,'glLoadIdentity',None,[])()
buffer(0x8892,verts);A(G,'glVertexPointer',None,[I,U,I,P])(3,0x1406,0,None);A(G,'glEnableClientState',None,[U])(0x8074)
buffer(0x8892,normals);A(G,'glNormalPointer',None,[U,I,P])(0x1406,0,None);A(G,'glEnableClientState',None,[U])(0x8075)
A(G,'glClientActiveTexture',None,[U])(0x84C7);buffer(0x8892,tangents)
A(G,'glTexCoordPointer',None,[I,U,I,P])(4,0x1406,0,None);A(G,'glEnableClientState',None,[U])(0x8078)
A(G,'glClientActiveTexture',None,[U])(0x84C0)
for index,data in [(6,offsets),(7,rotations)]:
 buffer(0x8892,data);A(G,'glVertexAttribPointer',None,[U,I,U,U,I,P])(index,4,0x1406,0,0,None)
 A(G,'glEnableVertexAttribArray',None,[U])(index);A(G,'glVertexAttribDivisor',None,[U,U])(index,1)
def rotation(q):
 x,y,z,w=q
 return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
 [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
 [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
expected=[]
for off,quat in zip(offsets,rotations):
 rot=rotation(quat)
 for v in verts:expected.append(np.r_[rot@(v*off[3])+off[:3],1.,rot@normals[0],rot@tangents[0,:3],1.])
expected=np.array(expected)
def draw(vertex,fields,width,enabled=True):
 prog=program(vertex,'#version 120\nvoid main(){gl_FragColor=vec4(1.0);}',fields)
 A(G,'glUseProgram',None,[U])(prog)
 loc=A(G,'glGetUniformLocation',I,[U,C.c_char_p])(prog,b'arenaStaticInstancing')
 if loc>=0:A(G,'glUniform1i',None,[I,I])(loc,int(enabled))
 result=np.zeros((6,width),dtype=np.float32);buf=buffer(0x8C8E,result)
 A(G,'glBindBufferBase',None,[U,U,U])(0x8C8E,0,buf)
 A(G,'glEnable',None,[U])(0x8C89);A(G,'glBeginTransformFeedback',None,[U])(4)
 A(G,'glDrawArraysInstanced',None,[U,I,I,I])(4,0,3,2)
 A(G,'glEndTransformFeedback',None,[])();A(G,'glDisable',None,[U])(0x8C89)
 A(G,'glGetBufferSubData',None,[U,C.c_ssize_t,C.c_ssize_t,P])(0x8C8E,0,result.nbytes,result.ctypes.data)
 err=A(G,'glGetError',U,[])();assert err==0,hex(err)
 return result
v=prepare('objects_vertex.glsl',dict(base,staticInstancing=1,normalMap=1,normalMapUV=1))
out=draw(v,('gl_Position','passNormal','passTangent'),11)
assert np.allclose(out,expected,atol=2e-6),(out,expected)
v=prepare('shadowcasting_vertex.glsl',base)
assert np.allclose(draw(v,('gl_Position',),4),expected[:,:4],atol=2e-6)
plain=np.tile(np.c_[verts,np.ones(3)],(2,1))
assert np.allclose(draw(v,('gl_Position',),4,False),plain,atol=2e-6)
v=prepare('objects_vertex.glsl',dict(base,staticInstancing=0,normalMap=1,normalMapUV=1))
assert np.allclose(draw(v,('gl_Position',),4),plain,atol=2e-6)
print('Actual instanced draws: positions, scale, quaternion, normals, tangents, shadows and normal-path fallback passed')
