cbuffer _41_43 : register(b1, space3)
{
    float4 _43_m0 : packoffset(c0);
    float4 _43_m1 : packoffset(c1);
    float4 _43_m2 : packoffset(c2);
    float4 _43_m3 : packoffset(c3);
};

cbuffer _87_89 : register(b0, space3)
{
    float4 _89_m0 : packoffset(c0);
    float4 _89_m1 : packoffset(c1);
    float4 _89_m2 : packoffset(c2);
    float4 _89_m3 : packoffset(c3);
    float4 _89_m4 : packoffset(c4);
    float4 _89_m5 : packoffset(c5);
    float4 _89_m6 : packoffset(c6);
    float4 _89_m7 : packoffset(c7);
    float4 _89_m8[16] : packoffset(c8);
    float4 _89_m9[16] : packoffset(c24);
    int4 _89_m10 : packoffset(c40);
    row_major float4x4 _89_m11 : packoffset(c41);
    float4 _89_m12 : packoffset(c45);
};

Texture2D<float4> _30 : register(t0, space2);
SamplerState __30_sampler : register(s0, space2);
Texture2D<float4> _398 : register(t1, space2);
SamplerState __398_sampler : register(s1, space2);

static float2 _34;
static float4 _38;
static float3 _78;
static float3 _93;
static float4 _731;

struct SPIRV_Cross_Input
{
    float3 _93 : TEXCOORD0;
    float3 _78 : TEXCOORD1;
    float2 _34 : TEXCOORD2;
    float4 _38 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 _731 : SV_Target0;
};

void frag_main()
{
    float4 _36 = _30.Sample(__30_sampler, _34);
    float4 _49 = (_36 * _38) * _43_m0;
    if (_49.w < 0.5f)
    {
        discard;
    }
    float3 _65 = pow(_49.xyz, 2.2000000476837158203125f.xxx);
    float4 _765 = _49;
    _765.x = _65.x;
    _765.y = _65.y;
    _765.z = _65.z;
    float3 _95 = _89_m0.xyz - _93;
    float _98 = length(_95);
    float3 _105 = _95 / max(_98, 9.9999997473787516355514526367188e-05f).xxx;
    bool _116 = _43_m3.x > 0.5f;
    float3 _785;
    float4 _805;
    float _814;
    if (_116)
    {
        float _139 = dot(_93.xz, float2(0.860129058361053466796875f, 0.5100765228271484375f));
        float _148 = dot(_93.xz, float2(-0.4190581738948822021484375f, 0.907959401607513427734375f));
        float _201 = 1.0f - _30.Sample(__30_sampler, clamp((_93.xz - _43_m3.yz) * _43_m3.w, 0.0f.xx, 1.0f.xx)).x;
        float2 _230 = (((float2(0.860129058361053466796875f, 0.5100765228271484375f) * cos(((_139 * 0.550000011920928955078125f) + (_89_m7.w * 1.10000002384185791015625f)) + (sin((_148 * 0.20999999344348907470703125f) - (_89_m7.w * 0.3499999940395355224609375f)) * 2.0f))) * 0.04500000178813934326171875f) + ((float2(-0.4190581738948822021484375f, 0.907959401607513427734375f) * cos(((_148 * 1.2999999523162841796875f) - (_89_m7.w * 1.7000000476837158203125f)) + (sin((_139 * 0.37000000476837158203125f) + (_89_m7.w * 0.5f)) * 1.60000002384185791015625f))) * 0.0280000008642673492431640625f)) * (lerp(1.0f, 0.300000011920928955078125f, _201) * clamp(1.0f - ((_98 - 20.0f) * 0.011111111380159854888916015625f), 0.119999997317790985107421875f, 1.0f));
        _814 = _201;
        _805 = float4(pow(_43_m0.xyz, 2.2000000476837158203125f.xxx) * lerp(1.0f, 2.2999999523162841796875f, _201 * _201), 1.0f);
        _785 = normalize(float3(-_230.x, 1.0f, -_230.y));
    }
    else
    {
        _814 = 0.0f;
        _805 = _765;
        _785 = normalize(_78);
    }
    float _260 = dot(_785, -_89_m1.xyz);
    float _786;
    if (_116)
    {
        _786 = _89_m7.y + ((1.0f - _89_m7.y) * clamp((_260 * 0.5f) + 0.5f, 0.0f, 1.0f));
    }
    else
    {
        _786 = _89_m7.y + ((1.0f - _89_m7.y) * ((0.64999997615814208984375f * smoothstep(0.0500000007450580596923828125f - _89_m7.x, 0.0500000007450580596923828125f + _89_m7.x, _260)) + (0.3499999940395355224609375f * smoothstep(0.5f - _89_m7.x, 0.5f + _89_m7.x, _260))));
    }
    float _794;
    if (_89_m12.z > 0.0f)
    {
        float4 _328 = mul(float4(_93 + ((_785 * _89_m12.y) * 2.0f), 1.0f), _89_m11);
        float3 _335 = _328.xyz / _328.w.xxx;
        float2 _341 = (_335.xy * 0.5f) + 0.5f.xx;
        float _343 = _341.y;
        float _344 = 1.0f - _343;
        float2 _776 = _341;
        _776.y = _344;
        float _348 = _341.x;
        float _359 = min(min(_348, 1.0f - _348), min(_344, _343));
        bool _361 = _359 > 0.0f;
        bool _367;
        if (_361)
        {
            _367 = _335.z < 1.0f;
        }
        else
        {
            _367 = _361;
        }
        float _795;
        if (_367)
        {
            float _377 = _89_m12.y * (1.5f - clamp(_260, 0.0f, 1.0f));
            int _788;
            float _789;
            _789 = 0.0f;
            _788 = -1;
            float _829;
            for (; _788 <= 1; _789 = _829, _788++)
            {
                _829 = _789;
                for (int _827 = -1; _827 <= 1; )
                {
                    _829 += float((_335.z - _377) > _398.Sample(__398_sampler, _776 + (float2(float(_827), float(_788)) * _89_m12.x)).x);
                    _827++;
                    continue;
                }
            }
            _795 = 1.0f - (((_789 * 0.111111111938953399658203125f) * _89_m12.z) * clamp(_359 / _89_m12.w, 0.0f, 1.0f));
        }
        else
        {
            _795 = 1.0f;
        }
        _794 = _795;
    }
    else
    {
        _794 = 1.0f;
    }
    float3 _453 = _89_m2.xyz * _89_m1.w;
    float3 _809;
    _809 = (_453 * lerp(_89_m7.y, _786, _794)) + (lerp(_89_m4.xyz, _89_m3.xyz, ((_785.y * 0.5f) + 0.5f).xxx) * (0.4000000059604644775390625f + (0.60000002384185791015625f * _794)));
    for (int _801 = 0; _801 < _89_m10.x; )
    {
        float3 _496 = _89_m8[_801].xyz - _93;
        float _499 = length(_496);
        float _507 = clamp(1.0f - (_499 / _89_m8[_801].w), 0.0f, 1.0f);
        _809 += ((_89_m9[_801].xyz * (_507 * _507)) * ((dot(_785, _496 / max(_499, 9.9999997473787516355514526367188e-05f).xxx) * 0.5f) + 0.5f));
        _801++;
        continue;
    }
    float3 _602 = lerp((_805.xyz * _809) + _43_m1.xyz, (_805.xyz * (1.10000002384185791015625f.xxx + _43_m1.xyz)) * (0.75f + (0.25f * clamp(dot(_89_m3.xyz + _453, 0.3300000131130218505859375f.xxx), 0.0f, 1.0f))), _43_m1.w.xxx) + (((_43_m2.xyz * _43_m2.w) * pow(1.0f - clamp(dot(_785, _105), 0.0f, 1.0f), _89_m7.z)) * (0.4000000059604644775390625f + (0.60000002384185791015625f * clamp(_260 + 0.5f, 0.0f, 1.0f))));
    float3 _825;
    if (_116)
    {
        _825 = (lerp(_602, _89_m5.xyz + (_89_m3.xyz * 0.5f), ((0.119999997317790985107421875f + (0.5f * pow(1.0f - clamp(dot(normalize(lerp(float3(0.0f, 1.0f, 0.0f), _785, 0.60000002384185791015625f.xxx)), _105), 0.0f, 1.0f), 3.0f))) * (1.0f - (_814 * 0.699999988079071044921875f))).xxx) + ((_453 * pow(clamp(dot(_785, normalize(_105 - _89_m1.xyz)), 0.0f, 1.0f), 90.0f)) * 1.2000000476837158203125f)) * 1.0f;
    }
    else
    {
        _825 = _602;
    }
    _731 = float4(lerp(_825, _89_m5.xyz + (_89_m2.xyz * (pow(clamp(dot(-_105, _89_m1.xyz), 0.0f, 1.0f), 6.0f) * _89_m6.z)), clamp((1.0f - exp((-max(_98 - _89_m6.w, 0.0f)) * _89_m5.w)) * exp((-max(_93.y - _89_m6.x, 0.0f)) * _89_m6.y), 0.0f, 1.0f).xxx), _805.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _34 = stage_input._34;
    _38 = stage_input._38;
    _78 = stage_input._78;
    _93 = stage_input._93;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._731 = _731;
    return stage_output;
}
