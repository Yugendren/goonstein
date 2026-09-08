cbuffer _23_25 : register(b0, space1)
{
    float4 _25_m0 : packoffset(c0);
};


static float4 gl_Position;
static float2 _18;
static float2 _46;
static float2 _47;
static float4 _49;
static float4 _51;

struct SPIRV_Cross_Input
{
    float2 _18 : TEXCOORD0;
    float2 _47 : TEXCOORD1;
    float4 _51 : TEXCOORD2;
};

struct SPIRV_Cross_Output
{
    float2 _46 : TEXCOORD0;
    float4 _49 : TEXCOORD1;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    gl_Position = float4(((_18.x / _25_m0.x) * 2.0f) - 1.0f, 1.0f - ((_18.y / _25_m0.y) * 2.0f), 0.0f, 1.0f);
    _46 = _47;
    _49 = _51;
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _18 = stage_input._18;
    _47 = stage_input._47;
    _51 = stage_input._51;
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._46 = _46;
    stage_output._49 = _49;
    return stage_output;
}
