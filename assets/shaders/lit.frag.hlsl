static float4 _583;

cbuffer _41_43 : register(b1, space3)
{
    float4 _43_m0 : packoffset(c0);
    float4 _43_m1 : packoffset(c1);
    float4 _43_m2 : packoffset(c2);
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
Texture2D<float4> _241 : register(t1, space2);
SamplerState __241_sampler : register(s1, space2);

static float2 _34;
static float4 _38;
static float3 _78;
static float3 _93;
static float4 _505;

struct SPIRV_Cross_Input
{
    float3 _93 : TEXCOORD0;
    float3 _78 : TEXCOORD1;
    float2 _34 : TEXCOORD2;
    float4 _38 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 _505 : SV_Target0;
};

void frag_main()
{
    float4 _36 = _30.Sample(__30_sampler, _34);
    float4 _49 = (_36 * _38) * _43_m0;
    float _53 = _49.w;
    if (_53 < 0.5f)
    {
        discard;
    }
    float3 _65 = pow(_49.xyz, 2.2000000476837158203125f.xxx);
    float4 _539;
    _539.x = _65.x;
    _539.y = _65.y;
    _539.z = _65.z;
    float3 _80 = normalize(_78);
    float3 _95 = _89_m0.xyz - _93;
    float _98 = length(_95);
    float3 _105 = _95 / max(_98, 9.9999997473787516355514526367188e-05f).xxx;
    float _113 = dot(_80, -_89_m1.xyz);
    float _562;
    if (_89_m12.z > 0.0f)
    {
        float4 _170 = mul(float4(_93 + ((_80 * _89_m12.y) * 2.0f), 1.0f), _89_m11);
        float3 _177 = _170.xyz / _170.w.xxx;
        float2 _184 = (_177.xy * 0.5f) + 0.5f.xx;
        float _186 = _184.y;
        float _187 = 1.0f - _186;
        float2 _547 = _184;
        _547.y = _187;
        float _191 = _184.x;
        float _202 = min(min(_191, 1.0f - _191), min(_187, _186));
        bool _204 = _202 > 0.0f;
        bool _210;
        if (_204)
        {
            _210 = _177.z < 1.0f;
        }
        else
        {
            _210 = _204;
        }
        float _563;
        if (_210)
        {
            float _220 = _89_m12.y * (1.5f - clamp(_113, 0.0f, 1.0f));
            int _556;
            float _557;
            _557 = 0.0f;
            _556 = -1;
            float _574;
            for (; _556 <= 1; _557 = _574, _556++)
            {
                _574 = _557;
                for (int _572 = -1; _572 <= 1; )
                {
                    _574 += float((_177.z - _220) > _241.Sample(__241_sampler, _547 + (float2(float(_572), float(_556)) * _89_m12.x)).x);
                    _572++;
                    continue;
                }
            }
            _563 = 1.0f - (((_557 * 0.111111111938953399658203125f) * _89_m12.z) * clamp(_202 / _89_m12.w, 0.0f, 1.0f));
        }
        else
        {
            _563 = 1.0f;
        }
        _562 = _563;
    }
    else
    {
        _562 = 1.0f;
    }
    float3 _296 = _89_m2.xyz * _89_m1.w;
    float3 _571;
    _571 = (_296 * lerp(_89_m7.y, _89_m7.y + ((1.0f - _89_m7.y) * ((0.64999997615814208984375f * smoothstep(0.0500000007450580596923828125f - _89_m7.x, 0.0500000007450580596923828125f + _89_m7.x, _113)) + (0.3499999940395355224609375f * smoothstep(0.5f - _89_m7.x, 0.5f + _89_m7.x, _113)))), _562)) + (lerp(_89_m4.xyz, _89_m3.xyz, ((_80.y * 0.5f) + 0.5f).xxx) * (0.4000000059604644775390625f + (0.60000002384185791015625f * _562)));
    for (int _565 = 0; _565 < _89_m10.x; )
    {
        float3 _340 = _89_m8[_565].xyz - _93;
        float _343 = length(_340);
        float _351 = clamp(1.0f - (_343 / _89_m8[_565].w), 0.0f, 1.0f);
        _571 += ((_89_m9[_565].xyz * (_351 * _351)) * ((dot(_80, _340 / max(_343, 9.9999997473787516355514526367188e-05f).xxx) * 0.5f) + 0.5f));
        _565++;
        continue;
    }
    float3 _510 = lerp(lerp((_539.xyz * _571) + _43_m1.xyz, (_539.xyz * (1.10000002384185791015625f.xxx + _43_m1.xyz)) * (0.75f + (0.25f * clamp(dot(_89_m3.xyz + _296, 0.3300000131130218505859375f.xxx), 0.0f, 1.0f))), _43_m1.w.xxx) + (((_43_m2.xyz * _43_m2.w) * pow(1.0f - clamp(dot(_80, _105), 0.0f, 1.0f), _89_m7.z)) * (0.4000000059604644775390625f + (0.60000002384185791015625f * clamp(_113 + 0.5f, 0.0f, 1.0f)))), _89_m5.xyz + (_89_m2.xyz * (pow(clamp(dot(-_105, _89_m1.xyz), 0.0f, 1.0f), 6.0f) * _89_m6.z)), clamp((1.0f - exp((-max(_98 - _89_m6.w, 0.0f)) * _89_m5.w)) * exp((-max(_93.y - _89_m6.x, 0.0f)) * _89_m6.y), 0.0f, 1.0f).xxx);
    _505 = float4(_510, _53);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _34 = stage_input._34;
    _38 = stage_input._38;
    _78 = stage_input._78;
    _93 = stage_input._93;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._505 = _505;
    return stage_output;
}
