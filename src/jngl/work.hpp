/*
Copyright 2012 Jan Niklas Hasse <jhasse@gmail.com>
For conditions of distribution and use, see copyright notice in LICENSE.txt
*/

#pragma once

#include <boost/shared_ptr.hpp>

#ifndef _MSC_VER
#pragma GCC visibility push(default)
#endif
namespace jngl {
	class Work {
	public:
		virtual void step() = 0;
		virtual void draw() const = 0;
		virtual void onQuitEvent();
		virtual void onLoad();
		virtual ~Work();
	};

	boost::shared_ptr<Work> getWork();

	void setWork(boost::shared_ptr<Work> work);

	void setWork(Work*);

	void resetFrameLimiter();
}
#ifndef _MSC_VER
#pragma GCC visibility pop
#endif