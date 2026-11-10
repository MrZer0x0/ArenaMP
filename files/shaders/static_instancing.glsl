// Attributes 6/7 match the established groundcover binding, in a separate program.
// Positive uniform scale only; quaternion comes from the same OSG rotation as paging.
attribute vec4 aOffset;
attribute vec4 aRotation;
vec3 arenaInstanceDirection(vec3 v)
{
    return v + 2.0 * cross(aRotation.xyz, cross(aRotation.xyz, v) + aRotation.w * v);
}
vec3 arenaInstancePosition(vec3 v)
{
    return arenaInstanceDirection(v * aOffset.w) + aOffset.xyz;
}
