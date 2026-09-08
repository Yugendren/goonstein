cbuffer _11_13 : register(b0, space1)
{
    row_major float4x4 _13_m0 : packoffset(c0);
    row_major float4x4 _13_m1 : packoffset(c4);
    float4 _13_m2 : packoffset(c8);
    float4 _13_m3 : packoffset(c9);
    float4 _13_m4 : packoffset(c10);
    float4 _13_m5 : packoffset(c11);
    float4 _13_m6 : packoffset(c12);
};


static float4 gl_Position;
static float3 _21;
static float3 _47;
static float2 _87;
static float2 _89;
static float4 _100;
static float4 _102;
static float _123;

struct SPIRV_Cross_Input
{
    float3 _21 : TEXCOORD0;
    float3 _47 : TEXCOORD1;
    float2 _89 : TEXCOORD2;
    float4 _102 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float2 _87 : TEXCOORD0;
    float4 _100 : TEXCOORD1;
    float _123 : TEXCOORD2;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 _34 = mul(mul(float4(_21, 1.0f), _13_m1), _13_m0);
    gl_Position = _34;
    _87 = (_89 * _13_m6.xy) + _13_m6.zw;
    _100 = float4((_102.xyz * _13_m2.xyz) * ((_13_m5.xyz * clamp(dot(normalize(mul(_47, float3x3(_13_m1[0].xyz, _13_m1[1].xyz, _13_m1[2].xyz))), -_13_m4.xyz), 0.0f, 1.0f)) + _13_m4.w.xxx), _102.w * _13_m2.w);
    _123 = clamp((_34.w - _13_m3.x) / (_13_m3.y - _13_m3.x), 0.0f, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _21 = stage_input._21;
    _47 = stage_input._47;
    _89 = stage_input._89;
    _102 = stage_input._102;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._87 = _87;
    stage_output._100 = _100;
    stage_output._123 = _123;
    return stage_output;
}
