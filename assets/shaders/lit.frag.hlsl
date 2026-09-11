cbuffer _38_40 : register(b1, space3)
{
    float4 _40_m0 : packoffset(c0);
    float4 _40_m1 : packoffset(c1);
    float4 _40_m2 : packoffset(c2);
    float4 _40_m3 : packoffset(c3);
    float4 _40_m4 : packoffset(c4);
};

cbuffer _157_159 : register(b0, space3)
{
    float4 _159_m0 : packoffset(c0);
    float4 _159_m1 : packoffset(c1);
    float4 _159_m2 : packoffset(c2);
    float4 _159_m3 : packoffset(c3);
    float4 _159_m4 : packoffset(c4);
    float4 _159_m5 : packoffset(c5);
    float4 _159_m6 : packoffset(c6);
    float4 _159_m7 : packoffset(c7);
    float4 _159_m8[16] : packoffset(c8);
    float4 _159_m9[16] : packoffset(c24);
    int4 _159_m10 : packoffset(c40);
    row_major float4x4 _159_m11 : packoffset(c41);
    float4 _159_m12 : packoffset(c45);
};

Texture2D<float4> _30 : register(t0, space2);
SamplerState __30_sampler : register(s0, space2);
Texture2D<float4> _492 : register(t1, space2);
SamplerState __492_sampler : register(s1, space2);

static float2 _34;
static float4 _110;
static float3 _148;
static float3 _163;
static float4 _800;

struct SPIRV_Cross_Input
{
    float3 _163 : TEXCOORD0;
    float3 _148 : TEXCOORD1;
    float2 _34 : TEXCOORD2;
    float4 _110 : TEXCOORD3;
};

struct SPIRV_Cross_Output
{
    float4 _800 : SV_Target0;
};

void frag_main()
{
    float4 _37 = _30.SampleBias(__30_sampler, _34, -0.5f);
    float4 _862;
    if (_40_m3.x > 1.5f)
    {
        float3 _89 = _37.xyz * lerp(1.0f.xxx, 1.0f.xxx + (((_30.SampleBias(__30_sampler, (_34 * 0.1369999945163726806640625f) + float2(0.37000000476837158203125f, 0.61000001430511474609375f), 2.0f).xyz / max(_40_m4.xyz, 0.039999999105930328369140625f.xxx)) - 1.0f.xxx) * 1.89999997615814208984375f), 0.75f.xxx);
        float4 _833 = _37;
        _833.x = _89.x;
        _833.y = _89.y;
        _833.z = _89.z;
        _862 = _833;
    }
    else
    {
        _862 = _37;
    }
    float _123 = _862.w * _40_m0.w;
    float4 _127 = float4((lerp(_862.xyz, _40_m4.xyz, _40_m4.w.xxx) * _110.xyz) * _40_m0.xyz, _123);
    if (_123 < 0.5f)
    {
        discard;
    }
    float3 _139 = pow(_127.xyz, 2.2000000476837158203125f.xxx);
    float4 _841 = _127;
    _841.x = _139.x;
    _841.y = _139.y;
    _841.z = _139.z;
    float3 _165 = _159_m0.xyz - _163;
    float _168 = length(_165);
    float3 _175 = _165 / max(_168, 9.9999997473787516355514526367188e-05f).xxx;
    bool _184 = _40_m3.x > 0.5f;
    bool _190;
    if (_184)
    {
        _190 = _40_m3.x < 1.5f;
    }
    else
    {
        _190 = _184;
    }
    float3 _863;
    float4 _884;
    float _894;
    float _902;
    float _910;
    if (_190)
    {
        float _213 = dot(_163.xz, float2(0.860129058361053466796875f, 0.5100765228271484375f));
        float _222 = dot(_163.xz, float2(-0.4190581738948822021484375f, 0.907959401607513427734375f));
        float _231 = ((_213 * 0.550000011920928955078125f) + (_159_m7.w * 1.10000002384185791015625f)) + (sin((_222 * 0.20999999344348907470703125f) - (_159_m7.w * 0.3499999940395355224609375f)) * 2.0f);
        float _252 = ((_222 * 1.2999999523162841796875f) - (_159_m7.w * 1.7000000476837158203125f)) + (sin((_213 * 0.37000000476837158203125f) + (_159_m7.w * 0.5f)) * 1.60000002384185791015625f);
        float4 _269 = _30.Sample(__30_sampler, clamp((_163.xz - _40_m3.yz) * _40_m3.w, 0.0f.xx, 1.0f.xx));
        float _272 = 1.0f - _269.x;
        float _285 = lerp(1.0f, 0.300000011920928955078125f, _272) * clamp(1.0f - ((_168 - 20.0f) * 0.011111111380159854888916015625f), 0.119999997317790985107421875f, 1.0f);
        float2 _301 = (((float2(0.860129058361053466796875f, 0.5100765228271484375f) * cos(_231)) * 0.04500000178813934326171875f) + ((float2(-0.4190581738948822021484375f, 0.907959401607513427734375f) * cos(_252)) * 0.0280000008642673492431640625f)) * _285;
        _910 = _269.y * smoothstep(0.1500000059604644775390625f, 0.550000011920928955078125f, 0.5f + (0.5f * sin((_252 * 0.800000011920928955078125f) + (_159_m7.w * 0.89999997615814208984375f))));
        _902 = (0.5f * (sin(_231) + sin(_252))) * _285;
        _894 = _272;
        _884 = float4(pow(_40_m0.xyz, 2.2000000476837158203125f.xxx) * lerp(1.0f, 2.2999999523162841796875f, _272 * _272), 1.0f);
        _863 = normalize(float3(-_301.x, 1.0f, -_301.y));
    }
    else
    {
        _910 = 0.0f;
        _902 = 0.0f;
        _894 = 0.0f;
        _884 = _841;
        _863 = normalize(_148);
    }
    float _355 = dot(_863, -_159_m1.xyz);
    float _865;
    if (_190)
    {
        _865 = _159_m7.y + ((1.0f - _159_m7.y) * clamp((_355 * 0.5f) + 0.5f, 0.0f, 1.0f));
    }
    else
    {
        _865 = _159_m7.y + ((1.0f - _159_m7.y) * ((0.64999997615814208984375f * smoothstep(0.0500000007450580596923828125f - _159_m7.x, 0.0500000007450580596923828125f + _159_m7.x, _355)) + (0.3499999940395355224609375f * smoothstep(0.5f - _159_m7.x, 0.5f + _159_m7.x, _355))));
    }
    float _873;
    if (_159_m12.z > 0.0f)
    {
        float4 _423 = mul(float4(_163 + ((_863 * _159_m12.y) * 2.0f), 1.0f), _159_m11);
        float3 _430 = _423.xyz / _423.w.xxx;
        float2 _436 = (_430.xy * 0.5f) + 0.5f.xx;
        float _438 = _436.y;
        float _439 = 1.0f - _438;
        float2 _853 = _436;
        _853.y = _439;
        float _443 = _436.x;
        float _454 = min(min(_443, 1.0f - _443), min(_439, _438));
        bool _456 = _454 > 0.0f;
        bool _462;
        if (_456)
        {
            _462 = _430.z < 1.0f;
        }
        else
        {
            _462 = _456;
        }
        float _874;
        if (_462)
        {
            float _471 = _159_m12.y * (1.5f - clamp(_355, 0.0f, 1.0f));
            int _867;
            float _868;
            _868 = 0.0f;
            _867 = -1;
            float _919;
            for (; _867 <= 1; _868 = _919, _867++)
            {
                _919 = _868;
                for (int _917 = -1; _917 <= 1; )
                {
                    _919 += float((_430.z - _471) > _492.Sample(__492_sampler, _853 + (float2(float(_917), float(_867)) * _159_m12.x)).x);
                    _917++;
                    continue;
                }
            }
            _874 = 1.0f - (((_868 * 0.111111111938953399658203125f) * _159_m12.z) * clamp(_454 / _159_m12.w, 0.0f, 1.0f));
        }
        else
        {
            _874 = 1.0f;
        }
        _873 = _874;
    }
    else
    {
        _873 = 1.0f;
    }
    float3 _547 = _159_m2.xyz * _159_m1.w;
    float3 _889;
    _889 = (_547 * lerp(_159_m7.y, _865, _873)) + (lerp(_159_m4.xyz, _159_m3.xyz, ((_863.y * 0.5f) + 0.5f).xxx) * (0.4000000059604644775390625f + (0.60000002384185791015625f * _873)));
    for (int _880 = 0; _880 < _159_m10.x; )
    {
        float3 _589 = _159_m8[_880].xyz - _163;
        float _592 = length(_589);
        float _600 = clamp(1.0f - (_592 / _159_m8[_880].w), 0.0f, 1.0f);
        _889 += ((_159_m9[_880].xyz * (_600 * _600)) * ((dot(_863, _589 / max(_592, 9.9999997473787516355514526367188e-05f).xxx) * 0.5f) + 0.5f));
        _880++;
        continue;
    }
    float3 _694 = lerp((_884.xyz * _889) + _40_m1.xyz, (_884.xyz * (1.10000002384185791015625f.xxx + _40_m1.xyz)) * (0.75f + (0.25f * clamp(dot(_159_m3.xyz + _547, 0.3300000131130218505859375f.xxx), 0.0f, 1.0f))), _40_m1.w.xxx) + (((_40_m2.xyz * _40_m2.w) * pow(1.0f - clamp(dot(_863, _175), 0.0f, 1.0f), _159_m7.z)) * (0.4000000059604644775390625f + (0.60000002384185791015625f * clamp(_355 + 0.5f, 0.0f, 1.0f))));
    float3 _915;
    if (_190)
    {
        _915 = (lerp(_694, _159_m5.xyz + (_159_m3.xyz * 0.5f), ((0.119999997317790985107421875f + (0.5f * pow(1.0f - clamp(dot(normalize(lerp(float3(0.0f, 1.0f, 0.0f), _863, 0.07999999821186065673828125f.xxx)), _175), 0.0f, 1.0f), 3.0f))) * (1.0f - (_894 * 0.699999988079071044921875f))).xxx) * (1.0f + (_902 * 0.0900000035762786865234375f))) + ((float3(0.75f, 0.85000002384185791015625f, 0.89999997615814208984375f) * _910) * 0.300000011920928955078125f);
    }
    else
    {
        _915 = _694;
    }
    _800 = float4(lerp(_915, _159_m5.xyz + (_159_m2.xyz * (pow(clamp(dot(-_175, _159_m1.xyz), 0.0f, 1.0f), 6.0f) * _159_m6.z)), clamp((1.0f - exp((-max(_168 - _159_m6.w, 0.0f)) * _159_m5.w)) * exp((-max(_163.y - _159_m6.x, 0.0f)) * _159_m6.y), 0.0f, 1.0f).xxx), _884.w);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _34 = stage_input._34;
    _110 = stage_input._110;
    _148 = stage_input._148;
    _163 = stage_input._163;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._800 = _800;
    return stage_output;
}
