#define SHADOWS @shadows_enabled

#if SHADOWS
    uniform float maximumShadowMapDistance;
    uniform float shadowFadeStart;
    uniform int activeShadowMapCount;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        uniform sampler2DShadow shadowTexture@shadow_texture_unit_index;
        varying vec4 shadowSpaceCoords@shadow_texture_unit_index;

#if @perspectiveShadowMaps
        varying vec4 shadowRegionCoords@shadow_texture_unit_index;
#endif
    @endforeach
#endif // SHADOWS

// PCF (Percentage Closer Filtering) для сглаживания теней
float sampleShadowPCF(sampler2DShadow shadowMap, vec4 shadowCoords, float texelSize)
{
    float shadow = 0.0;
    vec3 coords = shadowCoords.xyz / shadowCoords.w;
    
    // 3x3 PCF kernel для мягких теней
    for(float y = -1.0; y <= 1.0; y += 1.0)
    {
        for(float x = -1.0; x <= 1.0; x += 1.0)
        {
            vec2 offset = vec2(x, y) * texelSize;
            vec4 offsetCoords = vec4(coords.xy + offset, coords.z, 1.0);
            shadow += shadow2D(shadowMap, offsetCoords.xyz).r;
        }
    }
    return shadow / 9.0;
}

float arenaSampleLocalShadowMap(sampler2DShadow shadowMap, vec4 shadowCoords)
{
    if (shadowCoords.w <= 0.0)
        return 1.0;
    vec3 coords = shadowCoords.xyz / shadowCoords.w;
    if (any(lessThanEqual(coords, vec3(0.0))) || any(greaterThanEqual(coords, vec3(1.0))))
        return 1.0;

    // Receiver-gradient bias removes acne on walls viewed at a shallow angle while
    // keeping contact shadows tighter than a large global polygon offset would.
    float receiverGradient = max(abs(dFdx(coords.z)), abs(dFdy(coords.z)));
    float comparisonDepth = clamp(coords.z - @localShadowReceiverBias
        - receiverGradient * @localShadowSlopeBias, 0.0, 1.0);

    // A fixed-cost, rotated-looking 9-tap kernel. Its radius grows slightly with
    // receiver depth, approximating the softer penumbra of area lights without
    // introducing per-frame noise or temporal shimmer.
    float depthRadius = mix(0.70, 1.85, smoothstep(0.08, 0.95, coords.z));
    float radius = @shadowMapTexelSize * @localShadowSoftness * depthRadius;
    vec2 diagonal = vec2(radius * 0.70710678);
    float shadow = shadow2D(shadowMap, vec3(coords.xy, comparisonDepth)).r * 2.0;
    shadow += shadow2D(shadowMap, vec3(coords.xy + vec2(radius, 0.0), comparisonDepth)).r;
    shadow += shadow2D(shadowMap, vec3(coords.xy + vec2(-radius, 0.0), comparisonDepth)).r;
    shadow += shadow2D(shadowMap, vec3(coords.xy + vec2(0.0, radius), comparisonDepth)).r;
    shadow += shadow2D(shadowMap, vec3(coords.xy + vec2(0.0, -radius), comparisonDepth)).r;
    shadow += shadow2D(shadowMap, vec3(coords.xy + diagonal, comparisonDepth)).r;
    shadow += shadow2D(shadowMap, vec3(coords.xy - diagonal, comparisonDepth)).r;
    shadow += shadow2D(shadowMap, vec3(coords.xy + vec2(diagonal.x, -diagonal.y), comparisonDepth)).r;
    shadow += shadow2D(shadowMap, vec3(coords.xy + vec2(-diagonal.x, diagonal.y), comparisonDepth)).r;
    return shadow / 10.0;
}

float arenaLocalShadowRatio(int atlasSlot)
{
#if SHADOWS && @localShadowAtlasLights > 0
    if (atlasSlot == 0)
        return min(arenaSampleLocalShadowMap(shadowTexture0, shadowSpaceCoords0),
            arenaSampleLocalShadowMap(shadowTexture1, shadowSpaceCoords1));
#if @localShadowAtlasLights > 1
    if (atlasSlot == 1)
        return min(arenaSampleLocalShadowMap(shadowTexture2, shadowSpaceCoords2),
            arenaSampleLocalShadowMap(shadowTexture3, shadowSpaceCoords3));
#endif
#endif
    return 1.0;
}

float unshadowedLightRatio(float distance)
{
    float shadowing = 1.0;
#if SHADOWS
#if @limitShadowMapDistance
    float fade = clamp((distance - shadowFadeStart) / (maximumShadowMapDistance - shadowFadeStart), 0.0, 1.0);
    if (fade == 1.0)
        return shadowing;
#endif
    #if @shadowMapsOverlap
        bool doneShadows = false;
        @foreach shadow_texture_unit_index @shadow_texture_unit_list
            if (@shadow_texture_unit_index < activeShadowMapCount && !doneShadows)
            {
                vec3 shadowXYZ = shadowSpaceCoords@shadow_texture_unit_index.xyz / shadowSpaceCoords@shadow_texture_unit_index.w;
#if @perspectiveShadowMaps
                vec3 shadowRegionXYZ = shadowRegionCoords@shadow_texture_unit_index.xyz / shadowRegionCoords@shadow_texture_unit_index.w;
#endif
                // Reject projections behind a shadow hemisphere or outside its depth range.
                // The old XY-only test allowed the opposite projection to pass through the
                // camera plane, producing large triangles that moved with the view.
                if (shadowSpaceCoords@shadow_texture_unit_index.w > 0.0
                    && all(lessThan(shadowXYZ, vec3(1.0, 1.0, 1.0)))
                    && all(greaterThan(shadowXYZ, vec3(0.0, 0.0, 0.0))))
                {
                    // Используем PCF для сглаженных теней
                    float texelSize = @shadowMapTexelSize;
                    shadowing = min(sampleShadowPCF(shadowTexture@shadow_texture_unit_index, shadowSpaceCoords@shadow_texture_unit_index, texelSize), shadowing);

                    
                    doneShadows = all(lessThan(shadowXYZ, vec3(0.95, 0.95, 1.0))) && all(greaterThan(shadowXYZ, vec3(0.05, 0.05, 0.0)));
#if @perspectiveShadowMaps
                    doneShadows = doneShadows && all(lessThan(shadowRegionXYZ, vec3(1.0, 1.0, 1.0))) && all(greaterThan(shadowRegionXYZ.xy, vec2(-1.0, -1.0)));
#endif
                }
            }
        @endforeach
    #else
        @foreach shadow_texture_unit_index @shadow_texture_unit_list
            vec3 shadowXYZ = shadowSpaceCoords@shadow_texture_unit_index.xyz / shadowSpaceCoords@shadow_texture_unit_index.w;
            if (@shadow_texture_unit_index < activeShadowMapCount
                && shadowSpaceCoords@shadow_texture_unit_index.w > 0.0
                && all(lessThan(shadowXYZ, vec3(1.0, 1.0, 1.0)))
                && all(greaterThan(shadowXYZ, vec3(0.0, 0.0, 0.0))))
            {
                float texelSize = @shadowMapTexelSize;
                shadowing = min(sampleShadowPCF(shadowTexture@shadow_texture_unit_index,
                    shadowSpaceCoords@shadow_texture_unit_index, texelSize), shadowing);
            }
        @endforeach
    #endif
#if @limitShadowMapDistance
    shadowing = mix(shadowing, 1.0, fade);
#endif
#endif // SHADOWS
    return shadowing;
}

void applyShadowDebugOverlay()
{
#if SHADOWS && @useShadowDebugOverlay
    bool doneOverlay = false;
    float colourIndex = 0.0;
    @foreach shadow_texture_unit_index @shadow_texture_unit_list
        if (@shadow_texture_unit_index < activeShadowMapCount && !doneOverlay)
        {
            vec3 shadowXYZ = shadowSpaceCoords@shadow_texture_unit_index.xyz / shadowSpaceCoords@shadow_texture_unit_index.w;
#if @perspectiveShadowMaps
            vec3 shadowRegionXYZ = shadowRegionCoords@shadow_texture_unit_index.xyz / shadowRegionCoords@shadow_texture_unit_index.w;
#endif
            if (shadowSpaceCoords@shadow_texture_unit_index.w > 0.0
                && all(lessThan(shadowXYZ, vec3(1.0, 1.0, 1.0)))
                && all(greaterThan(shadowXYZ, vec3(0.0, 0.0, 0.0))))
            {
                colourIndex = mod(@shadow_texture_unit_index.0, 3.0);
		        if (colourIndex < 1.0)
			        gl_FragData[0].x += 0.1;
		        else if (colourIndex < 2.0)
			        gl_FragData[0].y += 0.1;
		        else
			        gl_FragData[0].z += 0.1;

                doneOverlay = all(lessThan(shadowXYZ, vec3(0.95, 0.95, 1.0))) && all(greaterThan(shadowXYZ, vec3(0.05, 0.05, 0.0)));
#if @perspectiveShadowMaps
                doneOverlay = doneOverlay && all(lessThan(shadowRegionXYZ.xyz, vec3(1.0, 1.0, 1.0))) && all(greaterThan(shadowRegionXYZ.xy, vec2(-1.0, -1.0)));
#endif
            }
        }
    @endforeach
#endif // SHADOWS
}