package vn.hoang.leicafujicam.raw

import android.opengl.GLES30
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Owned and used on the rendering GL context. Three explicit units; no GL state is assumed. */
internal class CanonIccGlResources(private val profile: CanonIccProfile) {
    private val textures = IntArray(3)

    fun bind(program: Int, firstTextureUnit: Int) {
        val limit = IntArray(1)
        GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_IMAGE_UNITS, limit, 0)
        require(firstTextureUnit >= 0 && firstTextureUnit + 3 <= limit[0])
        if (textures[0] == 0) upload(firstTextureUnit)
        val names = arrayOf("uCanonIccClut", "uCanonIccCurves", "uCanonIccTargetTrc")
        for (index in textures.indices) {
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + firstTextureUnit + index)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, textures[index])
            GLES30.glUniform1i(GLES30.glGetUniformLocation(program, names[index]), firstTextureUnit + index)
        }
        GLES30.glUniform1i(GLES30.glGetUniformLocation(program, "uCanonIccGrid"), profile.grid)
        GLES30.glUniform2i(GLES30.glGetUniformLocation(program, "uCanonIccCurveSizes"), profile.inputCurves[0].size, profile.outputCurves[0].size)
        GLES30.glUniform3i(GLES30.glGetUniformLocation(program, "uCanonIccTrcSizes"), profile.targetCurves[0].size, profile.targetCurves[1].size, profile.targetCurves[2].size)
        GLES30.glUniformMatrix3fv(GLES30.glGetUniformLocation(program, "uCanonIccXyzToTarget"), 1, false, profile.xyzToTarget, 0)
        GLES30.glUniform3fv(GLES30.glGetUniformLocation(program, "uCanonIccPcsWhite"), 1, profile.pcsWhite, 0)
    }

    private fun upload(firstTextureUnit: Int) {
        val curveWidth = maxOf(profile.inputCurves[0].size, profile.outputCurves[0].size)
        val trcWidth = profile.targetCurves.maxOf { it.size }
        val limit = IntArray(1)
        GLES30.glGetIntegerv(GLES30.GL_MAX_TEXTURE_SIZE, limit, 0)
        require(maxOf(profile.grid * profile.grid, curveWidth, trcWidth) <= limit[0])
        val cube = FloatArray(profile.grid * profile.grid * profile.grid * 4)
        for (i in profile.clut.indices) cube[(i / 3) * 4 + i % 3] = profile.clut[i]
        val curves = FloatArray(curveWidth * 2 * 4)
        for ((row, channels) in arrayOf(profile.inputCurves, profile.outputCurves).withIndex()) {
            for (channel in 0..2) for (i in channels[channel].indices)
                curves[(row * curveWidth + i) * 4 + channel] = channels[channel][i]
        }
        val trcs = FloatArray(trcWidth * 4)
        for (channel in 0..2) for (i in profile.targetCurves[channel].indices)
            trcs[i * 4 + channel] = profile.targetCurves[channel][i]
        GLES30.glGenTextures(3, textures, 0)
        try {
            fun texture(index: Int, width: Int, height: Int, values: FloatArray) {
                GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + firstTextureUnit + index)
                GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, textures[index])
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
                GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
                val data = ByteBuffer.allocateDirect(values.size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()
                data.put(values).position(0)
                GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA32F, width, height, 0, GLES30.GL_RGBA, GLES30.GL_FLOAT, data)
                check(GLES30.glGetError() == GLES30.GL_NO_ERROR) { "Canon ICC texture allocation failed" }
            }
            texture(0, profile.grid, profile.grid * profile.grid, cube)
            texture(1, curveWidth, 2, curves)
            texture(2, trcWidth, 1, trcs)
        } catch (failure: Throwable) {
            release()
            throw failure
        }
    }

    fun release() {
        if (textures.any { it != 0 }) GLES30.glDeleteTextures(textures.size, textures, 0)
        textures.fill(0)
    }
}
