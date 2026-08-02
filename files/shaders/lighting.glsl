#include "lighting_pbr_compat.glsl"

#if @hdrLighting
#include "lighting_util.glsl"

uniform int arenaLocalShadowActive;
uniform vec3 arenaLocalShadowPosition0;
uniform vec3 arenaLocalShadowPosition1;
uniform float arenaLocalShadowStrength0;
uniform float arenaLocalShadowStrength1;

float arenaPointIllumination(int lightIndex, float lightDistance)
{
    float illumination = lcalcIllumination(lightIndex, lightDistance);
#if @advancedLocalLighting && !@lightingMethodFFP
    float radius = max(lcalcRadius(lightIndex), 1.0);
    float normalizedDistance = clamp(lightDistance / radius, 0.0, 1.5);
    float cutoff = max(0.0, 1.0 - pow(normalizedDistance, 4.0));
    cutoff *= cutoff;
    float physicalFalloff = cutoff / (1.0 + 4.0 * normalizedDistance * normalizedDistance);
    illumination = mix(illumination, physicalFalloff, clamp(@pointLightFalloffStrength, 0.0, 1.0));
#endif
    return illumination * max(@pointLightIntensity, 0.0);
}

float arenaPointShadowFactor(int lightIndex, float shadowing)
{
#if @advancedLocalLighting
    if (arenaLocalShadowActive != 0)
    {
        float tolerance = 24.0;
#if !@lightingMethodFFP
        tolerance = max(tolerance, lcalcRadius(lightIndex) * 0.08);
#endif
        vec3 lightPosition = lcalcPosition(lightIndex);
        if (distance(lightPosition, arenaLocalShadowPosition0) <= tolerance)
            return mix(1.0, arenaLocalShadowRatio(0), clamp(arenaLocalShadowStrength0, 0.0, 1.0));
#if @localShadowAtlasLights > 1
        if (arenaLocalShadowActive > 1
            && distance(lightPosition, arenaLocalShadowPosition1) <= tolerance)
            return mix(1.0, arenaLocalShadowRatio(1), clamp(arenaLocalShadowStrength1, 0.0, 1.0));
#endif
    }
#endif
    return 1.0;
}


// Bloom и glow параметры (можно настроить через uniforms или defines)
#ifndef BLOOM_STRENGTH
#define BLOOM_STRENGTH 0.15  // Снижено для интерьеров
#endif

#ifndef GLOW_INTENSITY
#define GLOW_INTENSITY 0.4  // Снижено для интерьеров
#endif

#ifndef GLOW_RADIUS
#define GLOW_RADIUS 1.8  // Уменьшен радиус
#endif

// Отдельные настройки для точечных источников
#ifndef POINT_BLOOM_MULTIPLIER
#define POINT_BLOOM_MULTIPLIER 0.3  // Дополнительное снижение bloom для факелов/свечей
#endif

#ifndef POINT_GLOW_MULTIPLIER
#define POINT_GLOW_MULTIPLIER 0.5  // Дополнительное снижение glow для факелов/свечей
#endif

// Переменная для накопления bloom эффекта
vec3 bloomAccumulator = vec3(0.0);

// Функция для вычисления bloom вклада от источника света
vec3 calculateBloom(vec3 lightColor, float lightIntensity, float distance, float radius, float multiplier)
{
    float bloomFactor = lightIntensity * smoothstep(radius * 2.0, 0.0, distance);
    bloomFactor = pow(bloomFactor, 2.5);  // Более резкое затухание
    return lightColor * bloomFactor * BLOOM_STRENGTH * multiplier;
}

// Функция для вычисления glow эффекта
vec3 calculateGlow(vec3 lightColor, float distance, float radius, float multiplier)
{
    float glowFactor = smoothstep(radius * GLOW_RADIUS, 0.0, distance);
    glowFactor = pow(glowFactor, 2.0);  // Более мягкое затухание
    return lightColor * glowFactor * GLOW_INTENSITY * 0.2 * multiplier;
}

void perLightSun(out vec3 diffuseOut, vec3 viewPos, vec3 viewNormal)
{
    vec3 lightDir = normalize(lcalcPosition(0));
    float lambert = dot(viewNormal.xyz, lightDir);

#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(normalize(viewPos), viewNormal.xyz);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif

    vec3 sunDiffuse = lcalcDiffuse(0).xyz * lambert * 0.7;
    diffuseOut = sunDiffuse;
    
    // Bloom для яркого солнечного света
    float sunBloom = max(lambert - 0.8, 0.0) * 5.0;
    bloomAccumulator += sunDiffuse * sunBloom * BLOOM_STRENGTH * 0.5;
}

void perLightPoint(out vec3 ambientOut, out vec3 diffuseOut, int lightIndex, vec3 viewPos, vec3 viewNormal, float shadowing)
{
    vec3 lightPos = lcalcPosition(lightIndex) - viewPos;
    float lightDistance = length(lightPos);

// cull non-FFP point lighting by radius, light is guaranteed to not fall outside this bound with our cutoff
#if !@lightingMethodFFP
    float radius = lcalcRadius(lightIndex);

    // Расширяем радиус проверки для glow эффекта
    if (lightDistance > radius * GLOW_RADIUS * 2.0)
    {
        ambientOut = vec3(0.0);
        diffuseOut = vec3(0.0);
        return;
    }
#endif

    lightPos = normalize(lightPos);

    float illumination = arenaPointIllumination(lightIndex, lightDistance);
    vec3 lightDiffuse = lcalcDiffuse(lightIndex);
    
    ambientOut = lcalcAmbient(lightIndex) * illumination;
    float lambert = dot(viewNormal.xyz, lightPos) * illumination
        * arenaPointShadowFactor(lightIndex, shadowing);

#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(normalize(viewPos), viewNormal.xyz);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif

    // Основное освещение
    diffuseOut = lightDiffuse * lambert * 0.6;
    
#if !@lightingMethodFFP
    // Добавляем glow эффект (мягкое свечение вокруг источника) - снижено для интерьеров
    vec3 glowContribution = calculateGlow(lightDiffuse, lightDistance, radius, POINT_GLOW_MULTIPLIER);
    diffuseOut += glowContribution;
    
    // Bloom от точечных источников света - снижено для интерьеров
    vec3 bloomContribution = calculateBloom(lightDiffuse, illumination * lambert, lightDistance, radius, POINT_BLOOM_MULTIPLIER);
    bloomAccumulator += bloomContribution;
#endif
}

#if PER_PIXEL_LIGHTING
void doLighting(vec3 viewPos, vec3 viewNormal, float shadowing, out vec3 diffuseLight, out vec3 ambientLight)
#else
void doLighting(vec3 viewPos, vec3 viewNormal, out vec3 diffuseLight, out vec3 ambientLight, out vec3 shadowDiffuse)
#endif
{
    vec3 ambientOut, diffuseOut;
    
    // Сброс аккумулятора bloom
    bloomAccumulator = vec3(0.0);

    perLightSun(diffuseOut, viewPos, viewNormal);
    ambientLight = gl_LightModel.ambient.xyz;
#if PER_PIXEL_LIGHTING
    // Ambient lift для осветления теней - делаем тени менее глубокими
    float ambientLift = 0.15;
    float sunShadowing = arenaLocalShadowActive != 0 ? 1.0 : shadowing;
    diffuseLight = diffuseOut * max(sunShadowing, ambientLift);
#else
    shadowDiffuse = diffuseOut;
    diffuseLight = vec3(0.0);
#endif

    for (int i = @startLight; i < @endLight; ++i)
    {
#if @lightingMethodUBO
        #if PER_PIXEL_LIGHTING
        perLightPoint(ambientOut, diffuseOut, PointLightIndex[i], viewPos, viewNormal, shadowing);
#else
        perLightPoint(ambientOut, diffuseOut, PointLightIndex[i], viewPos, viewNormal, 1.0);
#endif
#else
        #if PER_PIXEL_LIGHTING
        perLightPoint(ambientOut, diffuseOut, i, viewPos, viewNormal, shadowing);
#else
        perLightPoint(ambientOut, diffuseOut, i, viewPos, viewNormal, 1.0);
#endif
#endif
        ambientLight += ambientOut;
        diffuseLight += diffuseOut;
    }
    
    // Добавляем накопленный bloom к финальному освещению
    diffuseLight += bloomAccumulator;
}

vec3 getSpecular(vec3 viewNormal, vec3 viewPos, float shininess, vec3 matSpec, float shadowing)
{
    vec3 normal = normalize(viewNormal);
    vec3 viewDir = normalize(-viewPos);
    vec3 sunDir = normalize(lcalcPosition(0));
    vec3 result = vec3(0.0);

#if @materialQuality >= 4
    float sunShadowing = arenaLocalShadowActive != 0 ? 1.0 : shadowing;
    result += pbrSunSpecular(normal, viewDir, sunDir, shininess,
        lcalcSpecular(0).xyz * matSpec) * sunShadowing;
#else
    float sunNdotL = max(dot(normal, sunDir), 0.0);
    if (sunNdotL > 0.0)
    {
        vec3 sunHalf = normalize(sunDir + viewDir);
        float sunShadowing = arenaLocalShadowActive != 0 ? 1.0 : shadowing;
        result += pow(max(dot(normal, sunHalf), 0.0), max(1e-4, shininess))
            * lcalcSpecular(0).xyz * matSpec * 0.35 * sunShadowing;
    }
#endif

#if @advancedLocalLighting
    for (int i = @startLight; i < @endLight; ++i)
    {
#if @lightingMethodUBO
        int pointIndex = PointLightIndex[i];
#else
        int pointIndex = i;
#endif
        vec3 toLight = lcalcPosition(pointIndex) - viewPos;
        float pointDistance = length(toLight);
#if !@lightingMethodFFP
        if (pointDistance > lcalcRadius(pointIndex) * 1.5)
            continue;
#endif
        vec3 pointDir = toLight / max(pointDistance, 0.0001);
        float NdotL = max(dot(normal, pointDir), 0.0);
        if (NdotL <= 0.0)
            continue;

        float illumination = arenaPointIllumination(pointIndex, pointDistance);
        float pointShadowing = arenaPointShadowFactor(pointIndex, shadowing);
        vec3 pointColour = max(lcalcDiffuse(pointIndex).xyz, vec3(0.0));
#if @materialQuality >= 4
        result += pbrSunSpecular(normal, viewDir, pointDir, shininess,
            pointColour * matSpec) * illumination * pointShadowing * @pointLightSpecularStrength;
#else
        vec3 halfVector = normalize(pointDir + viewDir);
        float highlight = pow(max(dot(normal, halfVector), 0.0), max(1e-4, shininess));
        result += highlight * NdotL * pointColour * matSpec * illumination
            * pointShadowing * @pointLightSpecularStrength;
#endif
    }
#endif

    return result;
}

#else
#include "lighting_util.glsl"

uniform int arenaLocalShadowActive;
uniform vec3 arenaLocalShadowPosition0;
uniform vec3 arenaLocalShadowPosition1;
uniform float arenaLocalShadowStrength0;
uniform float arenaLocalShadowStrength1;

float arenaPointIllumination(int lightIndex, float lightDistance)
{
    float illumination = lcalcIllumination(lightIndex, lightDistance);
#if @advancedLocalLighting && !@lightingMethodFFP
    float radius = max(lcalcRadius(lightIndex), 1.0);
    float normalizedDistance = clamp(lightDistance / radius, 0.0, 1.5);
    float cutoff = max(0.0, 1.0 - pow(normalizedDistance, 4.0));
    cutoff *= cutoff;
    float physicalFalloff = cutoff / (1.0 + 4.0 * normalizedDistance * normalizedDistance);
    illumination = mix(illumination, physicalFalloff, clamp(@pointLightFalloffStrength, 0.0, 1.0));
#endif
    return illumination * max(@pointLightIntensity, 0.0);
}

float arenaPointShadowFactor(int lightIndex, float shadowing)
{
#if @advancedLocalLighting
    if (arenaLocalShadowActive != 0)
    {
        float tolerance = 24.0;
#if !@lightingMethodFFP
        tolerance = max(tolerance, lcalcRadius(lightIndex) * 0.08);
#endif
        vec3 lightPosition = lcalcPosition(lightIndex);
        if (distance(lightPosition, arenaLocalShadowPosition0) <= tolerance)
            return mix(1.0, arenaLocalShadowRatio(0), clamp(arenaLocalShadowStrength0, 0.0, 1.0));
#if @localShadowAtlasLights > 1
        if (arenaLocalShadowActive > 1
            && distance(lightPosition, arenaLocalShadowPosition1) <= tolerance)
            return mix(1.0, arenaLocalShadowRatio(1), clamp(arenaLocalShadowStrength1, 0.0, 1.0));
#endif
    }
#endif
    return 1.0;
}


void perLightSun(out vec3 diffuseOut, vec3 viewPos, vec3 viewNormal)
{
    vec3 lightDir = normalize(lcalcPosition(0));
    float lambert = dot(viewNormal.xyz, lightDir);

#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(normalize(viewPos), viewNormal.xyz);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif

    diffuseOut = lcalcDiffuse(0).xyz * lambert;
}

void perLightPoint(out vec3 ambientOut, out vec3 diffuseOut, int lightIndex, vec3 viewPos, vec3 viewNormal, float shadowing)
{
    vec3 lightPos = lcalcPosition(lightIndex) - viewPos;
    float lightDistance = length(lightPos);

// cull non-FFP point lighting by radius, light is guaranteed to not fall outside this bound with our cutoff
#if !@lightingMethodFFP
    float radius = lcalcRadius(lightIndex);

    if (lightDistance > radius * 2.0)
    {
        ambientOut = vec3(0.0);
        diffuseOut = vec3(0.0);
        return;
    }
#endif

    lightPos = normalize(lightPos);

    float illumination = arenaPointIllumination(lightIndex, lightDistance);
    ambientOut = lcalcAmbient(lightIndex) * illumination;
    float lambert = dot(viewNormal.xyz, lightPos) * illumination
        * arenaPointShadowFactor(lightIndex, shadowing);

#ifndef GROUNDCOVER
    lambert = max(lambert, 0.0);
#else
    float eyeCosine = dot(normalize(viewPos), viewNormal.xyz);
    if (lambert < 0.0)
    {
        lambert = -lambert;
        eyeCosine = -eyeCosine;
    }
    lambert *= clamp(-8.0 * (1.0 - 0.3) * eyeCosine + 1.0, 0.3, 1.0);
#endif

    diffuseOut = lcalcDiffuse(lightIndex) * lambert;
}

#if PER_PIXEL_LIGHTING
void doLighting(vec3 viewPos, vec3 viewNormal, float shadowing, out vec3 diffuseLight, out vec3 ambientLight)
#else
void doLighting(vec3 viewPos, vec3 viewNormal, out vec3 diffuseLight, out vec3 ambientLight, out vec3 shadowDiffuse)
#endif
{
    vec3 ambientOut, diffuseOut;

    perLightSun(diffuseOut, viewPos, viewNormal);
    ambientLight = gl_LightModel.ambient.xyz;
#if PER_PIXEL_LIGHTING
    float sunShadowing = arenaLocalShadowActive != 0 ? 1.0 : shadowing;
    diffuseLight = diffuseOut * sunShadowing;
#else
    shadowDiffuse = diffuseOut;
    diffuseLight = vec3(0.0);
#endif

    for (int i = @startLight; i < @endLight; ++i)
    {
#if @lightingMethodUBO
        #if PER_PIXEL_LIGHTING
        perLightPoint(ambientOut, diffuseOut, PointLightIndex[i], viewPos, viewNormal, shadowing);
#else
        perLightPoint(ambientOut, diffuseOut, PointLightIndex[i], viewPos, viewNormal, 1.0);
#endif
#else
        #if PER_PIXEL_LIGHTING
        perLightPoint(ambientOut, diffuseOut, i, viewPos, viewNormal, shadowing);
#else
        perLightPoint(ambientOut, diffuseOut, i, viewPos, viewNormal, 1.0);
#endif
#endif
        ambientLight += ambientOut;
        diffuseLight += diffuseOut;
    }
}

vec3 getSpecular(vec3 viewNormal, vec3 viewPos, float shininess, vec3 matSpec, float shadowing)
{
    vec3 normal = normalize(viewNormal);
    vec3 viewDir = normalize(-viewPos);
    vec3 sunDir = normalize(lcalcPosition(0));
    vec3 result = vec3(0.0);

#if @materialQuality >= 4
    float sunShadowing = arenaLocalShadowActive != 0 ? 1.0 : shadowing;
    result += pbrSunSpecular(normal, viewDir, sunDir, shininess,
        lcalcSpecular(0).xyz * matSpec) * sunShadowing;
#else
    float sunNdotL = max(dot(normal, sunDir), 0.0);
    if (sunNdotL > 0.0)
    {
        vec3 sunHalf = normalize(sunDir + viewDir);
        float sunShadowing = arenaLocalShadowActive != 0 ? 1.0 : shadowing;
        result += pow(max(dot(normal, sunHalf), 0.0), max(1e-4, shininess))
            * lcalcSpecular(0).xyz * matSpec * 0.35 * sunShadowing;
    }
#endif

#if @advancedLocalLighting
    for (int i = @startLight; i < @endLight; ++i)
    {
#if @lightingMethodUBO
        int pointIndex = PointLightIndex[i];
#else
        int pointIndex = i;
#endif
        vec3 toLight = lcalcPosition(pointIndex) - viewPos;
        float pointDistance = length(toLight);
#if !@lightingMethodFFP
        if (pointDistance > lcalcRadius(pointIndex) * 1.5)
            continue;
#endif
        vec3 pointDir = toLight / max(pointDistance, 0.0001);
        float NdotL = max(dot(normal, pointDir), 0.0);
        if (NdotL <= 0.0)
            continue;

        float illumination = arenaPointIllumination(pointIndex, pointDistance);
        float pointShadowing = arenaPointShadowFactor(pointIndex, shadowing);
        vec3 pointColour = max(lcalcDiffuse(pointIndex).xyz, vec3(0.0));
#if @materialQuality >= 4
        result += pbrSunSpecular(normal, viewDir, pointDir, shininess,
            pointColour * matSpec) * illumination * pointShadowing * @pointLightSpecularStrength;
#else
        vec3 halfVector = normalize(pointDir + viewDir);
        float highlight = pow(max(dot(normal, halfVector), 0.0), max(1e-4, shininess));
        result += highlight * NdotL * pointColour * matSpec * illumination
            * pointShadowing * @pointLightSpecularStrength;
#endif
    }
#endif

    return result;
}

#endif
