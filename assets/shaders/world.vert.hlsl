cbuffer _97_99 : register(b0, space1)
{
    row_major float4x4 _99_m0 : packoffset(c0);
    row_major float4x4 _99_m1 : packoffset(c4);
    float4 _99_m2 : packoffset(c8);
    float4 _99_m3 : packoffset(c9);
};


static float4 gl_Position;
static float3 _106;
static float4 _115;
static float3 _155;
static float3 _159;
static float3 _162;
static float4 _165;
static float2 _197;
static float2 _221;

struct SPIRV_Cross_Input
{
    float3 _106 : TEXCOORD0;
    float3 _155 : TEXCOORD1;
    float2 _221 : TEXCOORD2;
    float4 _115 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float3 _159 : TEXCOORD0;
    float3 _162 : TEXCOORD1;
    float2 _197 : TEXCOORD2;
    float4 _165 : TEXCOORD3;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    float4 _113 = mul(float4(_106, 1.0f), _99_m1);
    float _121 = 1.0f - _115.w;
    float3 _321;
    do
    {
        if (_121 <= 0.00200000009499490261077880859375f)
        {
            _321 = 0.0f.xxx;
            break;
        }
        float2 _264 = _113.xz;
        float _268 = dot(_264, float2(0.115000002086162567138671875f, 0.061999998986721038818359375f)) - (_99_m3.y * 1.14999997615814208984375f);
        float _287 = (sin(_268) + (0.3400000035762786865234375f * sin((_268 * 2.36999988555908203125f) + 1.7000000476837158203125f))) * (0.550000011920928955078125f + (0.449999988079071044921875f * sin(dot(_264, float2(0.013500000350177288055419921875f, 0.02099999971687793731689453125f)) - (_99_m3.y * 0.20999999344348907470703125f))));
        float _291 = (_121 * _121) * 0.300000011920928955078125f;
        _321 = float3((0.87000000476837158203125f * _287) * _291, ((-abs(_287)) * _291) * 0.2199999988079071044921875f, (0.4900000095367431640625f * _287) * _291);
        break;
    } while(false);
    float3 _135 = _113.xyz + _321;
    float4 _310 = _113;
    _310.x = _135.x;
    _310.y = _135.y;
    _310.z = _135.z;
    float3 _157 = mul(_155, float3x3(_99_m1[0].xyz, _99_m1[1].xyz, _99_m1[2].xyz));
    _159 = _310.xyz;
    _162 = _157;
    _165 = _115;
    if (_99_m3.x > 0.5f)
    {
        float3 _176 = abs(normalize(_157));
        float _182 = _176.x;
        float _184 = _176.y;
        bool _185 = _182 > _184;
        bool _193;
        if (_185)
        {
            _193 = _182 > _176.z;
        }
        else
        {
            _193 = _185;
        }
        if (_193)
        {
            _197 = _310.zy * _99_m2.x;
        }
        else
        {
            if (_184 > _176.z)
            {
                _197 = _310.xz * _99_m2.x;
            }
            else
            {
                _197 = _310.xy * _99_m2.x;
            }
        }
    }
    else
    {
        _197 = (_221 * _99_m2.xy) + _99_m2.zw;
    }
    gl_Position = mul(_310, _99_m0);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _106 = stage_input._106;
    _115 = stage_input._115;
    _155 = stage_input._155;
    _221 = stage_input._221;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._159 = _159;
    stage_output._162 = _162;
    stage_output._165 = _165;
    stage_output._197 = _197;
    return stage_output;
}
