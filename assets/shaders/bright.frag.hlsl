cbuffer _37_39 : register(b0, space3)
{
    float4 _39_m0 : packoffset(c0);
};

Texture2D<float4> _13 : register(t0, space2);
SamplerState __13_sampler : register(s0, space2);

static float2 _17;
static float4 _77;

struct SPIRV_Cross_Input
{
    float2 _17 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _77 : SV_Target0;
};

void frag_main()
{
    float4 _20 = _13.Sample(__13_sampler, _17);
    float _35 = max(max(_20.x, _20.y), _20.z);
    float _49 = _35 - _39_m0.x;
    float _56 = clamp(_49 + _39_m0.y, 0.0f, 2.0f * _39_m0.y);
    _77 = float4(_20.xyz * (max((_56 * _56) / ((4.0f * _39_m0.y) + 9.9999997473787516355514526367188e-05f), _49) / max(_35, 9.9999997473787516355514526367188e-05f)), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _17 = stage_input._17;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._77 = _77;
    return stage_output;
}
