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
static float2 _118;
static float2 _142;
static float4 _152;
static float4 _154;
static float _175;

struct SPIRV_Cross_Input
{
    float3 _21 : TEXCOORD0;
    float3 _47 : TEXCOORD1;
    float2 _142 : TEXCOORD2;
    float4 _154 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float2 _118 : TEXCOORD0;
    float4 _152 : TEXCOORD1;
    float _175 : TEXCOORD2;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 _28 = mul(float4(_21, 1.0f), _13_m1);
    float4 _34 = mul(_28, _13_m0);
    float3 _50 = normalize(mul(_47, float3x3(_13_m1[0].xyz, _13_m1[1].xyz, _13_m1[2].xyz)));
    gl_Position = _34;
    if (_13_m6.x < 0.0f)
    {
        float3 _95 = abs(_50);
        float _99 = -_13_m6.x;
        float _101 = _95.x;
        float _103 = _95.y;
        bool _104 = _101 > _103;
        bool _113;
        if (_104)
        {
            _113 = _101 > _95.z;
        }
        else
        {
            _113 = _104;
        }
        if (_113)
        {
            _118 = _28.zy * _99;
        }
        else
        {
            if (_103 > _95.z)
            {
                _118 = _28.xz * _99;
            }
            else
            {
                _118 = _28.xy * _99;
            }
        }
    }
    else
    {
        _118 = (_142 * _13_m6.xy) + _13_m6.zw;
    }
    _152 = float4((_154.xyz * _13_m2.xyz) * ((_13_m5.xyz * clamp(dot(_50, -_13_m4.xyz), 0.0f, 1.0f)) + _13_m4.w.xxx), _154.w * _13_m2.w);
    _175 = clamp((_34.w - _13_m3.x) / (_13_m3.y - _13_m3.x), 0.0f, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _21 = stage_input._21;
    _47 = stage_input._47;
    _142 = stage_input._142;
    _154 = stage_input._154;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._118 = _118;
    stage_output._152 = _152;
    stage_output._175 = _175;
    return stage_output;
}
