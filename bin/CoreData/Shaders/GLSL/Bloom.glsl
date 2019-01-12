#ifdef BGFX_SHADER
#include "varying_quad.def.sc"
#include "urho3d_compatibility.sh"
#ifdef COMPILEVS
    $input a_position
    $output vTexCoord, vScreenPos
#endif
#ifdef COMPILEPS
    $input vTexCoord, vScreenPos
#endif

#include "common.sh"

#include "uniforms.sh"
#include "samplers.sh"
#include "transform.sh"
#include "screen_pos.sh"

#else

#include "Uniforms.glsl"
#include "Samplers.glsl"
#include "Transform.glsl"
#include "ScreenPos.glsl"

varying vec2 vTexCoord;
varying vec2 vScreenPos;

#endif // BGFX_SHADER

#ifdef COMPILEPS
#ifdef BGFX_SHADER
uniform vec4 u_BloomThreshold;
uniform vec4 u_BloomMix;
uniform vec4 u_BlurHInvSize;

#define cBloomThreshold u_BloomThreshold.x
#define cBloomMix vec2(u_BloomMix.xy)
#define cBlurHInvSize vec2(u_BlurHInvSize.xy)
#else
uniform float cBloomThreshold;
uniform vec2 cBloomMix;
uniform vec2 cBlurHInvSize;
#endif
#endif

void VS()
{
    mat4 modelMatrix = iModelMatrix;
    vec3 worldPos = GetWorldPos(modelMatrix);
    gl_Position = GetClipPos(worldPos);
    vTexCoord = GetQuadTexCoord(gl_Position);
    vScreenPos = GetScreenPosPreDiv(gl_Position);
}

void PS()
{
    #ifdef BRIGHT
    vec3 rgb = texture2D(sDiffMap, vScreenPos).rgb;
    gl_FragColor = vec4((rgb - vec3(cBloomThreshold, cBloomThreshold, cBloomThreshold)) / (1.0 - cBloomThreshold), 1.0);
    #endif

    #ifdef BLURH
    vec3 rgb = texture2D(sDiffMap, vTexCoord + vec2(-2.0, 0.0) * cBlurHInvSize).rgb * 0.1;
    rgb += texture2D(sDiffMap, vTexCoord + vec2(-1.0, 0.0) * cBlurHInvSize).rgb * 0.25;
    rgb += texture2D(sDiffMap, vTexCoord + vec2(0.0, 0.0) * cBlurHInvSize).rgb * 0.3;
    rgb += texture2D(sDiffMap, vTexCoord + vec2(1.0, 0.0) * cBlurHInvSize).rgb * 0.25;
    rgb += texture2D(sDiffMap, vTexCoord + vec2(2.0, 0.0) * cBlurHInvSize).rgb * 0.1;
    gl_FragColor = vec4(rgb, 1.0);
    #endif

    #ifdef BLURV
    vec3 rgb = texture2D(sDiffMap, vTexCoord + vec2(0.0, -2.0) * cBlurHInvSize).rgb * 0.1;
    rgb += texture2D(sDiffMap, vTexCoord + vec2(0.0, -1.0) * cBlurHInvSize).rgb * 0.25;
    rgb += texture2D(sDiffMap, vTexCoord + vec2(0.0, 0.0) * cBlurHInvSize).rgb * 0.3;
    rgb += texture2D(sDiffMap, vTexCoord + vec2(0.0, 1.0) * cBlurHInvSize).rgb * 0.25;
    rgb += texture2D(sDiffMap, vTexCoord + vec2(0.0, 2.0) * cBlurHInvSize).rgb * 0.1;
    gl_FragColor = vec4(rgb, 1.0);
    #endif

    #ifdef COMBINE
    vec3 original = texture2D(sDiffMap, vScreenPos).rgb * cBloomMix.x;
    vec3 bloom = texture2D(sNormalMap, vTexCoord).rgb  * cBloomMix.y;
    // Prevent oversaturation
    original *= max(vec3(1.0) - bloom, vec3(0.0));
    gl_FragColor = vec4(original + bloom, 1.0);
    #endif
}

