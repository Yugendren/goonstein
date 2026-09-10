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
Texture2D<float4> _422 : register(t1, space2);
SamplerState __422_sampler : register(s1, space2);

static float2 _34;
static float4 _38;
static float3 _78;
static float3 _93;
static float4 _732;

struct SPIRV_Cross_Input
{
    float3 _93 : TEXCOORD0;
    float3 _78 : TEXCOORD1;
    float2 _34 : TEXCOORD2;
    float4 _38 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 _732 : SV_Target0;
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
    float4 _766 = _49;
    _766.x = _65.x;
    _766.y = _65.y;
    _766.z = _65.z;
    float3 _95 = _89_m0.xyz - _93;
    float _98 = length(_95);
    float3 _105 = _95 / max(_98, 9.9999997473787516355514526367188e-05f).xxx;
    bool _116 = _43_m3.x > 0.5f;
    float3 _787;
    float4 _807;
    float _816;
    float _823;
    float _830;
    if (_116)
    {
        float _139 = dot(_93.xz, float2(0.860129058361053466796875f, 0.5100765228271484375f));
        float _148 = dot(_93.xz, float2(-0.4190581738948822021484375f, 0.907959401607513427734375f));
        float _158 = ((_139 * 0.550000011920928955078125f) + (_89_m7.w * 1.10000002384185791015625f)) + (sin((_148 * 0.20999999344348907470703125f) - (_89_m7.w * 0.3499999940395355224609375f)) * 2.0f);
        float _180 = ((_148 * 1.2999999523162841796875f) - (_89_m7.w * 1.7000000476837158203125f)) + (sin((_139 * 0.37000000476837158203125f) + (_89_m7.w * 0.5f)) * 1.60000002384185791015625f);
        float4 _198 = _30.Sample(__30_sampler, clamp((_93.xz - _43_m3.yz) * _43_m3.w, 0.0f.xx, 1.0f.xx));
        float _201 = 1.0f - _198.x;
        float _214 = lerp(1.0f, 0.300000011920928955078125f, _201) * clamp(1.0f - ((_98 - 20.0f) * 0.011111111380159854888916015625f), 0.119999997317790985107421875f, 1.0f);
        float2 _230 = (((float2(0.860129058361053466796875f, 0.5100765228271484375f) * cos(_158)) * 0.04500000178813934326171875f) + ((float2(-0.4190581738948822021484375f, 0.907959401607513427734375f) * cos(_180)) * 0.0280000008642673492431640625f)) * _214;
        _830 = _198.y * smoothstep(0.1500000059604644775390625f, 0.550000011920928955078125f, 0.5f + (0.5f * sin((_180 * 0.800000011920928955078125f) + (_89_m7.w * 0.89999997615814208984375f))));
        _823 = (0.5f * (sin(_158) + sin(_180))) * _214;
        _816 = _201;
        _807 = float4(pow(_43_m0.xyz, 2.2000000476837158203125f.xxx) * lerp(1.0f, 2.2999999523162841796875f, _201 * _201), 1.0f);
        _787 = normalize(float3(-_230.x, 1.0f, -_230.y));
    }
    else
    {
        _830 = 0.0f;
        _823 = 0.0f;
        _816 = 0.0f;
        _807 = _766;
        _787 = normalize(_78);
    }
    float _284 = dot(_787, -_89_m1.xyz);
    float _788;
    if (_116)
    {
        _788 = _89_m7.y + ((1.0f - _89_m7.y) * clamp((_284 * 0.5f) + 0.5f, 0.0f, 1.0f));
    }
    else
    {
        _788 = _89_m7.y + ((1.0f - _89_m7.y) * ((0.64999997615814208984375f * smoothstep(0.0500000007450580596923828125f - _89_m7.x, 0.0500000007450580596923828125f + _89_m7.x, _284)) + (0.3499999940395355224609375f * smoothstep(0.5f - _89_m7.x, 0.5f + _89_m7.x, _284))));
    }
    float _796;
    if (_89_m12.z > 0.0f)
    {
        float4 _352 = mul(float4(_93 + ((_787 * _89_m12.y) * 2.0f), 1.0f), _89_m11);
        float3 _359 = _352.xyz / _352.w.xxx;
        float2 _365 = (_359.xy * 0.5f) + 0.5f.xx;
        float _367 = _365.y;
        float _368 = 1.0f - _367;
        float2 _778 = _365;
        _778.y = _368;
        float _372 = _365.x;
        float _383 = min(min(_372, 1.0f - _372), min(_368, _367));
        bool _385 = _383 > 0.0f;
        bool _391;
        if (_385)
        {
            _391 = _359.z < 1.0f;
        }
        else
        {
            _391 = _385;
        }
        float _797;
        if (_391)
        {
            float _401 = _89_m12.y * (1.5f - clamp(_284, 0.0f, 1.0f));
            int _790;
            float _791;
            _791 = 0.0f;
            _790 = -1;
            float _838;
            for (; _790 <= 1; _791 = _838, _790++)
            {
                _838 = _791;
                for (int _836 = -1; _836 <= 1; )
                {
                    _838 += float((_359.z - _401) > _422.Sample(__422_sampler, _778 + (float2(float(_836), float(_790)) * _89_m12.x)).x);
                    _836++;
                    continue;
                }
            }
            _797 = 1.0f - (((_791 * 0.111111111938953399658203125f) * _89_m12.z) * clamp(_383 / _89_m12.w, 0.0f, 1.0f));
        }
        else
        {
            _797 = 1.0f;
        }
        _796 = _797;
    }
    else
    {
        _796 = 1.0f;
    }
    float3 _477 = _89_m2.xyz * _89_m1.w;
    float3 _811;
    _811 = (_477 * lerp(_89_m7.y, _788, _796)) + (lerp(_89_m4.xyz, _89_m3.xyz, ((_787.y * 0.5f) + 0.5f).xxx) * (0.4000000059604644775390625f + (0.60000002384185791015625f * _796)));
    for (int _803 = 0; _803 < _89_m10.x; )
    {
        float3 _520 = _89_m8[_803].xyz - _93;
        float _523 = length(_520);
        float _531 = clamp(1.0f - (_523 / _89_m8[_803].w), 0.0f, 1.0f);
        _811 += ((_89_m9[_803].xyz * (_531 * _531)) * ((dot(_787, _520 / max(_523, 9.9999997473787516355514526367188e-05f).xxx) * 0.5f) + 0.5f));
        _803++;
        continue;
    }
    float3 _626 = lerp((_807.xyz * _811) + _43_m1.xyz, (_807.xyz * (1.10000002384185791015625f.xxx + _43_m1.xyz)) * (0.75f + (0.25f * clamp(dot(_89_m3.xyz + _477, 0.3300000131130218505859375f.xxx), 0.0f, 1.0f))), _43_m1.w.xxx) + (((_43_m2.xyz * _43_m2.w) * pow(1.0f - clamp(dot(_787, _105), 0.0f, 1.0f), _89_m7.z)) * (0.4000000059604644775390625f + (0.60000002384185791015625f * clamp(_284 + 0.5f, 0.0f, 1.0f))));
    float3 _834;
    if (_116)
    {
        _834 = (lerp(_626, _89_m5.xyz + (_89_m3.xyz * 0.5f), ((0.119999997317790985107421875f + (0.5f * pow(1.0f - clamp(dot(normalize(lerp(float3(0.0f, 1.0f, 0.0f), _787, 0.07999999821186065673828125f.xxx)), _105), 0.0f, 1.0f), 3.0f))) * (1.0f - (_816 * 0.699999988079071044921875f))).xxx) * (1.0f + (_823 * 0.0900000035762786865234375f))) + ((float3(0.75f, 0.85000002384185791015625f, 0.89999997615814208984375f) * _830) * 0.300000011920928955078125f);
    }
    else
    {
        _834 = _626;
    }
    _732 = float4(lerp(_834, _89_m5.xyz + (_89_m2.xyz * (pow(clamp(dot(-_105, _89_m1.xyz), 0.0f, 1.0f), 6.0f) * _89_m6.z)), clamp((1.0f - exp((-max(_98 - _89_m6.w, 0.0f)) * _89_m5.w)) * exp((-max(_93.y - _89_m6.x, 0.0f)) * _89_m6.y), 0.0f, 1.0f).xxx), _807.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _34 = stage_input._34;
    _38 = stage_input._38;
    _78 = stage_input._78;
    _93 = stage_input._93;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._732 = _732;
    return stage_output;
}
