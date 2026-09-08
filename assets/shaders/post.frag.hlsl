cbuffer _74_76 : register(b0, space3)
{
    float4 _76_m0 : packoffset(c0);
    float4 _76_m1 : packoffset(c1);
    float4 _76_m2 : packoffset(c2);
    float4 _76_m3 : packoffset(c3);
    float4 _76_m4 : packoffset(c4);
    float4 _76_m5 : packoffset(c5);
};

Texture2D<float4> _61 : register(t0, space2);
SamplerState __61_sampler : register(s0, space2);
Texture2D<float4> _69 : register(t1, space2);
SamplerState __69_sampler : register(s1, space2);

static float2 _64;
static float4 _191;

struct SPIRV_Cross_Input
{
    float2 _64 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _191 : SV_Target0;
};

void frag_main()
{
    float3 _90 = (_61.Sample(__61_sampler, _64).xyz + (_69.Sample(__69_sampler, _64).xyz * _76_m3.w)) * _76_m3.x;
    float _98 = dot(_90, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
    float3 _135 = max(((((lerp(_98.xxx, _90, _76_m3.y.xxx) * _76_m5.xyz) + (_76_m4.xyz * (1.0f - clamp(_98, 0.0f, 1.0f)))) - 0.5f.xxx) * _76_m3.z) + 0.5f.xxx, 0.0f.xxx);
    float2 _142 = _64 - 0.5f.xx;
    _191 = float4(pow(max(lerp((clamp((_135 * ((_135 * 2.5099999904632568359375f) + 0.02999999932944774627685546875f.xxx)) / ((_135 * ((_135 * 2.4300000667572021484375f) + 0.589999973773956298828125f.xxx)) + 0.14000000059604644775390625f.xxx), 0.0f.xxx, 1.0f.xxx) * (1.0f - ((_76_m0.z * dot(_142, _142)) * 1.39999997615814208984375f))) + ((frac(sin(dot(floor(_64 * _76_m1.xy) + (_76_m0.x * 7.309999942779541015625f).xx, float2(12.98980045318603515625f, 78.233001708984375f))) * 43758.546875f) - 0.5f) * _76_m0.y).xxx, _76_m2.xyz, _76_m2.w.xxx) * _76_m0.w, 0.0f.xxx), 0.4545454680919647216796875f.xxx), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _64 = stage_input._64;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._191 = _191;
    return stage_output;
}
