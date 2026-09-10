static const float _368[16] = { 0.0f, 8.0f, 2.0f, 10.0f, 12.0f, 4.0f, 14.0f, 6.0f, 3.0f, 11.0f, 1.0f, 9.0f, 15.0f, 7.0f, 13.0f, 5.0f };

cbuffer _85_87 : register(b0, space3)
{
    float4 _87_m0 : packoffset(c0);
    float4 _87_m1 : packoffset(c1);
    float4 _87_m2 : packoffset(c2);
    float4 _87_m3 : packoffset(c3);
    float4 _87_m4 : packoffset(c4);
    float4 _87_m5 : packoffset(c5);
    float4 _87_m6 : packoffset(c6);
    float4 _87_m7 : packoffset(c7);
    float4 _87_m8 : packoffset(c8);
    float4 _87_m9[64] : packoffset(c9);
    int4 _87_m10 : packoffset(c73);
};

Texture2D<float4> _141 : register(t0, space2);
SamplerState __141_sampler : register(s0, space2);
Texture2D<float4> _172 : register(t1, space2);
SamplerState __172_sampler : register(s1, space2);
Texture2D<float4> _563 : register(t2, space2);
SamplerState __563_sampler : register(s2, space2);
Texture2D<float4> _725 : register(t3, space2);
SamplerState __725_sampler : register(s3, space2);

static float2 _77;
static float4 _759;

struct SPIRV_Cross_Input
{
    float2 _77 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _759 : SV_Target0;
};

float mod(float x, float y)
{
    return x - y * floor(x / y);
}

float2 mod(float2 x, float2 y)
{
    return x - y * floor(x / y);
}

float3 mod(float3 x, float3 y)
{
    return x - y * floor(x / y);
}

float4 mod(float4 x, float4 y)
{
    return x - y * floor(x / y);
}

void frag_main()
{
    bool _95 = _87_m6.w > 1.5f;
    float2 _855;
    if (_95)
    {
        float2 _107 = _87_m1.xy / _87_m6.w.xx;
        _855 = (floor(_77 * _107) + 0.5f.xx) / _107;
    }
    else
    {
        _855 = _77;
    }
    float3 _857;
    if (_87_m8.x > 0.0f)
    {
        float2 _136 = ((_77 - 0.5f.xx) * (_87_m8.x * 2.0f)) / _87_m1.xy;
        _857 = float3(_141.Sample(__141_sampler, _855 + _136).x, _141.Sample(__141_sampler, _855).y, _141.Sample(__141_sampler, _855 - _136).z);
    }
    else
    {
        _857 = _141.Sample(__141_sampler, _855).xyz;
    }
    float3 _858;
    if (_87_m3.w > 0.0f)
    {
        _858 = _857 + (_172.Sample(__172_sampler, _855).xyz * _87_m3.w);
    }
    else
    {
        _858 = _857;
    }
    float3 _185 = _858 * _87_m3.x;
    float _188 = dot(_185, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
    float3 _221 = max(((((lerp(_188.xxx, _185, _87_m3.y.xxx) * _87_m5.xyz) + (_87_m4.xyz * (1.0f - clamp(_188, 0.0f, 1.0f)))) - 0.5f.xxx) * _87_m3.z) + 0.5f.xxx, 0.0f.xxx);
    float2 _228 = _77 - 0.5f.xx;
    float2 _245 = _77 * _87_m1.xy;
    float3 _277 = pow(max(lerp((clamp((_221 * ((_221 * 2.5099999904632568359375f) + 0.02999999932944774627685546875f.xxx)) / ((_221 * ((_221 * 2.4300000667572021484375f) + 0.589999973773956298828125f.xxx)) + 0.14000000059604644775390625f.xxx), 0.0f.xxx, 1.0f.xxx) * (1.0f - ((_87_m0.z * dot(_228, _228)) * 1.39999997615814208984375f))) + ((frac(sin(dot(floor(_245) + (_87_m0.x * 7.309999942779541015625f).xx, float2(12.98980045318603515625f, 78.233001708984375f))) * 43758.546875f) - 0.5f) * _87_m0.y).xxx, _87_m2.xyz, _87_m2.w.xxx), 0.0f.xxx), 0.4545454680919647216796875f.xxx);
    float3 _859;
    if (_87_m8.z > 0.0f)
    {
        float _291 = dot(_277, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
        float _305 = _245.x;
        float _307 = _245.y;
        _859 = _277 * (1.0f - (((max(smoothstep(0.449999988079071044921875f, 0.920000016689300537109375f, sin((_305 + _307) * 0.2800000011920928955078125f)), smoothstep(0.550000011920928955078125f, 0.949999988079071044921875f, sin(((_305 - _307) * 0.310000002384185791015625f) + 1.7000000476837158203125f)) * (1.0f - smoothstep(0.039999999105930328369140625f, 0.2599999904632568359375f, _291))) * (1.0f - smoothstep(0.119999997317790985107421875f, 0.5f, _291))) * _87_m8.z) * 0.60000002384185791015625f));
    }
    else
    {
        _859 = _277;
    }
    float3 _860;
    if (_87_m8.y > 0.0f)
    {
        int2 _375 = int2(mod(_245, 4.0f.xx));
        _860 = _859 + ((((_368[(_375.y * 4) + _375.x] + 0.5f) * 0.0625f) - 0.5f) * _87_m8.y).xxx;
    }
    else
    {
        _860 = _859;
    }
    float3 _880;
    if (_87_m6.z > 0.5f)
    {
        _880 = lerp(_860, floor((clamp(_860, 0.0f.xxx, 1.0f.xxx) * _87_m6.z) + 0.5f.xxx) / _87_m6.z.xxx, clamp(_87_m6.x, 0.0f, 1.0f).xxx);
    }
    else
    {
        float3 _881;
        if (_87_m6.x > 0.0f)
        {
            float3 _863;
            _863 = _860;
            int _861 = 0;
            float _864 = 1000000000.0f;
            for (; _861 < _87_m10.x; )
            {
                float3 _449 = _87_m9[_861].xyz - _860;
                float _453 = dot(_449, _449);
                bool _456 = _453 < _864;
                bool3 _901 = _456.xxx;
                _864 = _456 ? _453 : _864;
                _863 = float3(_901.x ? _87_m9[_861].xyz.x : _863.x, _901.y ? _87_m9[_861].xyz.y : _863.y, _901.z ? _87_m9[_861].xyz.z : _863.z);
                _861++;
                continue;
            }
            _881 = lerp(_860, _863, _87_m6.x.xxx);
        }
        else
        {
            _881 = _860;
        }
        _880 = _881;
    }
    float3 _882;
    if (_87_m6.y > 0.0f)
    {
        float _492 = max(_87_m7.x, 1.0f) * (_95 ? _87_m6.w : 1.0f);
        float2 _499 = _492.xx / _87_m1.xy;
        float2 _875;
        if (_87_m7.y > 0.0f)
        {
            float _511 = floor(_87_m0.x * 8.0f);
            float _514 = _245.y;
            float _532 = _245.x;
            _875 = _855 + (((float2(sin((_514 * 0.070000000298023223876953125f) + (_511 * 1.7000000476837158203125f)) + sin((_514 * 0.02099999971687793731689453125f) - (_511 * 2.2999999523162841796875f)), cos((_532 * 0.063000001013278961181640625f) - (_511 * 1.2999999523162841796875f)) + cos((_532 * 0.0170000009238719940185546875f) + (_511 * 3.099999904632568359375f))) * 0.5f) * _87_m7.y) / _87_m1.xy);
        }
        else
        {
            _875 = _855;
        }
        float4 _566 = _563.Sample(__563_sampler, _875);
        float _567 = _566.x;
        float2 _573 = float2(_499.x, 0.0f);
        float2 _574 = _875 + _573;
        float4 _575 = _563.Sample(__563_sampler, _574);
        float2 _585 = _875 - _573;
        float4 _586 = _563.Sample(__563_sampler, _585);
        float2 _597 = float2(0.0f, _499.y);
        float2 _598 = _875 + _597;
        float4 _599 = _563.Sample(__563_sampler, _598);
        float2 _609 = _875 - _597;
        float4 _610 = _563.Sample(__563_sampler, _609);
        float _630 = smoothstep(0.070000000298023223876953125f, 0.25f, max(max(abs(_575.x - _567), abs(_586.x - _567)), max(abs(_599.x - _567), abs(_610.x - _567))) / (max(1.0f - _567, 0.00039999998989515006542205810546875f) * _492));
        float _876;
        if (_87_m7.z > 0.0f)
        {
            float _796 = dot(_141.Sample(__141_sampler, _875).xyz, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
            float _800 = _796 / (1.0f + _796);
            float _805 = dot(_141.Sample(__141_sampler, _574).xyz, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
            float _814 = dot(_141.Sample(__141_sampler, _585).xyz, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
            float _823 = dot(_141.Sample(__141_sampler, _598).xyz, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
            float _832 = dot(_141.Sample(__141_sampler, _609).xyz, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
            _876 = max(_630, smoothstep(0.04500000178813934326171875f, 0.300000011920928955078125f, max(max(abs((_805 / (1.0f + _805)) - _800), abs((_814 / (1.0f + _814)) - _800)), max(abs((_823 / (1.0f + _823)) - _800), abs((_832 / (1.0f + _832)) - _800)))) * _87_m7.z);
        }
        else
        {
            _876 = _630;
        }
        _882 = _880 * (1.0f - ((_876 * _87_m6.y) * 0.85000002384185791015625f));
    }
    else
    {
        _882 = _880;
    }
    float3 _883;
    if (_87_m7.w > 0.0f)
    {
        _883 = lerp(_882, _882 * clamp(((_725.Sample(__725_sampler, _245 / max(_87_m8.w, 1.0f).xx).xyz - 0.920000016689300537109375f.xxx) * 3.0f) + 0.959999978542327880859375f.xxx, 0.0f.xxx, 1.10000002384185791015625f.xxx), _87_m7.w.xxx);
    }
    else
    {
        _883 = _882;
    }
    _759 = float4(_883 * _87_m0.w, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _77 = stage_input._77;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._759 = _759;
    return stage_output;
}
