cbuffer _29_31 : register(b0, space3)
{
    row_major float4x4 _31_m0 : packoffset(c0);
    float4 _31_m1 : packoffset(c4);
    float4 _31_m2 : packoffset(c5);
    float4 _31_m3 : packoffset(c6);
    float4 _31_m4 : packoffset(c7);
    float4 _31_m5 : packoffset(c8);
    float4 _31_m6 : packoffset(c9);
    float4 _31_m7 : packoffset(c10);
    float4 _31_m8 : packoffset(c11);
};


static float2 _39;
static float4 _229;

struct SPIRV_Cross_Input
{
    float2 _39 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _229 : SV_Target0;
};

void frag_main()
{
    float _48 = -_39.y;
    float4 _51 = mul(float4(_39.x, _48, 1.0f, 1.0f), _31_m0);
    float4 _62 = mul(float4(_39.x, _48, 0.0f, 1.0f), _31_m0);
    float3 _79 = normalize((_51.xyz / _51.w.xxx) - (_62.xyz / _62.w.xxx));
    float _82 = _79.y;
    float3 _245;
    if (_82 >= 0.0f)
    {
        _245 = lerp(_31_m5.xyz, _31_m4.xyz, pow(clamp(_82, 0.0f, 1.0f), 0.60000002384185791015625f).xxx);
    }
    else
    {
        _245 = lerp(_31_m5.xyz, _31_m6.xyz, pow(clamp(-_82, 0.0f, 1.0f), 0.5f).xxx);
    }
    float3 _246;
    if ((_31_m7.y > 0.0f) && (_82 > 0.0f))
    {
        float3 _134 = _79 * 180.0f;
        float _241 = frac(sin(dot(floor(_134), float3(12.98980045318603515625f, 78.233001708984375f, 37.71900177001953125f))) * 43758.546875f);
        _246 = _245 + ((((step(1.0f - (_31_m7.y * 0.00999999977648258209228515625f), _241) * (0.5f + (0.5f * sin((_31_m7.x * 3.0f) + (_241 * 40.0f))))) * smoothstep(0.3499999940395355224609375f, 0.0f, length(frac(_134) - 0.5f.xxx))).xxx * 2.5f) * pow(clamp(_82, 0.0f, 1.0f), 0.4000000059604644775390625f));
    }
    else
    {
        _246 = _245;
    }
    float _191 = clamp(dot(_79, -_31_m2.xyz), 0.0f, 1.0f);
    _229 = float4(lerp(_246 + (_31_m3.xyz * ((pow(_191, 256.0f) * 6.0f) + (pow(_191, 8.0f) * _31_m3.w))), _31_m8.xyz, (_31_m7.z * pow(1.0f - clamp(abs(_82), 0.0f, 1.0f), 4.0f)).xxx), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _39 = stage_input._39;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output._229 = _229;
    return stage_output;
}
