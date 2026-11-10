"""Compile/link real shader variants on surfaceless Mesa (not an engine run)."""
from pathlib import Path
import re
from egl_context import *
ROOT=Path(__file__).resolve().parents[2]/'files/shaders'
def include(name):
 s=(ROOT/name).read_text()
 return re.sub(r'#include\s+"([^"]+)"',lambda m:include(m[1]),s)
def prepare(name,defines):
 s=include(name)
 def loop(m):
  values=str(defines.get(m[2],'')).split(',')
  return ''.join(m[3].replace('@'+m[1],v) for v in values if v)
 s=re.sub(r'@foreach\s+(\w+)\s+@(\w+)\s*\n(.*?)@endforeach',loop,s,flags=re.S)
 return re.sub(r'@(\w+)',lambda m:str(defines.get(m[1],0)),s)
base=dict(maxLights=8,maxLightsInScene=64,lightingMethodFFP=1,getLight='gl_LightSource',
 startLight=1,endLight=8,alphaFunc=519,clamp=1,materialQuality=1,diffuseMap=1,
 shadowNormalOffset='1.0',shadow_texture_unit_list='4,5',disableNormalOffsetShadows=1)
def check_variants():
 count=0
 for method in ('ffp','uniform','ubo'):
  for instanced in (0,1):
   for normal in (0,1):
    for shadows in (0,1):
     d=dict(base,staticInstancing=instanced,normalMap=normal,normalMapUV=1,shadows_enabled=shadows)
     if method!='ffp':
      d.update(lightingMethodFFP=0,getLight='LightBuffer',endLight='PointLightCount')
      d['lightingMethodPerObjectUniform' if method=='uniform' else 'lightingMethodUBO' if method=='ubo' else 'lightingMethodClustered']=1
     if method=='ubo':d.update(useUBO=1,useGPUShader4=1,startLight=0)
     if method=='clustered':d.update(useGPUShader4=1,forcePPL=1)
     v=prepare('objects_vertex.glsl',d);f=prepare('objects_fragment.glsl',d)
     try: program(v,f)
     except Exception:
      Path('/tmp/arena_failed.vert').write_text(v);Path('/tmp/arena_failed.frag').write_text(f)
      print('FAILED variant',method,instanced,normal,shadows);raise
     count+=1
 for alpha in (512,513,514,515,516,517,518,519):
  program(prepare('shadowcasting_vertex.glsl',base),prepare('shadowcasting_fragment.glsl',dict(base,alphaFunc=alpha)))
  count+=1
 print('Compiled/linked shader variants:',count)
if __name__=='__main__':check_variants()
