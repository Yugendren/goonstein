cbuffer _146_148 : register(b0, space1)
{
    row_major float4x4 _148_m0 : packoffset(c0);
    row_major float4x4 _148_m1 : packoffset(c4);
    float4 _148_m2 : packoffset(c8);
    float4 _148_m3 : packoffset(c9);
};


static float4 gl_Position;
static float4 _98;
static float4 _100;
static float4 _102;
static float4 _104;
static float3 _132;
static float4 _139;
static float3 _182;
static float3 _186;
static float3 _189;
static float4 _192;
static float4 _194;
static float2 _227;
static float2 _251;

struct SPIRV_Cross_Input
{
    float3 _132 : TEXCOORD0;
    float3 _182 : TEXCOORD1;
    float2 _251 : TEXCOORD2;
    float4 _139 : TEXCOORD3;
    float4 _98 : TEXCOORD4;
    float4 _100 : TEXCOORD5;
    float4 _102 : TEXCOORD6;
    float4 _104 : TEXCOORD7;
    float4 _194 : TEXCOORD8;
};

struct SPIRV_Cross_Output
{
    float3 _186 : TEXCOORD0;
    float3 _189 : TEXCOORD1;
    float2 _227 : TEXCOORD2;
    float4 _192 : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 _138 = mul(float4(_132, 1.0f), float4x4(_98, _100, _102, _104));
    float _145 = 1.0f - _139.w;
    float3 _352;
    do
    {
        if (_145 <= 0.00200000009499490261077880859375f)
        {
            _352 = 0.0f.xxx;
            break;
        }
        float2 _295 = _138.xz;
        float _299 = dot(_295, float2(0.115000002086162567138671875f, 0.061999998986721038818359375f)) - (_148_m3.y * 1.14999997615814208984375f);
        float _318 = (sin(_299) + (0.3400000035762786865234375f * sin((_299 * 2.36999988555908203125f) + 1.7000000476837158203125f))) * (0.550000011920928955078125f + (0.449999988079071044921875f * sin(dot(_295, float2(0.013500000350177288055419921875f, 0.02099999971687793731689453125f)) - (_148_m3.y * 0.20999999344348907470703125f))));
        float _322 = (_145 * _145) * 0.300000011920928955078125f;
        _352 = float3((0.87000000476837158203125f * _318) * _322, ((-abs(_318)) * _322) * 0.2199999988079071044921875f, (0.4900000095367431640625f * _318) * _322);
        break;
    } while(false);
    float3 _163 = _138.xyz + _352;
    float4 _341 = _138;
    _341.x = _163.x;
    _341.y = _163.y;
    _341.z = _163.z;
    float3 _184 = mul(_182, float3x3(_98.xyz, _100.xyz, _102.xyz));
    _186 = _341.xyz;
    _189 = _184;
    _192 = _139 * _194;
    if (_148_m3.x > 0.5f)
    {
        float3 _206 = abs(normalize(_184));
        float _212 = _206.x;
        float _214 = _206.y;
        bool _215 = _212 > _214;
        bool _223;
        if (_215)
        {
            _223 = _212 > _206.z;
        }
        else
        {
            _223 = _215;
        }
        if (_223)
        {
            _227 = _341.zy * _148_m2.x;
        }
        else
        {
            if (_214 > _206.z)
            {
                _227 = _341.xz * _148_m2.x;
            }
            else
            {
                _227 = _341.xy * _148_m2.x;
            }
        }
    }
    else
    {
        _227 = (_251 * _148_m2.xy) + _148_m2.zw;
    }
    gl_Position = mul(_341, _148_m0);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _98 = stage_input._98;
    _100 = stage_input._100;
    _102 = stage_input._102;
    _104 = stage_input._104;
    _132 = stage_input._132;
    _139 = stage_input._139;
    _182 = stage_input._182;
    _194 = stage_input._194;
    _251 = stage_input._251;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._186 = _186;
    stage_output._189 = _189;
    stage_output._192 = _192;
    stage_output._227 = _227;
    return stage_output;
}
