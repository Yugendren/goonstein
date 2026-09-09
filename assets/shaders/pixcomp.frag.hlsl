static const int2 _246[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };

cbuffer _69_71 : register(b0, space3)
{
    float4 _71_m0 : packoffset(c0);
    float4 _71_m1 : packoffset(c1);
    float4 _71_m2 : packoffset(c2);
    float4 _71_m3[64] : packoffset(c3);
    int4 _71_m4 : packoffset(c67);
};

Texture2D<float4> _186 : register(t0, space2);
SamplerState __186_sampler : register(s0, space2);
Texture2D<float4> _192 : register(t1, space2);
SamplerState __192_sampler : register(s1, space2);

static float gl_FragDepth;
static float2 _153;
static float4 _344;

struct SPIRV_Cross_Input
{
    float2 _153 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float4 _344 : SV_Target0;
    float gl_FragDepth : SV_Depth;
};

void frag_main()
{
    do
    {
        int2 _165 = int2(_71_m0.xy);
        int2 _173 = int2(floor((_153 + _71_m1.xy) * _71_m0.xy));
        int2 _179 = _165 - int2(1, 1);
        int2 _180 = clamp(_173, int2(0, 0), _179);
        float4 _190 = _186.Load(int3(_180, 0));
        int _201 = _173.x;
        bool _202 = _201 < 0;
        bool _209;
        if (!_202)
        {
            _209 = _173.y < 0;
        }
        else
        {
            _209 = _202;
        }
        bool _218;
        if (!_209)
        {
            _218 = _201 >= _165.x;
        }
        else
        {
            _218 = _209;
        }
        bool _227;
        if (!_218)
        {
            _227 = _173.y >= _165.y;
        }
        else
        {
            _227 = _218;
        }
        float _804 = _227 ? 1.0f : _192.Load(int3(_180, 0)).x;
        bool4 _806 = _227.xxxx;
        float4 _807 = float4(_806.x ? 0.0f.xxxx.x : _190.x, _806.y ? 0.0f.xxxx.y : _190.y, _806.z ? 0.0f.xxxx.z : _190.z, _806.w ? 0.0f.xxxx.w : _190.w);
        bool _236 = _807.w > 0.5f;
        float _712;
        bool _725;
        float3 _727;
        float _744;
        _744 = 1.0f;
        _727 = 0.0f.xxx;
        _725 = false;
        _712 = 0.0f;
        bool _809;
        float _811;
        float3 _786;
        float _794;
        for (int _704 = 0; _704 < 4; _744 = _794, _727 = _786, _725 = _809, _712 = _811, _704++)
        {
            int2 _270 = clamp(_173 + _246[_704], int2(0, 0), _179);
            float4 _275 = _186.Load(int3(_270, 0));
            float4 _280 = _192.Load(int3(_270, 0));
            float _281 = _280.x;
            bool _284 = _275.w > 0.5f;
            if (_284)
            {
                bool _290 = _281 < _744;
                float3 _787;
                if (_290)
                {
                    _787 = _275.xyz;
                }
                else
                {
                    _787 = _727;
                }
                _794 = _290 ? _281 : _744;
                _786 = _787;
            }
            else
            {
                _794 = _744;
                _786 = _727;
            }
            _809 = _284 ? true : _725;
            bool _810 = _236 ? _284 : _236;
            bool _310;
            if (_810)
            {
                _310 = _281 > (_804 + 0.00150000001303851604461669921875f);
            }
            else
            {
                _310 = _810;
            }
            _811 = _310 ? 1.0f : _712;
        }
        if (!_236)
        {
            bool _328;
            if (_725)
            {
                _328 = _71_m2.y <= 0.001000000047497451305389404296875f;
            }
            else
            {
                _328 = !_725;
            }
            if (_328)
            {
                discard;
            }
            float _404 = max(max(_727.x, _727.y), _727.z);
            float _408 = (_404 > 1.0f) ? _404 : 1.0f;
            float3 _477 = pow(max(_727 / _408.xxx, 0.0f.xxx), 0.4545454680919647216796875f.xxx);
            bool _416 = _71_m2.z > 0.5f;
            float3 _730;
            if (_416)
            {
                float3 _729;
                _729 = _477;
                int _728 = 0;
                float _751 = 1000000000.0f;
                for (; _728 < _71_m4.x; )
                {
                    float3 _431 = _71_m3[_728].xyz - _477;
                    float _434 = dot(_431, _431);
                    bool _437 = _434 < _751;
                    bool3 _814 = _437.xxx;
                    _751 = _437 ? _434 : _751;
                    _729 = float3(_814.x ? _71_m3[_728].xyz.x : _729.x, _814.y ? _71_m3[_728].xyz.y : _729.y, _814.z ? _71_m3[_728].xyz.z : _729.z);
                    _728++;
                    continue;
                }
                _730 = _729;
            }
            else
            {
                float3 _731;
                if (_71_m2.x > 0.5f)
                {
                    _731 = floor((_477 * _71_m2.x) + 0.5f.xxx) / _71_m2.x.xxx;
                }
                else
                {
                    _731 = _477;
                }
                _730 = _731;
            }
            float3 _737;
            if (_416)
            {
                float3 _736;
                _736 = _477;
                int _735 = 0;
                float _748 = 1000000000.0f;
                for (; _735 < _71_m4.x; )
                {
                    float3 _530 = _71_m3[_735].xyz - _477;
                    float _533 = dot(_530, _530);
                    bool _536 = _533 < _748;
                    bool3 _817 = _536.xxx;
                    _748 = _536 ? _533 : _748;
                    _736 = float3(_817.x ? _71_m3[_735].xyz.x : _736.x, _817.y ? _71_m3[_735].xyz.y : _736.y, _817.z ? _71_m3[_735].xyz.z : _736.z);
                    _735++;
                    continue;
                }
                _737 = _736;
            }
            else
            {
                float3 _738;
                if (_71_m2.x > 0.5f)
                {
                    _738 = floor((_477 * _71_m2.x) + 0.5f.xxx) / _71_m2.x.xxx;
                }
                else
                {
                    _738 = _477;
                }
                _737 = _738;
            }
            _344 = float4(lerp(pow(max(_737, 0.0f.xxx), 2.2000000476837158203125f.xxx) * _408, lerp((pow(max(_730, 0.0f.xxx), 2.2000000476837158203125f.xxx) * _408) * 0.180000007152557373046875f, float3(0.00999999977648258209228515625f, 0.00999999977648258209228515625f, 0.014999999664723873138427734375f), 0.5f.xxx), _71_m2.y.xxx), 1.0f);
            gl_FragDepth = _744;
            break;
        }
        float _602 = max(max(_807.x, _807.y), _807.z);
        float _606 = (_602 > 1.0f) ? _602 : 1.0f;
        float3 _675 = pow(max(_807.xyz / _606.xxx, 0.0f.xxx), 0.4545454680919647216796875f.xxx);
        float3 _708;
        if (_71_m2.z > 0.5f)
        {
            float3 _707;
            _707 = _675;
            int _706 = 0;
            float _723 = 1000000000.0f;
            for (; _706 < _71_m4.x; )
            {
                float3 _629 = _71_m3[_706].xyz - _675;
                float _632 = dot(_629, _629);
                bool _635 = _632 < _723;
                bool3 _820 = _635.xxx;
                _723 = _635 ? _632 : _723;
                _707 = float3(_820.x ? _71_m3[_706].xyz.x : _707.x, _820.y ? _71_m3[_706].xyz.y : _707.y, _820.z ? _71_m3[_706].xyz.z : _707.z);
                _706++;
                continue;
            }
            _708 = _707;
        }
        else
        {
            float3 _709;
            if (_71_m2.x > 0.5f)
            {
                _709 = floor((_675 * _71_m2.x) + 0.5f.xxx) / _71_m2.x.xxx;
            }
            else
            {
                _709 = _675;
            }
            _708 = _709;
        }
        float3 _669 = pow(max(_708, 0.0f.xxx), 2.2000000476837158203125f.xxx) * _606;
        _344 = float4(lerp(_669, _669 * 0.449999988079071044921875f, (_712 * _71_m2.w).xxx), 1.0f);
        gl_FragDepth = _804;
        break;
    } while(false);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    _153 = stage_input._153;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.gl_FragDepth = gl_FragDepth;
    stage_output._344 = _344;
    return stage_output;
}
