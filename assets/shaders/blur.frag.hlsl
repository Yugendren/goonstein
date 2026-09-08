static const float _60[5] = { 0.227026998996734619140625f, 0.19459460675716400146484375f, 0.121621601283550262451171875f, 0.054053999483585357666015625f, 0.01621600054204463958740234375f };

cbuffer _39_41 : register(b0, space3)
{
    float4 _41_m0 : packoffset(c0);
};

Texture2D<float4> _13 : register(t0, space2);
SamplerState __13_sampler : register(s0, space2);

static float2 _17;
static float4 _91;

struct SPIRV_Cross_Input
{
    float2 _17 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _91 : SV_Target0;
};

void frag_main()
{
    float3 _99;
    _99 = _13.Sample(__13_sampler, _17).xyz * 0.227026998996734619140625f;
    for (int _98 = 1; _98 < 5; )
    {
        float2 _49 = _41_m0.xy * float(_98);
        _99 = (_99 + (_13.Sample(__13_sampler, _17 + _49).xyz * _60[_98])) + (_13.Sample(__13_sampler, _17 - _49).xyz * _60[_98]);
        _98++;
        continue;
    }
    _91 = float4(_99, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _17 = stage_input._17;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._91 = _91;
    return stage_output;
}
