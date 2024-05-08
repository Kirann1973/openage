// Copyright 2017-2024 the openage authors. See copying.md for legal info.

#include "renderer.h"

#include <algorithm>

#include "error/error.h"
#include "log/log.h"
#include "renderer/opengl/context.h"
#include "renderer/opengl/geometry.h"
#include "renderer/opengl/lookup.h"
#include "renderer/opengl/render_pass.h"
#include "renderer/opengl/render_target.h"
#include "renderer/opengl/shader_program.h"
#include "renderer/opengl/texture.h"
#include "renderer/opengl/uniform_buffer.h"
#include "renderer/opengl/uniform_input.h"
#include "renderer/opengl/window.h"
#include "renderer/resources/buffer_info.h"


namespace openage::renderer::opengl {

GlRenderer::GlRenderer(const std::shared_ptr<GlContext> &ctx,
                       const util::Vector2s &viewport_size) :
	gl_context{ctx},
	display{std::make_shared<GlRenderTarget>(ctx,
                                             viewport_size[0],
                                             viewport_size[1])} {
	// color used to clear the color buffers
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	// global GL alpha blending settings
	// glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBlendFuncSeparate(
		GL_SRC_ALPHA,           // source (overlaying) RGB factor
		GL_ONE_MINUS_SRC_ALPHA, // destination (underlying) RGB factor
		GL_ONE,                 // source (overlaying) alpha factor
		GL_ONE_MINUS_SRC_ALPHA  // destination (underlying) alpha factor
	);

	// global GL depth testing settings
	glDepthFunc(GL_LEQUAL);
	glDepthRange(0.0, 1.0);

	log::log(MSG(info) << "Created OpenGL renderer");
}

std::shared_ptr<Texture2d> GlRenderer::add_texture(const resources::Texture2dData &data) {
	return std::make_shared<GlTexture2d>(this->gl_context, data);
}

std::shared_ptr<Texture2d> GlRenderer::add_texture(const resources::Texture2dInfo &info) {
	return std::make_shared<GlTexture2d>(this->gl_context, info);
}

std::shared_ptr<ShaderProgram> GlRenderer::add_shader(std::vector<resources::ShaderSource> const &srcs) {
	return std::make_shared<GlShaderProgram>(this->gl_context, srcs);
}

std::shared_ptr<Geometry> GlRenderer::add_mesh_geometry(resources::MeshData const &mesh) {
	return std::make_shared<GlGeometry>(this->gl_context, mesh);
}

std::shared_ptr<Geometry> GlRenderer::add_bufferless_quad() {
	return std::make_shared<GlGeometry>();
}

std::shared_ptr<RenderPass> GlRenderer::add_render_pass(std::vector<Renderable> renderables, const std::shared_ptr<RenderTarget> &target) {
	return std::make_shared<GlRenderPass>(std::move(renderables), target);
}

std::shared_ptr<RenderTarget> GlRenderer::create_texture_target(std::vector<std::shared_ptr<Texture2d>> const &textures) {
	std::vector<std::shared_ptr<GlTexture2d>> gl_textures;
	gl_textures.reserve(textures.size());
	for (auto tex : textures) {
		gl_textures.push_back(std::dynamic_pointer_cast<GlTexture2d>(tex));
	}

	return std::make_shared<GlRenderTarget>(this->gl_context, gl_textures);
}

std::shared_ptr<RenderTarget> GlRenderer::get_display_target() {
	return this->display;
}

std::shared_ptr<UniformBuffer> GlRenderer::add_uniform_buffer(resources::UniformBufferInfo const &info) {
	auto inputs = info.get_inputs();
	std::unordered_map<std::string, GlInBlockUniform> uniforms{};
	size_t offset = 0;
	for (auto const &input : inputs) {
		auto type = GL_UBO_INPUT_TYPE.get(input.type);
		auto size = resources::UniformBufferInfo::get_size(input, info.get_layout());

		// align offset to the size of the type
		offset += offset % size;

		uniforms.emplace(
			std::make_pair(input.name,
		                   GlInBlockUniform{type,
		                                    offset,
		                                    resources::UniformBufferInfo::get_size(input, info.get_layout()),
		                                    resources::UniformBufferInfo::get_stride_size(input.type, info.get_layout()),
		                                    input.count}));

		offset += size;
	}

	return std::make_shared<GlUniformBuffer>(this->gl_context,
	                                         info.get_size(),
	                                         uniforms,
	                                         this->gl_context->get_uniform_buffer_binding());
}

std::shared_ptr<UniformBuffer> GlRenderer::add_uniform_buffer(std::shared_ptr<ShaderProgram> const &prog,
                                                              std::string const &block_name) {
	auto gl_prog = std::dynamic_pointer_cast<GlShaderProgram>(prog);
	auto block_def = gl_prog->get_uniform_block(block_name.c_str());

	return std::make_shared<GlUniformBuffer>(this->gl_context,
	                                         block_def.data_size,
	                                         block_def.uniforms,
	                                         this->gl_context->get_uniform_buffer_binding());
}

resources::Texture2dData GlRenderer::display_into_data() {
	GLint params[4];
	glGetIntegerv(GL_VIEWPORT, params);

	GLint width = params[2];
	GLint height = params[3];

	resources::Texture2dInfo tex_info(width, height, resources::pixel_format::rgba8);
	std::vector<uint8_t> data(tex_info.get_data_size());

	std::static_pointer_cast<GlRenderTarget>(this->get_display_target())->bind_read();
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadnPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, tex_info.get_data_size(), data.data());

	resources::Texture2dData img(std::move(tex_info), std::move(data));
	return img.flip_y();
}

void GlRenderer::resize_display_target(size_t width, size_t height) {
	this->display->resize(width, height);
}

void GlRenderer::optimise(const std::shared_ptr<GlRenderPass> &pass) {
	if (!pass->get_is_optimised()) {
		auto renderables = pass->get_renderables();
		std::stable_sort(renderables.begin(), renderables.end(), [](const Renderable &a, const Renderable &b) {
			GLuint shader_a = std::dynamic_pointer_cast<GlShaderProgram>(
								  std::dynamic_pointer_cast<GlUniformInput>(a.uniform)->get_program())
			                      ->get_handle();
			GLuint shader_b = std::dynamic_pointer_cast<GlShaderProgram>(
								  std::dynamic_pointer_cast<GlUniformInput>(b.uniform)->get_program())
			                      ->get_handle();
			return shader_a < shader_b;
		});

		pass->set_renderables(renderables);
		pass->set_is_optimised(true);
	}
}

void GlRenderer::check_error() {
	// thanks for the global state, opengl!
	GlContext::check_error();
}

void GlRenderer::render(const std::shared_ptr<RenderPass> &pass) {
	auto gl_target = std::dynamic_pointer_cast<GlRenderTarget>(pass->get_target());
	gl_target->bind_write();

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// TODO: Option for face culling
	// glEnable(GL_CULL_FACE);

	auto gl_pass = std::dynamic_pointer_cast<GlRenderPass>(pass);
	// GlRenderer::optimise(gl_pass);

	for (auto const &obj : gl_pass->get_renderables()) {
		if (obj.alpha_blending) {
			glEnable(GL_BLEND);
		}
		else {
			glDisable(GL_BLEND);
		}

		if (obj.depth_test) {
			glEnable(GL_DEPTH_TEST);
		}
		else {
			glDisable(GL_DEPTH_TEST);
		}

		auto in = std::dynamic_pointer_cast<GlUniformInput>(obj.uniform);
		auto program = std::static_pointer_cast<GlShaderProgram>(in->get_program());

		// this also calls program->use()
		program->update_uniforms(in);

		// draw the geometry
		if (obj.geometry != nullptr) {
			auto geom = std::dynamic_pointer_cast<GlGeometry>(obj.geometry);
			// TODO read obj.blend + family
			geom->draw();
		}
	}
}

} // namespace openage::renderer::opengl
