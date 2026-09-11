cbuffer _42_44 : register(b1, space3)
{
    float4 _44_m0 : packoffset(c0);
    float4 _44_m1 : packoffset(c1);
    float4 _44_m2 : packoffset(c2);
    float4 _44_m3 : packoffset(c3);
    float4 _44_m4 : packoffset(c4);
};

cbuffer _106_108 : register(b0, space3)
{
    float4 _108_m0 : packoffset(c0);
    float4 _108_m1 : packoffset(c1);
    float4 _108_m2 : packoffset(c2);
    float4 _108_m3 : packoffset(c3);
    float4 _108_m4 : packoffset(c4);
    float4 _108_m5 : packoffset(c5);
    float4 _108_m6 : packoffset(c6);
    float4 _108_m7 : packoffset(c7);
    float4 _108_m8[16] : packoffset(c8);
    float4 _108_m9[16] : packoffset(c24);
    int4 _108_m10 : packoffset(c40);
    row_major float4x4 _108_m11 : packoffset(c41);
    float4 _108_m12 : packoffset(c45);
};

Texture2D<float4> _30 : register(t0, space2);
SamplerState __30_sampler : register(s0, space2);
Texture2D<float4> _440 : register(t1, space2);
SamplerState __440_sampler : register(s1, space2);

static float2 _34;
static float4 _65;
static float3 _97;
static float3 _112;
static float4 _749;

struct SPIRV_Cross_Input
{
    float3 _112 : TEXCOORD0;
    float3 _97 : TEXCOORD1;
    float2 _34 : TEXCOORD2;
    float4 _65 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 _749 : SV_Target0;
};

void frag_main()
{
    float4 _37 = _30.SampleBias(__30_sampler, _34, -0.5f);
    float4 _71 = (float4(lerp(_37.xyz, _44_m4.xyz, _44_m4.w.xxx), _37.w) * _65) * _44_m0;
    if (_71.w < 0.5f)
    {
        discard;
    }
    float3 _84 = pow(_71.xyz, 2.2000000476837158203125f.xxx);
    float4 _784 = _71;
    _784.x = _84.x;
    _784.y = _84.y;
    _784.z = _84.z;
    float3 _114 = _108_m0.xyz - _112;
    float _117 = length(_114);
    float3 _124 = _114 / max(_117, 9.9999997473787516355514526367188e-05f).xxx;
    bool _134 = _44_m3.x > 0.5f;
    float3 _805;
    float4 _825;
    float _834;
    float _841;
    float _848;
    if (_134)
    {
        float _157 = dot(_112.xz, float2(0.860129058361053466796875f, 0.5100765228271484375f));
        float _166 = dot(_112.xz, float2(-0.4190581738948822021484375f, 0.907959401607513427734375f));
        float _176 = ((_157 * 0.550000011920928955078125f) + (_108_m7.w * 1.10000002384185791015625f)) + (sin((_166 * 0.20999999344348907470703125f) - (_108_m7.w * 0.3499999940395355224609375f)) * 2.0f);
        float _198 = ((_166 * 1.2999999523162841796875f) - (_108_m7.w * 1.7000000476837158203125f)) + (sin((_157 * 0.37000000476837158203125f) + (_108_m7.w * 0.5f)) * 1.60000002384185791015625f);
        float4 _216 = _30.Sample(__30_sampler, clamp((_112.xz - _44_m3.yz) * _44_m3.w, 0.0f.xx, 1.0f.xx));
        float _219 = 1.0f - _216.x;
        float _232 = lerp(1.0f, 0.300000011920928955078125f, _219) * clamp(1.0f - ((_117 - 20.0f) * 0.011111111380159854888916015625f), 0.119999997317790985107421875f, 1.0f);
        float2 _248 = (((float2(0.860129058361053466796875f, 0.5100765228271484375f) * cos(_176)) * 0.04500000178813934326171875f) + ((float2(-0.4190581738948822021484375f, 0.907959401607513427734375f) * cos(_198)) * 0.0280000008642673492431640625f)) * _232;
        _848 = _216.y * smoothstep(0.1500000059604644775390625f, 0.550000011920928955078125f, 0.5f + (0.5f * sin((_198 * 0.800000011920928955078125f) + (_108_m7.w * 0.89999997615814208984375f))));
        _841 = (0.5f * (sin(_176) + sin(_198))) * _232;
        _834 = _219;
        _825 = float4(pow(_44_m0.xyz, 2.2000000476837158203125f.xxx) * lerp(1.0f, 2.2999999523162841796875f, _219 * _219), 1.0f);
        _805 = normalize(float3(-_248.x, 1.0f, -_248.y));
    }
    else
    {
        _848 = 0.0f;
        _841 = 0.0f;
        _834 = 0.0f;
        _825 = _784;
        _805 = normalize(_97);
    }
    float _302 = dot(_805, -_108_m1.xyz);
    float _806;
    if (_134)
    {
        _806 = _108_m7.y + ((1.0f - _108_m7.y) * clamp((_302 * 0.5f) + 0.5f, 0.0f, 1.0f));
    }
    else
    {
        _806 = _108_m7.y + ((1.0f - _108_m7.y) * ((0.64999997615814208984375f * smoothstep(0.0500000007450580596923828125f - _108_m7.x, 0.0500000007450580596923828125f + _108_m7.x, _302)) + (0.3499999940395355224609375f * smoothstep(0.5f - _108_m7.x, 0.5f + _108_m7.x, _302))));
    }
    float _814;
    if (_108_m12.z > 0.0f)
    {
        float4 _370 = mul(float4(_112 + ((_805 * _108_m12.y) * 2.0f), 1.0f), _108_m11);
        float3 _377 = _370.xyz / _370.w.xxx;
        float2 _383 = (_377.xy * 0.5f) + 0.5f.xx;
        float _385 = _383.y;
        float _386 = 1.0f - _385;
        float2 _796 = _383;
        _796.y = _386;
        float _390 = _383.x;
        float _401 = min(min(_390, 1.0f - _390), min(_386, _385));
        bool _403 = _401 > 0.0f;
        bool _409;
        if (_403)
        {
            _409 = _377.z < 1.0f;
        }
        else
        {
            _409 = _403;
        }
        float _815;
        if (_409)
        {
            float _419 = _108_m12.y * (1.5f - clamp(_302, 0.0f, 1.0f));
            int _808;
            float _809;
            _809 = 0.0f;
            _808 = -1;
            float _856;
            for (; _808 <= 1; _809 = _856, _808++)
            {
                _856 = _809;
                for (int _854 = -1; _854 <= 1; )
                {
                    _856 += float((_377.z - _419) > _440.Sample(__440_sampler, _796 + (float2(float(_854), float(_808)) * _108_m12.x)).x);
                    _854++;
                    continue;
                }
            }
            _815 = 1.0f - (((_809 * 0.111111111938953399658203125f) * _108_m12.z) * clamp(_401 / _108_m12.w, 0.0f, 1.0f));
        }
        else
        {
            _815 = 1.0f;
        }
        _814 = _815;
    }
    else
    {
        _814 = 1.0f;
    }
    float3 _495 = _108_m2.xyz * _108_m1.w;
    float3 _829;
    _829 = (_495 * lerp(_108_m7.y, _806, _814)) + (lerp(_108_m4.xyz, _108_m3.xyz, ((_805.y * 0.5f) + 0.5f).xxx) * (0.4000000059604644775390625f + (0.60000002384185791015625f * _814)));
    for (int _821 = 0; _821 < _108_m10.x; )
    {
        float3 _537 = _108_m8[_821].xyz - _112;
        float _540 = length(_537);
        float _548 = clamp(1.0f - (_540 / _108_m8[_821].w), 0.0f, 1.0f);
        _829 += ((_108_m9[_821].xyz * (_548 * _548)) * ((dot(_805, _537 / max(_540, 9.9999997473787516355514526367188e-05f).xxx) * 0.5f) + 0.5f));
        _821++;
        continue;
    }
    float3 _643 = lerp((_825.xyz * _829) + _44_m1.xyz, (_825.xyz * (1.10000002384185791015625f.xxx + _44_m1.xyz)) * (0.75f + (0.25f * clamp(dot(_108_m3.xyz + _495, 0.3300000131130218505859375f.xxx), 0.0f, 1.0f))), _44_m1.w.xxx) + (((_44_m2.xyz * _44_m2.w) * pow(1.0f - clamp(dot(_805, _124), 0.0f, 1.0f), _108_m7.z)) * (0.4000000059604644775390625f + (0.60000002384185791015625f * clamp(_302 + 0.5f, 0.0f, 1.0f))));
    float3 _852;
    if (_134)
    {
        _852 = (lerp(_643, _108_m5.xyz + (_108_m3.xyz * 0.5f), ((0.119999997317790985107421875f + (0.5f * pow(1.0f - clamp(dot(normalize(lerp(float3(0.0f, 1.0f, 0.0f), _805, 0.07999999821186065673828125f.xxx)), _124), 0.0f, 1.0f), 3.0f))) * (1.0f - (_834 * 0.699999988079071044921875f))).xxx) * (1.0f + (_841 * 0.0900000035762786865234375f))) + ((float3(0.75f, 0.85000002384185791015625f, 0.89999997615814208984375f) * _848) * 0.300000011920928955078125f);
    }
    else
    {
        _852 = _643;
    }
    _749 = float4(lerp(_852, _108_m5.xyz + (_108_m2.xyz * (pow(clamp(dot(-_124, _108_m1.xyz), 0.0f, 1.0f), 6.0f) * _108_m6.z)), clamp((1.0f - exp((-max(_117 - _108_m6.w, 0.0f)) * _108_m5.w)) * exp((-max(_112.y - _108_m6.x, 0.0f)) * _108_m6.y), 0.0f, 1.0f).xxx), _825.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _34 = stage_input._34;
    _65 = stage_input._65;
    _97 = stage_input._97;
    _112 = stage_input._112;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._749 = _749;
    return stage_output;
}
