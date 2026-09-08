static float4 _403;

cbuffer _41_43 : register(b1, space3)
{
    float4 _43_m0 : packoffset(c0);
    float4 _43_m1 : packoffset(c1);
    float4 _43_m2 : packoffset(c2);
};

cbuffer _86_88 : register(b0, space3)
{
    float4 _88_m0 : packoffset(c0);
    float4 _88_m1 : packoffset(c1);
    float4 _88_m2 : packoffset(c2);
    float4 _88_m3 : packoffset(c3);
    float4 _88_m4 : packoffset(c4);
    float4 _88_m5 : packoffset(c5);
    float4 _88_m6 : packoffset(c6);
    float4 _88_m7 : packoffset(c7);
    float4 _88_m8[16] : packoffset(c8);
    float4 _88_m9[16] : packoffset(c24);
    int4 _88_m10 : packoffset(c40);
};

Texture2D<float4> _30 : register(t0, space2);
SamplerState __30_sampler : register(s0, space2);

static float2 _34;
static float4 _38;
static float3 _78;
static float3 _92;
static float4 _358;

struct SPIRV_Cross_Input
{
    float3 _92 : TEXCOORD0;
    float3 _78 : TEXCOORD1;
    float2 _34 : TEXCOORD2;
    float4 _38 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 _358 : SV_Target0;
};

void frag_main()
{
    float4 _36 = _30.Sample(__30_sampler, _34);
    float4 _49 = (_36 * _38) * _43_m0;
    float _53 = _49.w;
    if (_53 < 0.5f)
    {
        discard;
    }
    float3 _65 = pow(_49.xyz, 2.2000000476837158203125f.xxx);
    float4 _392;
    _392.x = _65.x;
    _392.y = _65.y;
    _392.z = _65.z;
    float3 _80 = normalize(_78);
    float3 _94 = _88_m0.xyz - _92;
    float _97 = length(_94);
    float3 _104 = _94 / max(_97, 9.9999997473787516355514526367188e-05f).xxx;
    float _112 = dot(_80, -_88_m1.xyz);
    float3 _151 = _88_m2.xyz * _88_m1.w;
    float3 _401;
    _401 = (_151 * (_88_m7.y + ((1.0f - _88_m7.y) * ((0.64999997615814208984375f * smoothstep(0.0500000007450580596923828125f - _88_m7.x, 0.0500000007450580596923828125f + _88_m7.x, _112)) + (0.3499999940395355224609375f * smoothstep(0.5f - _88_m7.x, 0.5f + _88_m7.x, _112)))))) + lerp(_88_m4.xyz, _88_m3.xyz, ((_80.y * 0.5f) + 0.5f).xxx);
    for (int _399 = 0; _399 < _88_m10.x; )
    {
        float3 _190 = _88_m8[_399].xyz - _92;
        float _193 = length(_190);
        float _202 = clamp(1.0f - (_193 / _88_m8[_399].w), 0.0f, 1.0f);
        _401 += ((_88_m9[_399].xyz * (_202 * _202)) * ((dot(_80, _190 / max(_193, 9.9999997473787516355514526367188e-05f).xxx) * 0.5f) + 0.5f));
        _399++;
        continue;
    }
    float3 _363 = lerp(lerp((_392.xyz * _401) + _43_m1.xyz, (_392.xyz * (1.10000002384185791015625f.xxx + _43_m1.xyz)) * (0.75f + (0.25f * clamp(dot(_88_m3.xyz + _151, 0.3300000131130218505859375f.xxx), 0.0f, 1.0f))), _43_m1.w.xxx) + (((_43_m2.xyz * _43_m2.w) * pow(1.0f - clamp(dot(_80, _104), 0.0f, 1.0f), _88_m7.z)) * (0.4000000059604644775390625f + (0.60000002384185791015625f * clamp(_112 + 0.5f, 0.0f, 1.0f)))), _88_m5.xyz + (_88_m2.xyz * (pow(clamp(dot(-_104, _88_m1.xyz), 0.0f, 1.0f), 6.0f) * _88_m6.z)), clamp((1.0f - exp((-max(_97 - _88_m6.w, 0.0f)) * _88_m5.w)) * exp((-max(_92.y - _88_m6.x, 0.0f)) * _88_m6.y), 0.0f, 1.0f).xxx);
    _358 = float4(_363, _53);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _34 = stage_input._34;
    _38 = stage_input._38;
    _78 = stage_input._78;
    _92 = stage_input._92;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._358 = _358;
    return stage_output;
}
