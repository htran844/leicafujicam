package vn.hoang.leicafujicam.raw

import android.opengl.GLES30
import android.opengl.Matrix
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.nio.ShortBuffer

/**
 * Shared fullscreen vertex shader and its draw state.
 *
 * Fragment-pass classes own their programs; this class owns the common vertex input contract.
 */
internal class RawFullscreenQuad {
    private val vertexBuffer: FloatBuffer = ByteBuffer
        .allocateDirect(FULL_QUAD_VERTICES.size * Float.SIZE_BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
        .put(FULL_QUAD_VERTICES)
        .apply { position(0) }
    private val texCoordBuffer: FloatBuffer = ByteBuffer
        .allocateDirect(TEXTURE_COORDS.size * Float.SIZE_BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
        .put(TEXTURE_COORDS)
        .apply { position(0) }
    private val indexBuffer: ShortBuffer = ByteBuffer
        .allocateDirect(DRAW_ORDER.size * Short.SIZE_BYTES)
        .order(ByteOrder.nativeOrder())
        .asShortBuffer()
        .put(DRAW_ORDER)
        .apply { position(0) }

    fun createProgram(fragmentShader: String, name: String): Int =
        RawGlesProgram.linkFragment(VERTEX_SHADER, fragmentShader, name)

    fun bindIdentityTextureMatrix(program: Int) {
        val identityMatrix = FloatArray(16)
        Matrix.setIdentityM(identityMatrix, 0)
        GLES30.glUniformMatrix4fv(
            GLES30.glGetUniformLocation(program, "uTexMatrix"),
            1,
            false,
            identityMatrix,
            0,
        )
    }

    /**
     * Maps the quad's [0, 1] texture coordinates onto the texel window ([x], [y], [width] × [height])
     * of a [textureWidth] × [textureHeight] texture, so a viewport of the window's size samples
     * exactly its texel centres.
     */
    fun bindTextureWindowMatrix(
        program: Int,
        x: Int,
        y: Int,
        width: Int,
        height: Int,
        textureWidth: Int,
        textureHeight: Int,
    ) {
        val matrix = FloatArray(16)
        Matrix.setIdentityM(matrix, 0)
        matrix[0] = width.toFloat() / textureWidth
        matrix[5] = height.toFloat() / textureHeight
        matrix[12] = x.toFloat() / textureWidth
        matrix[13] = y.toFloat() / textureHeight
        GLES30.glUniformMatrix4fv(GLES30.glGetUniformLocation(program, "uTexMatrix"), 1, false, matrix, 0)
    }

    fun draw(program: Int) {
        val positionHandle = GLES30.glGetAttribLocation(program, "aPosition")
        val texCoordHandle = GLES30.glGetAttribLocation(program, "aTexCoord")
        if (positionHandle >= 0) {
            GLES30.glEnableVertexAttribArray(positionHandle)
            vertexBuffer.position(0)
            GLES30.glVertexAttribPointer(
                positionHandle,
                2,
                GLES30.GL_FLOAT,
                false,
                0,
                vertexBuffer,
            )
        }
        if (texCoordHandle >= 0) {
            GLES30.glEnableVertexAttribArray(texCoordHandle)
            texCoordBuffer.position(0)
            GLES30.glVertexAttribPointer(
                texCoordHandle,
                2,
                GLES30.GL_FLOAT,
                false,
                0,
                texCoordBuffer,
            )
        }
        indexBuffer.position(0)
        GLES30.glDrawElements(
            GLES30.GL_TRIANGLES,
            DRAW_ORDER.size,
            GLES30.GL_UNSIGNED_SHORT,
            indexBuffer,
        )
        if (positionHandle >= 0) GLES30.glDisableVertexAttribArray(positionHandle)
        if (texCoordHandle >= 0) GLES30.glDisableVertexAttribArray(texCoordHandle)
    }

    companion object {
        val VERTEX_SHADER = """
            #version 300 es

            in vec4 aPosition;
            in vec2 aTexCoord;

            out vec2 vTexCoord;

            uniform mat4 uTexMatrix;

            void main() {
                gl_Position = aPosition;
                vTexCoord = (uTexMatrix * vec4(aTexCoord, 0.0, 1.0)).xy;
            }
        """.trimIndent()

        private val FULL_QUAD_VERTICES = floatArrayOf(
            -1.0f, -1.0f,
            1.0f, -1.0f,
            -1.0f, 1.0f,
            1.0f, 1.0f,
        )
        private val TEXTURE_COORDS = floatArrayOf(
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
            1.0f, 1.0f,
        )
        private val DRAW_ORDER = shortArrayOf(0, 1, 2, 1, 3, 2)
    }
}
