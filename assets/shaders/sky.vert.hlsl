static float4 gl_Position;
static int gl_VertexIndex;
static float2 _24;

struct SPIRV_Cross_Input
{
    uint gl_VertexIndex : SV_VertexID;
};

struct SPIRV_Cross_Output
{
    float2 _24 : TEXCOORD0;
    float4 gl_Position : SV_Position;
};

void vert_main()
{
    _24 = (float2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0f) - 1.0f.xx;
    gl_Position = float4(_24.x, -_24.y, 1.0f, 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_VertexIndex = int(stage_input.gl_VertexIndex);
    vert_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_Position = gl_Position;
    stage_output._24 = _24;
    return stage_output;
}
