#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "shader.h"
#include "opengl.h"

#include "text_buffer.h"

#include <iostream>

namespace ftdgl {
namespace text {
namespace impl {
/*
  # 6x subpixel AA pattern
  #
  #   R = (f(x - 2/3, y) + f(x - 1/3, y) + f(x, y)) / 3
  #   G = (f(x - 1/3, y) + f(x, y) + f(x + 1/3, y)) / 3
  #   B = (f(x, y) + f(x + 1/3, y) + f(x + 2/3, y)) / 3
  #
  # The shader would require three texture lookups if the texture format
  # stored data for offsets -1/3, 0, and +1/3 since the shader also needs
  # data for offsets -2/3 and +2/3. To avoid this, the texture format stores
  # data for offsets 0, +1/3, and +2/3 instead. That way the shader can get
  # data for offsets -2/3 and -1/3 with only one additional texture lookup.
  #
*/

constexpr
glm::vec2 JITTER_PATTERN[] = {
    {-1 / 12.0, -5 / 12.0},
	{ 1 / 12.0,  1 / 12.0},
	{ 3 / 12.0, -1 / 12.0},
	{ 5 / 12.0,  5 / 12.0},
	{ 7 / 12.0, -3 / 12.0},
	{ 9 / 12.0,  3 / 12.0},
};

class TextBufferImpl : public TextBuffer {
public:
    TextBufferImpl(const viewport::viewport_s & viewport)
        : m_Viewport {viewport} {
        Init();
    }

    virtual ~TextBufferImpl() {
        Destroy();
    }

    virtual bool AddText(pen_s & pen, const markup_s & markup, const std::wstring & text);
    bool AddChar(pen_s & pen, const markup_s & markup, wchar_t ch);

    virtual uint32_t GetTexture() const { return m_RenderedTexture; }

private:
	GLuint m_RenderedTexture;
	GLuint m_FrameBuffer;
    const viewport::viewport_s & m_Viewport;

public:
    void Init();
    void Destroy();
};

bool TextBufferImpl::AddText(pen_s & pen, const markup_s & markup, const std::wstring & text) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    for(size_t i=0;i < text.length(); i++) {
        if (!AddChar(pen, markup, text[i])) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

const char * vert_source = "\n"
        "uniform mat3 matrix3;\n"
        "uniform mat4 matrix4;\n"
        "attribute vec4 position4;\n"
        "varying vec2 _coord2;\n"
        "void main() {\n"
        "	_coord2 = position4.zw;\n"
        "	gl_Position = matrix4 * vec4(position4.xy, 0.0, 1.0);\n"
        "}\n";

const char * frag_source = "\n"
        "uniform vec4 color;\n"
        "varying vec2 _coord2;\n"
        "void main() {\n"
        "	if (_coord2.x * _coord2.x - _coord2.y > 0.0) {\n"
        "		discard;\n"
        "	}\n"
        "\n"
        "	// Upper 4 bits: front faces\n"
        "	// Lower 4 bits: back faces\n"
        "	gl_FragColor = color * (gl_FrontFacing ? 16.0 / 255.0 : 1.0 / 255.0);\n"
        "}\n";

bool TextBufferImpl::AddChar(pen_s & pen, const markup_s & markup, wchar_t ch) {
    (void)pen;
    (void)markup;
    (void)ch;

    float pt_width = m_Viewport.pixel_width * 72 / m_Viewport.dpi;
    float pt_height = m_Viewport.pixel_height * 72 / m_Viewport.dpi_height;

    //TODO empty glyph
    if (ch == L' ')
        return true;

    if (ch == L'\n') {
        pen.y -= (markup.font->GetAscender() - markup.font->GetDescender()) * m_Viewport.window_height * markup.font->GetPtSize() / pt_height;
        pen.x = 0;
        return true;
    }

    glm::mat4 translate = glm::translate(glm::mat4(1.0), glm::vec3(-1, -1, 0));
    glm::mat4 translate1 = glm::translate(glm::mat4(1.0), glm::vec3(0, markup.font->GetDescender(), 0));
    glm::mat4 translate2 = glm::translate(glm::mat4(1.0), glm::vec3(2 * pen.x / m_Viewport.window_width, 2 * pen.y / m_Viewport.window_height, 0));
    glm::mat4 scale = glm::scale(glm::mat4(1.0), glm::vec3(markup.font->GetPtSize() / pt_width, markup.font->GetPtSize() / pt_height, 0));
    glm::mat4 transform = translate2 * translate * scale * translate1;

    GLuint program = shader_load(vert_source, frag_source);

    auto glyph = markup.font->LoadGlyph(ch);

    if (!glyph)
        return true;

	GLuint vertexbuffer;
	glGenBuffers(1, &vertexbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
	glBufferData(GL_ARRAY_BUFFER, glyph->GetSize(),
                 glyph->GetAddr(), GL_STATIC_DRAW);

    glUseProgram(program);

    auto c = glm::vec4(0.0, 0.0, 0.0, 0.0);

    GLint posAttrib = glGetAttribLocation(program, "position4");
    glEnableVertexAttribArray(posAttrib);
    glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
    glVertexAttribPointer(posAttrib, 4, GL_FLOAT, GL_FALSE, 0, 0);

    GLint color_index = glGetUniformLocation(program, "color");

    for(size_t i = 0; i < sizeof(JITTER_PATTERN) / sizeof(glm::vec2); i++) {
        glm::mat4 translate3 = glm::translate(glm::mat4(1.0), glm::vec3(JITTER_PATTERN[i].x * 72 / m_Viewport.dpi / pt_width ,
                                                                        JITTER_PATTERN[i].y * 72 / m_Viewport.dpi_height / pt_height,
                                                                        0));
        glm::mat4 transform_x = translate3 * transform;

        glUniformMatrix4fv(glGetUniformLocation(program, "matrix4"),
                           1, GL_FALSE, &transform_x[0][0]);

        if (i % 2 == 0) {
            c = glm::vec4(i == 0 ? 1.0 : 0.0,
                          i == 2 ? 1.0 : 0.0,
                          i == 4 ? 1.0 : 0.0,
                          0.0);
        }

        glUniform4fv(color_index, 1, &c[0]);

		glDrawArrays(GL_TRIANGLES, 0, glyph->GetSize() / sizeof(GLfloat) / 4);
    }

    glDisableVertexAttribArray(posAttrib);
    glUseProgram(0);

    pen.x += glyph->GetAdvanceX();
    //TOOD: kerning
    return true;
}

void TextBufferImpl::Init() {
	glGenFramebuffers(1, &m_FrameBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);

	// The texture we're going to render to
	glGenTextures(1, &m_RenderedTexture);

	// "Bind" the newly created texture : all future texture functions will modify this texture
	glBindTexture(GL_TEXTURE_2D, m_RenderedTexture);

	// Give an empty image to OpenGL ( the last "0" means "empty" )
	glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB, 500 * 2, 220 * 2, 0,GL_RGB, GL_UNSIGNED_BYTE, 0);

	// Poor filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// The depth buffer
	GLuint depthrenderbuffer;
	glGenRenderbuffers(1, &depthrenderbuffer);
	glBindRenderbuffer(GL_RENDERBUFFER, depthrenderbuffer);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, 500 * 2, 220 * 2);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthrenderbuffer);

	// Set "renderedTexture" as our colour attachement #0
	glFramebufferTextureEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_RenderedTexture, 0);

	// Set the list of draw buffers.
	GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
	glDrawBuffers(1, DrawBuffers); // "1" is the size of DrawBuffers

	// Always check that our framebuffer is ok
	if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        printf("frame buffer status error\n");
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);
    glClearColor(0,0,0,0);
    //glClearColor(1.0,0.40,0.45,1.00);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void TextBufferImpl::Destroy() {
    glDeleteFramebuffers(1, &m_FrameBuffer);
}

} //namespace impl

TextBufferPtr CreateTextBuffer(const viewport::viewport_s & viewport) {
    return std::make_shared<impl::TextBufferImpl>(viewport);
}

} //namespace text
} //namespace ftdgl