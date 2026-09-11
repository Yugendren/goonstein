cbuffer _42_44 : register(b1, space3)
{
    float4 _44_m0 : packoffset(c0);
    float4 _44_m1 : packoffset(c1);
    float4 _44_m2 : packoffset(c2);
    float4 _44_m3 : packoffset(c3);
    float4 _44_m4 : packoffset(c4);
};

cbuffer _111_113 : register(b0, space3)
{
    float4 _113_m0 : packoffset(c0);
    float4 _113_m1 : packoffset(c1);
    float4 _113_m2 : packoffset(c2);
    float4 _113_m3 : packoffset(c3);
    float4 _113_m4 : packoffset(c4);
    float4 _113_m5 : packoffset(c5);
    float4 _113_m6 : packoffset(c6);
    float4 _113_m7 : packoffset(c7);
    float4 _113_m8[16] : packoffset(c8);
    float4 _113_m9[16] : packoffset(c24);
    int4 _113_m10 : packoffset(c40);
    row_major float4x4 _113_m11 : packoffset(c41);
    float4 _113_m12 : packoffset(c45);
};

Texture2D<float4> _30 : register(t0, space2);
SamplerState __30_sampler : register(s0, space2);
Texture2D<float4> _445 : register(t1, space2);
SamplerState __445_sampler : register(s1, space2);

static float2 _34;
static float4 _59;
static float3 _102;
static float3 _117;
static float4 _754;

struct SPIRV_Cross_Input
{
    float3 _117 : TEXCOORD0;
    float3 _102 : TEXCOORD1;
    float2 _34 : TEXCOORD2;
    float4 _59 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 _754 : SV_Target0;
};

void frag_main()
{
    float4 _37 = _30.SampleBias(__30_sampler, _34, -0.5f);
    float _72 = _37.w * _44_m0.w;
    float4 _76 = float4((lerp(_37.xyz, _44_m4.xyz, _44_m4.w.xxx) * _59.xyz) * _44_m0.xyz, _72);
    if (_72 < 0.5f)
    {
        discard;
    }
    float3 _89 = pow(_76.xyz, 2.2000000476837158203125f.xxx);
    float4 _789 = _76;
    _789.x = _89.x;
    _789.y = _89.y;
    _789.z = _89.z;
    float3 _119 = _113_m0.xyz - _117;
    float _122 = length(_119);
    float3 _129 = _119 / max(_122, 9.9999997473787516355514526367188e-05f).xxx;
    bool _139 = _44_m3.x > 0.5f;
    float3 _810;
    float4 _830;
    float _839;
    float _846;
    float _853;
    if (_139)
    {
        float _162 = dot(_117.xz, float2(0.860129058361053466796875f, 0.5100765228271484375f));
        float _171 = dot(_117.xz, float2(-0.4190581738948822021484375f, 0.907959401607513427734375f));
        float _181 = ((_162 * 0.550000011920928955078125f) + (_113_m7.w * 1.10000002384185791015625f)) + (sin((_171 * 0.20999999344348907470703125f) - (_113_m7.w * 0.3499999940395355224609375f)) * 2.0f);
        float _203 = ((_171 * 1.2999999523162841796875f) - (_113_m7.w * 1.7000000476837158203125f)) + (sin((_162 * 0.37000000476837158203125f) + (_113_m7.w * 0.5f)) * 1.60000002384185791015625f);
        float4 _221 = _30.Sample(__30_sampler, clamp((_117.xz - _44_m3.yz) * _44_m3.w, 0.0f.xx, 1.0f.xx));
        float _224 = 1.0f - _221.x;
        float _237 = lerp(1.0f, 0.300000011920928955078125f, _224) * clamp(1.0f - ((_122 - 20.0f) * 0.011111111380159854888916015625f), 0.119999997317790985107421875f, 1.0f);
        float2 _253 = (((float2(0.860129058361053466796875f, 0.5100765228271484375f) * cos(_181)) * 0.04500000178813934326171875f) + ((float2(-0.4190581738948822021484375f, 0.907959401607513427734375f) * cos(_203)) * 0.0280000008642673492431640625f)) * _237;
        _853 = _221.y * smoothstep(0.1500000059604644775390625f, 0.550000011920928955078125f, 0.5f + (0.5f * sin((_203 * 0.800000011920928955078125f) + (_113_m7.w * 0.89999997615814208984375f))));
        _846 = (0.5f * (sin(_181) + sin(_203))) * _237;
        _839 = _224;
        _830 = float4(pow(_44_m0.xyz, 2.2000000476837158203125f.xxx) * lerp(1.0f, 2.2999999523162841796875f, _224 * _224), 1.0f);
        _810 = normalize(float3(-_253.x, 1.0f, -_253.y));
    }
    else
    {
        _853 = 0.0f;
        _846 = 0.0f;
        _839 = 0.0f;
        _830 = _789;
        _810 = normalize(_102);
    }
    float _307 = dot(_810, -_113_m1.xyz);
    float _811;
    if (_139)
    {
        _811 = _113_m7.y + ((1.0f - _113_m7.y) * clamp((_307 * 0.5f) + 0.5f, 0.0f, 1.0f));
    }
    else
    {
        _811 = _113_m7.y + ((1.0f - _113_m7.y) * ((0.64999997615814208984375f * smoothstep(0.0500000007450580596923828125f - _113_m7.x, 0.0500000007450580596923828125f + _113_m7.x, _307)) + (0.3499999940395355224609375f * smoothstep(0.5f - _113_m7.x, 0.5f + _113_m7.x, _307))));
    }
    float _819;
    if (_113_m12.z > 0.0f)
    {
        float4 _375 = mul(float4(_117 + ((_810 * _113_m12.y) * 2.0f), 1.0f), _113_m11);
        float3 _382 = _375.xyz / _375.w.xxx;
        float2 _388 = (_382.xy * 0.5f) + 0.5f.xx;
        float _390 = _388.y;
        float _391 = 1.0f - _390;
        float2 _801 = _388;
        _801.y = _391;
        float _395 = _388.x;
        float _406 = min(min(_395, 1.0f - _395), min(_391, _390));
        bool _408 = _406 > 0.0f;
        bool _414;
        if (_408)
        {
            _414 = _382.z < 1.0f;
        }
        else
        {
            _414 = _408;
        }
        float _820;
        if (_414)
        {
            float _424 = _113_m12.y * (1.5f - clamp(_307, 0.0f, 1.0f));
            int _813;
            float _814;
            _814 = 0.0f;
            _813 = -1;
            float _861;
            for (; _813 <= 1; _814 = _861, _813++)
            {
                _861 = _814;
                for (int _859 = -1; _859 <= 1; )
                {
                    _861 += float((_382.z - _424) > _445.Sample(__445_sampler, _801 + (float2(float(_859), float(_813)) * _113_m12.x)).x);
                    _859++;
                    continue;
                }
            }
            _820 = 1.0f - (((_814 * 0.111111111938953399658203125f) * _113_m12.z) * clamp(_406 / _113_m12.w, 0.0f, 1.0f));
        }
        else
        {
            _820 = 1.0f;
        }
        _819 = _820;
    }
    else
    {
        _819 = 1.0f;
    }
    float3 _500 = _113_m2.xyz * _113_m1.w;
    float3 _834;
    _834 = (_500 * lerp(_113_m7.y, _811, _819)) + (lerp(_113_m4.xyz, _113_m3.xyz, ((_810.y * 0.5f) + 0.5f).xxx) * (0.4000000059604644775390625f + (0.60000002384185791015625f * _819)));
    for (int _826 = 0; _826 < _113_m10.x; )
    {
        float3 _542 = _113_m8[_826].xyz - _117;
        float _545 = length(_542);
        float _553 = clamp(1.0f - (_545 / _113_m8[_826].w), 0.0f, 1.0f);
        _834 += ((_113_m9[_826].xyz * (_553 * _553)) * ((dot(_810, _542 / max(_545, 9.9999997473787516355514526367188e-05f).xxx) * 0.5f) + 0.5f));
        _826++;
        continue;
    }
    float3 _648 = lerp((_830.xyz * _834) + _44_m1.xyz, (_830.xyz * (1.10000002384185791015625f.xxx + _44_m1.xyz)) * (0.75f + (0.25f * clamp(dot(_113_m3.xyz + _500, 0.3300000131130218505859375f.xxx), 0.0f, 1.0f))), _44_m1.w.xxx) + (((_44_m2.xyz * _44_m2.w) * pow(1.0f - clamp(dot(_810, _129), 0.0f, 1.0f), _113_m7.z)) * (0.4000000059604644775390625f + (0.60000002384185791015625f * clamp(_307 + 0.5f, 0.0f, 1.0f))));
    float3 _857;
    if (_139)
    {
        _857 = (lerp(_648, _113_m5.xyz + (_113_m3.xyz * 0.5f), ((0.119999997317790985107421875f + (0.5f * pow(1.0f - clamp(dot(normalize(lerp(float3(0.0f, 1.0f, 0.0f), _810, 0.07999999821186065673828125f.xxx)), _129), 0.0f, 1.0f), 3.0f))) * (1.0f - (_839 * 0.699999988079071044921875f))).xxx) * (1.0f + (_846 * 0.0900000035762786865234375f))) + ((float3(0.75f, 0.85000002384185791015625f, 0.89999997615814208984375f) * _853) * 0.300000011920928955078125f);
    }
    else
    {
        _857 = _648;
    }
    _754 = float4(lerp(_857, _113_m5.xyz + (_113_m2.xyz * (pow(clamp(dot(-_129, _113_m1.xyz), 0.0f, 1.0f), 6.0f) * _113_m6.z)), clamp((1.0f - exp((-max(_122 - _113_m6.w, 0.0f)) * _113_m5.w)) * exp((-max(_117.y - _113_m6.x, 0.0f)) * _113_m6.y), 0.0f, 1.0f).xxx), _830.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _34 = stage_input._34;
    _59 = stage_input._59;
    _102 = stage_input._102;
    _117 = stage_input._117;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._754 = _754;
    return stage_output;
}
