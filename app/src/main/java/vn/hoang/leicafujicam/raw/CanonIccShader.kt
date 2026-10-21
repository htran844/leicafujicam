package vn.hoang.leicafujicam.raw

/** ICC-standard tetrahedral interpolation; numerical equality to Canon UCS is not asserted. */
internal object CanonIccShader {
    val UNIFORMS = """
        uniform highp sampler2D uCanonIccClut;
        uniform highp sampler2D uCanonIccCurves;
        uniform highp sampler2D uCanonIccTargetTrc;
        uniform int uCanonIccGrid;
        uniform ivec2 uCanonIccCurveSizes;
        uniform ivec3 uCanonIccTrcSizes;
        uniform mat3 uCanonIccXyzToTarget;
        uniform vec3 uCanonIccPcsWhite;
    """.trimIndent()

    val FUNCTIONS = """
        float canonIccCurve(float value, int channel, int row, int count) {
            float p = clamp(value, 0.0, 1.0) * float(count - 1);
            int i = min(int(p), count - 2);
            float a = texelFetch(uCanonIccCurves, ivec2(i, row), 0)[channel];
            float b = texelFetch(uCanonIccCurves, ivec2(i + 1, row), 0)[channel];
            return mix(a, b, p - float(i));
        }
        vec3 canonIccCorner(ivec3 index) {
            return texelFetch(uCanonIccClut, ivec2(index.b, index.r * uCanonIccGrid + index.g), 0).rgb;
        }
        vec3 canonIccTetrahedron(vec3 inputRgb) {
            vec3 p = clamp(inputRgb, 0.0, 1.0) * float(uCanonIccGrid - 1);
            ivec3 base = min(ivec3(p), ivec3(uCanonIccGrid - 2));
            vec3 f = p - vec3(base);
            ivec3 a; ivec3 b; vec3 weights;
            if (f.r >= f.g) {
                if (f.g >= f.b) { a=ivec3(1,0,0); b=ivec3(1,1,0); weights=f.rgb; }
                else if (f.r >= f.b) { a=ivec3(1,0,0); b=ivec3(1,0,1); weights=f.rbg; }
                else { a=ivec3(0,0,1); b=ivec3(1,0,1); weights=f.brg; }
            } else {
                if (f.r >= f.b) { a=ivec3(0,1,0); b=ivec3(1,1,0); weights=f.grb; }
                else if (f.g >= f.b) { a=ivec3(0,1,0); b=ivec3(0,1,1); weights=f.gbr; }
                else { a=ivec3(0,0,1); b=ivec3(0,1,1); weights=f.bgr; }
            }
            return canonIccCorner(base) * (1.0 - weights.x)
                 + canonIccCorner(base+a) * (weights.x - weights.y)
                 + canonIccCorner(base+b) * (weights.y - weights.z)
                 + canonIccCorner(base+ivec3(1)) * weights.z;
        }
        float canonIccInverseTrc(float value, int channel, int count) {
            float first = texelFetch(uCanonIccTargetTrc, ivec2(0), 0)[channel];
            float last = texelFetch(uCanonIccTargetTrc, ivec2(count-1,0), 0)[channel];
            if (value <= first) return 0.0;
            if (value >= last) return 1.0;
            int lo=0; int hi=count-1;
            for (int step=0; step<16; ++step) {
                if (hi-lo <= 1) break;
                int middle=(lo+hi)/2;
                float v=texelFetch(uCanonIccTargetTrc,ivec2(middle,0),0)[channel];
                if (v <= value) lo=middle; else hi=middle;
            }
            float a=texelFetch(uCanonIccTargetTrc,ivec2(lo,0),0)[channel];
            float b=texelFetch(uCanonIccTargetTrc,ivec2(hi,0),0)[channel];
            float fraction=b>a ? (value-a)/(b-a) : 0.0;
            return (float(lo)+fraction)/float(count-1);
        }
        float canonIccLabInverse(float v) {
            return v > 6.0/29.0 ? v*v*v : (v-4.0/29.0)*(108.0/841.0);
        }
        float canonIccSrgbDecode(float v) {
            return v <= 0.04045 ? v/12.92 : pow((v+0.055)/1.055,2.4);
        }
        vec3 canonIccToLinearSrgb(vec3 rawRecipeRgbNormalized) {
            vec3 shaped;
            for (int c=0; c<3; ++c)
                shaped[c]=canonIccCurve(rawRecipeRgbNormalized[c],c,0,uCanonIccCurveSizes.x);
            vec3 pcs=canonIccTetrahedron(shaped);
            for (int c=0; c<3; ++c)
                pcs[c]=canonIccCurve(pcs[c],c,1,uCanonIccCurveSizes.y);
            float fy=(pcs.x*(65535.0/65280.0)*100.0+16.0)/116.0;
            float fx=fy+(pcs.y*(65535.0/256.0)-128.0)/500.0;
            float fz=fy-(pcs.z*(65535.0/256.0)-128.0)/200.0;
            vec3 xyz=vec3(canonIccLabInverse(fx),canonIccLabInverse(fy),canonIccLabInverse(fz))*uCanonIccPcsWhite;
            vec3 targetLinear=uCanonIccXyzToTarget*xyz;
            vec3 outputRgb;
            for (int c=0; c<3; ++c)
                outputRgb[c]=canonIccSrgbDecode(canonIccInverseTrc(targetLinear[c],c,uCanonIccTrcSizes[c]));
            return outputRgb;
        }
    """.trimIndent()
}
