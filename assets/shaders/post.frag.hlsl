cbuffer _67_69 : register(b0, space3)
{
    float4 _69_m0 : packoffset(c0);
    float4 _69_m1 : packoffset(c1);
    float4 _69_m2 : packoffset(c2);
    float4 _69_m3 : packoffset(c3);
    float4 _69_m4 : packoffset(c4);
    float4 _69_m5 : packoffset(c5);
    float4 _69_m6 : packoffset(c6);
    float4 _69_m7[64] : packoffset(c7);
    int4 _69_m8 : packoffset(c71);
};

Texture2D<float4> _103 : register(t0, space2);
SamplerState __103_sampler : register(s0, space2);
Texture2D<float4> _108 : register(t1, space2);
SamplerState __108_sampler : register(s1, space2);
Texture2D<float4> _319 : register(t2, space2);
SamplerState __319_sampler : register(s2, space2);

static float2 _59;
static float4 _398;

struct SPIRV_Cross_Input
{
    float2 _59 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _398 : SV_Target0;
};

void frag_main()
{
    bool _77 = _69_m6.w > 1.5f;
    float2 _435;
    if (_77)
    {
        float2 _89 = _69_m1.xy / _69_m6.w.xx;
        _435 = (floor(_59 * _89) + 0.5f.xx) / _89;
    }
    else
    {
        _435 = _59;
    }
    float3 _122 = (_103.Sample(__103_sampler, _435).xyz + (_108.Sample(__108_sampler, _435).xyz * _69_m3.w)) * _69_m3.x;
    float _130 = dot(_122, float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
    float3 _165 = max(((((lerp(_130.xxx, _122, _69_m3.y.xxx) * _69_m5.xyz) + (_69_m4.xyz * (1.0f - clamp(_130, 0.0f, 1.0f)))) - 0.5f.xxx) * _69_m3.z) + 0.5f.xxx, 0.0f.xxx);
    float2 _172 = _59 - 0.5f.xx;
    float3 _221 = pow(max(lerp((clamp((_165 * ((_165 * 2.5099999904632568359375f) + 0.02999999932944774627685546875f.xxx)) / ((_165 * ((_165 * 2.4300000667572021484375f) + 0.589999973773956298828125f.xxx)) + 0.14000000059604644775390625f.xxx), 0.0f.xxx, 1.0f.xxx) * (1.0f - ((_69_m0.z * dot(_172, _172)) * 1.39999997615814208984375f))) + ((frac(sin(dot(floor(_59 * _69_m1.xy) + (_69_m0.x * 7.309999942779541015625f).xx, float2(12.98980045318603515625f, 78.233001708984375f))) * 43758.546875f) - 0.5f) * _69_m0.y).xxx, _69_m2.xyz, _69_m2.w.xxx), 0.0f.xxx), 0.4545454680919647216796875f.xxx);
    float3 _448;
    if (_69_m6.z > 0.5f)
    {
        _448 = lerp(_221, floor((_221 * _69_m6.z) + 0.5f.xxx) / _69_m6.z.xxx, clamp(_69_m6.x, 0.0f, 1.0f).xxx);
    }
    else
    {
        float3 _449;
        if (_69_m6.x > 0.0f)
        {
            float3 _438;
            _438 = _221;
            int _436 = 0;
            float _439 = 1000000000.0f;
            for (; _436 < _69_m8.x; )
            {
                float3 _274 = _69_m7[_436].xyz - _221;
                float _278 = dot(_274, _274);
                bool _281 = _278 < _439;
                bool3 _463 = _281.xxx;
                _439 = _281 ? _278 : _439;
                _438 = float3(_463.x ? _69_m7[_436].xyz.x : _438.x, _463.y ? _69_m7[_436].xyz.y : _438.y, _463.z ? _69_m7[_436].xyz.z : _438.z);
                _436++;
                continue;
            }
            _449 = lerp(_221, _438, _69_m6.x.xxx);
        }
        else
        {
            _449 = _221;
        }
        _448 = _449;
    }
    float3 _450;
    if (_69_m6.y > 0.0f)
    {
        float2 _317 = (_77 ? _69_m6.w : 1.0f).xx / _69_m1.xy;
        float4 _322 = _319.Sample(__319_sampler, _435);
        float _323 = _322.x;
        float2 _329 = float2(_317.x, 0.0f);
        float2 _353 = float2(0.0f, _317.y);
        _450 = _448 * (1.0f - ((smoothstep(0.0007999999797903001308441162109375f, 0.0040000001899898052215576171875f, max(max(abs(_319.Sample(__319_sampler, _435 + _329).x - _323), abs(_319.Sample(__319_sampler, _435 - _329).x - _323)), max(abs(_319.Sample(__319_sampler, _435 + _353).x - _323), abs(_319.Sample(__319_sampler, _435 - _353).x - _323))) * (1.0f - (_323 * 0.89999997615814208984375f))) * _69_m6.y) * 0.85000002384185791015625f));
    }
    else
    {
        _450 = _448;
    }
    _398 = float4(_450 * _69_m0.w, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _59 = stage_input._59;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._398 = _398;
    return stage_output;
}
