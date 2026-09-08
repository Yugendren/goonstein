cbuffer _28_30 : register(b0, space1)
{
    row_major float4x4 _30_m0 : packoffset(c0);
};


static float4 gl_Position;
static float2 _9;
static float2 _11;
static float4 _15;
static float4 _17;
static float3 _36;

struct SPIRV_Cross_Input
{
    float3 _36 : TEXCOORD0;
    float2 _11 : TEXCOORD1;
    float4 _17 : TEXCOORD2;
};

struct SPIRV_Cross_Output
{
    float2 _9 : TEXCOORD0;
    float4 _15 : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    _9 = _11;
    _15 = _17;
    gl_Position = mul(float4(_36, 1.0f), _30_m0);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _11 = stage_input._11;
    _17 = stage_input._17;
    _36 = stage_input._36;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._9 = _9;
    stage_output._15 = _15;
    return stage_output;
}
