// SPDX-License-Identifier: GPL-2.0-or-later

#include "optglarea.h"

#include <cassert>
#include <utility>
#include <gdkmm/glcontext.h>
#include <gdkmm/gltexture.h>
#include <gtkmm/snapshot.h>
#include <2geom/int-point.h>
#include "ui/widget/canvas/texture.h"
#include "util/gobjectptr.h"
#include "util/scope_exit.h"

namespace Inkscape::UI::Widget {
namespace {

template <auto &f>
GLuint create_buffer()
{
    GLuint result;
    f(1, &result);
    return result;
}

template <typename T>
std::weak_ptr<T> weakify(std::shared_ptr<T> const &p)
{
    return p;
}

template <typename F>
std::pair<GDestroyNotify, gpointer> make_destroynotify(F &&f)
{
    auto data = new scope_exit{std::forward<F>(f)};
    auto destroy = +[] (gpointer data_c) {
        delete reinterpret_cast<decltype(data)>(data_c);
    };
    return {destroy, data};
}

template <typename F>
Glib::RefPtr<Gdk::GLTexture> build_texture(GdkGLTextureBuilder *builder, F &&on_release)
{
    auto [destroy, data] = make_destroynotify(std::forward<F>(on_release));
    auto tex = gdk_gl_texture_builder_build(builder, destroy, data);
    return std::static_pointer_cast<Gdk::GLTexture>(Glib::wrap(tex));
}

} // namespace

struct OptGLArea::GLState
{
    std::shared_ptr<Gdk::GLContext> const context;

    GLuint const framebuffer = create_buffer<glGenFramebuffers>();
    GLuint const stencilbuffer = create_buffer<glGenRenderbuffers>();

    Util::GObjectPtr<GdkGLTextureBuilder> const builder{gdk_gl_texture_builder_new()};

    std::optional<Geom::IntPoint> size;

    Texture current_texture;
    std::vector<Texture> spare_textures;

    GLState(Glib::RefPtr<Gdk::GLContext> &&context_)
        : context{std::move(context_)}
    {
        gdk_gl_texture_builder_set_context(builder.get(), context->gobj());
        gdk_gl_texture_builder_set_format(builder.get(), GDK_MEMORY_B8G8R8A8_PREMULTIPLIED);
    }

    ~GLState()
    {
        glDeleteRenderbuffers(1, &stencilbuffer);
        glDeleteFramebuffers (1, &framebuffer);
    }
};

OptGLArea::OptGLArea() = default;
OptGLArea::~OptGLArea() = default;

void OptGLArea::on_realize()
{
    Gtk::Widget::on_realize();
    if (opengl_enabled) init_opengl();
}

void OptGLArea::on_unrealize()
{
    if (opengl_enabled) uninit_opengl();
    Gtk::Widget::on_unrealize();
}

void OptGLArea::set_opengl_enabled(bool enabled)
{
    if (opengl_enabled == enabled) return;
    if (opengl_enabled && get_realized()) uninit_opengl();
    opengl_enabled = enabled;
    if (opengl_enabled && get_realized()) init_opengl();
}

void OptGLArea::init_opengl()
{
    auto context = create_context();
    if (!context) {
        opengl_enabled = false;
        return;
    }
    context->make_current();
    gl = std::make_shared<GLState>(std::move(context));
    Gdk::GLContext::clear_current();
}

void OptGLArea::uninit_opengl()
{
    gl->context->make_current();
    gl.reset();
    Gdk::GLContext::clear_current();
}

void OptGLArea::make_current()
{
    assert(gl);
    gl->context->make_current();
}

void OptGLArea::bind_framebuffer() const
{
    assert(gl);
    assert(gl->current_texture);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->current_texture.id(), 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, gl->stencilbuffer);
}

void OptGLArea::snapshot_vfunc(Glib::RefPtr<Gtk::Snapshot> const &snapshot)
{
    auto const rect = GRAPHENE_RECT_INIT(0, 0, static_cast<float>(get_width()),
                                               static_cast<float>(get_height()));

    if (opengl_enabled) {
        auto const size = Geom::IntPoint(get_width(), get_height()) * get_scale_factor();

        if (size.x() == 0 || size.y() == 0) {
            return;
        }

        gl->context->make_current();

        // Check if the size has changed.
        if (size != gl->size) {
            gl->size = size;

            // Resize the framebuffer.
            glBindRenderbuffer(GL_RENDERBUFFER, gl->stencilbuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, size.x(), size.y());

            // Resize the texture builder.
            gdk_gl_texture_builder_set_width (gl->builder.get(), size.x());
            gdk_gl_texture_builder_set_height(gl->builder.get(), size.y());
        }

        // Discard wrongly-sized spare textures.
        std::erase_if(gl->spare_textures, [&] (auto &tex) { return tex.size() != size; });
        // Todo: Consider clearing out excess spare textures every once in a while.

        // Set the current texture.
        assert(!gl->current_texture);
        if (!gl->spare_textures.empty()) {
            // Grab a spare texture.
            gl->current_texture = std::move(gl->spare_textures.back());
            gl->spare_textures.pop_back();
        } else {
            // Create a new one.
            gl->current_texture = Texture(size);
        }

        // This typically calls bind_framebuffer().
        paint_widget({});

        // Wrap the OpenGL texture we've just drawn to in a Gdk::GLTexture.
        gdk_gl_texture_builder_set_id(gl->builder.get(), gl->current_texture.id());
        auto gdktexture = build_texture(gl->builder.get(),
            [texture = std::move(gl->current_texture), context = gl->context, gl_weak = weakify(gl)] () mutable {
                if (auto gl = gl_weak.lock()) {
                    // Return the texture to the texture pool.
                    gl->spare_textures.emplace_back(std::move(texture));
                } else {
                    // Destroy the texture in its GL context.
                    context->make_current();
                    texture.clear();
                    Gdk::GLContext::clear_current();
                }
            }
        );

        // Render the texture upside-down.
        // Todo: The canvas does the same, so both transformations can be removed.
        snapshot->save();
        auto const point = GRAPHENE_POINT_INIT(0, static_cast<float>(get_height()));
        gtk_snapshot_translate(snapshot->gobj(), &point);
        snapshot->scale(1, -1);
        snapshot->append_texture(std::move(gdktexture), &rect);
        snapshot->restore();
    } else {
        auto const cr = snapshot->append_cairo(&rect);
        paint_widget(cr);
    }
}

} // namespace Inkscape::UI::Widget

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim:filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99:
