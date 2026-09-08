cbuffer _11_13 : register(b0, space1)
{
    row_major float4x4 _13_m0 : packoffset(c0);
    row_major float4x4 _13_m1 : packoffset(c4);
    float4 _13_m2 : packoffset(c8);
    float4 _13_m3 : packoffset(c9);
};


static float4 gl_Position;
static float3 _21;
static float3 _41;
static float3 _45;
static float3 _48;
static float4 _51;
static float4 _53;
static float2 _94;
static float2 _118;

struct SPIRV_Cross_Input
{
    float3 _21 : TEXCOORD0;
    float3 _41 : TEXCOORD1;
    float2 _118 : TEXCOORD2;
    float4 _53 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float3 _45 : TEXCOORD0;
    float3 _48 : TEXCOORD1;
    float2 _94 : TEXCOORD2;
    float4 _51 : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 _28 = mul(float4(_21, 1.0f), _13_m1);
    float3 _43 = mul(_41, float3x3(_13_m1[0].xyz, _13_m1[1].xyz, _13_m1[2].xyz));
    _45 = _28.xyz;
    _48 = _43;
    _51 = _53;
    if (_13_m3.x > 0.5f)
    {
        float3 _69 = abs(normalize(_43));
        float _76 = _69.x;
        float _79 = _69.y;
        bool _80 = _76 > _79;
        bool _89;
        if (_80)
        {
            _89 = _76 > _69.z;
        }
        else
        {
            _89 = _80;
        }
        if (_89)
        {
            _94 = _28.zy * _13_m2.x;
        }
        else
        {
            if (_79 > _69.z)
            {
                _94 = _28.xz * _13_m2.x;
            }
            else
            {
                _94 = _28.xy * _13_m2.x;
            }
        }
    }
    else
    {
        _94 = (_118 * _13_m2.xy) + _13_m2.zw;
    }
    gl_Position = mul(_28, _13_m0);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _21 = stage_input._21;
    _41 = stage_input._41;
    _53 = stage_input._53;
    _118 = stage_input._118;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._45 = _45;
    stage_output._48 = _48;
    stage_output._51 = _51;
    stage_output._94 = _94;
    return stage_output;
}
