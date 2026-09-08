Texture2D<float4> _13 : register(t0, space2);
SamplerState __13_sampler : register(s0, space2);

static float2 _17;
static float4 _21;
static float4 _23;

struct SPIRV_Cross_Input
{
    float2 _17 : TEXCOORD0;
    float4 _23 : TEXCOORD1;
};

struct SPIRV_Cross_Output
{
    float4 _21 : SV_Target0;
};

void frag_main()
{
    float4 _19 = _13.Sample(__13_sampler, _17);
    _21 = float4(_23.xyz * _19.xyz, _23.w * _19.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _17 = stage_input._17;
    _23 = stage_input._23;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._21 = _21;
    return stage_output;
}
