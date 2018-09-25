// Copyright 2009-2018 Jan Niklas Hasse <jhasse@bixense.com>
// For conditions of distribution and use, see copyright notice in LICENSE.txt

#pragma once

#include "jngl/types.hpp"

#include <boost/qvm/mat.hpp>

#ifdef IOS
	#include <OpenGLES/ES1/gl.h>
	#include <OpenGLES/ES1/glext.h>
	#include "ios/glew.h"
#else
	#ifdef ANDROID
		#include <EGL/egl.h>
		#include <GLES2/gl2.h>
		#include <GLES2/gl2ext.h>
		#define GL_RGBA8 GL_RGBA8_OES
		extern PFNGLGENVERTEXARRAYSOESPROC glGenVertexArrays;
		extern PFNGLBINDVERTEXARRAYOESPROC glBindVertexArray;
		extern PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArrays;
	#else
		#ifdef _MSC_VER
			#include <windows.h> // To avoid warnings about APIENTRY
		#endif
		#include <epoxy/gl.h>
	#endif
#endif

#ifndef GL_BGR
#define GL_BGR 0x80e0
#endif

namespace opengl
{
	extern boost::qvm::mat<float, 3, 3> modelview;
	extern boost::qvm::mat<float, 4, 4> projection;

	void translate(float x, float y);
	void scale(float x, float y);

	template<class T> struct Type {};

	template<>
	struct Type<float>
	{
		const static GLenum constant = GL_FLOAT;
	};

	template<>
	struct Type<double>
	{
#ifdef GL_DOUBLE
		const static GLenum constant = GL_DOUBLE;
		typedef GLdouble type;
#else
		const static GLenum constant = GL_FLOAT;
		typedef GLfloat type;
#endif
	};

#ifdef GL_DOUBLE
	typedef GLdouble CoordType;
#else
	typedef GLfloat CoordType;
#endif
}
