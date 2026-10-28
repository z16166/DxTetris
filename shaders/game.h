// game.h — HLSL shader (fxc-verified, MINE, final; no DX9 syntax allowed).
// HLSL2015 types (Texture2D/SamplerState) + object-style .Sample() (fxc 10.1 default); reserved keywords avoided.
// layout: pos/normal/uv/color stride 44 (POSITION/NORMAL/TEXCOORD0/COLOR — DirectXTK DGSLEffect input layout).
struct VSInput { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; float4 col : COLOR; };
struct VSOutput { float4 pos : SV_Position; float4 nrm : TEXCOORD1; float4 col : COLOR; float2 uv : TEXCOORD2; };
cbuffer Matrices : register(b0) { float4x4 worldViewProj; float4x4 world; float4x4 viewProj; };   // DGSL-style (unused slots ok)
cbuffer Lit : register(b1) { float4 lightColor; float4 lightDir; float4 ambient; float4 lightParams; };  // intensity, exponent, spec, rim

Texture2D tex : register(t1);
SamplerState sampler0 : register(s0);

VSOutput VSMain(VSInput vin)
{
    VSOutput o;
    o.pos = mul(worldViewProj, float4(vin.pos, 1.0f));
    o.nrm = float4(normalize(vin.nrm), 0.0f);
    o.col = vin.col;
    o.uv = vin.uv;
    return o;
}

float4 PSMain(VSOutput pin) : SV_Target
{
    float3 n = normalize(pin.nrm.xyz);
    float ndl = max(dot(n, normalize(float3(0.40f, -0.55f, -0.74f))), 0.0f);  // hard light dir (baked, consistent across faces)
    float amb = 0.30f;
    float3 albedo = pin.col.rgb * tex.Sample(sampler0, pin.uv).rgb;
    float3 lit = albedo * (amb + ndl * lightColor.rgb * lightParams.x);
    // rim (view = (0,0,-1) looking -Z: rim via fresnol dot n,v)
    float3 v = float3(0.0f, 0.0f, 1.0f);   // camera looks -Z, up Y-down: rim uses dot with normal
    float rim = pow(1.0f - min(dot(n, v), 1.0f), 2.2f);
    lit += rim * 0.22f * lightColor.rgb * lightParams.y;
    return float4(pow(lit, 1.0f / 2.2f), pin.col.a);   // alpha (locked: RGBA8 raw, no sRGB)
}
