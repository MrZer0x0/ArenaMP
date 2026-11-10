#version 120

varying vec2 diffuseMapUV;

varying float alphaPassthrough;

uniform int colorMode;
uniform bool useDiffuseMapForShadowAlpha = true;
uniform bool alphaTestShadows = true;

#include "static_instancing.glsl"
uniform bool arenaStaticInstancing;

void main(void)
{
    vec4 vertex = gl_Vertex;
    if (arenaStaticInstancing)
        vertex.xyz = arenaInstancePosition(vertex.xyz);
    gl_Position = gl_ModelViewProjectionMatrix * vertex;

    vec4 viewPos = (gl_ModelViewMatrix * vertex);
    gl_ClipVertex = viewPos;

    if (useDiffuseMapForShadowAlpha)
        diffuseMapUV = (gl_TextureMatrix[0] * gl_MultiTexCoord0).xy;
    else
        diffuseMapUV = vec2(0.0); // Avoid undefined behaviour if running on hardware predating the concept of dynamically uniform expressions
    if (colorMode == 2)
        alphaPassthrough = gl_Color.a;
    else
        // This is uniform, so if it's too low, we might be able to put the position/clip vertex outside the view frustum and skip the fragment shader and rasteriser
        alphaPassthrough = gl_FrontMaterial.diffuse.a;
}
