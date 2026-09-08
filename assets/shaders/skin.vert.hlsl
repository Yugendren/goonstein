cbuffer _11_13 : register(b0, space1)
{
    row_major float4x4 _13_m0 : packoffset(c0);
    row_major float4x4 _13_m1 : packoffset(c4);
    float4 _13_m2 : packoffset(c8);
    float4 _13_m3 : packoffset(c9);
};

cbuffer _29_31 : register(b1, space1)
{
    row_major float4x4 _31_m0[64] : packoffset(c0);
};


static float4 gl_Position;
static float4 _21;
static uint4 _35;
static float3 _112;
static float3 _121;
static float3 _124;
static float3 _143;
static float2 _148;
static float2 _150;
static float4 _153;

struct SPIRV_Cross_Input
{
    float3 _112 : TEXCOORD0;
    float3 _143 : TEXCOORD1;
    float2 _150 : TEXCOORD2;
    uint4 _35 : TEXCOORD3;
    float4 _21 : TEXCOORD4;
};

struct SPIRV_Cross_Output
{
    float3 _121 : TEXCOORD0;
    float3 _124 : TEXCOORD1;
    float2 _148 : TEXCOORD2;
    float4 _153 : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4x4 _41 = _31_m0[_35.x] * _21.x;
    float4x4 _49 = _31_m0[_35.y] * _21.y;
    float4x4 _70 = _31_m0[_35.z] * _21.z;
    float4x4 _91 = _31_m0[_35.w] * _21.w;
    float4 _94 = ((_41[0] + _49[0]) + _70[0]) + _91[0];
    float4 _97 = ((_41[1] + _49[1]) + _70[1]) + _91[1];
    float4 _100 = ((_41[2] + _49[2]) + _70[2]) + _91[2];
    float4 _119 = mul(float4(_112, 1.0f), mul(float4x4(_94, _97, _100, ((_41[3] + _49[3]) + _70[3]) + _91[3]), _13_m1));
    _121 = _119.xyz;
    _124 = mul(_143, mul(float3x3(_94.xyz, _97.xyz, _100.xyz), float3x3(_13_m1[0].xyz, _13_m1[1].xyz, _13_m1[2].xyz)));
    _148 = _150;
    _153 = 1.0f.xxxx;
    gl_Position = mul(_119, _13_m0);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _21 = stage_input._21;
    _35 = stage_input._35;
    _112 = stage_input._112;
    _143 = stage_input._143;
    _150 = stage_input._150;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._121 = _121;
    stage_output._124 = _124;
    stage_output._148 = _148;
    stage_output._153 = _153;
    return stage_output;
}
