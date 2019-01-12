#ifdef BGFX_SHADER
#include "varying_quad.def.sc"
#include "urho3d_compatibility.sh"
#ifdef COMPILEVS
    $input a_position
    $output vScreenPos
#endif
#ifdef COMPILEPS
    $input vScreenPos
#endif

#include "common.sh"

#include "uniforms.sh"
#include "samplers.sh"
#include "transform.sh"
#include "screen_pos.sh"
#include "lighting.sh"

#else

#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"
#include "Lighting.glsl"

varying vec2 vScreenPos;

#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}

void PS()
{
    vec3 rgb = texture2D(sDiffMap, vScreenPos).rgb;
    float intensity = GetIntensity(rgb);
    gl_FragColor = vec4(intensity, intensity, intensity, 1.0);
}
