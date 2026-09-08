cbuffer _40_42 : register(b0, space3)
{
    float4 _42_m0 : packoffset(c0);
};

Texture2D<float4> _13 : register(t0, space2);
SamplerState __13_sampler : register(s0, space2);

static float2 _17;
static float4 _21;
static float4 _36;
static float _50;

struct SPIRV_Cross_Input
{
    float2 _17 : TEXCOORD0;
    float4 _21 : TEXCOORD1;
    float _50 : TEXCOORD2;
};

struct SPIRV_Cross_Output
{
    float4 _36 : SV_Target0;
};

void frag_main()
{
    float4 _19 = _13.Sample(__13_sampler, _17);
    float4 _23 = _19 * _21;
    float _28 = _23.w;
    if (_28 < 0.5f)
    {
        discard;
    }
    _36 = float4(lerp(_23.xyz, _42_m0.xyz, _50.xxx), _28);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _17 = stage_input._17;
    _21 = stage_input._21;
    _50 = stage_input._50;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._36 = _36;
    return stage_output;
}
