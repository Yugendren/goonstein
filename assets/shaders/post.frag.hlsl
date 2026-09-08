cbuffer _40_42 : register(b0, space3)
{
    float4 _42_m0 : packoffset(c0);
    float4 _42_m1 : packoffset(c1);
};

Texture2D<float4> _30 : register(t0, space2);
SamplerState __30_sampler : register(s0, space2);

static float2 _33;
static float4 _106;

struct SPIRV_Cross_Input
{
    float2 _33 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _106 : SV_Target0;
};

float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float3 y)
{
    return x - y * floor(x / y);
}

float4 mod(float4 x, float4 y)
{
    return x - y * floor(x / y);
}

void frag_main()
{
    float2 _50 = floor(_33 * _42_m1.xy);
    float2 _87 = _33 - 0.5f.xx;
    _106 = float4((((_30.Sample(__30_sampler, _33).xyz + ((frac(sin(dot(_50 + (_42_m0.x * 7.309999942779541015625f).xx, float2(12.98980045318603515625f, 78.233001708984375f))) * 43758.546875f) - 0.5f) * _42_m0.y).xxx) * (1.0f - (0.0599999986588954925537109375f * mod(_50.y, 2.0f)))) * (1.0f - ((_42_m0.z * dot(_87, _87)) * 1.60000002384185791015625f))) * _42_m0.w, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _33 = stage_input._33;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._106 = _106;
    return stage_output;
}
