#include "Render/D3D11Renderer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wrl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <DirectXMath.h>
#include <wincodec.h>

#include "World/Chunk/Chunk.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace {

constexpr UINT window_width = 1280;
constexpr UINT window_height = 720;
constexpr UINT shadow_map_size = 2048;
constexpr UINT shadow_cascade_count = 3;

struct Vertex final {
    float position[3];
    float normal[3];
    float uv[2];
    float ambient_occlusion;
};

struct ViewmodelVertex final {
    float position[3];
    float normal[3];
    float tangent[3];
    float uv[2];
};

struct ViewmodelConstants final {
    XMFLOAT4X4 model_view_projection;
    XMFLOAT4X4 model;
    XMFLOAT4 light_direction;
    XMFLOAT4 light_color;
};

struct SceneConstants final {
    XMFLOAT4X4 view_projection;
    XMFLOAT4X4 cascade_light_view_projection[shadow_cascade_count];
    XMFLOAT4 camera_position;
    XMFLOAT4 sun_direction;
    XMFLOAT4 fog_color_and_density;
    XMFLOAT4 cascade_splits;
    XMFLOAT4 sun_color_and_intensity;
    XMFLOAT4 sky_ambient_and_intensity;
};

struct Face final {
    int neighbor_x;
    int neighbor_y;
    int neighbor_z;
    float shade;
    std::array<std::array<float, 3>, 4> corners;
};

constexpr std::array<Face, 6> block_faces{{
    { 1,  0,  0, 0.82F, {{{1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F},
                           {1.0F, 1.0F, 1.0F}, {1.0F, 0.0F, 1.0F}}}},
    {-1,  0,  0, 0.68F, {{{0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 1.0F},
                           {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F}}}},
    { 0,  1,  0, 1.00F, {{{0.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 1.0F},
                           {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 0.0F}}}},
    { 0, -1,  0, 0.48F, {{{0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F},
                           {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 1.0F}}}},
    { 0,  0,  1, 0.76F, {{{1.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F},
                           {0.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 1.0F}}}},
    { 0,  0, -1, 0.60F, {{{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
                           {1.0F, 1.0F, 0.0F}, {1.0F, 0.0F, 0.0F}}}},
}};

constexpr char vertex_shader_source[] = R"(
cbuffer SceneConstants : register(b0) {
    matrix viewProjection;
    matrix cascadeLightViewProjection[3];
    float4 cameraPosition;
    float4 sunDirection;
    float4 fogColorAndDensity;
    float4 cascadeSplits;
    float4 sunColorAndIntensity;
    float4 skyAmbientAndIntensity;
};
struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float ambientOcclusion : AO;
};
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float ambientOcclusion : AO;
};
PSInput main(VSInput input) {
    PSInput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.worldPosition = input.position;
    output.normal = input.normal;
    output.uv = input.uv;
    output.ambientOcclusion = input.ambientOcclusion;
    return output;
})";

constexpr char pixel_shader_source[] = R"(
Texture2D blockAtlas : register(t0);
Texture2DArray<float> shadowMap : register(t1);

SamplerState blockSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1);

cbuffer SceneConstants : register(b0) {
    matrix viewProjection;
    matrix cascadeLightViewProjection[3];
    float4 cameraPosition;
    float4 sunDirection;
    float4 fogColorAndDensity;
    float4 cascadeSplits;
    float4 sunColorAndIntensity;
    float4 skyAmbientAndIntensity;
};
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    float ambientOcclusion : AO;
};

int selectShadowCascade(float distanceToCamera)
{
    if (distanceToCamera < cascadeSplits.x) return 0;
    if (distanceToCamera < cascadeSplits.y) return 1;
    return 2;
}

float sampleSunShadow(
    float3 worldPosition,
    float3 surfaceNormal,
    float3 lightDirection,
    int cascadeIndex)
{
    const float4 lightPosition = mul(
        float4(worldPosition, 1.0),
        cascadeLightViewProjection[cascadeIndex]);

    const float3 projected =
        lightPosition.xyz / lightPosition.w;

    const float2 shadowUV = float2(
        projected.x * 0.5 + 0.5,
        -projected.y * 0.5 + 0.5);

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0)
        return 1.0;

    const float normalLight =
        saturate(dot(surfaceNormal, lightDirection));

    const float cascadeBiasScale =
        cascadeIndex == 0 ? 1.0 :
        cascadeIndex == 1 ? 1.35 : 1.75;

    const float bias = max(
        0.00008,
        0.00045 * (1.0 - normalLight))
        * cascadeBiasScale;

    const float2 shadowTexel =
        float2(1.0 / 2048.0, 1.0 / 2048.0);

    float visibility = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            visibility += shadowMap.SampleCmpLevelZero(
                shadowSampler,
                float3(
                    shadowUV + float2(x, y) * shadowTexel,
                    cascadeIndex),
                projected.z - bias);
        }
    }

    return visibility / 9.0;
}

float4 main(PSInput input) : SV_TARGET {
    const float2 texel = float2(1.0 / 1024.0, 1.0 / 256.0);
    const float tileStart = floor(input.uv.x * 4.0) * 0.25;
    const float2 minimumUV = float2(tileStart + texel.x * 0.5, texel.y * 0.5);
    const float2 maximumUV = float2(tileStart + 0.25 - texel.x * 0.5, 1.0 - texel.y * 0.5);
    const float3 albedo = blockAtlas.Sample(blockSampler, input.uv).rgb;

    const float heightLeft = dot(blockAtlas.Sample(
        blockSampler, clamp(input.uv - float2(texel.x, 0), minimumUV, maximumUV)).rgb,
        float3(0.299, 0.587, 0.114));
    const float heightRight = dot(blockAtlas.Sample(
        blockSampler, clamp(input.uv + float2(texel.x, 0), minimumUV, maximumUV)).rgb,
        float3(0.299, 0.587, 0.114));
    const float heightDown = dot(blockAtlas.Sample(
        blockSampler, clamp(input.uv - float2(0, texel.y), minimumUV, maximumUV)).rgb,
        float3(0.299, 0.587, 0.114));
    const float heightUp = dot(blockAtlas.Sample(
        blockSampler, clamp(input.uv + float2(0, texel.y), minimumUV, maximumUV)).rgb,
        float3(0.299, 0.587, 0.114));

    const float3 baseNormal = normalize(input.normal);
    const float3 positionDx = ddx(input.worldPosition);
    const float3 positionDy = ddy(input.worldPosition);
    const float2 uvDx = ddx(input.uv);
    const float2 uvDy = ddy(input.uv);
    const float3 tangentRaw = cross(positionDy, baseNormal) * uvDx.x
        + cross(baseNormal, positionDx) * uvDy.x;
    const float3 bitangentRaw = cross(positionDy, baseNormal) * uvDx.y
        + cross(baseNormal, positionDx) * uvDy.y;
    const float inverseScale = rsqrt(max(
        max(dot(tangentRaw, tangentRaw), dot(bitangentRaw, bitangentRaw)), 0.0001));
    const float3 tangent = tangentRaw * inverseScale;
    const float3 bitangent = bitangentRaw * inverseScale;
    const float3 detailNormal = normalize(float3(
        (heightLeft - heightRight) * 3.0,
        (heightDown - heightUp) * 3.0, 1.0));
    const float3 normal = normalize(
        tangent * detailNormal.x + bitangent * detailNormal.y + baseNormal * detailNormal.z);

    const float3 lightDirection = normalize(-sunDirection.xyz);
    const float3 viewDirection = normalize(cameraPosition.xyz - input.worldPosition);
    const float3 halfVector = normalize(lightDirection + viewDirection);
    const float normalDotLight = saturate(dot(normal, lightDirection));
    const float normalDotHalf = saturate(dot(normal, halfVector));
    const float materialIndex = floor(input.uv.x * 4.0);
    const float roughness = materialIndex == 2.0 ? 0.68 : 0.88;
    const float specularPower = lerp(96.0, 10.0, roughness);
    const float specular = pow(normalDotHalf, specularPower)
        * (1.0 - roughness) * normalDotLight;
    const float hemisphereBase =
        lerp(0.16, 0.30, saturate(normal.y * 0.5 + 0.5));

    const float hemisphere =
        hemisphereBase * skyAmbientAndIntensity.w;

    const float distanceToCamera =
        length(cameraPosition.xyz - input.worldPosition);

    const int cascadeIndex =
        selectShadowCascade(distanceToCamera);

    const float rawShadow =
        sampleSunShadow(
            input.worldPosition,
            normal,
            lightDirection,
            cascadeIndex);

    const float sunVisibility =
        lerp(0.28, 1.0, rawShadow);

    const float directDiffuse =
        normalDotLight
        * 1.38
        * sunVisibility
        * sunColorAndIntensity.w;

    float3 litColor =
        albedo
        * (
            skyAmbientAndIntensity.rgb * hemisphere
            + sunColorAndIntensity.rgb * directDiffuse
          )
        * input.ambientOcclusion;

    litColor += specular
        * sunVisibility
        * sunColorAndIntensity.w
        * sunColorAndIntensity.rgb;

    const float fogAmount = 1.0 - exp(
        -distanceToCamera * distanceToCamera * fogColorAndDensity.w);
    litColor = lerp(litColor, fogColorAndDensity.rgb, saturate(fogAmount));

    // Back buffer is currently non-sRGB, so encode the final linear result
    // for display. This conversion must happen exactly once.
    const float3 linearColor = max(litColor, 0.0);
    const float3 srgbLow = linearColor * 12.92;
    const float3 srgbHigh =
        1.055 * pow(max(linearColor, 0.000001), 1.0 / 2.4) - 0.055;
    const float3 srgbColor = lerp(
        srgbHigh,
        srgbLow,
        1.0 - step(0.0031308, linearColor));

    return float4(saturate(srgbColor), 1.0);
}
)";



constexpr char viewmodel_vertex_shader_source[] = R"(
cbuffer ViewmodelConstants : register(b3)
{
    matrix modelViewProjection;
    matrix model;
    float4 viewmodelLightDirection;
    float4 viewmodelLightColor;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 viewPosition : POSITION1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

PSInput main(VSInput input)
{
    PSInput output;

    float4 localPosition =
        float4(input.position, 1.0);

    output.position =
        mul(localPosition, modelViewProjection);

    float4 transformedPosition =
        mul(localPosition, model);

    output.viewPosition =
        transformedPosition.xyz;

    output.normal =
        normalize(
            mul(
                float4(input.normal, 0.0),
                model).xyz);

    output.tangent =
        normalize(
            mul(
                float4(input.tangent, 0.0),
                model).xyz);

    output.uv = input.uv;

    return output;
}
)";

constexpr char viewmodel_pixel_shader_source[] = R"(
Texture2D pickAlbedo : register(t3);
Texture2D pickNormal : register(t4);
Texture2D pickRoughness : register(t5);
Texture2D pickMetallic : register(t6);

SamplerState pickSampler : register(s3);

cbuffer ViewmodelConstants : register(b3)
{
    matrix modelViewProjection;
    matrix model;
    float4 viewmodelLightDirection;
    float4 viewmodelLightColor;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float3 viewPosition : POSITION1;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

float distributionGGX(
    float3 N,
    float3 H,
    float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float NdotH =
        max(dot(N, H), 0.0);

    float NdotH2 =
        NdotH * NdotH;

    float denominator =
        NdotH2 * (a2 - 1.0) + 1.0;

    return a2 /
        max(
            3.14159265
            * denominator
            * denominator,
            0.0001);
}

float geometrySchlickGGX(
    float NdotV,
    float roughness)
{
    float r =
        roughness + 1.0;

    float k =
        (r * r) / 8.0;

    return NdotV /
        max(
            NdotV * (1.0 - k) + k,
            0.0001);
}

float geometrySmith(
    float3 N,
    float3 V,
    float3 L,
    float roughness)
{
    float NdotV =
        max(dot(N, V), 0.0);

    float NdotL =
        max(dot(N, L), 0.0);

    return
        geometrySchlickGGX(
            NdotV,
            roughness)
        *
        geometrySchlickGGX(
            NdotL,
            roughness);
}

float3 fresnelSchlick(
    float cosTheta,
    float3 F0)
{
    return
        F0
        +
        (1.0 - F0)
        * pow(
            1.0 - cosTheta,
            5.0);
}

float4 main(PSInput input) : SV_TARGET
{
    float3 albedo =
        pickAlbedo.Sample(
            pickSampler,
            input.uv).rgb;

    float roughness =
        clamp(
            pickRoughness.Sample(
                pickSampler,
                input.uv).r,
            0.08,
            1.0);

    float metallic =
        saturate(
            pickMetallic.Sample(
                pickSampler,
                input.uv).r);

    float3 sampledNormal =
        pickNormal.Sample(
            pickSampler,
            input.uv).xyz
        * 2.0 - 1.0;

    float3 N =
        normalize(input.normal);

    float3 T =
        normalize(
            input.tangent
            - N
            * dot(
                input.tangent,
                N));

    float3 B =
        normalize(cross(N, T));

    float3 mappedNormal =
        normalize(
            T * sampledNormal.x
            + B * sampledNormal.y
            + N * sampledNormal.z);

    N = mappedNormal;

    float3 V =
        normalize(-input.viewPosition);

    float3 L =
        normalize(
            -viewmodelLightDirection.xyz);

    float3 H =
        normalize(V + L);

    float3 F0 =
        lerp(
            float3(
                0.04,
                0.04,
                0.04),
            albedo,
            metallic);

    float NDF =
        distributionGGX(
            N,
            H,
            roughness);

    float G =
        geometrySmith(
            N,
            V,
            L,
            roughness);

    float3 F =
        fresnelSchlick(
            max(
                dot(H, V),
                0.0),
            F0);

    float3 numerator =
        NDF * G * F;

    float denominator =
        max(
            4.0
            * max(dot(N, V), 0.0)
            * max(dot(N, L), 0.0),
            0.001);

    float3 specular =
        numerator / denominator;

    float3 kS = F;

    float3 kD =
        (1.0 - kS)
        * (1.0 - metallic);

    float NdotL =
        max(
            dot(N, L),
            0.0);

    float3 radiance =
        viewmodelLightColor.rgb
        * viewmodelLightColor.w;

    float3 direct =
        (
            kD
            * albedo
            / 3.14159265
            + specular
        )
        * radiance
        * NdotL;

    // Camera-space ambient keeps the held object readable
    // even when the world sun is behind the player.
    float3 ambient =
        albedo
        * lerp(
            0.19,
            0.095,
            metallic);

    float3 linearColor =
        max(
            ambient + direct,
            0.0);

    // Back buffer is non-sRGB in the current renderer.
    float3 srgbLow =
        linearColor * 12.92;

    float3 srgbHigh =
        1.055
        * pow(
            max(
                linearColor,
                0.000001),
            1.0 / 2.4)
        - 0.055;

    float3 srgbColor =
        lerp(
            srgbHigh,
            srgbLow,
            1.0
            - step(
                0.0031308,
                linearColor));

    return float4(
        saturate(srgbColor),
        1.0);
}
)";

constexpr char hud_pixel_shader_source[] = R"(
cbuffer HudConstants : register(b2)
{
    float4 hudColor;
};

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    return hudColor;
}
)";

constexpr char sky_vertex_shader_source[] = R"(
struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float2 position =
        vertexId == 0 ? float2(-1.0, -1.0) :
        vertexId == 1 ? float2(-1.0,  3.0) :
                        float2( 3.0, -1.0);

    output.position = float4(position, 0.0, 1.0);
    output.uv = position * float2(0.5, -0.5) + 0.5;

    return output;
}
)";

constexpr char sky_pixel_shader_source[] = R"(
cbuffer SkyConstants : register(b1) {
    matrix inverseViewProjection;
    float4 cameraPositionSky;
    float4 sunDirectionSky;
    float4 horizonColor;
    float4 zenithColor;
    float4 sunColorSky;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    float2 ndc;
    ndc.x = input.uv.x * 2.0 - 1.0;
    ndc.y = 1.0 - input.uv.y * 2.0;

    float4 farPoint =
        mul(float4(ndc, 1.0, 1.0), inverseViewProjection);

    farPoint.xyz /= farPoint.w;

    const float3 viewDirection =
        normalize(farPoint.xyz - cameraPositionSky.xyz);

    const float vertical =
        saturate(viewDirection.y * 0.5 + 0.5);

    // Horizon stays brighter while zenith becomes richer blue.
    const float gradient =
        pow(saturate(vertical), 0.55);

    float3 skyColor =
        lerp(horizonColor.rgb, zenithColor.rgb, gradient);

    // Warm the horizon opposite/around low sun elevations.
    const float horizonBand =
        pow(1.0 - saturate(abs(viewDirection.y)), 5.0);

    skyColor +=
        sunColorSky.rgb
        * horizonBand
        * sunColorSky.w
        * 0.12;

    const float3 directionToSun =
        normalize(-sunDirectionSky.xyz);

    const float sunDot =
        saturate(dot(viewDirection, directionToSun));

    // Soft atmospheric glow.
    const float sunGlow =
        pow(sunDot, 256.0);

    // Compact visible solar disk.
    const float sunDisk =
        smoothstep(0.99935, 0.99975, sunDot);

    skyColor +=
        sunColorSky.rgb
        * (
            sunGlow * 1.1
            + sunDisk * 4.0
          )
        * sunColorSky.w;

    // Back buffer is non-sRGB: encode sky exactly once.
    const float3 linearColor =
        max(skyColor, 0.0);

    const float3 srgbLow =
        linearColor * 12.92;

    const float3 srgbHigh =
        1.055
        * pow(max(linearColor, 0.000001), 1.0 / 2.4)
        - 0.055;

    const float3 srgbColor =
        lerp(
            srgbHigh,
            srgbLow,
            1.0 - step(0.0031308, linearColor));

    return float4(saturate(srgbColor), 1.0);
}
)";

struct HudConstants final {
    XMFLOAT4 color;
};

struct SkyConstants final {
    XMFLOAT4X4 inverse_view_projection;
    XMFLOAT4 camera_position;
    XMFLOAT4 sun_direction;
    XMFLOAT4 horizon_color;
    XMFLOAT4 zenith_color;
    XMFLOAT4 sun_color;
};

constexpr int atlas_tile_size = 256;
constexpr int atlas_tile_count = 4;
constexpr int atlas_width = atlas_tile_size * atlas_tile_count;
constexpr int atlas_height = atlas_tile_size;

std::vector<std::uint8_t> build_block_atlas() {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(atlas_width) * atlas_height * 4U);
    for (int tile = 0; tile < atlas_tile_count; ++tile) {
        for (int y = 0; y < atlas_tile_size; ++y) {
            for (int x = 0; x < atlas_tile_size; ++x) {
                const int detail = (x * 37 + y * 71 + tile * 19) & 15;
                int red = 0;
                int green = 0;
                int blue = 0;
                if (tile == 0) {
                    red = 42 + detail;
                    green = 145 + detail * 2;
                    blue = 55 + detail;
                } else if (tile == 1) {
                    red = 105 + detail * 2;
                    green = 66 + detail;
                    blue = 34 + detail / 2;
                } else if (tile == 2) {
                    red = green = blue = 92 + detail * 3;
                } else if (y < 5 || (y < 8 && ((x + y) % 5 == 0))) {
                    red = 38 + detail;
                    green = 132 + detail * 2;
                    blue = 49 + detail;
                } else {
                    red = 101 + detail * 2;
                    green = 63 + detail;
                    blue = 32 + detail / 2;
                }

                const std::size_t pixel = (static_cast<std::size_t>(y) * atlas_width
                    + static_cast<std::size_t>(tile * atlas_tile_size + x)) * 4U;
                pixels[pixel] = static_cast<std::uint8_t>(std::clamp(red, 0, 255));
                pixels[pixel + 1] = static_cast<std::uint8_t>(std::clamp(green, 0, 255));
                pixels[pixel + 2] = static_cast<std::uint8_t>(std::clamp(blue, 0, 255));
                pixels[pixel + 3] = 255;
            }
        }
    }
    return pixels;
}

std::wstring block_atlas_path() {
    std::array<wchar_t, 32768> executable_path{};
    const DWORD length = GetModuleFileNameW(
        nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
    if (length == 0 || length >= executable_path.size()) return {};
    std::wstring path(executable_path.data(), length);
    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return {};
    path.resize(separator + 1);
    path += L"assets\\textures\\block_albedo.png";
    return path;
}

bool load_block_atlas(
    std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) noexcept {
    try {
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) return false;

        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        const auto path = block_atlas_path();
        if (path.empty()
            || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
            || FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, &decoder))
            || FAILED(decoder->GetFrame(0, &frame))
            || FAILED(factory->CreateFormatConverter(&converter))
            || FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))
            || FAILED(converter->GetSize(&width, &height))
            || width != static_cast<UINT>(atlas_width)
            || height != static_cast<UINT>(atlas_height)) return false;

        pixels.resize(static_cast<std::size_t>(width) * height * 4U);
        return SUCCEEDED(converter->CopyPixels(
            nullptr, width * 4U, static_cast<UINT>(pixels.size()), pixels.data()));
    } catch (...) {
        return false;
    }
}


std::wstring texture_asset_path(
    const wchar_t* filename)
{
    std::array<wchar_t, 32768> executable_path{};

    const DWORD length =
        GetModuleFileNameW(
            nullptr,
            executable_path.data(),
            static_cast<DWORD>(
                executable_path.size()));

    if (length == 0
        || length >= executable_path.size())
        return {};

    std::wstring path(
        executable_path.data(),
        length);

    const auto separator =
        path.find_last_of(L"\\/");

    if (separator == std::wstring::npos)
        return {};

    path.resize(separator + 1);

    path += L"assets\\textures\\";
    path += filename;

    return path;
}

bool load_rgba_image(
    const wchar_t* filename,
    std::vector<std::uint8_t>& pixels,
    UINT& width,
    UINT& height) noexcept
{
    try {
        const HRESULT com_result =
            CoInitializeEx(
                nullptr,
                COINIT_MULTITHREADED);

        if (FAILED(com_result)
            && com_result != RPC_E_CHANGED_MODE)
            return false;

        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;

        const auto path =
            texture_asset_path(filename);

        if (path.empty())
            return false;

        if (FAILED(
                CoCreateInstance(
                    CLSID_WICImagingFactory,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&factory))))
            return false;

        if (FAILED(
                factory->CreateDecoderFromFilename(
                    path.c_str(),
                    nullptr,
                    GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad,
                    &decoder)))
            return false;

        if (FAILED(
                decoder->GetFrame(
                    0,
                    &frame)))
            return false;

        if (FAILED(
                factory->CreateFormatConverter(
                    &converter)))
            return false;

        if (FAILED(
                converter->Initialize(
                    frame.Get(),
                    GUID_WICPixelFormat32bppRGBA,
                    WICBitmapDitherTypeNone,
                    nullptr,
                    0.0,
                    WICBitmapPaletteTypeCustom)))
            return false;

        if (FAILED(
                converter->GetSize(
                    &width,
                    &height)))
            return false;

        pixels.resize(
            static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height)
            * 4U);

        return SUCCEEDED(
            converter->CopyPixels(
                nullptr,
                width * 4U,
                static_cast<UINT>(
                    pixels.size()),
                pixels.data()));
    }
    catch (...) {
        return false;
    }
}

void append_viewmodel_box(
    std::vector<ViewmodelVertex>& vertices,
    const XMFLOAT3& center,
    const XMFLOAT3& size,
    const float u0,
    const float u1,
    const float v0 = 0.03F,
    const float v1 = 0.97F)
{
    const float hx =
        size.x * 0.5F;

    const float hy =
        size.y * 0.5F;

    const float hz =
        size.z * 0.5F;

    struct BoxFace {
        XMFLOAT3 normal;
        XMFLOAT3 tangent;
        std::array<XMFLOAT3, 4> corners;
    };

    const std::array<BoxFace, 6> faces{{
        {
            { 0.0F, 0.0F, -1.0F },
            { 1.0F, 0.0F,  0.0F },
            {{
                {-hx,-hy,-hz},
                {-hx, hy,-hz},
                { hx, hy,-hz},
                { hx,-hy,-hz}
            }}
        },
        {
            { 0.0F, 0.0F, 1.0F },
            {-1.0F, 0.0F, 0.0F },
            {{
                { hx,-hy, hz},
                { hx, hy, hz},
                {-hx, hy, hz},
                {-hx,-hy, hz}
            }}
        },
        {
            {-1.0F, 0.0F, 0.0F },
            { 0.0F, 0.0F,-1.0F },
            {{
                {-hx,-hy, hz},
                {-hx, hy, hz},
                {-hx, hy,-hz},
                {-hx,-hy,-hz}
            }}
        },
        {
            {1.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 1.0F},
            {{
                {hx,-hy,-hz},
                {hx, hy,-hz},
                {hx, hy, hz},
                {hx,-hy, hz}
            }}
        },
        {
            {0.0F, 1.0F, 0.0F},
            {1.0F, 0.0F, 0.0F},
            {{
                {-hx,hy,-hz},
                {-hx,hy, hz},
                { hx,hy, hz},
                { hx,hy,-hz}
            }}
        },
        {
            {0.0F,-1.0F, 0.0F},
            {1.0F, 0.0F, 0.0F},
            {{
                {-hx,-hy, hz},
                {-hx,-hy,-hz},
                { hx,-hy,-hz},
                { hx,-hy, hz}
            }}
        }
    }};

    const std::array<XMFLOAT2, 4> uv{{
        {u0, v1},
        {u0, v0},
        {u1, v0},
        {u1, v1}
    }};

    constexpr int indices[6]{
        0, 1, 2,
        0, 2, 3
    };

    for (const auto& face : faces) {
        for (const int index : indices) {
            const auto& c =
                face.corners[index];

            const auto& t =
                uv[index];

            vertices.push_back(
                ViewmodelVertex{
                    {
                        center.x + c.x,
                        center.y + c.y,
                        center.z + c.z
                    },
                    {
                        face.normal.x,
                        face.normal.y,
                        face.normal.z
                    },
                    {
                        face.tangent.x,
                        face.tangent.y,
                        face.tangent.z
                    },
                    {
                        t.x,
                        t.y
                    }
                });
        }
    }
}


std::vector<ViewmodelVertex>
build_right_arm_mesh()
{
    std::vector<ViewmodelVertex> vertices;

    vertices.reserve(36 * 7);

    // Upper sleeve continues offscreen toward the body.
    append_viewmodel_box(
        vertices,
        {0.48F, -1.08F, 0.12F},
        {0.40F, 0.92F, 0.40F},
        0.025F,
        0.475F);

    // Sleeve / forearm.
    append_viewmodel_box(
        vertices,
        {0.28F, -0.50F, 0.08F},
        {0.31F, 0.78F, 0.31F},
        0.025F,
        0.475F);

    // Wrist.
    append_viewmodel_box(
        vertices,
        {0.10F, -0.08F, 0.03F},
        {0.25F, 0.23F, 0.26F},
        0.525F,
        0.975F);

    // Palm.
    append_viewmodel_box(
        vertices,
        {0.00F, 0.13F, 0.00F},
        {0.24F, 0.27F, 0.25F},
        0.525F,
        0.975F);

    // Fingers curling around the pick handle.
    append_viewmodel_box(
        vertices,
        {-0.08F, 0.28F, -0.08F},
        {0.10F, 0.18F, 0.11F},
        0.525F,
        0.975F);

    append_viewmodel_box(
        vertices,
        {0.02F, 0.30F, -0.08F},
        {0.10F, 0.19F, 0.11F},
        0.525F,
        0.975F);

    append_viewmodel_box(
        vertices,
        {0.12F, 0.27F, -0.07F},
        {0.09F, 0.17F, 0.11F},
        0.525F,
        0.975F);

    return vertices;
}

std::vector<ViewmodelVertex>
build_basic_pick_mesh()
{
    std::vector<ViewmodelVertex> vertices;

    vertices.reserve(36 * 7);

    // --------------------------------------------------------
    // WOOD HANDLE
    // Atlas left half = wood.
    // --------------------------------------------------------

    append_viewmodel_box(
        vertices,
        {0.0F, 0.56F, 0.0F},
        {0.16F, 1.18F, 0.16F},
        0.025F,
        0.475F);

    // Slightly thicker grip end.
    append_viewmodel_box(
        vertices,
        {0.0F, -0.055F, 0.0F},
        {0.20F, 0.18F, 0.20F},
        0.025F,
        0.475F);

    // --------------------------------------------------------
    // FORGED STEEL HEAD
    // Atlas right half = metal.
    // --------------------------------------------------------

    append_viewmodel_box(
        vertices,
        {0.0F, 1.20F, 0.0F},
        {1.15F, 0.20F, 0.25F},
        0.525F,
        0.975F);

    // Left taper.
    append_viewmodel_box(
        vertices,
        {-0.65F, 1.15F, 0.0F},
        {0.24F, 0.16F, 0.21F},
        0.525F,
        0.975F);

    append_viewmodel_box(
        vertices,
        {-0.82F, 1.08F, 0.0F},
        {0.18F, 0.12F, 0.16F},
        0.525F,
        0.975F);

    // Right taper.
    append_viewmodel_box(
        vertices,
        {0.65F, 1.15F, 0.0F},
        {0.24F, 0.16F, 0.21F},
        0.525F,
        0.975F);

    append_viewmodel_box(
        vertices,
        {0.82F, 1.08F, 0.0F},
        {0.18F, 0.12F, 0.16F},
        0.525F,
        0.975F);

    return vertices;
}

int terrain_height(const int x, const int z) noexcept {
    const float rolling = std::sin(static_cast<float>(x) * 0.48F) * 1.5F
        + std::cos(static_cast<float>(z) * 0.41F) * 1.35F
        + std::sin(static_cast<float>(x + z) * 0.23F) * 0.8F;
    return std::clamp(5 + static_cast<int>(std::round(rolling)), 2, 9);
}

class ChunkWorld final {
public:
    using Key = std::pair<int, int>;
    static constexpr int stream_radius = 2;

    bool stream_around(const int center_x, const int center_z) {
        std::set<Key> desired;
        for (int z = center_z - stream_radius; z <= center_z + stream_radius; ++z) {
            for (int x = center_x - stream_radius; x <= center_x + stream_radius; ++x) {
                desired.emplace(x, z);
            }
        }

        bool changed = false;
        for (auto it = chunks_.begin(); it != chunks_.end();) {
            if (!desired.contains(it->first)) {
                it = chunks_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        for (const auto& key : desired) {
            if (!chunks_.contains(key)) {
                chunks_.emplace(key, generate_chunk(key.first, key.second));
                changed = true;
            }
        }
        return changed;
    }

    [[nodiscard]] mcr::world::Chunk::BlockId block(
        const int world_x, const int y, const int world_z) const noexcept {
        if (y < 0 || y >= mcr::world::Chunk::height) return 0;
        const int chunk_x = floor_div(world_x, mcr::world::Chunk::width);
        const int chunk_z = floor_div(world_z, mcr::world::Chunk::width);
        const auto found = chunks_.find({chunk_x, chunk_z});
        if (found == chunks_.end()) return 0;
        return found->second->block(
            world_x - chunk_x * mcr::world::Chunk::width, y,
            world_z - chunk_z * mcr::world::Chunk::width);
    }

    bool set_block(const int world_x, const int y, const int world_z,
                   const mcr::world::Chunk::BlockId block_id) {
        if (y < 0 || y >= mcr::world::Chunk::height) return false;
        const int chunk_x = floor_div(world_x, mcr::world::Chunk::width);
        const int chunk_z = floor_div(world_z, mcr::world::Chunk::width);
        const auto found = chunks_.find({chunk_x, chunk_z});
        if (found == chunks_.end()) return false;
        found->second->set_block(
            world_x - chunk_x * mcr::world::Chunk::width, y,
            world_z - chunk_z * mcr::world::Chunk::width, block_id);
        overrides_[{world_x, y, world_z}] = block_id;
        return true;
    }

    [[nodiscard]] const std::map<Key, std::unique_ptr<mcr::world::Chunk>>& chunks() const noexcept {
        return chunks_;
    }

    [[nodiscard]] std::size_t loaded_count() const noexcept { return chunks_.size(); }

private:
    static int floor_div(const int value, const int divisor) noexcept {
        int quotient = value / divisor;
        if (value % divisor < 0) --quotient;
        return quotient;
    }

    std::unique_ptr<mcr::world::Chunk> generate_chunk(const int chunk_x, const int chunk_z) {
        auto chunk = std::make_unique<mcr::world::Chunk>(
            mcr::world::ChunkCoord{chunk_x, chunk_z});
        for (int z = 0; z < mcr::world::Chunk::width; ++z) {
            for (int x = 0; x < mcr::world::Chunk::width; ++x) {
                const int world_x = chunk_x * mcr::world::Chunk::width + x;
                const int world_z = chunk_z * mcr::world::Chunk::width + z;
                const int surface = terrain_height(world_x, world_z);
                for (int y = 0; y <= surface; ++y) {
                    const mcr::world::Chunk::BlockId block_id =
                        y == surface ? 1 : (y >= surface - 2 ? 2 : 3);
                    chunk->set_block(x, y, z, block_id);
                }
            }
        }

        for (const auto& [position, block_id] : overrides_) {
            const auto [world_x, y, world_z] = position;
            if (floor_div(world_x, mcr::world::Chunk::width) == chunk_x
                && floor_div(world_z, mcr::world::Chunk::width) == chunk_z) {
                chunk->set_block(
                    world_x - chunk_x * mcr::world::Chunk::width, y,
                    world_z - chunk_z * mcr::world::Chunk::width, block_id);
            }
        }
        return chunk;
    }

    std::map<Key, std::unique_ptr<mcr::world::Chunk>> chunks_;
    std::map<std::tuple<int, int, int>, mcr::world::Chunk::BlockId> overrides_;
};

int texture_tile(const mcr::world::Chunk::BlockId block, const Face& face) noexcept {
    if (block == 1) {
        if (face.neighbor_y > 0) return 0;
        if (face.neighbor_y < 0) return 1;
        return 3;
    }
    return block == 2 ? 1 : 2;
}

std::array<float, 2> texture_uv(const int tile, const unsigned corner) noexcept {
    constexpr std::array<std::array<float, 2>, 4> corners{{
        {{0.0F, 1.0F}}, {{0.0F, 0.0F}}, {{1.0F, 0.0F}}, {{1.0F, 1.0F}}
    }};
    const float inset = 0.5F;
    const float usable = static_cast<float>(atlas_tile_size) - 1.0F;
    return {
        (static_cast<float>(tile * atlas_tile_size) + inset
            + corners[corner][0] * usable) / static_cast<float>(atlas_width),
        (inset + corners[corner][1] * usable) / static_cast<float>(atlas_height)
    };
}

float vertex_ambient_occlusion(
    const ChunkWorld& world, const int world_x, const int y, const int world_z,
    const Face& face, const std::array<float, 3>& corner) noexcept {
    std::array<int, 3> first_axis{};
    std::array<int, 3> second_axis{};
    int first_sign = 1;
    int second_sign = 1;
    if (face.neighbor_x != 0) {
        first_axis = {0, 1, 0};
        second_axis = {0, 0, 1};
        first_sign = corner[1] > 0.5F ? 1 : -1;
        second_sign = corner[2] > 0.5F ? 1 : -1;
    } else if (face.neighbor_y != 0) {
        first_axis = {1, 0, 0};
        second_axis = {0, 0, 1};
        first_sign = corner[0] > 0.5F ? 1 : -1;
        second_sign = corner[2] > 0.5F ? 1 : -1;
    } else {
        first_axis = {1, 0, 0};
        second_axis = {0, 1, 0};
        first_sign = corner[0] > 0.5F ? 1 : -1;
        second_sign = corner[1] > 0.5F ? 1 : -1;
    }

    const auto occupied = [&](const int first, const int second) {
        return world.block(
            world_x + face.neighbor_x + first_axis[0] * first
                + second_axis[0] * second,
            y + face.neighbor_y + first_axis[1] * first
                + second_axis[1] * second,
            world_z + face.neighbor_z + first_axis[2] * first
                + second_axis[2] * second) != 0;
    };
    const bool first_side = occupied(first_sign, 0);
    const bool second_side = occupied(0, second_sign);
    const bool diagonal = occupied(first_sign, second_sign);
    if (first_side && second_side) return 0.55F;
    return 1.0F - 0.14F * static_cast<float>(
        static_cast<int>(first_side) + static_cast<int>(second_side)
        + static_cast<int>(diagonal));
}

std::vector<Vertex> mesh_world(const ChunkWorld& world) {
    std::vector<Vertex> vertices;
    vertices.reserve(world.loaded_count() * 12000);
    constexpr std::array<unsigned, 6> triangle_indices{0, 1, 2, 0, 2, 3};

    for (const auto& [key, chunk] : world.chunks()) {
        const int base_x = key.first * mcr::world::Chunk::width;
        const int base_z = key.second * mcr::world::Chunk::width;
        for (int y = 0; y < mcr::world::Chunk::height; ++y) {
            for (int z = 0; z < mcr::world::Chunk::width; ++z) {
                for (int x = 0; x < mcr::world::Chunk::width; ++x) {
                    const auto block = chunk->block(x, y, z);
                    if (block == 0) continue;
                    const int world_x = base_x + x;
                    const int world_z = base_z + z;
                    for (const auto& face : block_faces) {
                        if (world.block(world_x + face.neighbor_x, y + face.neighbor_y,
                                        world_z + face.neighbor_z) != 0) continue;
                        const int tile = texture_tile(block, face);
                        for (const unsigned corner_index : triangle_indices) {
                            const auto& corner = face.corners[corner_index];
                            const auto uv = texture_uv(tile, corner_index);
                            const float ambient_occlusion = vertex_ambient_occlusion(
                                world, world_x, y, world_z, face, corner);
                            vertices.push_back({
                                {static_cast<float>(world_x) + corner[0],
                                 static_cast<float>(y) + corner[1],
                                 static_cast<float>(world_z) + corner[2]},
                                {static_cast<float>(face.neighbor_x),
                                 static_cast<float>(face.neighbor_y),
                                 static_cast<float>(face.neighbor_z)},
                                {uv[0], uv[1]},
                                ambient_occlusion
                            });
                        }
                    }
                }
            }
        }
    }
    return vertices;
}

class VisualDemo final {
public:
    explicit VisualDemo(
        const mcr::render::PlayerControlHooks* player_controls = nullptr) noexcept
        : player_controls_(player_controls) {}

    bool run() noexcept {
        if (!create_window() || !create_graphics()) {
            cleanup();
            return false;
        }

        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        previous_frame_ = std::chrono::steady_clock::now();
        MSG message{};
        while (message.message != WM_QUIT) {
            if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            } else if (!render()) {
                cleanup();
                return false;
            }
        }
        cleanup();
        return true;
    }

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    bool create_window() noexcept {
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = L"MCReduxD3D11Window";
        if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;

        constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT rectangle{0, 0, static_cast<LONG>(window_width), static_cast<LONG>(window_height)};
        AdjustWindowRect(&rectangle, style, FALSE);
        window_ = CreateWindowExW(0, window_class.lpszClassName,
            L"MC-Redux - Streamed Voxel World [FLY] (DirectX 11)", style,
            CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top, nullptr, nullptr, instance_, nullptr);
        return window_ != nullptr;
    }

    bool create_graphics() noexcept {
        DXGI_SWAP_CHAIN_DESC swap_description{};
        swap_description.BufferDesc.Width = window_width;
        swap_description.BufferDesc.Height = window_height;
        swap_description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_description.SampleDesc.Count = 1;
        swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_description.BufferCount = 2;
        swap_description.OutputWindow = window_;
        swap_description.Windowed = TRUE;
        swap_description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL requested_levels[]{D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL created_level{};
        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            requested_levels, 1, D3D11_SDK_VERSION, &swap_description,
            &swap_chain_, &device_, &created_level, &context_);
        if (FAILED(result)) {
            result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                requested_levels, 1, D3D11_SDK_VERSION, &swap_description,
                &swap_chain_, &device_, &created_level, &context_);
        }
        if (FAILED(result) || created_level < D3D_FEATURE_LEVEL_11_0) return false;

        ComPtr<ID3D11Texture2D> back_buffer;
        if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))
            || FAILED(device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_)))
            return false;

        D3D11_TEXTURE2D_DESC depth_description{};
        depth_description.Width = window_width;
        depth_description.Height = window_height;
        depth_description.MipLevels = 1;
        depth_description.ArraySize = 1;
        depth_description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depth_description.SampleDesc.Count = 1;
        depth_description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(device_->CreateTexture2D(&depth_description, nullptr, &depth_texture_))
            || FAILED(device_->CreateDepthStencilView(
                depth_texture_.Get(), nullptr, &depth_view_)))
            return false;

        D3D11_TEXTURE2D_DESC shadow_description{};
        shadow_description.Width = shadow_map_size;
        shadow_description.Height = shadow_map_size;
        shadow_description.MipLevels = 1;
        shadow_description.ArraySize = shadow_cascade_count;
        shadow_description.Format = DXGI_FORMAT_R32_TYPELESS;
        shadow_description.SampleDesc.Count = 1;
        shadow_description.Usage = D3D11_USAGE_DEFAULT;
        shadow_description.BindFlags =
            D3D11_BIND_DEPTH_STENCIL |
            D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device_->CreateTexture2D(
                &shadow_description,
                nullptr,
                &shadow_texture_)))
            return false;

        for (UINT cascade = 0;
             cascade < shadow_cascade_count;
             ++cascade) {
            D3D11_DEPTH_STENCIL_VIEW_DESC shadow_dsv{};
            shadow_dsv.Format = DXGI_FORMAT_D32_FLOAT;
            shadow_dsv.ViewDimension =
                D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            shadow_dsv.Texture2DArray.MipSlice = 0;
            shadow_dsv.Texture2DArray.FirstArraySlice = cascade;
            shadow_dsv.Texture2DArray.ArraySize = 1;

            if (FAILED(device_->CreateDepthStencilView(
                    shadow_texture_.Get(),
                    &shadow_dsv,
                    &shadow_depth_views_[cascade])))
                return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC shadow_srv{};
        shadow_srv.Format = DXGI_FORMAT_R32_FLOAT;
        shadow_srv.ViewDimension =
            D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        shadow_srv.Texture2DArray.MostDetailedMip = 0;
        shadow_srv.Texture2DArray.MipLevels = 1;
        shadow_srv.Texture2DArray.FirstArraySlice = 0;
        shadow_srv.Texture2DArray.ArraySize =
            shadow_cascade_count;

        if (FAILED(device_->CreateShaderResourceView(
                shadow_texture_.Get(),
                &shadow_srv,
                &shadow_shader_resource_)))
            return false;

        ComPtr<ID3DBlob> vertex_bytecode;
        ComPtr<ID3DBlob> pixel_bytecode;
        ComPtr<ID3DBlob> sky_vertex_bytecode;
        ComPtr<ID3DBlob> sky_pixel_bytecode;
        ComPtr<ID3DBlob> hud_pixel_bytecode;
        ComPtr<ID3DBlob> errors;
        UINT compile_flags = 0;
#if defined(_DEBUG)
        compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        if (FAILED(D3DCompile(vertex_shader_source, sizeof(vertex_shader_source) - 1,
                              nullptr, nullptr, nullptr, "main", "vs_5_0",
                              compile_flags, 0, &vertex_bytecode, &errors))
            || FAILED(D3DCompile(pixel_shader_source, sizeof(pixel_shader_source) - 1,
                                 nullptr, nullptr, nullptr, "main", "ps_5_0",
                                 compile_flags, 0, &pixel_bytecode, &errors)))
            return false;
        if (FAILED(device_->CreateVertexShader(vertex_bytecode->GetBufferPointer(),
                                               vertex_bytecode->GetBufferSize(), nullptr,
                                               &vertex_shader_))
            || FAILED(device_->CreatePixelShader(pixel_bytecode->GetBufferPointer(),
                                                 pixel_bytecode->GetBufferSize(), nullptr,
                                                 &pixel_shader_)))
            return false;

        if (FAILED(D3DCompile(
                sky_vertex_shader_source,
                sizeof(sky_vertex_shader_source) - 1,
                nullptr, nullptr, nullptr,
                "main", "vs_5_0",
                compile_flags, 0,
                &sky_vertex_bytecode,
                &errors))
            || FAILED(D3DCompile(
                sky_pixel_shader_source,
                sizeof(sky_pixel_shader_source) - 1,
                nullptr, nullptr, nullptr,
                "main", "ps_5_0",
                compile_flags, 0,
                &sky_pixel_bytecode,
                &errors)))
            return false;

        if (FAILED(D3DCompile(
                hud_pixel_shader_source,
                sizeof(hud_pixel_shader_source) - 1,
                nullptr, nullptr, nullptr,
                "main", "ps_5_0",
                compile_flags, 0,
                &hud_pixel_bytecode,
                &errors)))
            return false;

        if (FAILED(device_->CreateVertexShader(
                sky_vertex_bytecode->GetBufferPointer(),
                sky_vertex_bytecode->GetBufferSize(),
                nullptr,
                &sky_vertex_shader_))
            || FAILED(device_->CreatePixelShader(
                sky_pixel_bytecode->GetBufferPointer(),
                sky_pixel_bytecode->GetBufferSize(),
                nullptr,
                &sky_pixel_shader_))
            || FAILED(device_->CreatePixelShader(
                hud_pixel_bytecode->GetBufferPointer(),
                hud_pixel_bytecode->GetBufferSize(),
                nullptr,
                &hud_pixel_shader_)))
            return false;

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 4> input_elements{{
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"AO", 0, DXGI_FORMAT_R32_FLOAT, 0, 32,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
        }};
        if (FAILED(device_->CreateInputLayout(input_elements.data(),
                                              static_cast<UINT>(input_elements.size()),
                                              vertex_bytecode->GetBufferPointer(),
                                              vertex_bytecode->GetBufferSize(),
                                              &input_layout_)))
            return false;

        if (!create_block_atlas()) return false;
        if (!create_viewmodel_resources()) return false;
        if (!update_streaming(true)) return false;

        D3D11_BUFFER_DESC constant_description{};
        constant_description.ByteWidth = sizeof(SceneConstants);
        constant_description.Usage = D3D11_USAGE_DEFAULT;
        constant_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(device_->CreateBuffer(&constant_description, nullptr, &scene_constants_)))
            return false;

        D3D11_BUFFER_DESC sky_constant_description{};
        sky_constant_description.ByteWidth = sizeof(SkyConstants);
        sky_constant_description.Usage = D3D11_USAGE_DEFAULT;
        sky_constant_description.BindFlags =
            D3D11_BIND_CONSTANT_BUFFER;

        if (FAILED(device_->CreateBuffer(
                &sky_constant_description,
                nullptr,
                &sky_constants_)))
            return false;

        D3D11_BUFFER_DESC hud_constant_description{};
        hud_constant_description.ByteWidth = sizeof(HudConstants);
        hud_constant_description.Usage = D3D11_USAGE_DEFAULT;
        hud_constant_description.BindFlags =
            D3D11_BIND_CONSTANT_BUFFER;

        if (FAILED(device_->CreateBuffer(
                &hud_constant_description,
                nullptr,
                &hud_constants_)))
            return false;

        D3D11_RASTERIZER_DESC rasterizer_description{};
        rasterizer_description.FillMode = D3D11_FILL_SOLID;
        rasterizer_description.CullMode = D3D11_CULL_NONE;
        rasterizer_description.DepthClipEnable = TRUE;
        if (FAILED(device_->CreateRasterizerState(
                &rasterizer_description, &rasterizer_)))
            return false;

        D3D11_RASTERIZER_DESC shadow_rasterizer_description{};
        shadow_rasterizer_description.FillMode =
            D3D11_FILL_SOLID;
        shadow_rasterizer_description.CullMode =
            D3D11_CULL_NONE;
        shadow_rasterizer_description.DepthClipEnable = TRUE;
        shadow_rasterizer_description.DepthBias = 100;
        shadow_rasterizer_description.SlopeScaledDepthBias = 1.5F;
        shadow_rasterizer_description.DepthBiasClamp = 0.01F;

        if (FAILED(device_->CreateRasterizerState(
                &shadow_rasterizer_description,
                &shadow_rasterizer_)))
            return false;

        D3D11_SAMPLER_DESC shadow_sampler_description{};
        shadow_sampler_description.Filter =
            D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        shadow_sampler_description.AddressU =
            D3D11_TEXTURE_ADDRESS_BORDER;
        shadow_sampler_description.AddressV =
            D3D11_TEXTURE_ADDRESS_BORDER;
        shadow_sampler_description.AddressW =
            D3D11_TEXTURE_ADDRESS_BORDER;
        shadow_sampler_description.BorderColor[0] = 1.0F;
        shadow_sampler_description.BorderColor[1] = 1.0F;
        shadow_sampler_description.BorderColor[2] = 1.0F;
        shadow_sampler_description.BorderColor[3] = 1.0F;
        shadow_sampler_description.ComparisonFunc =
            D3D11_COMPARISON_LESS_EQUAL;
        shadow_sampler_description.MinLOD = 0.0F;
        shadow_sampler_description.MaxLOD =
            D3D11_FLOAT32_MAX;

        if (FAILED(device_->CreateSamplerState(
                &shadow_sampler_description,
                &shadow_sampler_)))
            return false;

        shadow_viewport_.TopLeftX = 0.0F;
        shadow_viewport_.TopLeftY = 0.0F;
        shadow_viewport_.Width =
            static_cast<float>(shadow_map_size);
        shadow_viewport_.Height =
            static_cast<float>(shadow_map_size);
        shadow_viewport_.MinDepth = 0.0F;
        shadow_viewport_.MaxDepth = 1.0F;

        viewport_.TopLeftX = 0.0F;
        viewport_.TopLeftY = 0.0F;
        viewport_.Width = static_cast<float>(window_width);
        viewport_.Height = static_cast<float>(window_height);
        viewport_.MinDepth = 0.0F;
        viewport_.MaxDepth = 1.0F;
        return true;
    }

    bool create_block_atlas() noexcept {
        try {
            std::vector<std::uint8_t> pixels;
            UINT texture_width = atlas_width;
            UINT texture_height = atlas_height;
            if (!load_block_atlas(pixels, texture_width, texture_height)) {
                pixels = build_block_atlas();
            }
            D3D11_TEXTURE2D_DESC texture_description{};
            texture_description.Width = texture_width;
            texture_description.Height = texture_height;
            texture_description.MipLevels = 0;
            texture_description.ArraySize = 1;
            texture_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            texture_description.SampleDesc.Count = 1;
            texture_description.Usage = D3D11_USAGE_DEFAULT;
            texture_description.BindFlags =
                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            texture_description.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(device_->CreateTexture2D(
                    &texture_description, nullptr, &texture))
                || FAILED(device_->CreateShaderResourceView(
                    texture.Get(), nullptr, &block_atlas_))) return false;
            context_->UpdateSubresource(
                texture.Get(), 0, nullptr, pixels.data(), texture_width * 4U, 0);
            context_->GenerateMips(block_atlas_.Get());

            D3D11_SAMPLER_DESC sampler_description{};
            sampler_description.Filter = D3D11_FILTER_ANISOTROPIC;
            sampler_description.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_description.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_description.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_description.MaxAnisotropy = 8;
            sampler_description.MaxLOD = D3D11_FLOAT32_MAX;
            return SUCCEEDED(device_->CreateSamplerState(
                &sampler_description, &block_sampler_));
        } catch (...) {
            return false;
        }
    }


    bool create_viewmodel_texture(
        const wchar_t* filename,
        const bool srgb,
        ComPtr<ID3D11ShaderResourceView>& output) noexcept
    {
        std::vector<std::uint8_t> pixels;
        UINT width = 0;
        UINT height = 0;

        if (!load_rgba_image(
                filename,
                pixels,
                width,
                height))
            return false;

        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 0;
        description.ArraySize = 1;

        description.Format =
            srgb
            ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            : DXGI_FORMAT_R8G8B8A8_UNORM;

        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;

        description.BindFlags =
            D3D11_BIND_SHADER_RESOURCE
            |
            D3D11_BIND_RENDER_TARGET;

        description.MiscFlags =
            D3D11_RESOURCE_MISC_GENERATE_MIPS;

        ComPtr<ID3D11Texture2D> texture;

        if (FAILED(
                device_->CreateTexture2D(
                    &description,
                    nullptr,
                    &texture)))
            return false;

        if (FAILED(
                device_->CreateShaderResourceView(
                    texture.Get(),
                    nullptr,
                    &output)))
            return false;

        context_->UpdateSubresource(
            texture.Get(),
            0,
            nullptr,
            pixels.data(),
            width * 4U,
            0);

        context_->GenerateMips(
            output.Get());

        return true;
    }

    bool create_viewmodel_resources() noexcept
    {
        try {
            ComPtr<ID3DBlob> vertex_bytecode;
            ComPtr<ID3DBlob> pixel_bytecode;
            ComPtr<ID3DBlob> errors;

            UINT flags = 0;

#if defined(_DEBUG)
            flags =
                D3DCOMPILE_DEBUG
                |
                D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

            if (FAILED(
                    D3DCompile(
                        viewmodel_vertex_shader_source,
                        sizeof(
                            viewmodel_vertex_shader_source)
                            - 1,
                        nullptr,
                        nullptr,
                        nullptr,
                        "main",
                        "vs_5_0",
                        flags,
                        0,
                        &vertex_bytecode,
                        &errors)))
                return false;

            errors.Reset();

            if (FAILED(
                    D3DCompile(
                        viewmodel_pixel_shader_source,
                        sizeof(
                            viewmodel_pixel_shader_source)
                            - 1,
                        nullptr,
                        nullptr,
                        nullptr,
                        "main",
                        "ps_5_0",
                        flags,
                        0,
                        &pixel_bytecode,
                        &errors)))
                return false;

            if (FAILED(
                    device_->CreateVertexShader(
                        vertex_bytecode->GetBufferPointer(),
                        vertex_bytecode->GetBufferSize(),
                        nullptr,
                        &viewmodel_vertex_shader_)))
                return false;

            if (FAILED(
                    device_->CreatePixelShader(
                        pixel_bytecode->GetBufferPointer(),
                        pixel_bytecode->GetBufferSize(),
                        nullptr,
                        &viewmodel_pixel_shader_)))
                return false;

            constexpr std::array<
                D3D11_INPUT_ELEMENT_DESC,
                4> input_elements{{
                {
                    "POSITION",
                    0,
                    DXGI_FORMAT_R32G32B32_FLOAT,
                    0,
                    0,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0
                },
                {
                    "NORMAL",
                    0,
                    DXGI_FORMAT_R32G32B32_FLOAT,
                    0,
                    12,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0
                },
                {
                    "TANGENT",
                    0,
                    DXGI_FORMAT_R32G32B32_FLOAT,
                    0,
                    24,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0
                },
                {
                    "TEXCOORD",
                    0,
                    DXGI_FORMAT_R32G32_FLOAT,
                    0,
                    36,
                    D3D11_INPUT_PER_VERTEX_DATA,
                    0
                }
            }};

            if (FAILED(
                    device_->CreateInputLayout(
                        input_elements.data(),
                        static_cast<UINT>(
                            input_elements.size()),
                        vertex_bytecode->GetBufferPointer(),
                        vertex_bytecode->GetBufferSize(),
                        &viewmodel_input_layout_)))
                return false;

            const auto vertices =
                build_basic_pick_mesh();

            if (vertices.empty())
                return false;

            const auto arm_vertices =
                build_right_arm_mesh();

            if (arm_vertices.empty())
                return false;

            D3D11_BUFFER_DESC vertex_description{};
            vertex_description.ByteWidth =
                static_cast<UINT>(
                    vertices.size()
                    * sizeof(ViewmodelVertex));

            vertex_description.Usage =
                D3D11_USAGE_IMMUTABLE;

            vertex_description.BindFlags =
                D3D11_BIND_VERTEX_BUFFER;

            D3D11_SUBRESOURCE_DATA vertex_data{};
            vertex_data.pSysMem =
                vertices.data();

            if (FAILED(
                    device_->CreateBuffer(
                        &vertex_description,
                        &vertex_data,
                        &viewmodel_vertex_buffer_)))
                return false;

            viewmodel_vertex_count_ =
                static_cast<UINT>(
                    vertices.size());

            D3D11_BUFFER_DESC arm_vertex_description{};
            arm_vertex_description.ByteWidth =
                static_cast<UINT>(
                    arm_vertices.size()
                    * sizeof(ViewmodelVertex));

            arm_vertex_description.Usage =
                D3D11_USAGE_IMMUTABLE;

            arm_vertex_description.BindFlags =
                D3D11_BIND_VERTEX_BUFFER;

            D3D11_SUBRESOURCE_DATA arm_vertex_data{};
            arm_vertex_data.pSysMem =
                arm_vertices.data();

            if (FAILED(
                    device_->CreateBuffer(
                        &arm_vertex_description,
                        &arm_vertex_data,
                        &viewmodel_arm_vertex_buffer_)))
                return false;

            viewmodel_arm_vertex_count_ =
                static_cast<UINT>(
                    arm_vertices.size());

            D3D11_BUFFER_DESC constants_description{};
            constants_description.ByteWidth =
                sizeof(ViewmodelConstants);

            constants_description.Usage =
                D3D11_USAGE_DEFAULT;

            constants_description.BindFlags =
                D3D11_BIND_CONSTANT_BUFFER;

            if (FAILED(
                    device_->CreateBuffer(
                        &constants_description,
                        nullptr,
                        &viewmodel_constants_)))
                return false;

            if (!create_viewmodel_texture(
                    L"pick_albedo.png",
                    true,
                    pick_albedo_))
                return false;

            if (!create_viewmodel_texture(
                    L"pick_normal.png",
                    false,
                    pick_normal_))
                return false;

            if (!create_viewmodel_texture(
                    L"pick_roughness.png",
                    false,
                    pick_roughness_))
                return false;

            if (!create_viewmodel_texture(
                    L"pick_metallic.png",
                    false,
                    pick_metallic_))
                return false;

            if (!create_viewmodel_texture(
                    L"arm_albedo.png",
                    true,
                    arm_albedo_))
                return false;

            if (!create_viewmodel_texture(
                    L"arm_normal.png",
                    false,
                    arm_normal_))
                return false;

            if (!create_viewmodel_texture(
                    L"arm_roughness.png",
                    false,
                    arm_roughness_))
                return false;

            if (!create_viewmodel_texture(
                    L"arm_metallic.png",
                    false,
                    arm_metallic_))
                return false;

            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter =
                D3D11_FILTER_ANISOTROPIC;

            sampler.AddressU =
                D3D11_TEXTURE_ADDRESS_CLAMP;

            sampler.AddressV =
                D3D11_TEXTURE_ADDRESS_CLAMP;

            sampler.AddressW =
                D3D11_TEXTURE_ADDRESS_CLAMP;

            sampler.MaxAnisotropy = 8;
            sampler.MinLOD = 0.0F;

            sampler.MaxLOD =
                D3D11_FLOAT32_MAX;

            if (FAILED(
                    device_->CreateSamplerState(
                        &sampler,
                        &viewmodel_sampler_)))
                return false;

            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool rebuild_mesh() noexcept {
        try {
            const auto vertices = mesh_world(world_);
            if (vertices.empty()) return false;
            D3D11_BUFFER_DESC description{};
            description.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Vertex));
            description.Usage = D3D11_USAGE_IMMUTABLE;
            description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = vertices.data();
            ComPtr<ID3D11Buffer> replacement;
            if (FAILED(device_->CreateBuffer(&description, &data, &replacement))) return false;
            vertex_buffer_ = std::move(replacement);
            vertex_count_ = static_cast<UINT>(vertices.size());
            return true;
        } catch (...) {
            return false;
        }
    }

    bool update_streaming(const bool force = false) noexcept {
        const int chunk_x = static_cast<int>(
            std::floor(camera_position_.x / static_cast<float>(mcr::world::Chunk::width)));
        const int chunk_z = static_cast<int>(
            std::floor(camera_position_.z / static_cast<float>(mcr::world::Chunk::width)));
        if (!force && stream_center_valid_ && chunk_x == stream_center_x_
            && chunk_z == stream_center_z_) return true;

        try {
            world_.stream_around(chunk_x, chunk_z);
        } catch (...) {
            return false;
        }
        if (!rebuild_mesh()) return false;
        stream_center_x_ = chunk_x;
        stream_center_z_ = chunk_z;
        stream_center_valid_ = true;
        return true;
    }

    [[nodiscard]] bool player_collides(const XMFLOAT3& eye_position) const noexcept {
        constexpr float radius = 0.30F;
        constexpr float eye_height = 1.62F;
        constexpr float player_height = 1.80F;
        constexpr float epsilon = 0.001F;
        const int minimum_x = static_cast<int>(std::floor(eye_position.x - radius));
        const int maximum_x = static_cast<int>(std::floor(eye_position.x + radius - epsilon));
        const int minimum_y = static_cast<int>(std::floor(eye_position.y - eye_height));
        const int maximum_y = static_cast<int>(
            std::floor(eye_position.y + player_height - eye_height - epsilon));
        const int minimum_z = static_cast<int>(std::floor(eye_position.z - radius));
        const int maximum_z = static_cast<int>(std::floor(eye_position.z + radius - epsilon));

        for (int y = minimum_y; y <= maximum_y; ++y) {
            for (int z = minimum_z; z <= maximum_z; ++z) {
                for (int x = minimum_x; x <= maximum_x; ++x) {
                    if (world_.block(x, y, z) != 0) return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool on_ground() const noexcept {
        auto probe = camera_position_;
        probe.y -= 0.06F;
        return player_collides(probe);
    }

    bool move_with_collision(const XMFLOAT3 displacement) noexcept {
        const float largest = std::max({
            std::abs(displacement.x), std::abs(displacement.y), std::abs(displacement.z)});
        const int steps = std::max(1, static_cast<int>(std::ceil(largest / 0.20F)));
        const XMFLOAT3 step{
            displacement.x / static_cast<float>(steps),
            displacement.y / static_cast<float>(steps),
            displacement.z / static_cast<float>(steps)};
        bool vertical_collision = false;

        for (int index = 0; index < steps; ++index) {
            auto candidate = camera_position_;
            candidate.x += step.x;
            if (!player_collides(candidate)) camera_position_.x = candidate.x;

            candidate = camera_position_;
            candidate.z += step.z;
            if (!player_collides(candidate)) camera_position_.z = candidate.z;

            candidate = camera_position_;
            candidate.y += step.y;
            if (!player_collides(candidate)) {
                camera_position_.y = candidate.y;
            } else if (step.y != 0.0F) {
                vertical_collision = true;
            }
        }
        return vertical_collision;
    }

    void update_camera(const float delta_seconds) noexcept {
        if (GetForegroundWindow() != window_) {
            mouse_looking_ = false;
            return;
        }

        if (player_controls_
            && player_controls_->select_tool_slot) {

            for (int slot = 0; slot < 4; ++slot) {
                const int key = '1' + slot;
                const bool down =
                    (GetAsyncKeyState(key) & 0x8000) != 0;

                if (down && !tool_slot_key_was_down_[slot]) {
                    player_controls_->select_tool_slot(
                        static_cast<std::size_t>(slot));

                    mining_active_ = false;
                    mining_progress_ = 0.0F;
                }

                tool_slot_key_was_down_[slot] = down;
            }
        }

        POINT cursor{};
        GetCursorPos(&cursor);
        const bool right_mouse_down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        if (right_mouse_down) {
            if (mouse_looking_) {
                yaw_ += static_cast<float>(cursor.x - previous_cursor_.x) * 0.004F;
                pitch_ -= static_cast<float>(cursor.y - previous_cursor_.y) * 0.004F;
                pitch_ = std::clamp(pitch_, -1.45F, 1.45F);
            }
            previous_cursor_ = cursor;
            mouse_looking_ = true;
        } else {
            mouse_looking_ = false;
        }

        const bool mode_key_down = (GetAsyncKeyState('F') & 0x8000) != 0;
        if (mode_key_down && !mode_key_was_down_) {
            fly_mode_ = !fly_mode_;
            vertical_velocity_ = 0.0F;
            SetWindowTextW(window_, fly_mode_
                ? L"MC-Redux - Streamed Voxel World [FLY] (DirectX 11)"
                : L"MC-Redux - Streamed Voxel World [WALK] (DirectX 11)");
        }
        mode_key_was_down_ = mode_key_down;

        const XMVECTOR flat_forward = XMVector3Normalize(
            XMVectorSet(std::sin(yaw_), 0.0F, std::cos(yaw_), 0.0F));
        const XMVECTOR right = XMVector3Normalize(
            XMVector3Cross(XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), flat_forward));
        XMVECTOR movement = XMVectorZero();
        if (GetAsyncKeyState('W') & 0x8000) movement = XMVectorAdd(movement, flat_forward);
        if (GetAsyncKeyState('S') & 0x8000) movement = XMVectorSubtract(movement, flat_forward);
        if (GetAsyncKeyState('D') & 0x8000) movement = XMVectorAdd(movement, right);
        if (GetAsyncKeyState('A') & 0x8000) movement = XMVectorSubtract(movement, right);
        if (fly_mode_ && (GetAsyncKeyState('E') & 0x8000))
            movement = XMVectorAdd(movement, XMVectorSet(0, 1, 0, 0));
        if (fly_mode_ && (GetAsyncKeyState('Q') & 0x8000))
            movement = XMVectorSubtract(movement, XMVectorSet(0, 1, 0, 0));

        const bool has_movement =
            XMVectorGetX(XMVector3LengthSq(movement)) > 0.0F;

        if (fly_mode_) {
            // Fly mode is a development/navigation mode and does not
            // consume gameplay stamina.
            if (player_controls_ && player_controls_->update_stamina) {
                player_controls_->update_stamina(delta_seconds, false);
            }

            if (has_movement) {
                const float speed =
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 18.0F : 7.0F;
                const XMVECTOR position = XMLoadFloat3(&camera_position_);
                XMStoreFloat3(&camera_position_, XMVectorAdd(position,
                    XMVectorScale(
                        XMVector3Normalize(movement), speed * delta_seconds)));
            }
            return;
        }

        const bool sprint_requested =
            has_movement
            && ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);

        bool sprint_allowed = true;
        if (player_controls_ && player_controls_->can_sprint) {
            sprint_allowed = player_controls_->can_sprint();
        }

        const bool sprinting =
            sprint_requested && sprint_allowed;

        if (player_controls_ && player_controls_->update_stamina) {
            player_controls_->update_stamina(
                delta_seconds,
                sprint_requested);
        }

        float stamina_movement_multiplier = 1.0F;
        if (player_controls_ && player_controls_->movement_multiplier) {
            stamina_movement_multiplier =
                player_controls_->movement_multiplier();
        }

        XMFLOAT3 displacement{};
        if (has_movement) {
            const float speed =
                (sprinting ? 8.0F : 4.5F)
                * stamina_movement_multiplier;

            XMStoreFloat3(
                &displacement,
                XMVectorScale(
                    XMVector3Normalize(movement),
                    speed * delta_seconds));

            displacement.y = 0.0F;
        }

        const bool jump_down =
            (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

        if (jump_down && !jump_was_down_ && on_ground()) {
            bool jump_allowed = true;

            if (player_controls_ && player_controls_->consume_jump) {
                jump_allowed =
                    player_controls_->consume_jump();
            }

            if (jump_allowed) {
                vertical_velocity_ = 8.0F;
            }
        }

        jump_was_down_ = jump_down;
        vertical_velocity_ = std::max(vertical_velocity_ - 24.0F * delta_seconds, -30.0F);
        displacement.y = vertical_velocity_ * delta_seconds;
        if (move_with_collision(displacement)) vertical_velocity_ = 0.0F;
    }

    struct BlockHit final {
        bool found{false};
        int x{0};
        int y{0};
        int z{0};
        int placement_x{0};
        int placement_y{0};
        int placement_z{0};
    };

    [[nodiscard]] BlockHit raycast_block(const XMVECTOR forward) const noexcept {
        XMFLOAT3 direction{};
        XMStoreFloat3(&direction, forward);
        int previous_x = static_cast<int>(std::floor(camera_position_.x));
        int previous_y = static_cast<int>(std::floor(camera_position_.y));
        int previous_z = static_cast<int>(std::floor(camera_position_.z));

        for (float distance = 0.0F; distance <= 8.0F; distance += 0.05F) {
            const int x = static_cast<int>(
                std::floor(camera_position_.x + direction.x * distance));
            const int y = static_cast<int>(
                std::floor(camera_position_.y + direction.y * distance));
            const int z = static_cast<int>(
                std::floor(camera_position_.z + direction.z * distance));
            if (x == previous_x && y == previous_y && z == previous_z) continue;
            if (world_.block(x, y, z) != 0) {
                return {true, x, y, z, previous_x, previous_y, previous_z};
            }
            previous_x = x;
            previous_y = y;
            previous_z = z;
        }
        return {};
    }

    [[nodiscard]] XMVECTOR pointer_ray(
        const XMMATRIX& view, const XMMATRIX& projection,
        const XMVECTOR fallback_direction) const noexcept {
        POINT cursor{};
        if (!GetCursorPos(&cursor) || !ScreenToClient(window_, &cursor)) {
            return fallback_direction;
        }

        const XMVECTOR screen_near = XMVectorSet(
            static_cast<float>(cursor.x), static_cast<float>(cursor.y), 0.0F, 1.0F);
        const XMVECTOR screen_far = XMVectorSet(
            static_cast<float>(cursor.x), static_cast<float>(cursor.y), 1.0F, 1.0F);
        const XMMATRIX world = XMMatrixIdentity();
        const XMVECTOR near_world = XMVector3Unproject(
            screen_near, 0.0F, 0.0F, static_cast<float>(window_width),
            static_cast<float>(window_height), 0.0F, 1.0F, projection, view, world);
        const XMVECTOR far_world = XMVector3Unproject(
            screen_far, 0.0F, 0.0F, static_cast<float>(window_width),
            static_cast<float>(window_height), 0.0F, 1.0F, projection, view, world);
        return XMVector3Normalize(XMVectorSubtract(far_world, near_world));
    }

    bool handle_block_edits(
        const XMVECTOR ray_direction,
        const float delta_seconds) noexcept {

        const bool remove_down =
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        const bool place_down =
            (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

        bool changed = false;

        try {
            const bool has_tool =
                !player_controls_
                || !player_controls_->has_equipped_tool
                || player_controls_->has_equipped_tool();

            if (GetForegroundWindow() == window_
                && remove_down
                && has_tool) {
                const auto hit =
                    raycast_block(ray_direction);

                if (hit.found) {
                    const bool same_target =
                        mining_active_
                        && hit.x == mining_x_
                        && hit.y == mining_y_
                        && hit.z == mining_z_;

                    if (!same_target) {
                        mining_active_ = true;
                        mining_x_ = hit.x;
                        mining_y_ = hit.y;
                        mining_z_ = hit.z;
                        mining_progress_ = 0.0F;
                    }

                    float performance = 1.0F;

                    if (player_controls_
                        && player_controls_->tool_performance) {
                        performance =
                            std::clamp(
                                player_controls_->tool_performance(),
                                0.01F,
                                1.0F);
                    }

                    constexpr float base_break_seconds =
                        0.45F;

                    mining_progress_ +=
                        (delta_seconds * performance)
                        / base_break_seconds;

                    if (mining_progress_ >= 1.0F) {
                        changed =
                            world_.set_block(
                                hit.x,
                                hit.y,
                                hit.z,
                                0);

                        if (changed
                            && player_controls_
                            && player_controls_->tool_used) {
                            player_controls_->tool_used();
                        }

                        mining_active_ = false;
                        mining_progress_ = 0.0F;
                    }
                } else {
                    mining_active_ = false;
                    mining_progress_ = 0.0F;
                }
            } else {
                mining_active_ = false;
                mining_progress_ = 0.0F;
            }

            if (GetForegroundWindow() == window_
                && place_down
                && !place_was_down_) {

                const auto hit =
                    raycast_block(ray_direction);

                if (hit.found
                    && world_.block(
                        hit.placement_x,
                        hit.placement_y,
                        hit.placement_z) == 0) {

                    changed =
                        world_.set_block(
                            hit.placement_x,
                            hit.placement_y,
                            hit.placement_z,
                            1)
                        || changed;
                }
            }

        } catch (...) {
            remove_was_down_ = remove_down;
            place_was_down_ = place_down;
            mining_active_ = false;
            mining_progress_ = 0.0F;
            return false;
        }

        remove_was_down_ = remove_down;
        place_was_down_ = place_down;

        return !changed || rebuild_mesh();
    }

    bool render() noexcept {
        const auto now = std::chrono::steady_clock::now();
        const float delta_seconds =
            std::min(std::chrono::duration<float>(now - previous_frame_).count(), 0.1F);
        previous_frame_ = now;
        update_camera(delta_seconds);
        if (!update_streaming()) return false;

        const XMVECTOR position = XMLoadFloat3(&camera_position_);
        const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
            std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
            std::cos(pitch_) * std::cos(yaw_), 0.0F));
        const XMMATRIX view =
            XMMatrixLookToLH(position, forward, XMVectorSet(0, 1, 0, 0));
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(
            XMConvertToRadians(65.0F),
            static_cast<float>(window_width) / static_cast<float>(window_height),
            0.1F, 250.0F);
        if (!handle_block_edits(
                pointer_ray(view, projection, forward),
                delta_seconds))
            return false;

        // One full day currently takes four real minutes.
        // Keep the value wrapped to avoid unbounded accumulation.
        // One complete day/night cycle = 30 real minutes.
        time_of_day_ += delta_seconds / 1800.0F;
        if (time_of_day_ >= 1.0F)
            time_of_day_ -= 1.0F;

        const float solar_angle =
            time_of_day_ * XM_2PI;

        // Y is positive above the horizon.
        const float sun_elevation =
            std::sin(solar_angle);

        const float sun_horizontal =
            std::cos(solar_angle);

        const XMVECTOR direction_to_sun =
            XMVector3Normalize(
                XMVectorSet(
                    sun_horizontal * 0.78F,
                    sun_elevation,
                    sun_horizontal * 0.62F,
                    0.0F));

        // Existing lighting convention stores direction FROM sun.
        const XMVECTOR sun_direction =
            XMVectorNegate(direction_to_sun);

        const float daylight =
            std::clamp(
                (sun_elevation + 0.08F) / 0.38F,
                0.0F,
                1.0F);

        const float sunset =
            std::clamp(
                1.0F - std::abs(sun_elevation) / 0.35F,
                0.0F,
                1.0F)
            * daylight;

        const XMFLOAT3 noon_sun{
            1.00F, 0.96F, 0.86F};

        const XMFLOAT3 sunset_sun{
            1.00F, 0.46F, 0.18F};

        const XMFLOAT3 sun_color{
            noon_sun.x * (1.0F - sunset)
                + sunset_sun.x * sunset,
            noon_sun.y * (1.0F - sunset)
                + sunset_sun.y * sunset,
            noon_sun.z * (1.0F - sunset)
                + sunset_sun.z * sunset
        };

        const float sun_intensity =
            0.08F + daylight * 0.92F;

        const XMFLOAT3 night_horizon{
            0.012F, 0.018F, 0.045F};

        const XMFLOAT3 day_horizon{
            0.38F, 0.60F, 0.82F};

        const XMFLOAT3 sunset_horizon{
            0.78F, 0.31F, 0.14F};

        XMFLOAT3 horizon_color{
            night_horizon.x
                + (day_horizon.x - night_horizon.x) * daylight,
            night_horizon.y
                + (day_horizon.y - night_horizon.y) * daylight,
            night_horizon.z
                + (day_horizon.z - night_horizon.z) * daylight
        };

        horizon_color.x =
            horizon_color.x * (1.0F - sunset)
            + sunset_horizon.x * sunset;
        horizon_color.y =
            horizon_color.y * (1.0F - sunset)
            + sunset_horizon.y * sunset;
        horizon_color.z =
            horizon_color.z * (1.0F - sunset)
            + sunset_horizon.z * sunset;

        const XMFLOAT3 night_zenith{
            0.004F, 0.008F, 0.025F};

        const XMFLOAT3 day_zenith{
            0.10F, 0.32F, 0.62F};

        const XMFLOAT3 zenith_color{
            night_zenith.x
                + (day_zenith.x - night_zenith.x) * daylight,
            night_zenith.y
                + (day_zenith.y - night_zenith.y) * daylight,
            night_zenith.z
                + (day_zenith.z - night_zenith.z) * daylight
        };

        constexpr std::array<float, shadow_cascade_count>
            cascade_ranges{24.0F, 52.0F, 96.0F};

        constexpr std::array<float, shadow_cascade_count>
            cascade_sizes{34.0F, 70.0F, 132.0F};

        std::array<XMMATRIX, shadow_cascade_count>
            cascade_light_view_projection{};

        for (UINT cascade = 0;
             cascade < shadow_cascade_count;
             ++cascade) {

            // Shift wider cascades farther along the player's view
            // direction so shadow texels are spent where they matter.
            const float forward_offset =
                cascade_ranges[cascade] * 0.28F;

            const XMVECTOR cascade_center =
                XMVectorAdd(
                    position,
                    XMVectorScale(forward, forward_offset));

            XMFLOAT3 center{};
            XMStoreFloat3(&center, cascade_center);

            const XMVECTOR shadow_center =
                XMVectorSet(
                    center.x,
                    4.0F,
                    center.z,
                    1.0F);

            const XMVECTOR light_position =
                XMVectorSubtract(
                    shadow_center,
                    XMVectorScale(sun_direction, 90.0F));

            const XMMATRIX light_view =
                XMMatrixLookAtLH(
                    light_position,
                    shadow_center,
                    XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F));

            const XMMATRIX light_projection =
                XMMatrixOrthographicLH(
                    cascade_sizes[cascade],
                    cascade_sizes[cascade],
                    1.0F,
                    200.0F);

            cascade_light_view_projection[cascade] =
                light_view * light_projection;
        }

        SceneConstants constants{};

        for (UINT cascade = 0;
             cascade < shadow_cascade_count;
             ++cascade) {
            XMStoreFloat4x4(
                &constants.cascade_light_view_projection[cascade],
                XMMatrixTranspose(
                    cascade_light_view_projection[cascade]));
        }

        constants.camera_position = {
            camera_position_.x,
            camera_position_.y,
            camera_position_.z,
            1.0F
        };

        XMFLOAT3 sun_direction_float{};
        XMStoreFloat3(
            &sun_direction_float,
            sun_direction);

        constants.sun_direction = {
            sun_direction_float.x,
            sun_direction_float.y,
            sun_direction_float.z,
            0.0F
        };

        // Fog follows the lower sky instead of staying fixed blue.
        constants.fog_color_and_density = {
            horizon_color.x,
            horizon_color.y,
            horizon_color.z,
            0.00008F
        };

        constants.cascade_splits =
            {24.0F, 52.0F, 96.0F, 0.0F};

        constants.sun_color_and_intensity = {
            sun_color.x,
            sun_color.y,
            sun_color.z,
            sun_intensity
        };

        const float ambient_intensity =
            0.18F + daylight * 0.82F;

        constants.sky_ambient_and_intensity = {
            horizon_color.x * 0.42F + zenith_color.x * 0.58F,
            horizon_color.y * 0.42F + zenith_color.y * 0.58F,
            horizon_color.z * 0.42F + zenith_color.z * 0.58F,
            ambient_intensity
        };

        constexpr UINT stride = sizeof(Vertex);
        constexpr UINT offset = 0;

        ID3D11Buffer* buffers[]{
            vertex_buffer_.Get()
        };

        ID3D11Buffer* constants_buffer[]{
            scene_constants_.Get()
        };

        context_->IASetInputLayout(input_layout_.Get());
        context_->IASetVertexBuffers(
            0, 1, buffers, &stride, &offset);
        context_->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // ------------------------------------------------------
        // CASCADED SUN SHADOW DEPTH PASSES
        // ------------------------------------------------------

        ID3D11ShaderResourceView* null_shadow_resource[]{
            nullptr
        };

        context_->PSSetShaderResources(
            1, 1, null_shadow_resource);

        context_->RSSetViewports(
            1,
            &shadow_viewport_);

        context_->RSSetState(
            shadow_rasterizer_.Get());

        context_->VSSetShader(
            vertex_shader_.Get(),
            nullptr,
            0);

        context_->VSSetConstantBuffers(
            0,
            1,
            constants_buffer);

        context_->PSSetShader(
            nullptr,
            nullptr,
            0);

        if (daylight > 0.01F) {
        for (UINT cascade = 0;
             cascade < shadow_cascade_count;
             ++cascade) {

            XMStoreFloat4x4(
                &constants.view_projection,
                XMMatrixTranspose(
                    cascade_light_view_projection[cascade]));

            context_->UpdateSubresource(
                scene_constants_.Get(),
                0,
                nullptr,
                &constants,
                0,
                0);

            context_->ClearDepthStencilView(
                shadow_depth_views_[cascade].Get(),
                D3D11_CLEAR_DEPTH,
                1.0F,
                0);

            context_->OMSetRenderTargets(
                0,
                nullptr,
                shadow_depth_views_[cascade].Get());

            context_->Draw(vertex_count_, 0);
        }
        }

        // ------------------------------------------------------
        // MAIN CAMERA / PBR PASS
        // ------------------------------------------------------

        XMStoreFloat4x4(
            &constants.view_projection,
            XMMatrixTranspose(view * projection));

        context_->UpdateSubresource(
            scene_constants_.Get(),
            0,
            nullptr,
            &constants,
            0,
            0);

        constexpr float clear_color[4]{
            0.0F, 0.0F, 0.0F, 1.0F
        };

        context_->ClearRenderTargetView(
            render_target_.Get(),
            clear_color);

        context_->ClearDepthStencilView(
            depth_view_.Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
            1.0F,
            0);

        ID3D11RenderTargetView* targets[]{
            render_target_.Get()
        };

        context_->OMSetRenderTargets(
            1,
            targets,
            depth_view_.Get());

        context_->RSSetViewports(
            1,
            &viewport_);

        context_->RSSetState(
            rasterizer_.Get());

        // ------------------------------------------------------
        // PROCEDURAL SKY PASS
        // ------------------------------------------------------

        SkyConstants sky_constants{};

        const XMMATRIX inverse_view_projection =
            XMMatrixInverse(nullptr, view * projection);

        XMStoreFloat4x4(
            &sky_constants.inverse_view_projection,
            XMMatrixTranspose(inverse_view_projection));

        sky_constants.camera_position = {
            camera_position_.x,
            camera_position_.y,
            camera_position_.z,
            1.0F
        };

        sky_constants.sun_direction =
            constants.sun_direction;

        sky_constants.horizon_color = {
            horizon_color.x,
            horizon_color.y,
            horizon_color.z,
            1.0F
        };

        sky_constants.zenith_color = {
            zenith_color.x,
            zenith_color.y,
            zenith_color.z,
            1.0F
        };

        sky_constants.sun_color = {
            sun_color.x,
            sun_color.y,
            sun_color.z,
            sun_intensity
        };

        context_->UpdateSubresource(
            sky_constants_.Get(),
            0,
            nullptr,
            &sky_constants,
            0,
            0);

        ID3D11Buffer* sky_buffers[]{
            sky_constants_.Get()
        };

        context_->IASetInputLayout(nullptr);
        context_->IASetVertexBuffers(
            0, 0, nullptr, nullptr, nullptr);
        context_->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context_->VSSetShader(
            sky_vertex_shader_.Get(),
            nullptr,
            0);

        context_->PSSetShader(
            sky_pixel_shader_.Get(),
            nullptr,
            0);

        context_->PSSetConstantBuffers(
            1,
            1,
            sky_buffers);

        context_->Draw(3, 0);

        // Reset the depth buffer after the background-only sky.
        context_->ClearDepthStencilView(
            depth_view_.Get(),
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
            1.0F,
            0);

        // ------------------------------------------------------
        // TERRAIN PBR PASS
        // ------------------------------------------------------

        ID3D11ShaderResourceView* textures[]{
            block_atlas_.Get(),
            shadow_shader_resource_.Get()
        };

        ID3D11SamplerState* samplers[]{
            block_sampler_.Get(),
            shadow_sampler_.Get()
        };

        context_->IASetInputLayout(input_layout_.Get());
        context_->IASetVertexBuffers(
            0, 1, buffers, &stride, &offset);
        context_->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context_->VSSetShader(
            vertex_shader_.Get(), nullptr, 0);

        context_->VSSetConstantBuffers(
            0, 1, constants_buffer);

        context_->PSSetShader(
            pixel_shader_.Get(), nullptr, 0);

        context_->PSSetConstantBuffers(
            0, 1, constants_buffer);

        context_->PSSetShaderResources(
            0, 2, textures);

        context_->PSSetSamplers(
            0, 2, samplers);

        context_->Draw(vertex_count_, 0);

        // Unbind before the next frame uses this texture as a DSV.
        ID3D11ShaderResourceView* null_shadow[]{
            nullptr
        };

        context_->PSSetShaderResources(
            1, 1, null_shadow);

        // ------------------------------------------------------
        // STAMINA HUD
        // ------------------------------------------------------

        if (player_controls_ && player_controls_->stamina
            && player_controls_->stamina() < 99.9F) {
            const float stamina =
                std::clamp(
                    player_controls_->stamina(),
                    0.0F,
                    100.0F);

            const float stamina_fraction =
                stamina / 100.0F;

            context_->OMSetRenderTargets(
                1,
                targets,
                nullptr);

            context_->IASetInputLayout(nullptr);
            context_->IASetVertexBuffers(
                0, 0, nullptr, nullptr, nullptr);
            context_->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            context_->VSSetShader(
                sky_vertex_shader_.Get(),
                nullptr,
                0);

            context_->PSSetShader(
                hud_pixel_shader_.Get(),
                nullptr,
                0);

            ID3D11Buffer* hud_buffers[]{
                hud_constants_.Get()
            };

            context_->PSSetConstantBuffers(
                2,
                1,
                hud_buffers);

            constexpr float bar_x = 24.0F;
            constexpr float bar_y =
                static_cast<float>(window_height) - 48.0F;
            constexpr float bar_width = 240.0F;
            constexpr float bar_height = 18.0F;

            D3D11_VIEWPORT hud_viewport{};
            hud_viewport.TopLeftX = bar_x;
            hud_viewport.TopLeftY = bar_y;
            hud_viewport.Width = bar_width;
            hud_viewport.Height = bar_height;
            hud_viewport.MinDepth = 0.0F;
            hud_viewport.MaxDepth = 1.0F;

            context_->RSSetViewports(
                1,
                &hud_viewport);

            HudConstants hud_constants{};
            hud_constants.color = {
                0.04F,
                0.04F,
                0.04F,
                1.0F
            };

            context_->UpdateSubresource(
                hud_constants_.Get(),
                0,
                nullptr,
                &hud_constants,
                0,
                0);

            context_->Draw(3, 0);

            if (stamina_fraction > 0.0F) {
                hud_viewport.TopLeftX =
                    bar_x + 2.0F;
                hud_viewport.TopLeftY =
                    bar_y + 2.0F;
                hud_viewport.Width =
                    (bar_width - 4.0F)
                    * stamina_fraction;
                hud_viewport.Height =
                    bar_height - 4.0F;

                context_->RSSetViewports(
                    1,
                    &hud_viewport);

                if (stamina > 30.0F) {
                    hud_constants.color = {
                        0.18F,
                        0.78F,
                        0.28F,
                        1.0F
                    };
                } else if (stamina > 10.0F) {
                    hud_constants.color = {
                        0.92F,
                        0.65F,
                        0.12F,
                        1.0F
                    };
                } else {
                    hud_constants.color = {
                        0.85F,
                        0.16F,
                        0.12F,
                        1.0F
                    };
                }

                context_->UpdateSubresource(
                    hud_constants_.Get(),
                    0,
                    nullptr,
                    &hud_constants,
                    0,
                    0);

                context_->Draw(3, 0);
            }

            context_->RSSetViewports(
                1,
                &viewport_);
        }

        // ------------------------------------------------------
        // 3D PBR FIRST-PERSON VIEWMODEL
        // ------------------------------------------------------

        if (player_controls_
            && player_controls_->equipped_tool_name
            && player_controls_->equipped_tool_name()
                == "Basic Pick") {

            // Clear world depth only. The pick then depth-tests
            // against itself while always appearing in front of
            // world geometry, like a conventional FPS viewmodel.
            context_->ClearDepthStencilView(
                depth_view_.Get(),
                D3D11_CLEAR_DEPTH
                | D3D11_CLEAR_STENCIL,
                1.0F,
                0);

            context_->OMSetRenderTargets(
                1,
                targets,
                depth_view_.Get());

            context_->RSSetViewports(
                1,
                &viewport_);

            context_->RSSetState(
                rasterizer_.Get());

            float swing = 0.0F;

            if (mining_active_) {
                const float phase =
                    std::clamp(
                        mining_progress_,
                        0.0F,
                        1.0F);

                swing =
                    std::sin(
                        phase * XM_PI);
            }

            // Camera-space Minecraft-like placement:
            // hand/pivot is lower-right and the head leans
            // inward toward the center of the screen.
            const XMMATRIX scale =
                XMMatrixScaling(
                    0.53F,
                    0.53F,
                    0.53F);

            const XMMATRIX base_rotation =
                XMMatrixRotationX(
                    XMConvertToRadians(-18.0F))
                *
                XMMatrixRotationY(
                    XMConvertToRadians(66.0F))
                *
                XMMatrixRotationZ(
                    XMConvertToRadians(0.0F));

            // Mining is an actual rotation around the held
            // object, not a HUD translation.
            const XMMATRIX swing_rotation =
                XMMatrixRotationX(
                    XMConvertToRadians(
                        31.0F * swing))
                *
                XMMatrixRotationY(
                    XMConvertToRadians(
                        -13.0F * swing))
                *
                XMMatrixRotationZ(
                    XMConvertToRadians(
                        48.0F * swing));

            const XMMATRIX translation =
                XMMatrixTranslation(
                    0.92F
                        - 0.24F * swing,
                    -1.02F
                        + 0.10F * swing,
                    1.52F
                        + 0.04F * swing);

            const XMMATRIX model =
                scale
                * base_rotation
                * swing_rotation
                * translation;

            const XMMATRIX viewmodel_projection =
                XMMatrixPerspectiveFovLH(
                    XMConvertToRadians(70.0F),
                    static_cast<float>(
                        window_width)
                    /
                    static_cast<float>(
                        window_height),
                    0.05F,
                    10.0F);

            const XMMATRIX mvp =
                model
                * viewmodel_projection;

            ViewmodelConstants constants{};

            XMStoreFloat4x4(
                &constants.model_view_projection,
                XMMatrixTranspose(mvp));

            XMStoreFloat4x4(
                &constants.model,
                XMMatrixTranspose(model));

            // Fixed camera-space key light keeps the steel
            // readable while the PBR maps define its response.
            constants.light_direction = {
                -0.45F,
                -0.70F,
                0.55F,
                0.0F
            };

            constants.light_color = {
                1.0F,
                0.95F,
                0.88F,
                3.2F
            };

            context_->UpdateSubresource(
                viewmodel_constants_.Get(),
                0,
                nullptr,
                &constants,
                0,
                0);

            const UINT stride =
                sizeof(ViewmodelVertex);

            const UINT offset = 0;

            ID3D11Buffer* vertex_buffers[]{
                viewmodel_vertex_buffer_.Get()
            };

            context_->IASetInputLayout(
                viewmodel_input_layout_.Get());

            context_->IASetVertexBuffers(
                0,
                1,
                vertex_buffers,
                &stride,
                &offset);

            context_->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            context_->VSSetShader(
                viewmodel_vertex_shader_.Get(),
                nullptr,
                0);

            ID3D11Buffer* viewmodel_buffers[]{
                viewmodel_constants_.Get()
            };

            context_->VSSetConstantBuffers(
                3,
                1,
                viewmodel_buffers);

            context_->PSSetShader(
                viewmodel_pixel_shader_.Get(),
                nullptr,
                0);

            context_->PSSetConstantBuffers(
                3,
                1,
                viewmodel_buffers);

            ID3D11ShaderResourceView*
                viewmodel_textures[]{
                    pick_albedo_.Get(),
                    pick_normal_.Get(),
                    pick_roughness_.Get(),
                    pick_metallic_.Get()
                };

            context_->PSSetShaderResources(
                3,
                4,
                viewmodel_textures);

            ID3D11SamplerState*
                viewmodel_samplers[]{
                    viewmodel_sampler_.Get()
                };

            context_->PSSetSamplers(
                3,
                1,
                viewmodel_samplers);

            // --------------------------------------------------
            // RIGHT HAND / FOREARM
            // Shares the same mining swing pivot as the pick.
            // --------------------------------------------------

            const XMMATRIX arm_local =
                XMMatrixRotationX(
                    XMConvertToRadians(2.0F))
                *
                XMMatrixRotationZ(
                    XMConvertToRadians(-18.0F))
                *
                XMMatrixTranslation(
                    -0.02F,
                    0.02F,
                    -0.02F);

            const XMMATRIX arm_model =
                arm_local
                * scale
                * base_rotation
                * swing_rotation
                * translation;

            const XMMATRIX arm_mvp =
                arm_model
                * viewmodel_projection;

            ViewmodelConstants arm_constants{};

            XMStoreFloat4x4(
                &arm_constants.model_view_projection,
                XMMatrixTranspose(arm_mvp));

            XMStoreFloat4x4(
                &arm_constants.model,
                XMMatrixTranspose(arm_model));

            arm_constants.light_direction =
                constants.light_direction;

            arm_constants.light_color = {
                1.0F,
                0.95F,
                0.90F,
                1.45F
            };

            context_->UpdateSubresource(
                viewmodel_constants_.Get(),
                0,
                nullptr,
                &arm_constants,
                0,
                0);

            ID3D11ShaderResourceView*
                arm_textures[]{
                    arm_albedo_.Get(),
                    arm_normal_.Get(),
                    arm_roughness_.Get(),
                    arm_metallic_.Get()
                };

            context_->PSSetShaderResources(
                3,
                4,
                arm_textures);

            ID3D11Buffer* arm_buffers[]{
                viewmodel_arm_vertex_buffer_.Get()
            };

            context_->IASetVertexBuffers(
                0,
                1,
                arm_buffers,
                &stride,
                &offset);

            context_->Draw(
                viewmodel_arm_vertex_count_,
                0);

            // Restore the pick constants and buffer.
            context_->UpdateSubresource(
                viewmodel_constants_.Get(),
                0,
                nullptr,
                &constants,
                0,
                0);

            context_->IASetVertexBuffers(
                0,
                1,
                vertex_buffers,
                &stride,
                &offset);

            context_->PSSetShaderResources(
                3,
                4,
                viewmodel_textures);

            context_->Draw(
                viewmodel_vertex_count_,
                0);

            // Avoid carrying pick SRVs into later passes.
            ID3D11ShaderResourceView*
                null_viewmodel_textures[]{
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr
                };

            context_->PSSetShaderResources(
                3,
                4,
                null_viewmodel_textures);
        }

        return SUCCEEDED(
            swap_chain_->Present(1, 0));
    }

    void cleanup() noexcept {
        if (context_) context_->ClearState();
        if (window_ && IsWindow(window_)) DestroyWindow(window_);
        window_ = nullptr;
    }

    const mcr::render::PlayerControlHooks* player_controls_{nullptr};

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    ComPtr<IDXGISwapChain> swap_chain_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11RenderTargetView> render_target_;
    ComPtr<ID3D11Texture2D> depth_texture_;
    ComPtr<ID3D11DepthStencilView> depth_view_;

    ComPtr<ID3D11Texture2D> shadow_texture_;
    std::array<
        ComPtr<ID3D11DepthStencilView>,
        shadow_cascade_count> shadow_depth_views_;
    ComPtr<ID3D11ShaderResourceView> shadow_shader_resource_;

    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11VertexShader> sky_vertex_shader_;
    ComPtr<ID3D11PixelShader> sky_pixel_shader_;
    ComPtr<ID3D11PixelShader> hud_pixel_shader_;
    ComPtr<ID3D11InputLayout> input_layout_;
    ComPtr<ID3D11Buffer> vertex_buffer_;
    ComPtr<ID3D11Buffer> scene_constants_;
    ComPtr<ID3D11Buffer> sky_constants_;
    ComPtr<ID3D11Buffer> hud_constants_;
    ComPtr<ID3D11RasterizerState> rasterizer_;
    ComPtr<ID3D11RasterizerState> shadow_rasterizer_;

    ComPtr<ID3D11ShaderResourceView> block_atlas_;
    ComPtr<ID3D11SamplerState> block_sampler_;
    ComPtr<ID3D11SamplerState> shadow_sampler_;

    ComPtr<ID3D11VertexShader> viewmodel_vertex_shader_;
    ComPtr<ID3D11PixelShader> viewmodel_pixel_shader_;
    ComPtr<ID3D11InputLayout> viewmodel_input_layout_;
    ComPtr<ID3D11Buffer> viewmodel_vertex_buffer_;
    ComPtr<ID3D11Buffer> viewmodel_arm_vertex_buffer_;
    ComPtr<ID3D11Buffer> viewmodel_constants_;

    ComPtr<ID3D11ShaderResourceView> pick_albedo_;
    ComPtr<ID3D11ShaderResourceView> pick_normal_;
    ComPtr<ID3D11ShaderResourceView> pick_roughness_;
    ComPtr<ID3D11ShaderResourceView> pick_metallic_;

    ComPtr<ID3D11ShaderResourceView> arm_albedo_;
    ComPtr<ID3D11ShaderResourceView> arm_normal_;
    ComPtr<ID3D11ShaderResourceView> arm_roughness_;
    ComPtr<ID3D11ShaderResourceView> arm_metallic_;
    ComPtr<ID3D11SamplerState> viewmodel_sampler_;

    UINT viewmodel_vertex_count_{0};
    UINT viewmodel_arm_vertex_count_{0};

    D3D11_VIEWPORT viewport_{};
    D3D11_VIEWPORT shadow_viewport_{};
    UINT vertex_count_{0};
    ChunkWorld world_;
    XMFLOAT3 camera_position_{8.0F, 11.0F, -16.0F};
    float yaw_{0.0F};
    float pitch_{-0.22F};
    bool mouse_looking_{false};
    bool remove_was_down_{false};
    bool place_was_down_{false};
    bool mode_key_was_down_{false};
    bool jump_was_down_{false};
    bool fly_mode_{true};

    std::array<bool, 4> tool_slot_key_was_down_{};

    bool mining_active_{false};
    int mining_x_{0};
    int mining_y_{0};
    int mining_z_{0};
    float mining_progress_{0.0F};
    float vertical_velocity_{0.0F};

    // Starts in late morning so the first frame is immediately readable.
    float time_of_day_{0.22F};

    bool stream_center_valid_{false};
    int stream_center_x_{0};
    int stream_center_z_{0};
    POINT previous_cursor_{};
    std::chrono::steady_clock::time_point previous_frame_{};
};

} // namespace
#endif

namespace mcr::render {

bool D3D11Renderer::initialize() noexcept {
    ready_ = true;
    return true;
}

bool D3D11Renderer::run_visual_demo() noexcept {
    PlayerControlHooks controls{};
    return run_visual_demo(controls);
}

bool D3D11Renderer::run_visual_demo(
    const PlayerControlHooks& controls) noexcept {
#ifdef _WIN32
    VisualDemo demo(&controls);
    return demo.run();
#else
    (void)controls;
    return true;
#endif
}

void D3D11Renderer::shutdown() noexcept { ready_ = false; }

std::string_view D3D11Renderer::backend_name() const noexcept {
    return "DirectX 11 PBR chunk renderer";
}

} // namespace mcr::render
