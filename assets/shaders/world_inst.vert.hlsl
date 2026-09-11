cbuffer _82_84 : register(b0, space1)
{
    row_major float4x4 _84_m0 : packoffset(c0);
    row_major float4x4 _84_m1 : packoffset(c4);
    float4 _84_m2 : packoffset(c8);
    float4 _84_m3 : packoffset(c9);
};


static float4 gl_Position;
static float4 _12;
static float4 _14;
static float4 _16;
static float4 _18;
static float3 _48;
static float3 _66;
static float3 _70;
static float3 _73;
static float4 _76;
static float4 _77;
static float4 _79;
static float2 _125;
static float2 _149;

struct SPIRV_Cross_Input
{
    float3 _48 : TEXCOORD0;
    float3 _66 : TEXCOORD1;
    float2 _149 : TEXCOORD2;
    float4 _77 : TEXCOORD3;
    float4 _12 : TEXCOORD4;
    float4 _14 : TEXCOORD5;
    float4 _16 : TEXCOORD6;
    float4 _18 : TEXCOORD7;
    float4 _79 : TEXCOORD8;
};

struct SPIRV_Cross_Output
{
    float3 _70 : TEXCOORD0;
    float3 _73 : TEXCOORD1;
    float2 _125 : TEXCOORD2;
    float4 _76 : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 _54 = mul(float4(_48, 1.0f), float4x4(_12, _14, _16, _18));
    float3 _68 = mul(_66, float3x3(_12.xyz, _14.xyz, _16.xyz));
    _70 = _54.xyz;
    _73 = _68;
    _76 = _77 * _79;
    if (_84_m3.x > 0.5f)
    {
        float3 _100 = abs(normalize(_68));
        float _107 = _100.x;
        float _110 = _100.y;
        bool _111 = _107 > _110;
        bool _120;
        if (_111)
        {
            _120 = _107 > _100.z;
        }
        else
        {
            _120 = _111;
        }
        if (_120)
        {
            _125 = _54.zy * _84_m2.x;
        }
        else
        {
            if (_110 > _100.z)
            {
                _125 = _54.xz * _84_m2.x;
            }
            else
            {
                _125 = _54.xy * _84_m2.x;
            }
        }
    }
    else
    {
        _125 = (_149 * _84_m2.xy) + _84_m2.zw;
    }
    gl_Position = mul(_54, _84_m0);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _12 = stage_input._12;
    _14 = stage_input._14;
    _16 = stage_input._16;
    _18 = stage_input._18;
    _48 = stage_input._48;
    _66 = stage_input._66;
    _77 = stage_input._77;
    _79 = stage_input._79;
    _149 = stage_input._149;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._70 = _70;
    stage_output._73 = _73;
    stage_output._76 = _76;
    stage_output._125 = _125;
    return stage_output;
}
