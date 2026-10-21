package vn.hoang.leicafujicam.processor

/** Shared Lanczos-3 output interpolation; callers provide samples on their native image grid. */
internal object GlesLanczosResampling {
    val kernel = """
        float lanczosWeight(float distance) {
            float x = abs(distance);
            if (x < 1.0e-6) return 1.0;
            if (x >= 3.0) return 0.0;
            float p = 3.141592653589793 * x;
            return (sin(p) / p) * (sin(p / 3.0) / (p / 3.0));
        }
    """.trimIndent()

    /** Define lanczosSource before inserting this body. Preserve signed lobes until final encoding. */
    val sampleRgb = """
        $kernel

        vec3 sampleLanczosRgb(vec2 sourcePosition) {
            ivec2 base = ivec2(floor(sourcePosition));
            vec2 fraction = sourcePosition - vec2(base);
            float wx[6];
            float wy[6];
            float sumX = 0.0;
            float sumY = 0.0;
            for (int i = 0; i < 6; ++i) {
                wx[i] = lanczosWeight(float(i - 2) - fraction.x);
                wy[i] = lanczosWeight(float(i - 2) - fraction.y);
                sumX += wx[i];
                sumY += wy[i];
            }
            vec3 total = vec3(0.0);
            for (int y = 0; y < 6; ++y) {
                vec3 row = vec3(0.0);
                for (int x = 0; x < 6; ++x) {
                    row += lanczosSource(base + ivec2(x - 2, y - 2)) * wx[x];
                }
                total += row * wy[y];
            }
            return total / (sumX * sumY);
        }
    """.trimIndent()
}
