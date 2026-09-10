cbuffer _41_43 : register(b1, space3)
{
    float4 _43_m0 : packoffset(c0);
    float4 _43_m1 : packoffset(c1);
    float4 _43_m2 : packoffset(c2);
    float4 _43_m3 : packoffset(c3);
    float4 _43_m4 : packoffset(c4);
};

cbuffer _105_107 : register(b0, space3)
{
    float4 _107_m0 : packoffset(c0);
    float4 _107_m1 : packoffset(c1);
    float4 _107_m2 : packoffset(c2);
    float4 _107_m3 : packoffset(c3);
    float4 _107_m4 : packoffset(c4);
    float4 _107_m5 : packoffset(c5);
    float4 _107_m6 : packoffset(c6);
    float4 _107_m7 : packoffset(c7);
    float4 _107_m8[16] : packoffset(c8);
    float4 _107_m9[16] : packoffset(c24);
    int4 _107_m10 : packoffset(c40);
    row_major float4x4 _107_m11 : packoffset(c41);
    float4 _107_m12 : packoffset(c45);
};

Texture2D<float4> _30 : register(t0, space2);
SamplerState __30_sampler : register(s0, space2);
Texture2D<float4> _439 : register(t1, space2);
SamplerState __439_sampler : register(s1, space2);

static float2 _34;
static float4 _64;
static float3 _96;
static float3 _111;
static float4 _748;

struct SPIRV_Cross_Input
{
    float3 _111 : TEXCOORD0;
    float3 _96 : TEXCOORD1;
    float2 _34 : TEXCOORD2;
    float4 _64 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 _748 : SV_Target0;
};

void frag_main()
{
    float4 _36 = _30.Sample(__30_sampler, _34);
    float4 _70 = (float4(lerp(_36.xyz, _43_m4.xyz, _43_m4.w.xxx), _36.w) * _64) * _43_m0;
    if (_70.w < 0.5f)
    {
        discard;
    }
    float3 _83 = pow(_70.xyz, 2.2000000476837158203125f.xxx);
    float4 _783 = _70;
    _783.x = _83.x;
    _783.y = _83.y;
    _783.z = _83.z;
    float3 _113 = _107_m0.xyz - _111;
    float _116 = length(_113);
    float3 _123 = _113 / max(_116, 9.9999997473787516355514526367188e-05f).xxx;
    bool _133 = _43_m3.x > 0.5f;
    float3 _804;
    float4 _824;
    float _833;
    float _840;
    float _847;
    if (_133)
    {
        float _156 = dot(_111.xz, float2(0.860129058361053466796875f, 0.5100765228271484375f));
        float _165 = dot(_111.xz, float2(-0.4190581738948822021484375f, 0.907959401607513427734375f));
        float _175 = ((_156 * 0.550000011920928955078125f) + (_107_m7.w * 1.10000002384185791015625f)) + (sin((_165 * 0.20999999344348907470703125f) - (_107_m7.w * 0.3499999940395355224609375f)) * 2.0f);
        float _197 = ((_165 * 1.2999999523162841796875f) - (_107_m7.w * 1.7000000476837158203125f)) + (sin((_156 * 0.37000000476837158203125f) + (_107_m7.w * 0.5f)) * 1.60000002384185791015625f);
        float4 _215 = _30.Sample(__30_sampler, clamp((_111.xz - _43_m3.yz) * _43_m3.w, 0.0f.xx, 1.0f.xx));
        float _218 = 1.0f - _215.x;
        float _231 = lerp(1.0f, 0.300000011920928955078125f, _218) * clamp(1.0f - ((_116 - 20.0f) * 0.011111111380159854888916015625f), 0.119999997317790985107421875f, 1.0f);
        float2 _247 = (((float2(0.860129058361053466796875f, 0.5100765228271484375f) * cos(_175)) * 0.04500000178813934326171875f) + ((float2(-0.4190581738948822021484375f, 0.907959401607513427734375f) * cos(_197)) * 0.0280000008642673492431640625f)) * _231;
        _847 = _215.y * smoothstep(0.1500000059604644775390625f, 0.550000011920928955078125f, 0.5f + (0.5f * sin((_197 * 0.800000011920928955078125f) + (_107_m7.w * 0.89999997615814208984375f))));
        _840 = (0.5f * (sin(_175) + sin(_197))) * _231;
        _833 = _218;
        _824 = float4(pow(_43_m0.xyz, 2.2000000476837158203125f.xxx) * lerp(1.0f, 2.2999999523162841796875f, _218 * _218), 1.0f);
        _804 = normalize(float3(-_247.x, 1.0f, -_247.y));
    }
    else
    {
        _847 = 0.0f;
        _840 = 0.0f;
        _833 = 0.0f;
        _824 = _783;
        _804 = normalize(_96);
    }
    float _301 = dot(_804, -_107_m1.xyz);
    float _805;
    if (_133)
    {
        _805 = _107_m7.y + ((1.0f - _107_m7.y) * clamp((_301 * 0.5f) + 0.5f, 0.0f, 1.0f));
    }
    else
    {
        _805 = _107_m7.y + ((1.0f - _107_m7.y) * ((0.64999997615814208984375f * smoothstep(0.0500000007450580596923828125f - _107_m7.x, 0.0500000007450580596923828125f + _107_m7.x, _301)) + (0.3499999940395355224609375f * smoothstep(0.5f - _107_m7.x, 0.5f + _107_m7.x, _301))));
    }
    float _813;
    if (_107_m12.z > 0.0f)
    {
        float4 _369 = mul(float4(_111 + ((_804 * _107_m12.y) * 2.0f), 1.0f), _107_m11);
        float3 _376 = _369.xyz / _369.w.xxx;
        float2 _382 = (_376.xy * 0.5f) + 0.5f.xx;
        float _384 = _382.y;
        float _385 = 1.0f - _384;
        float2 _795 = _382;
        _795.y = _385;
        float _389 = _382.x;
        float _400 = min(min(_389, 1.0f - _389), min(_385, _384));
        bool _402 = _400 > 0.0f;
        bool _408;
        if (_402)
        {
            _408 = _376.z < 1.0f;
        }
        else
        {
            _408 = _402;
        }
        float _814;
        if (_408)
        {
            float _418 = _107_m12.y * (1.5f - clamp(_301, 0.0f, 1.0f));
            int _807;
            float _808;
            _808 = 0.0f;
            _807 = -1;
            float _855;
            for (; _807 <= 1; _808 = _855, _807++)
            {
                _855 = _808;
                for (int _853 = -1; _853 <= 1; )
                {
                    _855 += float((_376.z - _418) > _439.Sample(__439_sampler, _795 + (float2(float(_853), float(_807)) * _107_m12.x)).x);
                    _853++;
                    continue;
                }
            }
            _814 = 1.0f - (((_808 * 0.111111111938953399658203125f) * _107_m12.z) * clamp(_400 / _107_m12.w, 0.0f, 1.0f));
        }
        else
        {
            _814 = 1.0f;
        }
        _813 = _814;
    }
    else
    {
        _813 = 1.0f;
    }
    float3 _494 = _107_m2.xyz * _107_m1.w;
    float3 _828;
    _828 = (_494 * lerp(_107_m7.y, _805, _813)) + (lerp(_107_m4.xyz, _107_m3.xyz, ((_804.y * 0.5f) + 0.5f).xxx) * (0.4000000059604644775390625f + (0.60000002384185791015625f * _813)));
    for (int _820 = 0; _820 < _107_m10.x; )
    {
        float3 _536 = _107_m8[_820].xyz - _111;
        float _539 = length(_536);
        float _547 = clamp(1.0f - (_539 / _107_m8[_820].w), 0.0f, 1.0f);
        _828 += ((_107_m9[_820].xyz * (_547 * _547)) * ((dot(_804, _536 / max(_539, 9.9999997473787516355514526367188e-05f).xxx) * 0.5f) + 0.5f));
        _820++;
        continue;
    }
    float3 _642 = lerp((_824.xyz * _828) + _43_m1.xyz, (_824.xyz * (1.10000002384185791015625f.xxx + _43_m1.xyz)) * (0.75f + (0.25f * clamp(dot(_107_m3.xyz + _494, 0.3300000131130218505859375f.xxx), 0.0f, 1.0f))), _43_m1.w.xxx) + (((_43_m2.xyz * _43_m2.w) * pow(1.0f - clamp(dot(_804, _123), 0.0f, 1.0f), _107_m7.z)) * (0.4000000059604644775390625f + (0.60000002384185791015625f * clamp(_301 + 0.5f, 0.0f, 1.0f))));
    float3 _851;
    if (_133)
    {
        _851 = (lerp(_642, _107_m5.xyz + (_107_m3.xyz * 0.5f), ((0.119999997317790985107421875f + (0.5f * pow(1.0f - clamp(dot(normalize(lerp(float3(0.0f, 1.0f, 0.0f), _804, 0.07999999821186065673828125f.xxx)), _123), 0.0f, 1.0f), 3.0f))) * (1.0f - (_833 * 0.699999988079071044921875f))).xxx) * (1.0f + (_840 * 0.0900000035762786865234375f))) + ((float3(0.75f, 0.85000002384185791015625f, 0.89999997615814208984375f) * _847) * 0.300000011920928955078125f);
    }
    else
    {
        _851 = _642;
    }
    _748 = float4(lerp(_851, _107_m5.xyz + (_107_m2.xyz * (pow(clamp(dot(-_123, _107_m1.xyz), 0.0f, 1.0f), 6.0f) * _107_m6.z)), clamp((1.0f - exp((-max(_116 - _107_m6.w, 0.0f)) * _107_m5.w)) * exp((-max(_111.y - _107_m6.x, 0.0f)) * _107_m6.y), 0.0f, 1.0f).xxx), _824.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _34 = stage_input._34;
    _64 = stage_input._64;
    _96 = stage_input._96;
    _111 = stage_input._111;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._748 = _748;
    return stage_output;
}
